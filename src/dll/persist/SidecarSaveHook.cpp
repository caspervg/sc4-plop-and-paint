#include "SidecarSaveHook.hpp"

#include <cmath>

class cIGZVariant;

#include "cGZPersistResourceKey.h"
#include "cIGZMessage2.h"
#include "cIGZMessage2Standard.h"
#include "cISC4City.h"
#include "cISC4DBSegment.h"
#include "cISC4DBSegmentIStream.h"
#include "cISC4DBSegmentOStream.h"
#include "cISC4Occupant.h"
#include "cISC4OccupantManager.h"
#include "cISC4PropOccupant.h"
#include "cISC4PropOccupantTerrainDecal.h"
#include "cRZAutoRefCount.h"
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
        constexpr uint32_t kUserFlagVisible = 1u << 0;
        constexpr uint32_t kDefaultTextureType = 0x7AB50E44u;

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

        int32_t RotationTurnsToPropOrientation_(const float turns)
        {
            const int32_t quarterTurns = static_cast<int32_t>(std::lround(turns * 4.0f));
            const int32_t normalized = quarterTurns % 4;
            return normalized < 0 ? normalized + 4 : normalized;
        }

        float PropOrientationToRotationTurns_(const int32_t orientation)
        {
            return static_cast<float>(orientation & 3) * 0.25f;
        }

        DecalEntry BuildRepositoryEntry_(const SidecarSaveHook::RuntimeDecalSnapshot& snapshot,
                                         const std::optional<DecalEntry>& existing)
        {
            DecalEntry entry = existing.value_or(DecalEntry{});
            entry.worldPos = snapshot.worldPos;
            entry.textureKey = snapshot.textureKey;
            entry.overlayInfo = OverlayInfoSnapshot{
                .baseSize = snapshot.overlayInfo.baseSize,
                .rotationTurns = snapshot.overlayInfo.rotationTurns,
                .aspectMultiplier = snapshot.overlayInfo.aspectMultiplier,
                .uvScaleU = snapshot.overlayInfo.uvScaleU,
                .uvScaleV = snapshot.overlayInfo.uvScaleV,
                .uvOffset = snapshot.overlayInfo.uvOffset,
                .unknown8 = snapshot.overlayInfo.unknown8,
            };
            entry.opacity = snapshot.opacity;
            entry.uvSubrect = snapshot.uvSubrect;

            uint32_t flags = entry.userFlags.value_or(kUserFlagVisible);
            if (snapshot.enabled) {
                flags |= kUserFlagVisible;
            }
            else {
                flags &= ~kUserFlagVisible;
            }
            entry.userFlags = flags;
            return entry;
        }

        bool IsVisibleFromEntry_(const DecalEntry& entry)
        {
            return !entry.userFlags.has_value() || ((*entry.userFlags & kUserFlagVisible) != 0);
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
        ClearRuntimeDecals_();
        repository_.Clear();
    }

    void SidecarSaveHook::OnCityShutdown()
    {
        ClearRuntimeDecals_();
        repository_.Clear();
        status_ = Status{};
    }

    std::vector<SidecarSaveHook::RuntimeDecalSnapshot> SidecarSaveHook::SnapshotRuntimeDecals() const
    {
        std::vector<RuntimeDecalSnapshot> result;
        result.reserve(runtimeDecals_.size());
        for (const RuntimeDecalRecord& record : runtimeDecals_) {
            result.push_back(record.snapshot);
        }
        return result;
    }

    std::optional<SidecarSaveHook::RuntimeDecalSnapshot> SidecarSaveHook::FindRuntimeDecal(const uint64_t id) const
    {
        for (const RuntimeDecalRecord& record : runtimeDecals_) {
            if (record.snapshot.id == id) {
                return record.snapshot;
            }
        }
        return std::nullopt;
    }

    std::optional<uint64_t> SidecarSaveHook::GetMostRecentRuntimeDecalId() const noexcept
    {
        if (runtimeDecals_.empty()) {
            return std::nullopt;
        }
        return runtimeDecals_.front().snapshot.id;
    }

    std::optional<uint64_t> SidecarSaveHook::TrackRuntimeDecal(
        cISC4Occupant* occupant,
        const RuntimeDecalSnapshot* preferredSnapshot)
    {
        return UpsertRuntimeDecal_(occupant, preferredSnapshot);
    }

    bool SidecarSaveHook::UpdateRuntimeDecal(const uint64_t id,
                                             cISC4City* city,
                                             const RuntimeDecalSnapshot& snapshot)
    {
        if (!city) {
            return false;
        }

        cISC4OccupantManager* const occupantMgr = city->GetOccupantManager();
        if (!occupantMgr) {
            return false;
        }

        for (RuntimeDecalRecord& record : runtimeDecals_) {
            if (record.snapshot.id != id || !record.occupant || !record.decal) {
                continue;
            }

            const WorldPos oldPos = record.snapshot.worldPos;
            const auto existingEntry = repository_.FindByPos(oldPos);

            const cGZPersistResourceKey textureKey(snapshot.textureKey.type,
                                                   snapshot.textureKey.group,
                                                   snapshot.textureKey.instance);
            record.decal->SetDecalTexture(textureKey, 1.0f);
            record.decal->SetDecalSize(snapshot.overlayInfo.baseSize);
            record.decal->SetOpacity(snapshot.opacity);

            if (record.prop) {
                if (!record.prop->SetOrientation(RotationTurnsToPropOrientation_(snapshot.overlayInfo.rotationTurns))) {
                    return false;
                }
            }

            const cS3DVector3 newPosition(snapshot.worldPos.x, snapshot.worldPos.y, snapshot.worldPos.z);
            if (!record.occupant->SetPosition(&newPosition)) {
                return false;
            }
            if (!occupantMgr->MoveOccupant(record.occupant, false)) {
                return false;
            }
            record.occupant->SetVisibility(snapshot.enabled, true);

            record.snapshot = snapshot;

            if (oldPos.x != snapshot.worldPos.x || oldPos.y != snapshot.worldPos.y || oldPos.z != snapshot.worldPos.z) {
                repository_.RemoveAt(oldPos);
            }
            repository_.SetEntry(BuildRepositoryEntry_(record.snapshot, existingEntry));
            return true;
        }

        return false;
    }

    bool SidecarSaveHook::RemoveRuntimeDecal(const uint64_t id, cISC4City* city)
    {
        if (!city) {
            return false;
        }

        cISC4OccupantManager* const occupantMgr = city->GetOccupantManager();
        if (!occupantMgr) {
            return false;
        }

        for (const RuntimeDecalRecord& record : runtimeDecals_) {
            if (record.snapshot.id == id && record.occupant) {
                return occupantMgr->RemoveOccupant(record.occupant, true, 0);
            }
        }

        return false;
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

        (void)UpsertRuntimeDecal_(occupant);
    }

    void SidecarSaveHook::OnRemoveOccupant_(cIGZMessage2Standard* msg)
    {
        if (!msg) return;
        auto* occupant = static_cast<cISC4Occupant*>(msg->GetVoid1());
        if (!IsTerrainDecalOccupant_(occupant)) return;
        RemoveRuntimeDecalByOccupant_(occupant);
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

    void SidecarSaveHook::ClearRuntimeDecals_() noexcept
    {
        runtimeDecals_.clear();
    }

    std::optional<uint64_t> SidecarSaveHook::UpsertRuntimeDecal_(
        cISC4Occupant* occupant,
        const RuntimeDecalSnapshot* preferredSnapshot)
    {
        if (!occupant) {
            return std::nullopt;
        }

        for (RuntimeDecalRecord& record : runtimeDecals_) {
            if (record.occupant == occupant) {
                if (preferredSnapshot) {
                    RuntimeDecalSnapshot snapshot = *preferredSnapshot;
                    snapshot.id = record.snapshot.id;
                    record.snapshot = snapshot;
                    const auto existingEntry = repository_.FindByPos(snapshot.worldPos);
                    repository_.SetEntry(BuildRepositoryEntry_(record.snapshot, existingEntry));
                }
                return record.snapshot.id;
            }
        }

        WorldPos pos{};
        if (!ExtractOccupantPos_(occupant, pos)) {
            return std::nullopt;
        }

        RuntimeDecalRecord record;
        record.occupant = cRZAutoRefCount<cISC4Occupant>(occupant, cRZAutoRefCount<cISC4Occupant>::kAddRef);
        occupant->QueryInterface(GZIID_cISC4PropOccupantTerrainDecal, record.decal.AsPPVoid());
        occupant->QueryInterface(GZIID_cISC4PropOccupant, record.prop.AsPPVoid());

        if (preferredSnapshot) {
            record.snapshot = *preferredSnapshot;
        }
        record.snapshot.id = runtimeDecals_.empty() ? 1 : (runtimeDecals_.front().snapshot.id + 1);
        record.snapshot.worldPos = pos;
        if (record.snapshot.textureKey.type == 0) {
            record.snapshot.textureKey.type = kDefaultTextureType;
        }

        if (record.prop && !preferredSnapshot) {
            record.snapshot.overlayInfo.rotationTurns =
                PropOrientationToRotationTurns_(record.prop->GetOrientation());
        }

        if (auto existing = repository_.FindByPos(pos)) {
            if (!preferredSnapshot && existing->textureKey) {
                record.snapshot.textureKey = *existing->textureKey;
            }
            if (!preferredSnapshot && existing->overlayInfo) {
                record.snapshot.overlayInfo.baseSize = existing->overlayInfo->baseSize;
                record.snapshot.overlayInfo.rotationTurns = existing->overlayInfo->rotationTurns;
                record.snapshot.overlayInfo.aspectMultiplier = existing->overlayInfo->aspectMultiplier;
                record.snapshot.overlayInfo.uvScaleU = existing->overlayInfo->uvScaleU;
                record.snapshot.overlayInfo.uvScaleV = existing->overlayInfo->uvScaleV;
                record.snapshot.overlayInfo.uvOffset = existing->overlayInfo->uvOffset;
                record.snapshot.overlayInfo.unknown8 = existing->overlayInfo->unknown8;
            }
            if (!preferredSnapshot && existing->opacity) {
                record.snapshot.opacity = *existing->opacity;
            }
            if (!preferredSnapshot && existing->uvSubrect) {
                record.snapshot.uvSubrect = *existing->uvSubrect;
            }
            if (!preferredSnapshot) {
                record.snapshot.enabled = IsVisibleFromEntry_(*existing);
            }
            repository_.SetEntry(BuildRepositoryEntry_(record.snapshot, existing));
            ApplyEntryToRenderer_(*existing);
        }
        else {
            repository_.SetEntry(BuildRepositoryEntry_(record.snapshot, std::nullopt));
        }

        runtimeDecals_.insert(runtimeDecals_.begin(), std::move(record));
        return runtimeDecals_.front().snapshot.id;
    }

    bool SidecarSaveHook::RemoveRuntimeDecalByOccupant_(cISC4Occupant* occupant)
    {
        if (!occupant) {
            return false;
        }

        for (auto it = runtimeDecals_.begin(); it != runtimeDecals_.end(); ++it) {
            if (it->occupant == occupant) {
                repository_.RemoveAt(it->snapshot.worldPos);
                runtimeDecals_.erase(it);
                return true;
            }
        }

        WorldPos pos{};
        if (!ExtractOccupantPos_(occupant, pos)) {
            return false;
        }
        return repository_.RemoveAt(pos);
    }
}
