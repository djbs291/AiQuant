#include "catch2_compat.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "TestTempFiles.hpp"
#include "fin/ml/LinearModel.hpp"
#include "fin/ml/LinearTrainer.hpp"

using fin::ml::LinearModel;
using Params = std::vector<std::pair<std::string, double>>;

TEST_CASE("A model file records its parameters and timeframe, and reads them back", "[ml][params]")
{
    const test_files::TempDir dir("aiquant_model_params_");
    const auto path = (dir.path() / "m.csv").string();

    LinearModel model;
    model.set_named_weights({{"close", 1.5}, {"rsi", 0.25}, {"bb_mid", -0.5}}, 0.5);
    const Params params{{"rsi", 10.0}, {"bb_period", 20.0}, {"bb_k", 2.5}};
    model.set_training_params(params);
    model.set_timeframe("M5");
    REQUIRE(fin::ml::save_linear_model(model, path));

    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    REQUIRE(text.str().find("# params: rsi=10,bb_period=20,bb_k=2.5\n") != std::string::npos);
    REQUIRE(text.str().find("# timeframe: M5\n") != std::string::npos);

    LinearModel loaded;
    REQUIRE(loaded.load_from_file(path));
    REQUIRE(loaded.training_params() == params);
    REQUIRE(loaded.timeframe() == "M5");
}

TEST_CASE("A model file from before the parameters loads without them", "[ml][params]")
{
    const test_files::TempDir dir("aiquant_model_params_legacy_");
    const auto path = dir.write("m.csv", "# features: close\nbias,0.5\nclose,1.5\n");
    LinearModel loaded;
    REQUIRE(loaded.load_from_file(path.string()));
    REQUIRE(loaded.training_params().empty());
    REQUIRE(loaded.timeframe().empty());
}

TEST_CASE("A corrupt parameters line fails the load", "[ml][params]")
{
    // Machine-written, so a value that does not read means corruption, as for a weight:
    // loading it would rebuild the features with periods nobody trained on.
    const test_files::TempDir dir("aiquant_model_params_corrupt_");
    for (const char *line : {"# params: rsi=abc", "# params: rsi=0", "# params: nope=3", "# params: rsi",
                             "# params: rsi=10.5", "# params: bb_k=-1"})
    {
        const auto path = dir.write("m.csv", std::string("# features: close\n") + line + "\nbias,0.5\nclose,1.5\n");
        LinearModel loaded;
        REQUIRE_FALSE(loaded.load_from_file(path.string()));
    }
}
