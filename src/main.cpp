#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <charconv>

#include "fin/io/Pipeline.hpp"
#include "fin/backtest/Backtester.hpp"

using namespace fin;

static std::optional<double> parse_double_flag(const std::vector<std::string> &args,
                                               const std::string &flag)
{
    for (std::size_t i = 1; i + 1 < args.size(); ++i) // start after path (args[0])
    {
        if (args[i] == flag)
        {
            const std::string &s = args[i + 1];
            double v = 0.0;
            auto *b = s.data();
            auto *e = s.data() + s.size();
            if (auto [p, ec] = std::from_chars(b, e, v); ec == std::errc{})
                return v;
        }
    }
    return std::nullopt;
}

static fin::io::Timeframe parse_timeframe_flag(const std::vector<std::string> &args)
{
    for (std::size_t i = 1; i + 1 < args.size(); ++i)
    {
        if (args[i] == "--tf")
        {
            const std::string &tf = args[i + 1];
            if (tf == "S1")
                return fin::io::Timeframe::S1;
            if (tf == "S5")
                return fin::io::Timeframe::S5;
            if (tf == "M5")
                return fin::io::Timeframe::M5;
            if (tf == "H1")
                return fin::io::Timeframe::H1;
            return fin::io::Timeframe::M1;
        }
    }
    return fin::io::Timeframe::M1;
}

static int cmd_backtest(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: aiquant backtest <ticks.csv> [--tf S1|S5|M1|M5|H1] [--cash N] [--qty N] [--fee N]\n";
        return 2;
    }

    const std::string path = args[0];

    fin::io::TickCsvOptions opt{}; // defaults: header, epoch-ms
    auto tf = parse_timeframe_flag(args);
    auto res = fin::io::resample_csv_with_stats(path, tf, opt);

    fin::backtest::BacktestConfig cfg{}; // defaults
    if (auto v = parse_double_flag(args, "--cash"))
        cfg.initial_cash = *v;
    if (auto v = parse_double_flag(args, "--qty"))
        cfg.trade_qty = *v;
    if (auto v = parse_double_flag(args, "--fee"))
        cfg.fee_per_trade = *v;
    fin::signal::SignalEngine eng{}; // defaults
    fin::backtest::Backtester bt(cfg, eng);

    for (const auto &c : res.candles)
        bt.on_candle(c);
    auto m = bt.finalize();

    std::cout << "Candles: " << res.candles.size() << "\n";
    std::cout << "Rows: " << res.stats.rows << ", Parsed: " << res.stats.parsed << ", Skipped: " << res.stats.skipped << "\n";
    std::cout << "Final Cash: " << m.final_cash << "\n";
    std::cout << "PnL: " << m.pnl << " (" << m.return_pct << "%)\n";
    std::cout << "Max DD: " << m.max_drawdown << "%\n";
    std::cout << "Trades: " << m.trades << ", Wins: " << m.wins << ", Losses: " << m.losses << "\n";
    return 0;
}

int main(int argc, char **argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty())
    {
        std::cout << "AiQuant CLI (MVP)\n";
        std::cout << "Commands: \n";
        std::cout << "  backtest <ticks.csv> [--tf S1|S5|M1|M5|H1] [--cash N] [--qty N] [--fee N]\n";
        std::cout << "    Resample candles and run RSI+EMA strategy\n";
        return 0;
    }

    const std::string cmd = args[0];
    if (cmd == "backtest")
    {
        return cmd_backtest({args.begin() + 1, args.end()});
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
