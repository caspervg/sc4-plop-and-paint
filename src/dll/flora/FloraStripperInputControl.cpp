#include "FloraStripperInputControl.hpp"

#include <cmath>
#include <limits>
#include <windows.h>

#include "cISC4FloraOccupant.h"
#include "cSC4BaseOccupantFilter.h"
#include "cISTETerrain.h"
#include "SC4List.h"
#include "../utils/Logger.h"

namespace {
    constexpr auto kFloraStripperControlID = 0x3B7C4E20u;
    constexpr float kPickRadiusMeters = 3.0f;
    constexpr uint32_t kHoverHighlight = 0x9u;
    constexpr uint32_t kFloraOccupantType = 0x74758926u;
    constexpr uint32_t kMaxOccupantQueryCount = std::numeric_limits<uint32_t>::max();

    class FloraOccupantFilter final : public cSC4BaseOccupantFilter {
    public:
        bool IsOccupantTypeIncluded(const uint32_t type) override {
            return type == kFloraOccupantType;
        }
    };
}

FloraStripperInputControl::FloraStripperInputControl()
    : cSC4BaseViewInputControl(kFloraStripperControlID) {}

FloraStripperInputControl::~FloraStripperInputControl() = default;

bool FloraStripperInputControl::Init() {
    if (initialized) {
        return true;
    }
    if (!cSC4BaseViewInputControl::Init()) {
        return false;
    }
    LOG_INFO("FloraStripperInputControl initialized");
    return true;
}

bool FloraStripperInputControl::Shutdown() {
    if (!initialized) {
        return true;
    }
    ClearHoveredFlora_();
    undoStack_.clear();
    cSC4BaseViewInputControl::Shutdown();
    LOG_INFO("FloraStripperInputControl shut down");
    return true;
}

bool FloraStripperInputControl::OnMouseDownL(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    if (stripMode_ == StripMode::Brush) {
        leftMouseDown_ = true;
        if (!cursorValid_) {
            return false;
        }
        DeleteFloraInBrush_();
        BuildOverlay_();
        return true;
    }
    if (!cursorValid_ || !hoveredOccupant_) {
        return false;
    }
    DeleteHoveredFlora_();
    return true;
}

bool FloraStripperInputControl::OnMouseUpL(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    leftMouseDown_ = false;
    return active_ && IsOnTop();
}

bool FloraStripperInputControl::OnMouseDownR(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    leftMouseDown_ = false;
    ClearHoveredFlora_();
    LOG_INFO("FloraStripperInputControl: RMB pressed, exiting strip mode");
    cancelPending_ = true;
    return true;
}

bool FloraStripperInputControl::OnMouseMove(const int32_t x, const int32_t z, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    UpdateCursorWorldFromScreen_(x, z);
    if (stripMode_ == StripMode::Brush) {
        ClearHoveredFlora_();
        if (leftMouseDown_) {
            DeleteFloraInBrush_();
        }
    }
    else {
        PickNearestFlora_();
    }
    BuildOverlay_();
    return true;
}

bool FloraStripperInputControl::OnKeyDown(const int32_t vkCode, const uint32_t modifiers) {
    if (!active_ || !IsOnTop()) {
        return false;
    }

    if (vkCode == VK_ESCAPE) {
        leftMouseDown_ = false;
        ClearHoveredFlora_();
        LOG_INFO("FloraStripperInputControl: ESC pressed, exiting strip mode");
        cancelPending_ = true;
        return true;
    }

    if (vkCode == 'Z' && (modifiers & MOD_CONTROL)) {
        UndoLastDeletion();
        return true;
    }

    if (vkCode == 'B') {
        stripMode_ = stripMode_ == StripMode::Single ? StripMode::Brush : StripMode::Single;
        leftMouseDown_ = false;
        if (stripMode_ == StripMode::Brush) {
            ClearHoveredFlora_();
            LOG_INFO("FloraStripperInputControl: Switched to brush mode");
        }
        else {
            PickNearestFlora_();
            LOG_INFO("FloraStripperInputControl: Switched to single mode");
        }
        BuildOverlay_();
        return true;
    }

    return false;
}

void FloraStripperInputControl::Activate() {
    cSC4BaseViewInputControl::Activate();
    if (!Init()) {
        LOG_WARN("FloraStripperInputControl: Init failed during Activate");
        return;
    }
    active_ = true;
    LOG_INFO("FloraStripperInputControl activated");
}

void FloraStripperInputControl::Deactivate() {
    active_ = false;
    leftMouseDown_ = false;
    ProcessPendingActions();
    ClearHoveredFlora_();
    overlay_.Clear();
    cSC4BaseViewInputControl::Deactivate();
    LOG_INFO("FloraStripperInputControl deactivated");
}

void FloraStripperInputControl::SetCity(cISC4City* pCity) {
    city_ = pCity;
    if (pCity) {
        floraSimulator_ = pCity->GetFloraSimulator();
        occupantManager_ = pCity->GetOccupantManager();
    }
    else {
        ClearHoveredFlora_();
        floraSimulator_.Reset();
        occupantManager_.Reset();
        undoStack_.clear();
    }
}

