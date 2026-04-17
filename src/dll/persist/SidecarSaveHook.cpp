#include "SidecarSaveHook.hpp"

class cIGZVariant;

#include "cGZPersistResourceKey.h"
#include "cIGZMessage2.h"
#include "cIGZMessage2Standard.h"
#include "cISC4DBSegment.h"
#include "cISC4DBSegmentIStream.h"
#include "cISC4DBSegmentOStream.h"
#include "cISC4Occupant.h"
#include "cISC4PropOccupantTerrainDecal.h"
#include "cS3DVector3.h"

#include "SidecarFormat.hpp"
#include "SidecarReader.hpp"
#include "SidecarWriter.hpp"
#include "terrain/TerrainDecalHook.hpp"
#include "utils/Logger.h"

namespace PlopAndPaint::Sidecar
{
    namespace
    {
        constexpr cGZPersistResourceKey kSidecarKey(kSidecarType, kSidecarGroup, kSidecarInstance);

        // GetVoid1 on Load/Save carries the persistence segment, but its
        // concrete type isn't cISC4DBSegment — it's something that exposes
        // cISC4DBSegment via QueryInterface. Calling an OpenIStream virtual
        // through the wrong vtable corrupts the stack (/RTCs trips
        // _RTC_CheckEsp), so we always go through QueryInterface.
        //
        // Caller is responsible for Release()ing the returned pointer.
        cISC4DBSegment* QuerySegmentFromMessage_(const cIGZMessage2Standard* msg)
        {
            if (!msg) return nullptr;
            auto* pUnknown = static_cast<cIGZUnknown*>(msg->GetVoid1());
            if (!pUnknown) return nullptr;
            cISC4DBSegment* segment = nullptr;
            if (!pUnknown->QueryInterface(GZIID_cISC4DBSegment, reinterpret_cast<void**>(&segment))
                || !segment) {
                return nullptr;
            }
            return segment;
        }

        bool IsTerrainDecalOccupant_(cISC4Occupant* occupant)
        {
            if (!occupant) return false;
            cISC4PropOccupantTerrainDecal* decalIface = nullptr;
            if (!occupant->QueryInterface(GZIID_cISC4PropOccupantTerrainDecal,
                                          reinterpret_cast<void**>(&decalIface))
                || !decalIface) {
                return false;
            }
            decalIface->Release();
            return true;
        }

        bool ExtractOccupantPos_(cISC4Occupant* occupant, WorldPos& outPos)
        {
            if (!occupant) return false;
            cS3DVector3 v;
            if (!occupant->GetPosition(&v)) return false;
            outPos.x = v.fX;
            outPos.y = v.fY;
            outPos.z = v.fZ;
            return true;
        }
    }

    SidecarSaveHook::SidecarSaveHook() = default;

    SidecarSaveHook::~SidecarSaveHook()
    {
        Uninstall();
    }

    void SidecarSaveHook::Install(TerrainDecal::TerrainDecalHook* terrainDecalHook) noexcept
    {
        terrainDecalHook_ = terrainDecalHook;
    }

    void SidecarSaveHook::Uninstall() noexcept
    {
        terrainDecalHook_ = nullptr;
        repository_.Clear();
    }

    void SidecarSaveHook::OnCityShutdown()
    {
        repository_.Clear();
        status_ = Status{};
    }

    bool SidecarSaveHook::HandleMessage(cIGZMessage2* message)
    {
        if (!message) return false;

        const auto type = message->GetType();
        const auto standardMsg = static_cast<cIGZMessage2Standard*>(message);

        switch (type) {
        case kSC4MessageLoad:
            OnLoad_(standardMsg);
            return true;
        case kSC4MessageSave:
            OnSave_(standardMsg);
            return true;
        case kSC4MessageInsertOccupant:
            OnInsertOccupant_(standardMsg);
            return true;
        case kSC4MessageRemoveOccupant:
            OnRemoveOccupant_(standardMsg);
            return true;
        default:
            return false;
        }
    }

