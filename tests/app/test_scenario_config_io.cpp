#include "catch2_compat.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "fin/app/ScenarioConfigIO.hpp"
#include "fin/app/ScenarioRunner.hpp"

namespace
{
    std::filesystem::path write_temp_config(std::string_view contents)
    {
        const auto ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        auto path = std::filesystem::temp_directory_path() / ("aiquant_scenario_" + std::to_string(ts) + ".ini");
        std::ofstream out(path);
        out << contents;
        return path;
    }

    std::filesystem::path write_temp_ticks_csv(std::size_t rows)
    {
        const auto ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        auto path = std::filesystem::temp_directory_path() / ("aiquant_ticks_" + std::to_string(ts) + ".csv");
        std::ofstream out(path);
        out << "Timestamp,symbol,price,volume\n";
        long long base_ts = 1693492800000LL;
        for (std::size_t i = 0; i < rows; ++i)
        {
            const long long row_ts = base_ts + static_cast<long long>(i) * 60000;
            const double price = 100.0 + static_cast<double>(i % 10) * 0.5;
            const double volume = 1.0 + static_cast<double>(i % 5);
            out << row_ts << ",TEST," << price << ',' << volume << '\n';
        }
        return path;
    }
}

TEST_CASE("load_scenario_file parses canonical config", "[scenario][config]")
{
    auto path = write_temp_config(R"(
        # Comment line
        ticks = sample.csv
        tf = M5
        train_ratio = 0.8
        ema_fast = 8
        ema_slow = 20
        rsi = 10
        use_ema_crossover = off
        no_ema_xover = on
        preview = 7
    )");

    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE(fin::app::load_scenario_file(path.string(), cfg, error));
    REQUIRE(cfg.ticks_path == "sample.csv");
    REQUIRE(cfg.timeframe == fin::io::Timeframe::M5);
    REQUIRE(cfg.train_ratio == Approx(0.8));
    REQUIRE(cfg.ema_fast == 8);
    REQUIRE(cfg.ema_slow == 20);
    REQUIRE(cfg.rsi_period == 10);
    REQUIRE_FALSE(cfg.use_ema_crossover);
    REQUIRE(cfg.validation_preview_limit == 7);

    std::filesystem::remove(path);
}

TEST_CASE("load_scenario_file detects invalid booleans", "[scenario][config]")
{
    auto path = write_temp_config("ticks=data.csv\nuse_ema_crossover = maybe\n");
    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE_FALSE(fin::app::load_scenario_file(path.string(), cfg, error));
    REQUIRE(error.find("Invalid boolean") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("load_scenario_file requires ticks path", "[scenario][config]")
{
    auto path = write_temp_config("tf = M1\n");
    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE_FALSE(fin::app::load_scenario_file(path.string(), cfg, error));
    REQUIRE(error.find("ticks") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("run_scenario executes on synthetic CSV", "[scenario][runner]")
{
    const auto ticks = write_temp_ticks_csv(200);

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();
    cfg.validation_preview_limit = 2;

    auto result = fin::app::run_scenario(cfg);
    REQUIRE(result.candles > 0);
    REQUIRE(result.feature_rows >= 3);
    REQUIRE(result.training.samples > 0);
    REQUIRE(result.metrics.trades >= 0);
    REQUIRE(result.validation_preview.size() <= cfg.validation_preview_limit);

    std::filesystem::remove(ticks);
}
