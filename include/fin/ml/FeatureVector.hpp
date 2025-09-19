#pragma once
#ifndef FIN_ML_FEATURE_VECTOR_HPP
#define FIN_ML_FEATURE_VECTOR_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fin/core/Timestamp.hpp"

namespace fin::indicators
{
    struct FeatureRow;
}

namespace fin::ml
{
    /**
     * @brief LightWeight container for a single feature vector
     *
     * The vector keeps the chronological context (timestamp) and optionally a
     * list of feature names so that models can either work by positional index
     * or perform name-based lookups when the ordering is not guaranteed
     */
    struct FeatureVector
    {
        fin::core::Timestamp ts{};        // Feature timestamp (candle start)
        std::vector<std::string> names{}; // Optional ordered feature names.
        std::vector<double> values{};     // Feature values aligned with "names"

        [[nodiscard]] std::size_t size() const noexcept { return values.size(); }
        [[nodiscard]] bool empty() const noexcept { return values.empty(); }
        [[nodiscard]] double operator[](std::size_t idx) const noexcept { return values[idx]; }

        // Returns the value for the named feature if available
        std::optional<double> value_of(std::string_view name) const;

        // Helper used by the MVP feature pipeline (FeatureBus -> model input)
        static FeatureVector from_feature_row(const fin::indicators::FeatureRow &row);
    };
}

#endif /*FIN_ML_FEATURE_VECTOR_HPP*/