void FloraStripperInputControl::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void FloraStripperInputControl::UndoLastDeletion() {
    if (undoStack_.empty()) {
        LOG_DEBUG("FloraStripperInputControl: Nothing to undo");
        return;
    }

    if (!floraSimulator_) {
        LOG_WARN("FloraStripperInputControl: No flora simulator available for undo; clearing history");
        undoStack_.clear();
        return;
    }

    const DeletedFloraInfo info = undoStack_.back();
    undoStack_.pop_back();

    float posArr[3] = {info.position.fX, info.position.fY, info.position.fZ};
    cISC4Occupant* occupant =
        reinterpret_cast<cISC4Occupant*>(floraSimulator_->AddNewFloraOccupant(info.floraType, posArr, false));
    if (!occupant) {
        LOG_WARN("FloraStripperInputControl: Failed to re-create flora 0x{:08X} for undo", info.floraType);
        return;
    }

    cRZAutoRefCount<cISC4Occupant> occRef(occupant, cRZAutoRefCount<cISC4Occupant>::kAddRef);
    cISC4FloraOccupant* floraOccupant = nullptr;
    if (!occupant->QueryInterface(GZIID_cISC4FloraOccupant, reinterpret_cast<void**>(&floraOccupant)) || !floraOccupant) {
        LOG_WARN("FloraStripperInputControl: Failed to get flora interface for undo");
        return;
    }

    cRZAutoRefCount<cISC4FloraOccupant> floraRef(floraOccupant);
    cS3DVector3 pos = info.position;
    occRef->SetPosition(&pos);
    floraRef->SetOrientation(static_cast<uint32_t>(info.orientation & 3));

    LOG_INFO("FloraStripperInputControl: Restored flora 0x{:08X} at ({:.2f}, {:.2f}, {:.2f}), {} undo(s) remaining",
             info.floraType, pos.fX, pos.fY, pos.fZ, undoStack_.size());
}

void FloraStripperInputControl::ProcessPendingActions() {
    if (cancelPending_) {
        cancelPending_ = false;
        if (onCancel_) {
            onCancel_();
        }
    }
}

bool FloraStripperInputControl::UpdateCursorWorldFromScreen_(const int32_t screenX, const int32_t screenZ) {
    cursorValid_ = false;
    if (!view3D) {
        return false;
    }

    float worldCoords[3] = {0.0f, 0.0f, 0.0f};
    if (!view3D->PickTerrain(screenX, screenZ, worldCoords, false)) {
        return false;
    }

    currentCursorWorld_ = cS3DVector3(worldCoords[0], worldCoords[1], worldCoords[2]);
    cursorValid_ = true;
    return true;
}

void FloraStripperInputControl::BuildSearchBounds_(float minBounds[3], float maxBounds[3]) const {
    minBounds[0] = currentCursorWorld_.fX - kPickRadiusMeters;
    minBounds[1] = -std::numeric_limits<float>::max();
    minBounds[2] = currentCursorWorld_.fZ - kPickRadiusMeters;

    maxBounds[0] = currentCursorWorld_.fX + kPickRadiusMeters;
    maxBounds[1] = std::numeric_limits<float>::max();
    maxBounds[2] = currentCursorWorld_.fZ + kPickRadiusMeters;
}

void FloraStripperInputControl::PickNearestFlora_() {
    if (!cursorValid_ || !occupantManager_) {
        ClearHoveredFlora_();
        return;
    }

    float minBounds[3]{};
    float maxBounds[3]{};
    BuildSearchBounds_(minBounds, maxBounds);

    SC4List<cISC4Occupant*> candidates;
    FloraOccupantFilter floraFilter;
    if (!occupantManager_->GetOccupantsByBBox(
            candidates,
            minBounds,
            maxBounds,
            &floraFilter,
            kMaxOccupantQueryCount)) {
        ClearHoveredFlora_();
        return;
    }

    cISC4Occupant* nearest = nullptr;
    float nearestDistSq = std::numeric_limits<float>::max();

    for (cISC4Occupant* occupant : candidates) {
        if (!occupant) {
            continue;
        }

        cISC4FloraOccupant* floraOccupant = nullptr;
        if (!occupant->QueryInterface(GZIID_cISC4FloraOccupant, reinterpret_cast<void**>(&floraOccupant)) || !floraOccupant) {
            continue;
        }
        floraOccupant->Release();

        cS3DVector3 pos{};
        if (!occupant->GetPosition(&pos)) {
            continue;
        }

        const float dx = pos.fX - currentCursorWorld_.fX;
        const float dz = pos.fZ - currentCursorWorld_.fZ;
        const float distSq = dx * dx + dz * dz;

        if (distSq < nearestDistSq) {
            nearestDistSq = distSq;
            nearest = occupant;
        }
    }

    occupantManager_->ReleaseOccupantList(candidates);
    SetHoveredFlora_(nearest);
}

