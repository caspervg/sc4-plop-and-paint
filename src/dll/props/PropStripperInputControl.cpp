#include "PropStripperInputControl.hpp"

#include <cmath>
#include <limits>
#include <unordered_set>
#include <windows.h>

#include "cISC4PropOccupant.h"
#include "cISTETerrain.h"
#include "SC4List.h"
#include "SC4Rect.h"
#include "../utils/Logger.h"

namespace {
    constexpr auto kPropStripperControlID = 0x3B7C4E1Fu;
    constexpr float kPickRadiusMeters = 3.0f;
    constexpr uint32_t kHoverHighlight = 0x9u;

    const char* SourceKindToString(const PropStripperInputControl::SourceKind sourceKind) {
        switch (sourceKind) {
        case PropStripperInputControl::SourceKind::City:
            return "city";
        case PropStripperInputControl::SourceKind::Lot:
            return "lot";
        case PropStripperInputControl::SourceKind::Street:
            return "street";
        }

        return "unknown";
    }
}

PropStripperInputControl::PropStripperInputControl()
    : cSC4BaseViewInputControl(kPropStripperControlID) {}

PropStripperInputControl::~PropStripperInputControl() = default;

bool PropStripperInputControl::Init() {
    if (initialized) {
        return true;
    }
    if (!cSC4BaseViewInputControl::Init()) {
        return false;
    }
    LOG_INFO("PropStripperInputControl initialized");
    return true;
}

bool PropStripperInputControl::Shutdown() {
    if (!initialized) {
        return true;
    }
    ClearHoveredProp_();
    undoStack_.clear();
    cSC4BaseViewInputControl::Shutdown();
    LOG_INFO("PropStripperInputControl shut down");
    return true;
}

bool PropStripperInputControl::OnMouseDownL(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    if (stripMode_ == StripMode::Brush) {
        leftMouseDown_ = true;
        if (!cursorValid_) {
            return false;
        }
        DeletePropsInBrush_();
        BuildOverlay_();
        return true;
    }

    if (!cursorValid_ || !hoveredOccupant_) {
        return false;
    }
    DeleteHoveredProp_();
    return true;
}

bool PropStripperInputControl::OnMouseUpL(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    leftMouseDown_ = false;
    return active_ && IsOnTop();
}

bool PropStripperInputControl::OnMouseDownR(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    ClearHoveredProp_();
    LOG_INFO("PropStripperInputControl: RMB pressed, exiting strip mode");
    cancelPending_ = true;
    return true;
}

bool PropStripperInputControl::OnMouseMove(const int32_t x, const int32_t z, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    UpdateCursorWorldFromScreen_(x, z);
    if (stripMode_ == StripMode::Brush) {
        ClearHoveredProp_();
        if (leftMouseDown_) {
            DeletePropsInBrush_();
        }
    }
    else {
        PickNearestProp_();
    }
    BuildOverlay_();
    return true;
}

bool PropStripperInputControl::OnKeyDown(const int32_t vkCode, const uint32_t modifiers) {
    if (!active_ || !IsOnTop()) {
        return false;
    }

    if (vkCode == VK_ESCAPE) {
        ClearHoveredProp_();
        LOG_INFO("PropStripperInputControl: ESC pressed, exiting strip mode");
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
            ClearHoveredProp_();
            LOG_INFO("PropStripperInputControl: Switched to brush mode");
        }
        else {
            PickNearestProp_();
            LOG_INFO("PropStripperInputControl: Switched to single mode");
        }
        BuildOverlay_();
        return true;
    }

    return false;
}

void PropStripperInputControl::Activate() {
    cSC4BaseViewInputControl::Activate();
    if (!Init()) {
        LOG_WARN("PropStripperInputControl: Init failed during Activate");
        return;
    }
    active_ = true;
    LOG_INFO("PropStripperInputControl activated");
}

void PropStripperInputControl::Deactivate() {
    active_ = false;
    leftMouseDown_ = false;
    ProcessPendingActions();
    ClearHoveredProp_();
    overlay_.Clear();
    cSC4BaseViewInputControl::Deactivate();
    LOG_INFO("PropStripperInputControl deactivated");
}