    void SidecarSaveHook::OnLoad_(cIGZMessage2Standard* msg)
    {
        status_.lastLoadOk = false;
        status_.lastLoadEntries = 0;
        status_.lastLoadUnknownChunks = 0;
        repository_.Clear();

        auto* segment = QuerySegmentFromMessage_(msg);
        if (!segment) {
            LOG_WARN("Sidecar load skipped: no DB segment in Load message");
            return;
        }

        cISC4DBSegmentIStream* rawStream = nullptr;
        if (!segment->OpenIStream(kSidecarKey, &rawStream) || !rawStream) {
            // Missing record is a normal state for saves produced without this DLL.
            LOG_INFO("Sidecar load: no SP4D record in savegame (first time or removed)");
            status_.lastLoadOk = true;
            segment->Release();
            return;
        }

        SidecarDocument doc;
        const auto result = ReadSidecarDocument(*rawStream, doc);
        segment->CloseIStream(rawStream);
        segment->Release();

        if (!result.ok) {
            LOG_WARN("Sidecar load failed: {}", result.error);
            repository_.Clear();
            return;
        }

        status_.lastLoadEntries = static_cast<uint32_t>(doc.decals.size());
        status_.lastLoadUnknownChunks = static_cast<uint32_t>(doc.unknownChunks.size());
        repository_.LoadFromDocument(std::move(doc));
        status_.lastLoadOk = true;

        LOG_INFO("Sidecar load: {} decal entries, {} unknown chunks preserved (format v{}.{})",
                 status_.lastLoadEntries,
                 status_.lastLoadUnknownChunks,
                 repository_.LoadedVersionMajor(),
                 repository_.LoadedVersionMinor());
    }

    void SidecarSaveHook::OnSave_(cIGZMessage2Standard* msg)
    {
        status_.lastSaveOk = false;
        status_.lastSaveEntries = 0;

        auto* segment = QuerySegmentFromMessage_(msg);
        if (!segment) {
            LOG_WARN("Sidecar save skipped: no DB segment in Save message");
            return;
        }

        auto doc = repository_.BuildDocument();
        status_.lastSaveEntries = static_cast<uint32_t>(doc.decals.size());

        // If there's nothing to save *and* we didn't load any unknown chunks,
        // skip writing entirely to keep savegames byte-clean without the DLL.
        if (doc.decals.empty() && doc.unknownChunks.empty()) {
            LOG_INFO("Sidecar save: nothing to persist; omitting SP4D record");
            status_.lastSaveOk = true;
            segment->Release();
            return;
        }

        cISC4DBSegmentOStream* rawStream = nullptr;
        if (!segment->OpenOStream(kSidecarKey, &rawStream, true) || !rawStream) {
            LOG_WARN("Sidecar save: failed to open output stream");
            segment->Release();
            return;
        }

        const bool ok = WriteSidecarDocument(*rawStream, doc);
        segment->CloseOStream(rawStream);
        segment->Release();

        if (!ok) {
            LOG_WARN("Sidecar save: stream error while writing ({} entries)", status_.lastSaveEntries);
            return;
        }

        status_.lastSaveOk = true;
        LOG_INFO("Sidecar save: {} decal entries written", status_.lastSaveEntries);
    }

    void SidecarSaveHook::OnInsertOccupant_(cIGZMessage2Standard* msg)
    {
        if (!msg) return;
        auto* occupant = static_cast<cISC4Occupant*>(msg->GetVoid1());
        if (!IsTerrainDecalOccupant_(occupant)) return;

        WorldPos pos;
        if (!ExtractOccupantPos_(occupant, pos)) return;

        if (auto existing = repository_.FindByPos(pos)) {
            // Restoring a previously-saved decal: push any stored UV subrect
            // into the renderer if we have one.
            ApplyEntryToRenderer_(*existing);
        }
        else {
            // Fresh decal placed during play. Track its position so we can
            // augment it later if the UI assigns a subrect, and so RemoveAt
            // has something to clean up on removal.
            DecalEntry entry;
            entry.worldPos = pos;
            repository_.SetEntry(entry);
        }
    }

    void SidecarSaveHook::OnRemoveOccupant_(cIGZMessage2Standard* msg)
    {
        if (!msg) return;
        auto* occupant = static_cast<cISC4Occupant*>(msg->GetVoid1());
        if (!IsTerrainDecalOccupant_(occupant)) return;

        WorldPos pos;
        if (!ExtractOccupantPos_(occupant, pos)) return;

        repository_.RemoveAt(pos);
    }

    void SidecarSaveHook::ApplyEntryToRenderer_(const DecalEntry& entry) const
    {
        if (!terrainDecalHook_ || !entry.uvSubrect) return;

        // Overlay-id correlation (occupant <-> overlayId) is not yet wired
        // through the hook; when it is, this method will receive the id and
        // call SetOverlayUvSubrect. For now the state is preserved on the
        // repository so a later callsite can apply it.
        //
        // Intentional no-op today, logged once per entry so the debug panel
        // can surface pending decals needing rebinding.
        LOG_DEBUG("Sidecar: decal at ({:.2f},{:.2f},{:.2f}) has stored UV subrect; awaiting overlay-id binding",
                  entry.worldPos.x, entry.worldPos.y, entry.worldPos.z);
    }
}