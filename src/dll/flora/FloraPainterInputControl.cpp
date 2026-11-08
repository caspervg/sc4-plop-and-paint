#include "FloraPainterInputControl.hpp"

#include "FloraRepository.hpp"
#include "../props/PropModelKey.hpp"
#include "../props/PropPainterInputControl.hpp"
#include "../utils/Logger.h"
#include "cISC4City.h"
#include "cISC4FloraOccupant.h"
#include "cISC4Occupant.h"

namespace {
    constexpr auto kFloraPlacerControlID = 0x8A3F9D2Cu;

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
    const auto lookup = [this](const uint32_t floraID) -> std::optional<PropModel> {
        const Flora* flora = floraRepository_ ? floraRepository_->FindFloraByInstanceId(floraID) : nullptr;
        return flora ? FindPropModel(flora->groupId.value(), flora->instanceId.value()) : std::nullopt;
    };
    cursorPreview_.SetModelLookup(lookup);
    cursorPreview_.UseUnpinnedModels();
    plannedPreview_.SetModelLookup(lookup);
}

bool FloraPainterInputControl::IsFloraPlacementValid_(const uint32_t typeID, const cS3DVector3& position) const {
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

    return floraSimulator_->LocationIsOKForNewFlora(typeID, position.fX, position.fZ, nullptr);
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

    cursorPreview_.SetCity(pCity);
    plannedPreview_.SetCity(pCity);
    cursorPreviewActive_ = false;
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
    return cursorPreviewActive_;
}

void FloraPainterInputControl::CreatePreviewOccupant_() {
    const uint32_t floraType = CurrentDirectTypeID_();
    if (floraType == 0 || cursorPreviewActive_ || !cursorPreview_.Available()) {
        return;
    }

    previewOccupantTypeID_ = floraType;
    previewPositionValid_ = false;
    cursorPreviewActive_ = true;
}

void FloraPainterInputControl::DestroyPreviewOccupant_() {
    cursorPreview_.Hide();
    cursorPreviewActive_ = false;
    previewOccupantTypeID_ = 0;
    previewPositionValid_ = false;
}

void FloraPainterInputControl::UpdatePreviewOccupant_() {
    if (!cursorPreviewActive_) {
        return;
    }
    if (!cursorValid_) {
        cursorPreview_.Hide();
        return;
    }

    PlannedPaint placement;
    placement.position = ResolveDirectPosition_(currentCursorWorld_);
    placement.rotation = settings_.rotation;
    placement.itemId = previewOccupantTypeID_;
    const bool valid = IsDirectPreviewPlacementValid_(placement);
    cursorPreview_.Show({{placement.itemId, placement.position, placement.rotation, valid, DirectFootprint_(placement)}},
                        ModelPreview::CurrentView(view3D));
    const bool hasModel = cursorPreview_.HasModel(placement.itemId);
    cursorPreview_.SetArrow(ShowsDirectionArrow_(valid) && hasModel, placement.position, placement.rotation);
    lastPreviewPosition_ = placement.position;
    lastPreviewRotation_ = placement.rotation;
    previewPositionValid_ = true;
}

void FloraPainterInputControl::ShowPlannedModels_(const std::vector<PaintOverlay::PreviewPlacement>& placements) {
    std::vector<ModelPreview::Item> items;
    items.reserve(placements.size());
    for (const auto& preview : placements) {
        const PlannedPaint& placement = preview.placement;
        items.push_back({placement.itemId, placement.position, placement.rotation, preview.valid, FootprintFor_(preview)});
    }
    plannedPreview_.Show(items, ModelPreview::CurrentView(view3D));
}

bool FloraPainterInputControl::IsDirectPreviewPlacementValid_(const PlannedPaint& placement) const {
    return IsFloraPlacementValid_(placement.itemId, placement.position);
}

bool FloraPainterInputControl::ShouldForceDirectOverlay_() const {
    // Without a model to show, keep the outline.
    return !cursorPreview_.HasModel(CurrentDirectTypeID_());
}

void FloraPainterInputControl::PopulatePreviewBounds_(PaintOverlay::PreviewPlacement& placement,
                                                      const uint32_t typeID) const {
    placement.hasModel = cursorPreview_.HasModel(typeID);
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
