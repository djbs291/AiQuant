#pragma once
#include "fin/indicators/IIndicator.hpp"
#include "fin/indicators/VWAP.hpp"

namespace fin::indicators
{

    // ---------- VWAP (price+volume required) ----------
    class VWAPIndicatorPV final : public IIndicatorScalarPV
    {
    public:
        VWAPIndicatorPV() = default;
        void update(double price, double volume) override { last_ = vwap_.update(price, volume); }
        bool is_ready() const override { return last_.has_value(); } // VWAP available from first tick
        double value() const override { return *last_; }
        void reset_session() { vwap_.reset_session(); }

    private:
        VWAP vwap_;
        std::optional<double> last_;
    };

} // namespace fin::indicators
