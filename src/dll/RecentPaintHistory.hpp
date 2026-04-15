#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "../shared/entities.hpp"

struct RecentPaintEntry {
    enum class Kind : uint8_t {
        Prop = 0,
        Flora = 1
    };

    enum class SourceKind : uint8_t {
        SingleProp = 0,
        PropAutoFamily = 1,
        PropUserFamily = 2,
        SingleFlora = 3,
        FloraFamily = 4,
        FloraChain = 5
    };

    SourceKind sourceKind{SourceKind::SingleProp};
    uint64_t sourceId{0};
    Kind kind{Kind::Prop};
    uint32_t typeId{0};
    uint64_t thumbnailKey{0};
    std::string name;
    std::vector<FamilyEntry> palette;
};

struct RecentPaintSource {
    RecentPaintEntry::SourceKind sourceKind{RecentPaintEntry::SourceKind::SingleProp};
    uint64_t sourceId{0};
};

class RecentPaintHistory {
public:
    static constexpr size_t kDefaultMaxEntries = 8;

    void Push(const RecentPaintEntry& entry) {
        std::erase_if(entries_, [&](const RecentPaintEntry& e) {
            return e.sourceKind == entry.sourceKind && e.sourceId == entry.sourceId;
        });

        entries_.push_front(entry);
        Trim_();
    }

    [[nodiscard]] const std::deque<RecentPaintEntry>& Entries() const { return entries_; }
    [[nodiscard]] bool Empty() const { return entries_.empty(); }
    [[nodiscard]] size_t Size() const { return entries_.size(); }
    [[nodiscard]] size_t MaxEntries() const { return maxEntries_; }

    void Clear() { entries_.clear(); }
    void SetMaxEntries(const size_t maxEntries) {
        maxEntries_ = std::max<size_t>(1, maxEntries);
        Trim_();
    }

    [[nodiscard]] std::vector<RecentPaintEntryData> Serialize() const {
        std::vector<RecentPaintEntryData> result;
        result.reserve(entries_.size());
        for (const auto& e : entries_) {
            RecentPaintEntryData serialized;
            serialized.sourceKind = static_cast<uint8_t>(e.sourceKind);
            serialized.sourceId = rfl::Hex<uint64_t>(e.sourceId);
            serialized.kind = static_cast<uint8_t>(e.kind);
            serialized.typeId = rfl::Hex<uint32_t>(e.typeId);
            serialized.thumbnailKey = rfl::Hex<uint64_t>(e.thumbnailKey);
            serialized.name = e.name;
            serialized.palette = e.palette;
            result.push_back(std::move(serialized));
        }
        return result;
    }

    void Deserialize(const std::vector<RecentPaintEntryData>& serialized) {
        entries_.clear();
        for (const auto& entry : serialized) {
            RecentPaintEntry e;
            e.sourceKind = static_cast<RecentPaintEntry::SourceKind>(entry.sourceKind);
            e.sourceId = entry.sourceId.value();
            e.kind = static_cast<RecentPaintEntry::Kind>(entry.kind);
            e.typeId = entry.typeId.value();
            e.thumbnailKey = entry.thumbnailKey.value();
            e.name = entry.name;
            e.palette = entry.palette;
            entries_.push_back(std::move(e));
        }

        Trim_();
    }

    template <typename PropLookup, typename FloraLookup, typename PropThumbnailLookup, typename FloraThumbnailLookup>
    void Validate(PropLookup&& propExists,
                  FloraLookup&& floraExists,
                  PropThumbnailLookup&& propThumbnailFor,
                  FloraThumbnailLookup&& floraThumbnailFor) {
        auto existsFor = [&](const RecentPaintEntry::Kind kind, const uint32_t typeId) {
            if (kind == RecentPaintEntry::Kind::Prop) {
                return static_cast<bool>(propExists(typeId));
            }
            return static_cast<bool>(floraExists(typeId));
        };

        auto thumbnailFor = [&](const RecentPaintEntry::Kind kind, const uint32_t typeId) {
            if (kind == RecentPaintEntry::Kind::Prop) {
                return propThumbnailFor(typeId);
            }
            return floraThumbnailFor(typeId);
        };

        for (auto it = entries_.begin(); it != entries_.end();) {
            auto& entry = *it;
            if (!entry.palette.empty()) {
                std::erase_if(entry.palette, [&](const FamilyEntry& member) {
                    return !existsFor(entry.kind, member.propID.value());
                });
                if (entry.palette.empty()) {
                    it = entries_.erase(it);
                    continue;
                }
                if (!existsFor(entry.kind, entry.typeId)) {
                    entry.typeId = entry.palette.front().propID.value();
                }
            }
            else if (!existsFor(entry.kind, entry.typeId)) {
                it = entries_.erase(it);
                continue;
            }

            entry.thumbnailKey = thumbnailFor(entry.kind, entry.typeId);
            ++it;
        }
    }

private:
    void Trim_() {
        while (entries_.size() > maxEntries_) {
            entries_.pop_back();
        }
    }

    size_t maxEntries_{kDefaultMaxEntries};
    std::deque<RecentPaintEntry> entries_;
};
