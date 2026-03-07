#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "../../shared/entities.hpp"

class WeightedPicker {
public:
    explicit WeightedPicker(const std::vector<FamilyEntry>& entries, uint32_t seed = 0);

    [[nodiscard]] bool Empty() const { return entries_.empty(); }
    [[nodiscard]] size_t Size() const { return entries_.size(); }

    uint32_t Pick();

private:
    std::vector<FamilyEntry> entries_{};
    std::vector<float> cumulativeWeights_{};
    float totalWeight_ = 0.0f;
    std::mt19937 rng_{};
};
