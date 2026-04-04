#include "PropPainterInputControl.hpp"

#include <windows.h>

#include "../common/Constants.hpp"
#include "../utils/Logger.h"
#include "PropRepository.hpp"
#include "cISC4Occupant.h"

namespace {
    constexpr auto kPropPainterControlID = 0x8A3F9D2B;

    void PopulatePreviewBounds(PaintOverlay::PreviewPlacement& pp, const Prop& prop) {
        pp.width  = prop.width;
        pp.height = prop.height;
        pp.depth  = prop.depth;
        pp.minX = prop.minX; pp.maxX = prop.maxX;
        pp.minY = prop.minY; pp.maxY = prop.maxY;
        pp.minZ = prop.minZ; pp.maxZ = prop.maxZ;
    }
}

PropPainterInputControl::PropPainterInputControl()
    : BasePainterInputControl(kPropPainterControlID) {}

PropPainterInputControl::~PropPainterInputControl() = default;

void PropPainterInputControl::SetPropToPaint(const uint32_t propID, const PropPaintSettings& settings,
                                              const std::string& name) {
    SetTypeToPaint(propID, settings, name);
    LOG_INFO("Setting prop to paint: {} (0x{:08X}), rotation: {}", name, propID, settings.rotation);
}

void PropPainterInputControl::SetPropRepository(const PropRepository* propRepository) {
    propRepository_ = propRepository;
}

// ── Virtual overrides ─────────────────────────────────────────────────────────

void PropPainterInputControl::OnCityChanged_(cISC4City* pCity) {
    if (pCity) {
        propManager_ = pCity->GetPropManager();
    }
    else {
        propManager_.Reset();
    }
}

bool PropPainterInputControl::PlaceAtWorld_(const cS3DVector3& pos, const int32_t rotation,
                                             const uint32_t typeID) {
    if (!propManager_) {
        LOG_WARN("PropPainterInputControl: PropManager not available");
        return false;
    }

    const uint32_t propToCreate = typeID != 0 ? typeID : typeToPaint_;
    if (propToCreate == 0) {
        LOG_WARN("PlaceAtWorld_: no prop ID available");
        return false;
    }

    cISC4PropOccupant* prop = nullptr;
    if (!propManager_->CreateProp(propToCreate, prop)) {
        LOG_WARN("Failed to create prop 0x{:08X}", propToCreate);
        return false;
    }

    cRZAutoRefCount propRef(prop);
    cISC4Occupant* occupant = prop->AsOccupant();
    if (!occupant) {
        LOG_WARN("Failed to get occupant interface from created prop");
        return false;
    }

    cS3DVector3 placePos = pos;
    if (!occupant->SetPosition(&placePos)) {
        LOG_WARN("Failed to set prop position");
        return false;
    }

    if (!prop->SetOrientation(rotation & 3)) {
        LOG_WARN("Failed to set prop orientation");
        return false;
    }

    if (!propManager_->AddCityProp(occupant)) {
        LOG_WARN("Failed to add prop to city - validation failed (?)");
        return false;
    }

    occupant->SetHighlight(0x9, true);
    AddOccupantToUndo_(occupant);

    LOG_INFO("Placed prop 0x{:08X} at ({:.2f}, {:.2f}, {:.2f}), rotation: {}",
             propToCreate, placePos.fX, placePos.fY, placePos.fZ, rotation & 3);
    return true;
}

void PropPainterInputControl::RemoveOccupant_(cISC4Occupant* occupant) {
    if (!propManager_) {
        LOG_WARN("No prop manager available during undo/cancel");
        return;
    }
    if (!propManager_->RemovePropA(occupant)) {
        LOG_WARN("Failed to remove placed prop");
    }
}

bool PropPainterInputControl::ShouldShowModelPreview_() const {
    return settings_.previewMode != PreviewMode::Hidden &&
        IsInDirectPaintState_() &&
        (settings_.previewMode == PreviewMode::FullModel ||
         settings_.previewMode == PreviewMode::Combined);
}

bool PropPainterInputControl::HasActivePreviewOccupant_() const {
    return previewProp_ != nullptr;
}