void FloraStripperInputControl::DeleteFloraInBrush_() {
    if (!cursorValid_ || !occupantManager_ || !floraSimulator_) {
        return;
    }

    float minBounds[3]{};
    float maxBounds[3]{};
    BuildSearchBounds_(minBounds, maxBounds);

    SC4List<cISC4Occupant*> candidates;
    FloraOccupantFilter floraFilter;
    if (!occupantManager_->GetOccupantsByBBox(
            candidates,
            minBounds,
            maxBounds,
            &floraFilter,
            kMaxOccupantQueryCount)) {
        return;
    }

    size_t removedCount = 0;
    for (cISC4Occupant* occupant : candidates) {
        if (!occupant) {
            continue;
        }

        cISC4FloraOccupant* floraOccupant = nullptr;
        if (!occupant->QueryInterface(GZIID_cISC4FloraOccupant, reinterpret_cast<void**>(&floraOccupant)) || !floraOccupant) {
            continue;
        }

        cRZAutoRefCount<cISC4FloraOccupant> floraRef(floraOccupant);
        cRZAutoRefCount<cISC4Occupant> occupantRef(occupant, cRZAutoRefCount<cISC4Occupant>::kAddRef);

        cS3DVector3 pos{};
        if (!occupantRef->GetPosition(&pos)) {
            continue;
        }

        const float dx = pos.fX - currentCursorWorld_.fX;
        const float dz = pos.fZ - currentCursorWorld_.fZ;
        if ((dx * dx + dz * dz) > (kPickRadiusMeters * kPickRadiusMeters)) {
            continue;
        }

        const uint32_t floraType = floraRef->GetFloraType();
        const int32_t orientation = static_cast<int32_t>(floraRef->GetOrientation());
        occupantRef->SetHighlight(0x0, true);

        if (!floraSimulator_->DemolishFloraOccupant(occupantRef, 0)) {
            LOG_WARN("FloraStripperInputControl: Failed to remove flora 0x{:08X} in brush mode", floraType);
            continue;
        }

        undoStack_.push_back({floraType, pos, orientation});
        ++removedCount;
    }

    occupantManager_->ReleaseOccupantList(candidates);

    if (removedCount > 0) {
        LOG_INFO("FloraStripperInputControl: Brush removed {} flora item(s), {} undo(s) available",
                 removedCount, undoStack_.size());
    }
}

void FloraStripperInputControl::SetHoveredFlora_(cISC4Occupant* newOccupant) {
    if (hoveredOccupant_ == newOccupant) {
        return;
    }

    if (hoveredOccupant_) {
        hoveredOccupant_->SetHighlight(0x0, true);
        hoveredOccupant_.Reset();
    }

    if (newOccupant) {
        newOccupant->AddRef();
        hoveredOccupant_ = cRZAutoRefCount<cISC4Occupant>(newOccupant);
        newOccupant->SetHighlight(kHoverHighlight, true);
    }
}

void FloraStripperInputControl::ClearHoveredFlora_() {
    if (hoveredOccupant_) {
        hoveredOccupant_->SetHighlight(0x0, true);
        hoveredOccupant_.Reset();
    }
}

void FloraStripperInputControl::DrawOverlay(IDirect3DDevice7* device) {
    if (!device) {
        return;
    }
    overlay_.Draw(device, false);
}

void FloraStripperInputControl::BuildOverlay_() {
    if (!cursorValid_) {
        overlay_.Clear();
        return;
    }
    overlay_.BuildStripperPreview(true, currentCursorWorld_, kPickRadiusMeters, GetTerrain_());
}

cISTETerrain* FloraStripperInputControl::GetTerrain_() const {
    if (!city_) {
        return nullptr;
    }
    return city_->GetTerrain();
}

void FloraStripperInputControl::DeleteHoveredFlora_() {
    if (!hoveredOccupant_ || !floraSimulator_) {
        return;
    }

    cISC4FloraOccupant* floraOccupant = nullptr;
    if (!hoveredOccupant_->QueryInterface(GZIID_cISC4FloraOccupant,
                                          reinterpret_cast<void**>(&floraOccupant)) || !floraOccupant) {
        LOG_WARN("FloraStripperInputControl: Failed to get flora interface before removal");
        return;
    }

    cRZAutoRefCount<cISC4FloraOccupant> floraRef(floraOccupant);

    cS3DVector3 pos{};
    if (!hoveredOccupant_->GetPosition(&pos)) {
        LOG_WARN("FloraStripperInputControl: Failed to get position before removal");
        return;
    }
    const uint32_t floraType = floraRef->GetFloraType();
    const int32_t orientation = static_cast<int32_t>(floraRef->GetOrientation());

    hoveredOccupant_->SetHighlight(0x0, true);

    if (!floraSimulator_->DemolishFloraOccupant(hoveredOccupant_, 0)) {
        LOG_WARN("FloraStripperInputControl: Failed to remove flora 0x{:08X}", floraType);
        hoveredOccupant_.Reset();
        return;
    }

    undoStack_.push_back({floraType, pos, orientation});
    LOG_INFO("FloraStripperInputControl: Removed flora 0x{:08X} at ({:.2f}, {:.2f}, {:.2f}), {} undo(s) available",
             floraType, pos.fX, pos.fY, pos.fZ, undoStack_.size());

    hoveredOccupant_.Reset();
}
