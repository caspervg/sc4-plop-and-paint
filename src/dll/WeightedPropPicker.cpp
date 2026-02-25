#include "WeightedPropPicker.hpp"

#include <algorithm>

WeightedPropPicker::WeightedPropPicker(const std::vector<PaletteEntry>& entries, const uint32_t seed)
    : entries_(entries)
      , rng_(seed ? seed : std::random_device{}()) {
    cumulativeWeights_.reserve(entries_.size());

    float cumulative = 0.0f;
    for (const auto& entry : entries_) {
        cumulative += std::max(entry.weight, 0.01f);
        cumulativeWeights_.push_back(cumulative);
    }
    totalWeight_ = cumulative;
}

uint32_t WeightedPropPicker::Pick() {
    if (entries_.empty()) {
        return 0;
    }
    if (entries_.size() == 1) {
        return entries_[0].propID.value();
    }

    std::uniform_real_distribution<float> dist(0.0f, totalWeight_);
    const float roll = dist(rng_);

    for (size_t i = 0; i < cumulativeWeights_.size(); ++i) {
        if (roll < cumulativeWeights_[i]) {
            return entries_[i].propID.value();
        }
    }

    return entries_.back().propID.value();
}
