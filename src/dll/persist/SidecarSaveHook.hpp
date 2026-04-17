#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "DecalSidecarRepository.hpp"
#include "cISC4Occupant.h"
#include "cISC4PropOccupant.h"
#include "cISC4PropOccupantTerrainDecal.h"
#include "cRZAutoRefCount.h"

class cIGZMessage2;
class cIGZMessage2Standard;
class cISC4City;

namespace TerrainDecal
{
    class TerrainDecalHook;
}

namespace PlopAndPaint::Sidecar
{
    // SC4 savegame-lifecycle message IDs.
    inline constexpr uint32_t kSC4MessageLoad = 0x26C63341;
    inline constexpr uint32_t kSC4MessageSave = 0x26C63344;
    inline constexpr uint32_t kSC4MessageInsertOccupant = 0x99EF1142;
    inline constexpr uint32_t kSC4MessageRemoveOccupant = 0x99EF1143;

    // Owns a DecalSidecarRepository and maps SC4 save/load messages into it.
    //
    // The director subscribes its own cIGZMessageTarget2 to the four IDs above
    // (Load/Save/Insert/Remove) and forwards them here via HandleMessage. This
    // keeps registration ownership in one place (the director) and makes this
    // class a pure logic component with no COM dependencies of its own.
    //
    // Threading: SC4 main thread only.
    class SidecarSaveHook
    {
    public:
        SidecarSaveHook();
        ~SidecarSaveHook();

        SidecarSaveHook(const SidecarSaveHook&) = delete;
        SidecarSaveHook& operator=(const SidecarSaveHook&) = delete;

        // Binds the optional terrain decal hook used to apply restored UV
        // subrect state at load time. Pass nullptr to detach.
        void Install(TerrainDecal::TerrainDecalHook* terrainDecalHook) noexcept;
        void Uninstall() noexcept;

        // Called from the director's DoMessage. Returns true if the message
        // was recognized and handled, false otherwise.
        bool HandleMessage(cIGZMessage2* message);

        // Clears the repository — call when the current city is tearing down
        // and before a new one loads, to avoid leaking entries across cities.
        void OnCityShutdown();

        [[nodiscard]] DecalSidecarRepository& Repository() noexcept { return repository_; }
        [[nodiscard]] const DecalSidecarRepository& Repository() const noexcept { return repository_; }

        struct RuntimeOverlayInfo
        {
            float baseSize = 16.0f;
            float rotationTurns = 0.0f;
            float aspectMultiplier = 1.0f;
            float uvScaleU = 1.0f;
            float uvScaleV = 1.0f;
            float uvOffset = 0.0f;
            float unknown8 = 0.0f;
        };

        struct RuntimeDecalSnapshot
        {
            uint64_t id = 0;
            WorldPos worldPos{};
            TextureKey textureKey{0, 0, 0};
            RuntimeOverlayInfo overlayInfo{};
            float opacity = 1.0f;
            bool enabled = true;
            std::optional<UvSubrect> uvSubrect;
        };

        [[nodiscard]] std::vector<RuntimeDecalSnapshot> SnapshotRuntimeDecals() const;
        [[nodiscard]] std::optional<RuntimeDecalSnapshot> FindRuntimeDecal(uint64_t id) const;
        [[nodiscard]] std::optional<uint64_t> GetMostRecentRuntimeDecalId() const noexcept;
        [[nodiscard]] std::optional<uint64_t> TrackRuntimeDecal(
            cISC4Occupant* occupant,
            const RuntimeDecalSnapshot* preferredSnapshot = nullptr);
        bool UpdateRuntimeDecal(uint64_t id, cISC4City* city, const RuntimeDecalSnapshot& snapshot);
        bool RemoveRuntimeDecal(uint64_t id, cISC4City* city);

        // Last operation status, useful for the debug UI.
        struct Status
        {
            bool lastLoadOk = false;
            bool lastSaveOk = false;
            uint32_t lastLoadEntries = 0;
            uint32_t lastSaveEntries = 0;
            uint32_t lastLoadUnknownChunks = 0;
        };

        [[nodiscard]] const Status& LastStatus() const noexcept { return status_; }

    private:
        void OnLoad_(cIGZMessage2Standard* msg);
        void OnSave_(cIGZMessage2Standard* msg);
        void OnInsertOccupant_(cIGZMessage2Standard* msg);
        void OnRemoveOccupant_(cIGZMessage2Standard* msg);

        void ApplyEntryToRenderer_(const DecalEntry& entry) const;
        void ClearRuntimeDecals_() noexcept;
        [[nodiscard]] std::optional<uint64_t> UpsertRuntimeDecal_(
            cISC4Occupant* occupant,
            const RuntimeDecalSnapshot* preferredSnapshot = nullptr);
        bool RemoveRuntimeDecalByOccupant_(cISC4Occupant* occupant);

        struct RuntimeDecalRecord
        {
            cRZAutoRefCount<cISC4PropOccupantTerrainDecal> decal;
            cRZAutoRefCount<cISC4PropOccupant> prop;
            cRZAutoRefCount<cISC4Occupant> occupant;
            RuntimeDecalSnapshot snapshot{};
        };

        DecalSidecarRepository repository_;
        TerrainDecal::TerrainDecalHook* terrainDecalHook_ = nullptr;
        std::vector<RuntimeDecalRecord> runtimeDecals_;
        Status status_{};
    };
}
