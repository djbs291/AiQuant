#pragma once
#ifndef FIN_STREAM_SYMBOL_MODEL_HPP
#define FIN_STREAM_SYMBOL_MODEL_HPP

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fin/indicators/FeatureSpec.hpp"
#include "fin/io/Options.hpp"
#include "fin/ml/IModel.hpp"

namespace fin::stream
{
    // What one symbol's pipeline predicts with, and what it has to compute to do so. Each
    // optional field left empty falls back to the StreamConfig: `features` to config.features
    // (or the default set), `params` to config.params, `timeframe` to config.timeframe.
    struct SymbolModel
    {
        std::shared_ptr<fin::ml::IModel> model;
        std::vector<std::string> features;
        std::optional<fin::indicators::FeatureParams> params;
        std::optional<fin::io::Timeframe> timeframe;
    };

    // What the user asked for explicitly -- a flag they typed, not a default. Only these can
    // contradict a model file; everything else the file records is simply adopted.
    struct ModelOverrides
    {
        std::vector<std::string> features;
        std::vector<std::pair<std::string, double>> params; // keyed as feature_param_keys()
        std::optional<fin::io::Timeframe> timeframe;
    };

    // Builds the SymbolModel for a model loaded from a file: its recorded feature set, its
    // recorded parameters applied over `base`, and its recorded timeframe. Fails, naming what
    // clashed, when an explicit override disagrees with what the file records -- a model run
    // on features computed differently from how it was trained predicts wrongly and says
    // nothing. A file from before these were recorded gives `base` and the overrides, as
    // before. `model` becomes the SymbolModel's model. Takes any IModel, so a linear model and
    // an MLP are both routed through the same path -- both read their metadata off IModel.
    bool symbol_model_from(std::shared_ptr<fin::ml::IModel> model, const ModelOverrides &overrides,
                           const fin::indicators::FeatureParams &base, SymbolModel &out, std::string &error);

} // namespace fin::stream

#endif // FIN_STREAM_SYMBOL_MODEL_HPP
