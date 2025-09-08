#pragma once
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
        ReadStats stats; // rows/parsed/skipped from the source
    };

    // Reads ticks from CSV and returns M1 candles (UTC, no gap fill)

    inline PipelineResult
    resample_csv_m1_with_stats(const std::string &path, const TickCsvOptions &opt = TickCsvOptions{})
    {
        FileTickSource src(path, opt);
        TickToCandleResampler res(Timeframe::M1);

        PipelineResult r{};
        while (auto t = src.next())
        {
            if (auto c = res.update(*t))
                r.candles.push_back(*c);
        }
        if (auto c = res.flush())
            r.candles.push_back(*c);

        r.stats = src.stats();
        return r;
    }
}
