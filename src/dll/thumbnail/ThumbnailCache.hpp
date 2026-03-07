#pragma once
#include <list>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "../common/Constants.hpp"
#include "../utils/Logger.h"
#include "public/ImGuiTexture.h"

template <typename KeyType>
class ThumbnailCache {
public:
    explicit ThumbnailCache(
        const size_t maxSize = Cache::kMaxSize,
        const size_t maxLoadPerFrame = Cache::kMaxLoadPerFrame)
            : maxSize_(maxSize)
            , maxLoadPerFrame_(maxLoadPerFrame) {}

    ~ThumbnailCache() = default;

    ThumbnailCache(const ThumbnailCache&) = delete;
    ThumbnailCache& operator=(const ThumbnailCache&) = delete;

    ThumbnailCache(ThumbnailCache&&) = default;
    ThumbnailCache& operator=(ThumbnailCache&&) = default;

    std::optional<void*> Get(const KeyType& key) {
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            return std::nullopt;
        }
        lruList_.splice(lruList_.begin(), lruList_, it->second.lruIter);

        return it->second.texture.GetID();
    }

    [[nodiscard]] bool Contains(const KeyType& key) const {
        return cache_.contains(key);
    }

    void Insert(const KeyType& key, ImGuiTexture value) {
        // If key already exists, update it
        auto existing = cache_.find(key);
        if (existing != cache_.end()) {
            existing->second.texture = std::move(value);
            lruList_.splice(lruList_.begin(), lruList_, existing->second.lruIter);
            return;
        }

        // Evict if necessary
        while (cache_.size() >= maxSize_ && !lruList_.empty()) {
            const KeyType& evictedKey = lruList_.back();
            cache_.erase(evictedKey);
            lruList_.pop_back();
        }

        // Insert new entry
        lruList_.push_front(key);
        CacheEntry entry{
            .texture = std::move(value),
            .lruIter = lruList_.begin()
        };
        cache_.emplace(key, std::move(entry));
    }

    void Request(const KeyType& key) {
        // Don't request if already in cache, pending, or known to fail.
        if (cache_.contains(key) || loading_.contains(key) || failed_.contains(key)) {
            return;
        }
        loadQueue_.push_back(key);
        loading_.insert(key);
    }

    void Clear() {
        cache_.clear();
        lruList_.clear();
        loadQueue_.clear();
        loading_.clear();
        failed_.clear();
    }

    [[nodiscard]] size_t Size() const {
        return cache_.size();
    }

    [[nodiscard]] size_t MaxSize() const {
        return maxSize_;
    }

    [[nodiscard]] bool IsQueueEmpty() const {
        return loadQueue_.empty();
    }

    template<typename LoaderFunc>
    void ProcessLoadQueue(LoaderFunc&& loader) {
        size_t loaded = 0;
        while (!loadQueue_.empty() && loaded < maxLoadPerFrame_) {
            const KeyType key = loadQueue_.front();
            loadQueue_.pop_front();
            loading_.erase(key);

            // Skip if already loaded - race condition?
            if (cache_.contains(key)) {
                continue;
            }

            ImGuiTexture texture = loader(key);
            if (texture.GetID() != nullptr) {
                failed_.erase(key);
                Insert(key, std::move(texture));
            } else {
                failed_.insert(key);
                LOG_WARN("Loading texture for {} failed", key);
            }
            loaded += 1;
        }
    }

    void OnDeviceReset() {
        Clear();
    }

private:
    struct CacheEntry {
        ImGuiTexture texture;
        std::list<KeyType>::iterator lruIter;
    };

    size_t maxSize_;
    size_t maxLoadPerFrame_;

    std::list<KeyType> lruList_;
    std::unordered_map<KeyType, CacheEntry> cache_;
    std::list<KeyType> loadQueue_;
    std::unordered_set<KeyType> loading_;
    std::unordered_set<KeyType> failed_;
};