void PropStripperInputControl::SetCity(cISC4City* pCity) {
    city_ = pCity;
    if (pCity) {
        propManager_ = pCity->GetPropManager();
    }
    else {
        ClearHoveredProp_();
        propManager_.Reset();
        undoStack_.clear();
    }
}

void PropStripperInputControl::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void PropStripperInputControl::SetEnabledSources(const uint32_t sourceFlags) {
    const uint32_t sanitizedFlags = sourceFlags & (SourceFlagCity | SourceFlagLot | SourceFlagStreet);
    if (enabledSources_ == sanitizedFlags) {
        return;
    }

    ClearHoveredProp_();
    leftMouseDown_ = false;

    enabledSources_ = sanitizedFlags;
    LOG_INFO("PropStripperInputControl: enabled source flags set to 0x{:X}", enabledSources_);
}

uint32_t PropStripperInputControl::GetEnabledSources() const noexcept {
    return enabledSources_;
}

bool PropStripperInputControl::HasEnabledSource(const SourceKind source) const noexcept {
    switch (source) {
    case SourceKind::City:
        return (enabledSources_ & SourceFlagCity) != 0;
    case SourceKind::Lot:
        return (enabledSources_ & SourceFlagLot) != 0;
    case SourceKind::Street:
        return (enabledSources_ & SourceFlagStreet) != 0;
    }

    return false;
}

void PropStripperInputControl::UndoLastDeletion() {
    if (undoStack_.empty()) {
        LOG_DEBUG("PropStripperInputControl: Nothing to undo");
        return;
    }

    if (!propManager_) {
        LOG_WARN("PropStripperInputControl: No prop manager available for undo; clearing history");
        undoStack_.clear();
        return;
    }

    const DeletedPropInfo info = undoStack_.back();
    undoStack_.pop_back();

    cISC4PropOccupant* prop = nullptr;
    if (!propManager_->CreateProp(info.propType, prop)) {
        LOG_WARN("PropStripperInputControl: Failed to re-create prop 0x{:08X} for undo", info.propType);
        return;
    }

    cRZAutoRefCount<cISC4PropOccupant> propRef(prop);
    cISC4Occupant* occupant = prop->AsOccupant();
    if (!occupant) {
        LOG_WARN("PropStripperInputControl: Failed to get occupant interface for undo");
        return;
    }

    cRZAutoRefCount<cISC4Occupant> occRef(occupant, cRZAutoRefCount<cISC4Occupant>::kAddRef);
    cS3DVector3 pos = info.position;
    if (!occRef->SetPosition(&pos)) {
        LOG_WARN("PropStripperInputControl: Failed to restore prop position");
        return;
    }

    if (!prop->SetOrientation(info.orientation)) {
        LOG_WARN("PropStripperInputControl: Failed to restore prop orientation");
        return;
    }

    bool restored = false;
    switch (info.source) {
    case SourceKind::City:
        restored = propManager_->AddCityProp(occRef);
        break;
    case SourceKind::Lot:
        restored = propManager_->AddLotProp(info.propType, pos, info.orientation) != nullptr;
        break;
    case SourceKind::Street:
        restored = propManager_->AddStreetProp(info.propType, pos, info.orientation) != nullptr;
        break;
    }

    if (!restored) {
        LOG_WARN("PropStripperInputControl: Failed to restore {} prop 0x{:08X}",
                 SourceKindToString(info.source), info.propType);
        return;
    }

    LOG_INFO("PropStripperInputControl: Restored {} prop 0x{:08X} at ({:.2f}, {:.2f}, {:.2f}), {} undo(s) remaining",
             SourceKindToString(info.source), info.propType, pos.fX, pos.fY, pos.fZ, undoStack_.size());
}

void PropStripperInputControl::ProcessPendingActions() {
    if (cancelPending_) {
        cancelPending_ = false;
        if (onCancel_) {
            onCancel_();
        }
    }
}

