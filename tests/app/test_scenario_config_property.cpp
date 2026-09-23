#include "catch2_compat.hpp"

#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "TestTempFiles.hpp"
#include "fin/app/ScenarioConfigIO.hpp"
#include "fin/app/ScenarioRunner.hpp"
#include "fin/indicators/FeatureSpec.hpp"

namespace
{
    // The contract a loaded config must satisfy, spelled out here on its own rather than by
    // calling validate_scenario_config: a test that graded the loader with the loader's own
    // answer key would pass whatever the two of them agreed on.
    void require_sane(const fin::app::ScenarioConfig &cfg)
    {
        REQUIRE_FALSE(cfg.ticks_path.empty());

        REQUIRE(cfg.ema_fast >= 1);
        REQUIRE(cfg.ema_slow >= 1);
        REQUIRE(cfg.rsi_period >= 1);
        REQUIRE(cfg.macd_fast >= 1);
        REQUIRE(cfg.macd_slow >= 1);
        REQUIRE(cfg.macd_signal >= 1);
        REQUIRE(cfg.sma_period >= 1);
        REQUIRE(cfg.bb_period >= 1);
        REQUIRE(cfg.atr_period >= 1);
        REQUIRE(cfg.adx_period >= 1);
        REQUIRE(cfg.stoch_k_period >= 1);
        REQUIRE(cfg.stoch_d_period >= 1);
        REQUIRE(cfg.zscore_period >= 1);
        REQUIRE(cfg.momentum_period >= 1);

        // train_ratio is clamped by the runner, so any finite value is usable; NaN is not,
        // because the clamp's comparisons are all false for it and it reaches a size_t cast.
        REQUIRE(std::isfinite(cfg.train_ratio));
        REQUIRE(std::isfinite(cfg.ridge_lambda));
        REQUIRE(cfg.ridge_lambda >= 0.0);
        REQUIRE(std::isfinite(cfg.bb_k));
        REQUIRE(cfg.bb_k > 0.0);

        REQUIRE(cfg.rsi_buy >= 0.0);
        REQUIRE(cfg.rsi_buy <= 100.0);
        REQUIRE(cfg.rsi_sell >= 0.0);
        REQUIRE(cfg.rsi_sell <= 100.0);

        if (cfg.initial_cash)
        {
            REQUIRE(std::isfinite(*cfg.initial_cash));
            REQUIRE(*cfg.initial_cash > 0.0);
        }
        if (cfg.trade_qty)
        {
            REQUIRE(std::isfinite(*cfg.trade_qty));
            REQUIRE(*cfg.trade_qty > 0.0);
        }
        if (cfg.fee_per_trade)
        {
            REQUIRE(std::isfinite(*cfg.fee_per_trade));
            REQUIRE(*cfg.fee_per_trade >= 0.0);
        }

        REQUIRE(std::isfinite(cfg.sgd.learning_rate));
        REQUIRE(cfg.sgd.learning_rate > 0.0);
        REQUIRE(std::isfinite(cfg.sgd.l2));
        REQUIRE(cfg.sgd.l2 >= 0.0);
        REQUIRE(std::isfinite(cfg.sgd.power_t));
        REQUIRE(cfg.sgd.power_t >= 0.0);
        REQUIRE(cfg.sgd.epochs >= 1);

        REQUIRE((!cfg.online_update || cfg.model == fin::app::ModelKind::Sgd));

        for (std::size_t i = 0; i < cfg.features.size(); ++i)
        {
            REQUIRE(fin::indicators::find_feature(cfg.features[i]) != nullptr);
            for (std::size_t j = i + 1; j < cfg.features.size(); ++j)
                REQUIRE(cfg.features[i] != cfg.features[j]);
        }
    }

    // run_scenario on a config built in code, the way the CLI flags and the Python dict build
    // it, reporting whether it was refused as a bad argument. The ticks file does not exist,
    // so a config that gets past validation fails later with a runtime_error instead.
    bool refused_as_invalid(const fin::app::ScenarioConfig &cfg)
    {
        try
        {
            (void)fin::app::run_scenario(cfg);
        }
        catch (const std::invalid_argument &)
        {
            return true;
        }
        catch (const std::exception &)
        {
        }
        return false;
    }

