#include "FloraPainterInputControl.hpp"

#include "FloraRepository.hpp"
#include "../props/PropPainterInputControl.hpp"
#include "../utils/Logger.h"
#include "cISC4City.h"
#include "cISC4FloraOccupant.h"
#include "cISC4Occupant.h"

namespace {
    constexpr auto kFloraPlacerControlID = 0x8A3F9D2Cu;
    constexpr float kPreviewPosEpsilon = 0.05f;

    bool TrySetFloraOrientation(cISC4Occupant* occupant, const int32_t rotation) {
        if (!occupant) {
            return false;
        }

        cISC4FloraOccupant* floraOccupant = nullptr;
        if (!occupant->QueryInterface(GZIID_cISC4FloraOccupant, reinterpret_cast<void**>(&floraOccupant))) {
            return false;
        }

        cRZAutoRefCount<cISC4FloraOccupant> floraRef(floraOccupant);
        return floraRef->SetOrientation(static_cast<uint32_t>(rotation & 3));
    }
}

FloraPainterInputControl::FloraPainterInputControl()
    : BasePainterInputControl(kFloraPlacerControlID) {}

FloraPainterInputControl::~FloraPainterInputControl() = default;

void FloraPainterInputControl::SetFloraToPaint(const uint32_t floraTypeID, const PropPaintSettings& settings,
                                               const std::string& name) {
    SetTypeToPaint(floraTypeID, settings, name);
    RefreshStaticFloraData_();
    LOG_INFO("Setting flora to paint: {} (0x{:08X})", name, floraTypeID);
}

void FloraPainterInputControl::SetFloraRepository(const FloraRepository* floraRepository) {
    floraRepository_ = floraRepository;
}

bool FloraPainterInputControl::IsFloraPlacementValid_(const uint32_t typeID,
                                                      const cS3DVector3& position,
                                                      cISC4Occupant* ignoredOccupant) const {
    if (!floraSimulator_ || typeID == 0) {
        return true;
    }

    cSC4StaticFloraData* floraData = typeID == typeToPaint_
        ? selectedFloraData_
        : floraSimulator_->GetStaticFloraData(typeID);
    if (!floraData) {
        LOG_DEBUG("No static flora data for 0x{:08X}", typeID);
        return false;
    }

    return floraSimulator_->LocationIsOKForNewFlora(typeID, position.fX, position.fZ, ignoredOccupant);
}

void FloraPainterInputControl::RefreshStaticFloraData_() {
    selectedFloraData_ = nullptr;
    if (!floraSimulator_ || typeToPaint_ == 0) {
        return;
    }

    selectedFloraData_ = floraSimulator_->GetStaticFloraData(typeToPaint_);
    if (!selectedFloraData_) {
        LOG_WARN("No static flora data found for 0x{:08X}", typeToPaint_);
    }
}

void FloraPainterInputControl::OnCityChanged_(cISC4City* pCity) {
    if (pCity) {
        floraSimulator_ = pCity->GetFloraSimulator();
    }
    else {
        floraSimulator_.Reset();
    }

    RefreshStaticFloraData_();
}

bool FloraPainterInputControl::PlaceAtWorld_(const cS3DVector3& pos, const int32_t rotation,
                                             const uint32_t typeID) {
    if (!floraSimulator_) {
        LOG_WARN("FloraPlacerInputControl: FloraSimulator not available");
        return false;
    }

    const uint32_t floraType = typeID != 0 ? typeID : typeToPaint_;
    if (floraType == 0) {
        LOG_WARN("PlaceAtWorld_: no flora type ID available");
        return false;
    }

    // Never commit the live preview occupant into the city. Demolish it first and
    // create a fresh flora occupant for the actual placement so preview state does
    // not leak into saved city data.
    if (previewOccupant_) {
        DestroyPreviewOccupant_();
    }

    if (!IsFloraPlacementValid_(floraType, pos)) {
        LOG_DEBUG("Location not OK for flora 0x{:08X} at ({:.2f}, {:.2f})", floraType, pos.fX, pos.fZ);
        return false;
    }

    float posArr[3] = {pos.fX, pos.fY, pos.fZ};

    // On the x86 game binary this overload returns the occupant-facing interface
    // pointer directly, despite the SDK header currently declaring a flora interface.
    cISC4Occupant* occupant =
        reinterpret_cast<cISC4Occupant*>(floraSimulator_->AddNewFloraOccupant(floraType, posArr, false));
    if (!occupant) {
        LOG_WARN("Failed to add flora occupant 0x{:08X}", floraType);
        return false;
    }

    if (!settings_.randomRotation && TrySetFloraOrientation(occupant, rotation)) {
        LOG_DEBUG("Set flora orientation to {}", rotation & 3);
    }

    occupant->SetHighlight(0x9, true);
    AddOccupantToUndo_(occupant);

    LOG_INFO("Placed flora 0x{:08X} at ({:.2f}, {:.2f}, {:.2f}), rotation: {}",
             floraType, pos.fX, pos.fY, pos.fZ, rotation & 3);
    return true;
}