bool PropStripperInputControl::UpdateCursorWorldFromScreen_(const int32_t screenX, const int32_t screenZ) {
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

bool PropStripperInputControl::TryGetCursorCell_(int& cellX, int& cellZ) const {
    if (!city_ || !cursorValid_) {
        return false;
    }

    return city_->PositionToCell(currentCursorWorld_.fX, currentCursorWorld_.fZ, cellX, cellZ) != 0;
}

void PropStripperInputControl::AppendCandidateProps_(std::vector<CollectedProp>& candidates, const SourceKind source) const {
    if (!propManager_ || !cursorValid_) {
        return;
    }

    SC4List<cISC4Occupant*> rawCandidates;
    switch (source) {
    case SourceKind::City: {
        const SC4Rect<float> rect(
            currentCursorWorld_.fX - kPickRadiusMeters,
            currentCursorWorld_.fZ - kPickRadiusMeters,
            currentCursorWorld_.fX + kPickRadiusMeters,
            currentCursorWorld_.fZ + kPickRadiusMeters);

        propManager_->GetCityProps(rawCandidates, rect);
        break;
    }
    case SourceKind::Lot:
    case SourceKind::Street: {
        int cellX = 0;
        int cellZ = 0;
        if (!TryGetCursorCell_(cellX, cellZ)) {
            return;
        }

        if (source == SourceKind::Lot) {
            propManager_->GetLotProps(rawCandidates, cellX, cellZ);
        }
        else {
            propManager_->GetStreetProps(rawCandidates, cellX, cellZ);
        }
        break;
    }
    }

    for (cISC4Occupant* occupant : rawCandidates) {
        if (!occupant) {
            continue;
        }

        cISC4PropOccupant* propOccupant = nullptr;
        if (!occupant->QueryInterface(GZIID_cISC4PropOccupant, reinterpret_cast<void**>(&propOccupant)) || !propOccupant) {
            continue;
        }

        cS3DVector3 pos{};
        if (!occupant->GetPosition(&pos)) {
            propOccupant->Release();
            continue;
        }

        candidates.push_back({
            .occupant = occupant,
            .source = source,
            .position = pos,
            .propType = propOccupant->GetPropType(),
            .orientation = propOccupant->GetOrientation()
        });
        propOccupant->Release();
    }
}

void PropStripperInputControl::CollectCandidateProps_(std::vector<CollectedProp>& candidates) const {
    candidates.clear();
    if (!propManager_ || !cursorValid_) {
        return;
    }

    if (HasEnabledSource(SourceKind::City)) {
        AppendCandidateProps_(candidates, SourceKind::City);
    }
    if (HasEnabledSource(SourceKind::Lot)) {
        AppendCandidateProps_(candidates, SourceKind::Lot);
    }
    if (HasEnabledSource(SourceKind::Street)) {
        AppendCandidateProps_(candidates, SourceKind::Street);
    }

    std::unordered_set<cISC4Occupant*> seen;
    auto it = candidates.begin();
    while (it != candidates.end()) {
        if (!it->occupant || !seen.insert(it->occupant).second) {
            it = candidates.erase(it);
        }
        else {
            ++it;
        }
    }
}

bool PropStripperInputControl::TryRemoveProp_(cISC4Occupant* const occupant, const uint32_t propType, const SourceKind source) const {
    if (!propManager_ || !occupant) {
        return false;
    }

    if (propManager_->RemovePropA(occupant)) {
        return true;
    }

    LOG_WARN("PropStripperInputControl: RemovePropA failed for {} prop 0x{:08X}",
             SourceKindToString(source), propType);
    return false;
}

void PropStripperInputControl::PickNearestProp_() {
    if (!cursorValid_ || !propManager_) {
        ClearHoveredProp_();
        return;
    }

    std::vector<CollectedProp> candidates;
    CollectCandidateProps_(candidates);

    cISC4Occupant* nearestOccupant = nullptr;
    float nearestDistSq = std::numeric_limits<float>::max();

    for (const auto& candidate : candidates) {
        const float dx = candidate.position.fX - currentCursorWorld_.fX;
        const float dz = candidate.position.fZ - currentCursorWorld_.fZ;
        const float distSq = dx * dx + dz * dz;

        if (distSq < nearestDistSq) {
            nearestDistSq = distSq;
            nearestOccupant = candidate.occupant;
        }
    }

    if (nearestDistSq <= (kPickRadiusMeters * kPickRadiusMeters)) {
        SetHoveredProp_(nearestOccupant);
    }
    else {
        ClearHoveredProp_();
    }
}

void PropStripperInputControl::DeletePropsInBrush_() {
    if (!cursorValid_ || !propManager_) {
        return;
    }

    std::vector<CollectedProp> candidates;
    CollectCandidateProps_(candidates);

    size_t removedCount = 0;
    for (const auto& candidate : candidates) {
        if (!candidate.occupant) {
            continue;
        }

        cRZAutoRefCount<cISC4Occupant> occupantRef(candidate.occupant, cRZAutoRefCount<cISC4Occupant>::kAddRef);
        const float dx = candidate.position.fX - currentCursorWorld_.fX;
        const float dz = candidate.position.fZ - currentCursorWorld_.fZ;
        if ((dx * dx + dz * dz) > (kPickRadiusMeters * kPickRadiusMeters)) {
            continue;
        }

        occupantRef->SetHighlight(0x0, true);

        if (!TryRemoveProp_(occupantRef, candidate.propType, candidate.source)) {
            continue;
        }

        undoStack_.push_back({candidate.source, candidate.propType, candidate.position, candidate.orientation});
        ++removedCount;
    }

    if (removedCount > 0) {
        LOG_INFO("PropStripperInputControl: Brush removed {} prop(s), {} undo(s) available",
                 removedCount, undoStack_.size());
    }
}

void PropStripperInputControl::SetHoveredProp_(cISC4Occupant* newOccupant) {
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

void PropStripperInputControl::ClearHoveredProp_() {
    if (hoveredOccupant_) {
        hoveredOccupant_->SetHighlight(0x0, true);
        hoveredOccupant_.Reset();
    }
}

void PropStripperInputControl::DrawOverlay(IDirect3DDevice7* device) {
    if (!device) {
        return;
    }
    overlay_.Draw(device, false);
}

void PropStripperInputControl::BuildOverlay_() {
    if (!cursorValid_) {
        overlay_.Clear();
        return;
    }
    overlay_.BuildStripperPreview(true, currentCursorWorld_, kPickRadiusMeters, GetTerrain_());
}

cISTETerrain* PropStripperInputControl::GetTerrain_() const {
    if (!city_) {
        return nullptr;
    }
    return city_->GetTerrain();
}

void PropStripperInputControl::DeleteHoveredProp_() {
    if (!hoveredOccupant_ || !propManager_) {
        return;
    }

    // Get prop info so we can undo the removal
    cISC4PropOccupant* propOccupant = nullptr;
    if (!hoveredOccupant_->QueryInterface(GZIID_cISC4PropOccupant,
                                          reinterpret_cast<void**>(&propOccupant)) || !propOccupant) {
        LOG_WARN("PropStripperInputControl: Failed to get prop interface before removal");
        return;
    }

    cRZAutoRefCount<cISC4PropOccupant> propRef(propOccupant);

    cS3DVector3 pos{};
    if (!hoveredOccupant_->GetPosition(&pos)) {
        LOG_WARN("PropStripperInputControl: Failed to get position before removal");
        return;
    }
    const uint32_t propType = propOccupant->GetPropType();
    const int32_t orientation = propOccupant->GetOrientation();

    hoveredOccupant_->SetHighlight(0x0, true);

    SourceKind source = SourceKind::City;
    std::vector<CollectedProp> candidates;
    CollectCandidateProps_(candidates);
    for (const auto& candidate : candidates) {
        if (candidate.occupant == hoveredOccupant_) {
            source = candidate.source;
            break;
        }
    }

    if (!TryRemoveProp_(hoveredOccupant_, propType, source)) {
        hoveredOccupant_.Reset();
        return;
    }

    undoStack_.push_back({source, propType, pos, orientation});
    LOG_INFO("PropStripperInputControl: Removed {} prop 0x{:08X} at ({:.2f}, {:.2f}, {:.2f}), {} undo(s) available",
             SourceKindToString(source), propType, pos.fX, pos.fY, pos.fZ, undoStack_.size());

    hoveredOccupant_.Reset();
}
