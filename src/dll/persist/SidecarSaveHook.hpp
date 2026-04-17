#pragma once

#include <cstdint>

#include "DecalSidecarRepository.hpp"

class cIGZMessage2;
class cIGZMessage2Standard;

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

        DecalSidecarRepository repository_;
        TerrainDecal::TerrainDecalHook* terrainDecalHook_ = nullptr;
        Status status_{};
    };
}
