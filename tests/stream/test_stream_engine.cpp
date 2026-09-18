#include "catch2_compat.hpp"

#include <memory>
#include <stdexcept>
#include <vector>

#include "fin/io/MockTickSource.hpp"
#include "fin/ml/IModel.hpp"
#include "fin/ml/LinearModel.hpp"
#include "fin/stream/StreamEngine.hpp"
#include "stream/TestStreamHelpers.hpp"

using fin::stream::StreamConfig;
using fin::stream::StreamEngine;
using stream_test::minutely_ticks;
using stream_test::RecordingSink;

namespace
{
    // predict() always throws: stands in for a model that meets a bar it cannot handle.
    class ThrowingModel final : public fin::ml::IModel
    {
    public:
        void reset() override {}
        [[nodiscard]] bool is_ready() const override { return true; }
        [[nodiscard]] double predict(const fin::ml::FeatureVector &) const override
        {
            throw std::runtime_error("this model refuses to predict");
        }
    };

    std::shared_ptr<fin::ml::IModel> close_echo_model()
    {
        // prediction = 1.0 * close + 0.0, so a bar's prediction is simply its own close and
        // the pending-prediction convention becomes visible by inspection.
        auto model = std::make_shared<fin::ml::LinearModel>();
        model->set_named_weights({{"close", 1.0}}, 0.0);
        return model;
    }
}

TEST_CASE("StreamEngine emits one event per candle, the last one partial", "[stream]")
{
    fin::io::MockTickSource source(minutely_ticks(10));

    StreamConfig cfg{};
    RecordingSink sink;
    StreamEngine engine(cfg, nullptr, &sink);
    const auto stats = engine.run(source);

    // Ten one-tick minutes: nine candles close as the bucket rolls, and flush() closes the
    // tenth. resample_csv_with_stats appends that flushed bar too, which is what keeps the
    // counts equal between the two paths.
    REQUIRE(sink.events.size() == 10);
    REQUIRE(stats.candles == 10);
    REQUIRE(stats.ticks == 10);
    REQUIRE(stats.signals == 10);

    for (std::size_t i = 0; i + 1 < sink.events.size(); ++i)
        REQUIRE_FALSE(sink.events[i].partial);
    REQUIRE(sink.events.back().partial);

    // The stream is the first place in the engine that fills the symbol in at all: the batch
    // path has no symbol at Candle level and leaves it empty.
    REQUIRE(engine.bound_symbol() == "ABC");
    for (const auto &event : sink.events)
        REQUIRE(event.symbol == "ABC");
}

TEST_CASE("A candle is judged on the previous candle's prediction", "[stream]")
{
    fin::io::MockTickSource source(minutely_ticks(10));

    StreamConfig cfg{};
    cfg.features = {"close"}; // ready from the first candle, so warmup hides nothing here

    RecordingSink sink;
    StreamEngine engine(cfg, close_echo_model(), &sink);
    engine.run(source);

    REQUIRE(sink.events.size() == 10);

    // The first bar has nothing behind it, so it is judged with no prediction at all.
    REQUIRE_FALSE(sink.events.front().prediction.has_value());

    // Every later bar carries the prediction made on its predecessor, which with this model
    // is exactly the predecessor's close. A one-bar shift here would be invisible in the
    // candle data and would quietly change every trade.
    for (std::size_t i = 1; i < sink.events.size(); ++i)
    {
        REQUIRE(sink.events[i].prediction.has_value());
        REQUIRE(*sink.events[i].prediction ==
                Approx(sink.events[i - 1].candle.close().value()).margin(1e-9));
    }
}

TEST_CASE("No prediction is carried until the whole feature set is warm", "[stream]")
{
    fin::io::MockTickSource source(minutely_ticks(60));

    StreamConfig cfg{}; // the default six, whose warmup is all-or-nothing
    RecordingSink sink;
    StreamEngine engine(cfg, close_echo_model(), &sink);
    const auto stats = engine.run(source);

    std::size_t first_row = sink.events.size();
    std::size_t first_prediction = sink.events.size();
    std::size_t rows = 0;
    for (std::size_t i = 0; i < sink.events.size(); ++i)
    {
        if (sink.events[i].has_row)
        {
            ++rows;
            if (first_row == sink.events.size())
                first_row = i;
        }
        if (sink.events[i].prediction.has_value() && first_prediction == sink.events.size())
            first_prediction = i;
    }

    REQUIRE(rows > 0);
    REQUIRE(rows == stats.feature_rows);
    REQUIRE(first_row < sink.events.size());

    // The invariant, stated without depending on what the warmup length happens to be: the
    // first bar that carries a prediction is exactly one after the first bar that had a row
    // to compute one from.
    REQUIRE(first_prediction == first_row + 1);

    // Everything before that is judged on no prediction at all.
    for (std::size_t i = 0; i <= first_row; ++i)
        REQUIRE_FALSE(sink.events[i].prediction.has_value());
}

TEST_CASE("A model that throws is counted and the stream keeps going", "[stream]")
{
    fin::io::MockTickSource source(minutely_ticks(10));

    StreamConfig cfg{};
    cfg.features = {"close"};

    RecordingSink sink;
    StreamEngine engine(cfg, std::make_shared<ThrowingModel>(), &sink);
    const auto stats = engine.run(source);

    // A live feed must not die because one bar upset the model.
    REQUIRE(sink.events.size() == 10);
    REQUIRE(stats.candles == 10);
    REQUIRE(stats.predictions == 0);
    REQUIRE(stats.prediction_errors == stats.feature_rows);
    REQUIRE(stats.prediction_errors > 0);

    for (const auto &event : sink.events)
        REQUIRE_FALSE(event.prediction.has_value());
}

TEST_CASE("StreamEngine runs without a sink and without a model", "[stream]")
{
    // The rules-only case: no model, nobody listening, but the counters still add up.
    fin::io::MockTickSource source(minutely_ticks(10));

    StreamEngine engine(StreamConfig{});
    const auto stats = engine.run(source);

    REQUIRE(stats.candles == 10);
    REQUIRE(stats.signals == 10);
    REQUIRE(stats.predictions == 0);
    REQUIRE(stats.prediction_errors == 0);
    REQUIRE(stats.buys + stats.sells + stats.holds == stats.signals);
}
