#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "DecalSidecarPayload.hpp"

namespace TerrainDecal
{
    struct OverlayUvSubrect;
}

namespace PlopAndPaint::Sidecar
{
    // Runtime state container. Holds the current known-good decal metadata
    // between save/load events, keyed by world position (bit-exact float
    // comparison — decal props don't perturb position between save and the
    // InsertOccupant that fires on load).
    //
    // No thread-safety: all usage is on SC4's main thread.
    class DecalSidecarRepository
    {
    public:
        DecalSidecarRepository() = default;

        void Clear() noexcept;

        // Replaces the current state with whatever was deserialized. Unknown
        // chunks / fields travel with it so they survive the round-trip.
        void LoadFromDocument(SidecarDocument document);

        // Snapshots the current state into a fresh SidecarDocument suitable
        // for WriteSidecarDocument.
        [[nodiscard]] SidecarDocument BuildDocument() const;

        // Inserts or overwrites the entry for the given world position. The
        // position in `entry` is normalized to match.
        void SetEntry(const DecalEntry& entry);

        // Removes the entry at a given world position.
        // Returns true if an entry was removed.
        bool RemoveAt(const WorldPos& pos) noexcept;

        // Returns a copy of the entry at a given world position.
        [[nodiscard]] std::optional<DecalEntry> FindByPos(const WorldPos& pos) const;

        [[nodiscard]] size_t EntryCount() const noexcept { return entries_.size(); }

        // Preserved unknown chunks from the last load, re-emitted verbatim on save.
        [[nodiscard]] const std::vector<UnknownChunk>& UnknownChunks() const noexcept { return unknownChunks_; }

        // Version metadata from the last document loaded. On a fresh repository
        // these are zero so BuildDocument stamps the current writer version.
        [[nodiscard]] uint16_t LoadedVersionMajor() const noexcept { return loadedVersionMajor_; }
        [[nodiscard]] uint16_t LoadedVersionMinor() const noexcept { return loadedVersionMinor_; }
        [[nodiscard]] uint32_t LoadedFlags() const noexcept { return loadedFlags_; }

        // Iteration helper — returns a snapshot copy so callers never alias
        // internal storage. Called at save-time.
        [[nodiscard]] std::vector<DecalEntry> SnapshotEntries() const;

        // UvSubrect <-> TerrainDecal::OverlayUvSubrect translation. Separate
        // from the document model so the sidecar layer stays decoupled from
        // the renderer's concrete types.
        static UvSubrect FromRenderer(const TerrainDecal::OverlayUvSubrect& src) noexcept;
        static TerrainDecal::OverlayUvSubrect ToRenderer(const UvSubrect& src) noexcept;

    private:
        struct PosKey
        {
            uint32_t x = 0;
            uint32_t y = 0;
            uint32_t z = 0;

            bool operator==(const PosKey& other) const noexcept = default;
        };

        struct PosKeyHash
        {
            size_t operator()(const PosKey& k) const noexcept
            {
                // Trivial 3-mix FNV-like hash; good enough for per-city entry counts.
                uint64_t h = 1469598103934665603ull;
                h = (h ^ k.x) * 1099511628211ull;
                h = (h ^ k.y) * 1099511628211ull;
                h = (h ^ k.z) * 1099511628211ull;
                return static_cast<size_t>(h ^ (h >> 32));
            }
        };

        static PosKey MakeKey_(const WorldPos& pos) noexcept;

        std::unordered_map<PosKey, DecalEntry, PosKeyHash> entries_;
        std::vector<UnknownChunk> unknownChunks_;
        uint16_t loadedVersionMajor_ = 0;
        uint16_t loadedVersionMinor_ = 0;
        uint32_t loadedFlags_ = 0;
    };
}
