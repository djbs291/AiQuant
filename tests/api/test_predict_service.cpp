#include "catch2_compat.hpp"

#include <stdexcept>
#include <string>

#include "TestTempFiles.hpp"
#include "fin/api/PredictService.hpp"

using fin::api::PredictRequest;
using fin::api::PredictService;
using fin::api::SignalRequest;

namespace
{
    // bias 0.5, close 0.1, rsi -0.02
    const char *kModelCsv = "# AiQuant LinearModel weights\n"
                            "# features: close,rsi\n"
                            "bias,0.5\n"
                            "close,0.1\n"
                            "rsi,-0.02\n";

    bool throws_invalid_argument(const PredictService &service, const PredictRequest &request)
    {
        try
        {
            (void)service.predict(request); // the value is irrelevant; the throw is the subject
        }
        catch (const std::invalid_argument &)
        {
            return true;
        }
        return false;
    }
}

TEST_CASE("predict applies the model to named features", "[api][predict]")
{
    const test_files::TempFile model("aiquant_predict_model_", ".csv", kModelCsv);
    const PredictService service;

    PredictRequest request;
    request.model_path = model.string();
    request.features = {{"rsi", 50.0}, {"close", 100.0}}; // order differs from the model's

    const auto response = service.predict(request);
    // Ordered by the model, not by the request: 0.5 + 0.1*100 - 0.02*50
    // Extra parentheses: the comma inside the braces would otherwise look like a second macro
    // argument to the bundled minicatch, whose REQUIRE is not variadic.
    REQUIRE((response.features == std::vector<std::string>{"close", "rsi"}));
    REQUIRE(response.prediction == Approx(9.5).margin(1e-9));
    REQUIRE(response.model_path == model.string());
}

TEST_CASE("predict refuses missing and unexpected features", "[api][predict]")
{
    const test_files::TempFile model("aiquant_predict_model_", ".csv", kModelCsv);
    const PredictService service;

    PredictRequest missing;
    missing.model_path = model.string();
    missing.features = {{"close", 100.0}};
    REQUIRE(throws_invalid_argument(service, missing));

    PredictRequest unexpected;
    unexpected.model_path = model.string();
    unexpected.features = {{"close", 100.0}, {"rsi", 50.0}, {"atr", 1.0}};
    REQUIRE(throws_invalid_argument(service, unexpected));

    PredictRequest empty;
    empty.model_path = model.string();
    REQUIRE(throws_invalid_argument(service, empty));
}

TEST_CASE("predict reports a missing or unreadable model", "[api][predict]")
{
    const PredictService without_default;

    PredictRequest no_model;
    no_model.features = {{"close", 100.0}};
    REQUIRE(throws_invalid_argument(without_default, no_model));

    PredictRequest bad_path;
    bad_path.model_path = "/nonexistent/model.csv";
    bad_path.features = {{"close", 100.0}};
    REQUIRE(throws_invalid_argument(without_default, bad_path));
}

TEST_CASE("predict falls back to the service's default model", "[api][predict]")
{
    const test_files::TempFile model("aiquant_predict_model_", ".csv", kModelCsv);
    const PredictService service(model.string());

    PredictRequest request;
    request.features = {{"close", 100.0}, {"rsi", 50.0}};

    const auto response = service.predict(request);
    REQUIRE(response.prediction == Approx(9.5).margin(1e-9));
    REQUIRE(response.model_path == model.string());
}

TEST_CASE("predict accepts a model file that records no feature set", "[api][predict]")
{
    const test_files::TempFile legacy("aiquant_predict_legacy_", ".csv",
                                      "# AiQuant LinearModel weights\n"
                                      "bias,0.2\n"
                                      "close,0.1\n");
    const PredictService service;

    PredictRequest request;
    request.model_path = legacy.string();
    request.features = {{"close", 100.0}};

    const auto response = service.predict(request);
    REQUIRE(response.prediction == Approx(10.2).margin(1e-9));
    REQUIRE((response.features == std::vector<std::string>{"close"}));
}

TEST_CASE("signal evaluates the rules with an explicit prediction", "[api][signal]")
{
    const PredictService service;

    SignalRequest request;
    request.close = 100.0;
    request.rsi = 20.0;       // <= 30 -> +1
    request.ema_fast = 11.0;  // fast > slow -> +1
    request.ema_slow = 10.0;
    request.prediction = 0.5; // positive -> +0.5

    const auto response = service.signal(request);
    REQUIRE(response.prediction.has_value());
    REQUIRE(response.signal.type == fin::signal::SignalType::Buy);
    REQUIRE(response.signal.score == Approx(2.5).margin(1e-9));
}

TEST_CASE("signal works with no model and no prediction", "[api][signal]")
{
    const PredictService service;

    SignalRequest request;
    request.close = 100.0;
    request.rsi = 80.0; // >= 70 -> -1

    const auto response = service.signal(request);
    REQUIRE_FALSE(response.prediction.has_value());
    REQUIRE(response.signal.type == fin::signal::SignalType::Sell);
    REQUIRE(response.signal.score == Approx(-1.0).margin(1e-9));
}

TEST_CASE("signal feeds the model prediction to the engine", "[api][signal]")
{
    const test_files::TempFile model("aiquant_predict_model_", ".csv", kModelCsv);
    const PredictService service(model.string());

    SignalRequest request;
    request.close = 100.0;
    request.rsi = 50.0; // neutral: no RSI contribution
    request.predict.features = {{"close", 100.0}, {"rsi", 50.0}};

    const auto response = service.signal(request);
    REQUIRE(response.prediction.has_value());
    REQUIRE(*response.prediction == Approx(9.5).margin(1e-9));
    // Extra parentheses: the comma inside the braces would otherwise look like a second macro
    // argument to the bundled minicatch, whose REQUIRE is not variadic.
    REQUIRE((response.features == std::vector<std::string>{"close", "rsi"}));
    // Only the model contributes: +0.5 -> Buy
    REQUIRE(response.signal.score == Approx(0.5).margin(1e-9));
    REQUIRE(response.signal.type == fin::signal::SignalType::Buy);
}