    fin::app::ScenarioConfig direct_config()
    {
        fin::app::ScenarioConfig cfg{};
        cfg.ticks_path = "aiquant-no-such-file.csv";
        return cfg;
    }

    bool loads(const std::string &ini, std::string &error)
    {
        const test_files::TempFile file("aiquant_ini_", ".ini", ini);
        fin::app::ScenarioConfig cfg{};
        return fin::app::load_scenario_file(file.string(), cfg, error);
    }
}

TEST_CASE("Randomized scenario files never break the loader's guarantees",
          "[scenario][config][property]")
{
    // Whatever the file holds, the loader must either refuse it with a message or hand back
    // a config that satisfies require_sane. Under the sanitizer CI job this is also a fuzz
    // run over the INI parsing paths, the same way the tick reader's property test is.
    const std::vector<std::string> keys = {
        "ticks", "ticks_path", "data", "symbol", "tf", "timeframe", "train_ratio", "ridge",
        "ridge_lambda", "model", "sgd_learning_rate", "sgd_lr", "sgd_l2", "sgd_epochs",
        "sgd_power_t", "sgd_standardize", "online_update", "online", "ema_fast", "ema_slow",
        "rsi", "macd_fast", "macd_slow", "macd_signal", "features", "sma", "sma_period",
        "bb_period", "bb_k", "atr", "adx", "stoch_k", "stoch_d", "zscore", "momentum",
        "rsi_buy", "rsi_sell", "use_ema_crossover", "no_ema_xover", "cash", "initial_cash",
        "qty", "trade_qty", "fee", "fee_per_trade", "model_out", "preview",
        "RSI_BUY", "Qty", "rsi_peroid", "", "  "};
    const std::vector<std::string> values = {
        "14", "0", "1", "-1", "3", "18446744073709551616", "0.7", "2.5", "-0.5", "1e-3",
        "nan", "-nan", "inf", "-inf", "infinity", "1e400", "1e-400", "5abc", "", " 3 ", "+5",
        "0x10", "true", "off", "maybe", "M5", "X9", "sgd", "ridge", "close,rsi,atr",
        "close,,rsi", "close,close", "bogus_feature", "150", "data.csv", "ABC"};
    const std::vector<std::string> separators = {" = ", "=", "==", " "};
    const std::vector<std::string> suffixes = {"", "", "", " # trailing comment", "\r"};

    std::size_t accepted = 0;
    std::size_t refused = 0;

    for (unsigned seed = 1; seed <= 8; ++seed)
    {
        std::mt19937 rng(seed); // fixed seeds: a failure is reproducible, unlike a clock seed
        std::uniform_int_distribution<std::size_t> key_pick(0, keys.size() - 1);
        std::uniform_int_distribution<std::size_t> value_pick(0, values.size() - 1);
        // Weighted towards " = " so most lines are well formed and the files that get past
        // the parser are the ones whose *values* are under test.
        std::discrete_distribution<std::size_t> sep_pick({12, 4, 1, 1});
        std::uniform_int_distribution<std::size_t> suffix_pick(0, suffixes.size() - 1);
        std::uniform_int_distribution<int> line_count(0, 3);
        std::uniform_int_distribution<int> coin(0, 9);

        for (int file_no = 0; file_no < 250; ++file_no)
        {
            std::string ini;
            // Usually present, so the ticks check is not what refuses most files.
            if (coin(rng) != 0)
                ini += "ticks = data.csv\n";

            const int lines = line_count(rng);
            for (int l = 0; l < lines; ++l)
            {
                switch (coin(rng))
                {
                case 0:
                    ini += "# a comment line\n";
                    break;
                case 1:
                    ini += "\n";
                    break;
                default:
                    ini += keys[key_pick(rng)] + separators[sep_pick(rng)] +
                           values[value_pick(rng)] + suffixes[suffix_pick(rng)] + "\n";
                    break;
                }
            }

            const test_files::TempFile file("aiquant_ini_property_", ".ini", ini);
            fin::app::ScenarioConfig cfg{};
            std::string error;
            if (fin::app::load_scenario_file(file.string(), cfg, error))
            {
                ++accepted;
                REQUIRE(error.empty());
                require_sane(cfg);
            }
            else
            {
                ++refused;
                REQUIRE_FALSE(error.empty());
            }
        }
    }

    // A generator that never reaches one of the two outcomes proves nothing about it.
    REQUIRE(accepted >= 200);
    REQUIRE(refused >= 200);
}

