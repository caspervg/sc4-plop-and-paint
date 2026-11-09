#include "WeightedPicker.hpp"

#include <algorithm>

WeightedPicker::WeightedPicker(const std::vector<FamilyEntry>& entries, const uint32_t seed)
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

uint32_t WeightedPicker::Pick() {
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

uint32_t WeightedPicker::Cycle(const uint32_t current, const bool backwards) const {
    const size_t count = entries_.size();
    const auto found = std::ranges::find_if(entries_, [current](const FamilyEntry& entry) {
        return entry.propID.value() == current;
    });
    // Not in the list: the first step lands on the first entry, or backwards on the last.
    const size_t start = found != entries_.end() ? static_cast<size_t>(found - entries_.begin())
                         : backwards             ? 0
                                                 : count - 1;
    for (size_t step = 1; step <= count; ++step) {
        const uint32_t id = entries_[(backwards ? start + count - step : start + step) % count].propID.value();
        if (id != current) {
            return id;
        }
    }
    return current;
}