void FloraPainterInputControl::RemoveOccupant_(cISC4Occupant* occupant) {
    if (!floraSimulator_) {
        LOG_WARN("No flora simulator available during undo/cancel");
        return;
    }
    if (!floraSimulator_->DemolishFloraOccupant(occupant, 0)) {
        LOG_WARN("Failed to demolish flora occupant");
    }
}

bool FloraPainterInputControl::ShouldShowModelPreview_() const {
    return settings_.previewMode != PreviewMode::Hidden &&
        IsInDirectPaintState_() &&
        (settings_.previewMode == PreviewMode::FullModel ||
         settings_.previewMode == PreviewMode::Combined);
}

bool FloraPainterInputControl::HasActivePreviewOccupant_() const {
    return previewOccupant_ != nullptr;
}

void FloraPainterInputControl::CreatePreviewOccupant_() {
    if (!floraSimulator_) {
        return;
    }
    LOG_DEBUG("In CreatePreviewOccupant_");

    const uint32_t previewFloraType = CurrentDirectTypeID_();
    if (previewFloraType == 0 || previewOccupant_ || !cursorValid_) {
        return;
    }

    if (!IsFloraPlacementValid_(previewFloraType, currentCursorWorld_)) {
        return;
    }

    cS3DVector3 initialPos = ResolveDirectPosition_(currentCursorWorld_);

    float posArr[3] = {initialPos.fX, initialPos.fY, initialPos.fZ};
    cISC4Occupant* occupant =
        reinterpret_cast<cISC4Occupant*>(floraSimulator_->AddNewFloraOccupant(previewFloraType, posArr, false));
    if (!occupant) {
        LOG_WARN("Failed to create flora preview occupant");
        return;
    }

    cISC4FloraOccupant* floraOccupant = nullptr;
    if (!occupant->QueryInterface(GZIID_cISC4FloraOccupant, reinterpret_cast<void**>(&floraOccupant))) {
        floraSimulator_->DemolishFloraOccupant(occupant, 0);
        return;
    }

    cRZAutoRefCount<cISC4Occupant> previewOccupant(occupant, cRZAutoRefCount<cISC4Occupant>::kAddRef);
    cRZAutoRefCount<cISC4FloraOccupant> previewFlora(floraOccupant);

    const bool needsVerticalAdjustment = std::abs(settings_.deltaYMeters) > kPreviewPosEpsilon;
    if (needsVerticalAdjustment && !previewOccupant->SetPosition(&initialPos)) {
        floraSimulator_->DemolishFloraOccupant(occupant, 0);
        return;
    }

    const int32_t normalizedRotation = settings_.rotation & 3;
    // if (!settings_.randomRotation && !previewFlora->SetOrientation(static_cast<uint32_t>(normalizedRotation))) {
    //     floraSimulator_->DemolishFloraOccupant(occupant, 0);
    //     return;
    // }

    lastPreviewPosition_ = initialPos;
    lastPreviewRotation_ = normalizedRotation;
    previewPositionValid_ = true;
    previewOccupant->SetHighlight(0x3, true);
    previewOccupant->SetVisibility(true, true);

    previewFlora_ = std::move(previewFlora);
    previewOccupant_ = std::move(previewOccupant);
    previewOccupantTypeID_ = previewFloraType;
    LOG_INFO("Created preview flora");
}