TEST_CASE("Non-finite numbers are refused at the line that holds them", "[scenario][config]")
{
    // from_chars parses "nan" and "inf" as doubles, and every range check below is a
    // comparison, which NaN passes by being false both ways. Refusing at the parse site also
    // names the line, which a range check at the end of the file cannot.
    for (const char *line : {"ridge = nan", "bb_k = inf", "train_ratio = nan", "rsi_buy = nan",
                             "cash = inf", "qty = nan", "fee = -inf", "sgd_lr = inf"})
    {
        std::string error;
        REQUIRE_FALSE(loads(std::string("ticks = data.csv\n") + line + "\n", error));
        REQUIRE(error.find("line 2") != std::string::npos);
    }
}

TEST_CASE("Account values that invent money are refused", "[scenario][config]")
{
    // Before this, `qty = -5` reported +844% on the MVP scenario with zero trades: buying a
    // negative quantity pays you. `fee = -1000` turned 18 trades into 18 wins.
    std::string error;
    REQUIRE_FALSE(loads("ticks = data.csv\nqty = -5\n", error));
    REQUIRE(error.find("qty") != std::string::npos);

    error.clear();
    REQUIRE_FALSE(loads("ticks = data.csv\nfee = -1000\n", error));
    REQUIRE(error.find("fee") != std::string::npos);

    error.clear();
    REQUIRE_FALSE(loads("ticks = data.csv\ncash = 0\n", error));
    REQUIRE(error.find("cash") != std::string::npos);

    error.clear();
    REQUIRE_FALSE(loads("ticks = data.csv\nrsi_sell = 150\n", error));
    REQUIRE(error.find("rsi_sell") != std::string::npos);

    // Zero is a legitimate fee, and the boundary must stay open.
    error.clear();
    REQUIRE(loads("ticks = data.csv\nfee = 0\n", error));
}

TEST_CASE("A config built in code is held to the same rules as a file", "[scenario][config]")
{
    // The CLI flags, the Python dict and the HTTP JSON all build a ScenarioConfig without
    // going through the INI loader, so run_scenario is where the rules have to live for them.
    // Before this, `--ridge -1` on the command line trained exactly what the INI refused.
    REQUIRE_FALSE(refused_as_invalid(direct_config()));

    auto ridge = direct_config();
    ridge.ridge_lambda = -1.0;
    REQUIRE(refused_as_invalid(ridge));

    auto ratio = direct_config();
    ratio.train_ratio = std::nan("");
    REQUIRE(refused_as_invalid(ratio));

    auto qty = direct_config();
    qty.trade_qty = -5.0;
    REQUIRE(refused_as_invalid(qty));

    auto period = direct_config();
    period.rsi_period = 0;
    REQUIRE(refused_as_invalid(period));

    auto lr = direct_config();
    lr.sgd.learning_rate = std::numeric_limits<double>::infinity();
    REQUIRE(refused_as_invalid(lr));

    auto online = direct_config();
    online.online_update = true; // with the default ridge model
    REQUIRE(refused_as_invalid(online));
}

TEST_CASE("online_update without sgd is refused when the file is loaded", "[scenario][config]")
{
    // run_scenario always refused it; the loader now does too, so `aiquant run-config` and
    // /run-file report it as a config error with the other config errors.
    std::string error;
    REQUIRE_FALSE(loads("ticks = data.csv\nonline_update = true\n", error));
    REQUIRE(error.find("online_update") != std::string::npos);

    error.clear();
    REQUIRE(loads("ticks = data.csv\nonline_update = true\nmodel = sgd\n", error));
}
