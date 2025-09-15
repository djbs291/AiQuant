#include <iostream>
#include <string>
#include <vector>
#include <optional>

#include "fin/io/Pipeline.hpp"
#include "fin/backtest/Backtester.hpp"

using namespace fin;

static int cmd_backtest(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: aiquant backtest <ticks.csv>\n";
        return 2;
    }

    const std::string path = args[0];

    fin::io::TickCsvOptions opt{}; // defaults: header, epoch-ms
    auto res = fin::io::resample_csv_m1_with_stats(path, opt);

    fin::backtest::BacktestConfig cfg{}; // defaults
    fin::signal::SignalEngine eng{};     // defaults
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
        std::cout << " backtest <ticks.csv>     Resample M1 and run RSI+EMA strategy\n";
        return 0;
    }

    const std::string cmd = args[0];
    if (cmd == "backtest")
    {
        return cmd_backtest({args.begin() + 1, args.end()});
    }

    std::cerr << "Unknow command: " << cmd << "\n";
    return 1;
}