void FloraPainterInputControl::DestroyPreviewOccupant_() {
    if (!previewOccupant_) {
        previewFlora_.Reset();
        return;
    }

    previewOccupant_->SetVisibility(false, true);
    if (floraSimulator_) {
        floraSimulator_->DemolishFloraOccupant(previewOccupant_, 0);
    }

    previewOccupant_.Reset();
    previewFlora_.Reset();
    previewOccupantTypeID_ = 0;
    previewPositionValid_ = false;
    LOG_INFO("Destroyed preview flora");
}

void FloraPainterInputControl::HidePreviewForPick_() {
    if (previewOccupant_) {
        previewOccupant_->SetVisibility(false, true);
    }
}

void FloraPainterInputControl::UpdatePreviewOccupantRotation_() {
    if (!previewFlora_ || !previewOccupant_) {
        return;
    }

    const int32_t normalizedRotation = settings_.rotation & 3;
    if (!settings_.randomRotation && normalizedRotation != lastPreviewRotation_) {
        if (previewFlora_->SetOrientation(static_cast<uint32_t>(normalizedRotation))) {
            lastPreviewRotation_ = normalizedRotation;
        }
    }

    previewOccupant_->SetHighlight(0x2, false);
    previewOccupant_->SetHighlight(0x3, true);
}

void FloraPainterInputControl::UpdatePreviewOccupant_() {
    if (!previewOccupant_) {
        return;
    }

    if (!cursorValid_) {
        previewOccupant_->SetVisibility(false, true);
        previewPositionValid_ = false;
        return;
    }

    cS3DVector3 worldPos = ResolveDirectPosition_(currentCursorWorld_);
    if (!IsFloraPlacementValid_(CurrentDirectTypeID_(), worldPos, previewOccupant_)) {
        previewOccupant_->SetVisibility(false, true);
        previewPositionValid_ = false;
        return;
    }

    const bool posChanged =
        std::abs(worldPos.fX - lastPreviewPosition_.fX) > kPreviewPosEpsilon ||
        std::abs(worldPos.fY - lastPreviewPosition_.fY) > kPreviewPosEpsilon ||
        std::abs(worldPos.fZ - lastPreviewPosition_.fZ) > kPreviewPosEpsilon;

    if (settings_.snapPointsToGrid && posChanged) {
        DestroyPreviewOccupant_();
        CreatePreviewOccupant_();
        return;
    }

    if (posChanged) {
        if (previewOccupant_->SetPosition(&worldPos)) {
            lastPreviewPosition_ = worldPos;
            previewPositionValid_ = true;
        }
        else {
            previewPositionValid_ = false;
        }
    }
    else {
        previewPositionValid_ = true;
    }

    previewOccupant_->SetVisibility(previewPositionValid_, true);
}

bool FloraPainterInputControl::IsDirectPreviewPlacementValid_(const PlannedPaint& placement) const {
    return IsFloraPlacementValid_(placement.itemId, placement.position, previewOccupant_);
}

bool FloraPainterInputControl::ShouldForceDirectOverlay_() const {
    if (!IsInDirectPaintState_() || settings_.previewMode != PreviewMode::FullModel || !cursorValid_) {
        return false;
    }

    cS3DVector3 overlayPos = ResolveDirectPosition_(currentCursorWorld_);
    return !IsFloraPlacementValid_(CurrentDirectTypeID_(), overlayPos, previewOccupant_);
}

void FloraPainterInputControl::PopulatePreviewBounds_(PaintOverlay::PreviewPlacement& placement,
                                                      const uint32_t typeID) const {
    if (!floraRepository_) {
        return;
    }
    if (const Flora* flora = floraRepository_->FindFloraByInstanceId(typeID)) {
        placement.width  = flora->width;
        placement.height = flora->height;
        placement.depth  = flora->depth;
        placement.minX = flora->minX; placement.maxX = flora->maxX;
        placement.minY = flora->minY; placement.maxY = flora->maxY;
        placement.minZ = flora->minZ; placement.maxZ = flora->maxZ;
    }
}
