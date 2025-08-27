#include "fin/indicators/MACD.hpp"

namespace fin::indicators
{

    MACD::MACD(std::size_t fast, std::size_t slow, std::size_t signal)
        : fast_(fast),
          slow_(slow),
          signal_(signal),
          ema_fast_(fast),
          ema_slow_(slow),
          ema_signal_(signal),
          current_(std::nullopt)
    {
    }

    void MACD::reset()
    {
        ema_fast_.reset();
        ema_slow_.reset();
        ema_signal_.reset();
        current_.reset();
    }

    std::optional<MACDValue> MACD::update(double close)
    {
        auto f = ema_fast_.update(close);
        auto s = ema_slow_.update(close);

        // Need both EMAs to comput a MACD value
        if (!f.has_value() || !s.has_value())
        {
            return std::nullopt;
        }

        double macd_line = *f - *s;

        // Signal EMA runs on the MACD stream
        auto sig = ema_signal_.update(macd_line);
        if (!sig.has_value())
        {
            return std::nullopt;
        }

        MACDValue out{macd_line, *sig, macd_line - *sig};
        current_ = out;
        return current_;
    }

    std::vector<std::optional<MACDValue>>
    MACD::compute(const std::vector<double> &closes,
                  std::size_t fast, std::size_t slow, std::size_t signal)
    {

        std::vector<std::optional<MACDValue>> out;
        out.reserve(closes.size());
        MACD macd(fast, slow, signal);
        for (double c : closes)
            out.emplace_back(macd.update(c));
        return out;
    }

} // namespace fin::indicators
