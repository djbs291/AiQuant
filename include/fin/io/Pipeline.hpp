#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "fin/io/Options.hpp"   // for TickCsvOptions (complete type for default arg)
#include "fin/io/Sources.hpp"   // FileTickSource
#include "fin/io/Resampler.hpp" // TickToCandleResampler
#include "fin/core/Candle.hpp"

namespace fin::io
{
    struct PipelineResult
    {
        std::vector<fin::core::Candle> candles;
        ReadStats stats;   // rows/parsed/skipped from the source
        std::string error; // non-empty when the file itself was unusable; see FileTickSource::error()

        // The symbol these candles belong to. A tick file may hold several, and a resampler is
        // per-symbol by construction, so exactly one is aggregated and the rest are counted
        // here. Empty only when no tick was ever accepted.
        std::string symbol;
        std::size_t ticks_other_symbol = 0;
    };

    // Reads ticks from CSV and returns candles for ONE symbol.
    //
    // An empty `symbol` binds the run to the first tick's symbol, which is what a single-symbol
    // file does naturally; naming one selects it out of a file that holds several. Ticks for any
    // other symbol are skipped and counted, never merged into the same bar.
    //
    // Merging them is exactly what this used to do: on a file holding ABC around 100 and XYZ
    // around 900, every bar came out opening on one instrument and closing on the other, with a
    // high and a low that straddled both. The symbol was parsed off each tick and then dropped.
    inline PipelineResult
    resample_csv_with_stats(const std::string &path,
                            Timeframe tf,
                            const TickCsvOptions &opt = TickCsvOptions{},
                            const std::string &symbol = std::string{})
    {
        FileTickSource src(path, opt);
        TickToCandleResampler res(tf);

        PipelineResult r{};
        // An explicit choice binds before the first tick arrives, so a file whose first tick is
        // the unwanted symbol still filters correctly.
        r.symbol = symbol;

        while (auto t = src.next())
        {
            const std::string &tick_symbol = t->symbol().value();
            if (r.symbol.empty())
                r.symbol = tick_symbol;

            if (tick_symbol != r.symbol)
            {
                ++r.ticks_other_symbol;
                continue;
            }

            if (auto c = res.update(*t))
                r.candles.push_back(*c);
        }
        if (auto c = res.flush())
            r.candles.push_back(*c);

        r.stats = src.stats();
        r.error = src.error();
        return r;
    }

    // M1 shorthand. Delegates rather than repeating the loop, which is how the two drifted
    // apart in the first place.
    inline PipelineResult
    resample_csv_m1_with_stats(const std::string &path,
                               const TickCsvOptions &opt = TickCsvOptions{},
                               const std::string &symbol = std::string{})
    {
        return resample_csv_with_stats(path, Timeframe::M1, opt, symbol);
    }
}