void PropPainterInputControl::CreatePreviewOccupant_() {
    if (!propManager_) {
        return;
    }

    const uint32_t previewPropID = CurrentDirectTypeID_();
    if (previewPropID == 0 || previewProp_) {
        return;
    }

    cISC4PropOccupant* prop = nullptr;
    if (!propManager_->CreateProp(previewPropID, prop)) {
        LOG_WARN("Failed to create prop for preview");
        return;
    }

    cISC4Occupant* occupant = prop->AsOccupant();
    if (!occupant) {
        prop->Release();
        return;
    }

    cRZAutoRefCount<cISC4PropOccupant> previewProp(prop);
    cRZAutoRefCount<cISC4Occupant> previewOccupant(occupant, cRZAutoRefCount<cISC4Occupant>::kAddRef);

    cS3DVector3 initialPos(0.0f, 1000.0f, 0.0f);
    if (!previewOccupant->SetPosition(&initialPos)) {
        return;
    }

    const int32_t normalizedRotation = settings_.rotation & 3;
    if (!previewProp->SetOrientation(normalizedRotation)) {
        return;
    }

    if (!propManager_->AddCityProp(previewOccupant)) {
        return;
    }

    lastPreviewPosition_ = initialPos;
    lastPreviewRotation_ = normalizedRotation;
    previewPositionValid_ = false;
    previewOccupant->SetHighlight(0x3, true);
    previewOccupant->SetVisibility(false, true);

    previewProp_ = std::move(previewProp);
    previewOccupant_ = std::move(previewOccupant);
    previewOccupantTypeID_ = previewPropID;
    LOG_INFO("Created preview prop");
}

void PropPainterInputControl::DestroyPreviewOccupant_() {
    if (!previewOccupant_) {
        previewProp_.Reset();
        return;
    }

    previewOccupant_->SetVisibility(false, true);
    if (propManager_) {
        propManager_->RemovePropA(previewOccupant_);
    }

    previewOccupant_.Reset();
    previewProp_.Reset();
    previewOccupantTypeID_ = 0;
    previewPositionValid_ = false;
    LOG_INFO("Destroyed preview prop");
}

void PropPainterInputControl::HidePreviewForPick_() {
    if (previewOccupant_) {
        previewOccupant_->SetVisibility(false, true);
    }
}

void PropPainterInputControl::UpdatePreviewOccupantRotation_() {
    if (!previewProp_ || !previewOccupant_) {
        return;
    }

    const int32_t normalizedRotation = settings_.rotation & 3;
    if (normalizedRotation != lastPreviewRotation_) {
        if (previewProp_->SetOrientation(normalizedRotation)) {
            lastPreviewRotation_ = normalizedRotation;
        }
    }

    previewOccupant_->SetHighlight(0x2, false);
    previewOccupant_->SetHighlight(0x3, true);
}

void PropPainterInputControl::UpdatePreviewOccupant_() {
    if (!previewOccupant_) {
        return;
    }

    if (!cursorValid_) {
        previewOccupant_->SetVisibility(false, true);
        return;
    }

    const cS3DVector3 worldPos = ResolveDirectPosition_(currentCursorWorld_);

    const bool posChanged =
        std::abs(worldPos.fX - lastPreviewPosition_.fX) > 0.05f ||
        std::abs(worldPos.fY - lastPreviewPosition_.fY) > 0.05f ||
        std::abs(worldPos.fZ - lastPreviewPosition_.fZ) > 0.05f;

    if (posChanged) {
        if (previewOccupant_->SetPosition(&worldPos)) {
            lastPreviewPosition_ = worldPos;
            previewPositionValid_ = true;
        }
    }
    else {
        previewPositionValid_ = true;
    }

    previewOccupant_->SetVisibility(true, true);
}

void PropPainterInputControl::PopulatePreviewBounds_(PaintOverlay::PreviewPlacement& placement,
                                                      const uint32_t typeID) const {
    if (!propRepository_) {
        return;
    }
    if (const Prop* prop = propRepository_->FindPropByInstanceId(typeID)) {
        PopulatePreviewBounds(placement, *prop);
    }
}
