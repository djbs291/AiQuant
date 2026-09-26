#include "catch2_compat.hpp"

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "TestTempFiles.hpp"
#include "fin/ml/LinearModel.hpp"
#include "fin/ml/LinearTrainer.hpp"

using fin::ml::LinearModel;

namespace
{
    using ModelMap = std::unordered_map<std::string, std::shared_ptr<LinearModel>>;

    LinearModel model_for(const std::string &symbol, double close_weight)
    {
        LinearModel model;
        model.set_named_weights({{"close", close_weight}, {"rsi", 0.25}}, 0.5);
        model.set_symbol(symbol);
        return model;
    }

    std::string read_file(const std::filesystem::path &path)
    {
        std::ifstream in(path);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }
}

TEST_CASE("A model file records the symbol it was trained on", "[ml][model_dir]")
{
    const test_files::TempDir dir("aiquant_model_symbol_");
    const auto path = dir.path() / "ABC.csv";
    REQUIRE(fin::ml::save_linear_model(model_for("ABC", 1.5), path.string()));
    REQUIRE(read_file(path).find("# symbol: ABC\n") != std::string::npos);

    LinearModel loaded;
    REQUIRE(loaded.load_from_file(path.string()));
    REQUIRE(loaded.symbol() == "ABC");
    const std::vector<std::string> recorded{"close", "rsi"};
    REQUIRE(loaded.feature_names() == recorded);
}

TEST_CASE("A model file from before the symbol line still loads", "[ml][model_dir]")
{
    // Files written before "# symbol:" existed carry no symbol; they load with an empty one,
    // and a model directory then takes the symbol from the file name alone.
    const test_files::TempDir dir("aiquant_model_legacy_");
    const auto path = dir.write("ABC.csv", "# AiQuant LinearModel weights\n# features: close\nbias,0.5\nclose,1.5\n");

    LinearModel loaded;
    REQUIRE(loaded.load_from_file(path.string()));
    REQUIRE(loaded.symbol().empty());

    ModelMap models;
    std::string error;
    REQUIRE(fin::ml::load_linear_model_dir(dir.string(), models, error));
    REQUIRE(models.count("ABC") == 1);
}

TEST_CASE("A model directory loads one model per symbol, keyed on the file name", "[ml][model_dir]")
{
    const test_files::TempDir dir("aiquant_model_dir_");
    REQUIRE(fin::ml::save_linear_model(model_for("ABC", 1.5), (dir.path() / "ABC.csv").string()));
    REQUIRE(fin::ml::save_linear_model(model_for("XYZ", -2.0), (dir.path() / "XYZ.csv").string()));
    // Neither of these is a model: another extension, and a dotfile.
    dir.write("notes.txt", "not a model\n");
    dir.write(".hidden.csv", "garbage that would not load\n");

    ModelMap models;
    std::string error;
    REQUIRE(fin::ml::load_linear_model_dir(dir.string(), models, error));
    REQUIRE(error.empty());
    REQUIRE(models.size() == 2);
    REQUIRE(models.at("ABC")->symbol() == "ABC");
    REQUIRE(models.at("XYZ")->symbol() == "XYZ");
    // Each file's own weights, not one model shared under two names.
    REQUIRE(models.at("ABC")->named_weights()[0].second == Approx(1.5));
    REQUIRE(models.at("XYZ")->named_weights()[0].second == Approx(-2.0));
}

TEST_CASE("A model saved under another symbol's name is refused", "[ml][model_dir]")
{
    // The mistake per-symbol models exist to prevent: ABC's model scoring XYZ.
    const test_files::TempDir dir("aiquant_model_misnamed_");
    REQUIRE(fin::ml::save_linear_model(model_for("ABC", 1.5), (dir.path() / "ABC.csv").string()));
    REQUIRE(fin::ml::save_linear_model(model_for("ABC", 1.5), (dir.path() / "XYZ.csv").string()));

    ModelMap models;
    models.emplace("untouched", nullptr);
    std::string error;
    REQUIRE_FALSE(fin::ml::load_linear_model_dir(dir.string(), models, error));
    REQUIRE(error.find("XYZ.csv") != std::string::npos);
    REQUIRE(error.find("'ABC'") != std::string::npos);
    // A failed load leaves the caller's map as it was.
    REQUIRE(models.size() == 1);
    REQUIRE(models.count("untouched") == 1);
}

TEST_CASE("A model directory with a corrupt file is refused by name", "[ml][model_dir]")
{
    const test_files::TempDir dir("aiquant_model_corrupt_");
    REQUIRE(fin::ml::save_linear_model(model_for("ABC", 1.5), (dir.path() / "ABC.csv").string()));
    dir.write("XYZ.csv", "bias,0.5\nclose,0.5abc\n");

    ModelMap models;
    std::string error;
    REQUIRE_FALSE(fin::ml::load_linear_model_dir(dir.string(), models, error));
    REQUIRE(error.find("XYZ.csv") != std::string::npos);
}

TEST_CASE("A missing or empty model directory is refused", "[ml][model_dir]")
{
    ModelMap models;
    std::string error;
    REQUIRE_FALSE(fin::ml::load_linear_model_dir("/no/such/aiquant/model/dir", models, error));
    REQUIRE(error.find("not found") != std::string::npos);

    const test_files::TempDir empty("aiquant_model_empty_");
    empty.write("readme.txt", "no models here\n");
    error.clear();
    REQUIRE_FALSE(fin::ml::load_linear_model_dir(empty.string(), models, error));
    REQUIRE(error.find("No model files") != std::string::npos);
}
