#include "GroundPanelTab.hpp"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cctype>
#include <numbers>
#include <optional>
#include <string_view>
#include <cstdio>
#include <string>
#include <tuple>
#include <utility>

#include "../decals/DecalTextureLoader.hpp"
#include "../packs/PackCatalogRepository.hpp"
#include "../SC4PlopAndPaintDirector.hpp"
#include "public/cIGZTerrainPaintService.h"
#include "GroundPainterInputControl.hpp"

using namespace Ground;
using Document = Ground::GroundPaintDocument;
using PanelUi::Hint;
using PanelUi::kGroupGap;
using PanelUi::NextInRow;
using PanelUi::Tooltip;

namespace {
    constexpr float kThumbSize = 40.0f;
    constexpr float kPickerThumbSize = 56.0f;
    constexpr float kPaletteTileSize = 32.0f;
    constexpr ImVec4 kAttentionColor(1.0f, 0.85f, 0.3f, 1.0f);

    struct ToolInfo {
        Tool tool;
        const char* label;
        const char* tip;
    };

    // Tools that paint, then tools that change what is painted.
    constexpr ToolInfo kPaintTools[] = {
        {Tool::Area, "Area", "Paint an area of any shape."},
        {Tool::Line, "Line", "Paint a line, for example a path, a kerb or a road marking."},
        {Tool::Brush, "Brush", "Paint freely with a round brush."},
        {Tool::Marking, "Marking", "Paint road markings, for example parking bays, zebra crossings, arrows, double lines, hatching and text."},
    };
    constexpr ToolInfo kEditTools[] = {
        {Tool::Select, "Select", "Select painted ground. You can then move, turn, change, copy or delete it."},
        {Tool::Erase, "Erase", "Remove painted ground."},
    };

    // Colours that road markings and ground paint often need, one click away.
    struct Swatch {
        const char* name;
        float r, g, b;
    };
    constexpr Swatch kSwatches[] = {
        {"Marking white", 0.95f, 0.95f, 0.92f}, {"Marking yellow", 0.98f, 0.80f, 0.12f},
        {"Bus lane red", 0.70f, 0.18f, 0.15f},  {"Cycle lane green", 0.25f, 0.55f, 0.30f},
        {"Disabled bay blue", 0.15f, 0.40f, 0.75f}, {"Asphalt gray", 0.32f, 0.32f, 0.33f},
        {"Dark asphalt", 0.16f, 0.16f, 0.17f},  {"Concrete", 0.70f, 0.69f, 0.65f},
        {"Earth", 0.45f, 0.35f, 0.24f},          {"Sand", 0.82f, 0.74f, 0.55f},
    };

    // A small picture of a marking, made by the code that paints it, so it shows what you get.
    Area MarkingPicture(const MarkingKind kind, const ArrowKind arrow) {
        MarkingSpec spec;
        spec.kind = kind;
        spec.arrow = arrow;
        std::vector<Vec2> points;
        PathStyle style = PathStyle::Straight;
        switch (kind) {
        case MarkingKind::ParkingRow:
            spec.lineWidth = 0.35f;
            spec.stallWidth = 2.4f;
            spec.stallDepth = 5.0f;
            points = {{0.0f, 7.5f}, {10.0f, 7.5f}};
            break;
        case MarkingKind::CarPark:
            spec.lineWidth = 0.3f;
            spec.stallWidth = 2.0f;
            spec.stallDepth = 3.5f;
            spec.aisleWidth = 2.0f;
            spec.rowPairs = 1;
            points = {{0.0f, 9.0f}, {10.0f, 9.0f}};
            break;
        case MarkingKind::Zebra:
            spec.stripeWidth = 1.0f;
            spec.stripeGap = 1.0f;
            spec.stripeLength = 6.0f;
            points = {{0.0f, 5.0f}, {10.0f, 5.0f}};
            break;
        case MarkingKind::Arrow:
            spec.lineWidth = 0.9f;
            spec.arrowWidth = 4.5f;
            points = {{5.0f, 10.0f}, {5.0f, 0.0f}};
            break;
        case MarkingKind::DoubleLine:
            spec.lineWidth = 0.5f;
            spec.lineSpacing = 0.7f;
            points = {{0.0f, 9.0f}, {4.0f, 4.0f}, {10.0f, 2.0f}};
            style = PathStyle::Curved;
            break;
        case MarkingKind::Hatching:
            spec.lineWidth = 0.4f;
            spec.hatchSpacing = 1.8f;
            points = {{0.0f, 0.0f}, {10.0f, 0.0f}, {10.0f, 10.0f}, {0.0f, 10.0f}};
            break;
        case MarkingKind::Text:
            spec.text = "Ab";
            spec.textHeight = 6.0f;
            spec.lineWidth = 0.8f;
            points = {{0.0f, 8.0f}, {10.0f, 8.0f}};
            break;
        }
        return BuildMarking(spec, points, style);
    }

    // A button showing a marking's picture; its name in the tooltip. True when clicked.
    bool PictureButton(const char* id, const Area& picture, const bool active, const char* name) {
        constexpr float kSize = 44.0f;
        constexpr float kPad = 5.0f;
        ImGui::PushID(id);
        const bool clicked = ImGui::InvisibleButton("##picture", ImVec2(kSize, kSize));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 background = ImGui::GetColorU32(active    ? ImGuiCol_ButtonActive
                                                    : hovered ? ImGuiCol_ButtonHovered
                                                              : ImGuiCol_Button);
        draw->AddRectFilled(min, max, background, 4.0f);
        if (active) {
            draw->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_CheckMark), 4.0f, 0, 2.0f);
        }

        // Fitted into the button, holes drawn over in the background colour.
        const Bounds bounds = BoundsOf(picture);
        const float width = std::max(bounds.max.x - bounds.min.x, bounds.max.z - bounds.min.z);
        if (width > 0.0f) {
            const float scale = (kSize - 2.0f * kPad) / width;
            const ImVec2 origin(min.x + kPad + ((kSize - 2.0f * kPad) - (bounds.max.x - bounds.min.x) * scale) * 0.5f,
                                min.y + kPad + ((kSize - 2.0f * kPad) - (bounds.max.z - bounds.min.z) * scale) * 0.5f);
            const auto signedArea = [](const Ring& ring) {
                double twice = 0.0;
                for (size_t i = 0; i < ring.size(); ++i) {
                    const Vec2 a = ring[i];
                    const Vec2 b = ring[(i + 1) % ring.size()];
                    twice += static_cast<double>(a.x) * b.z - static_cast<double>(b.x) * a.z;
                }
                return twice;
            };
            double outerSign = 0.0;
            for (const Ring& ring : picture) {
                if (std::abs(signedArea(ring)) > std::abs(outerSign)) {
                    outerSign = signedArea(ring);
                }
            }
            const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
            std::vector<ImVec2> points;
            for (const bool holes : {false, true}) {
                for (const Ring& ring : picture) {
                    if ((signedArea(ring) * outerSign < 0.0) != holes || ring.size() < 3) {
                        continue;
                    }
                    points.clear();
                    for (const Vec2& p : ring) {
                        points.emplace_back(origin.x + (p.x - bounds.min.x) * scale, origin.y + (p.z - bounds.min.z) * scale);
                    }
                    draw->AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), holes ? background : ink);
                }
            }
        }
        if (hovered) {
            ImGui::SetTooltip("%s", name);
        }
        ImGui::PopID();
        return clicked;
    }

    // Shows the active button pressed.
    bool ToggleButton(const char* label, const bool active) {
        if (active) {
            // An outline too: the colour alone is easy to miss.
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        }
        const bool clicked = ImGui::Button(label);
        if (active) {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }
        return clicked;
    }

    // The width of each of count controls sharing what is left of the line.
    float ShareOfLine(const int count) {
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        return std::max(1.0f, (ImGui::GetContentRegionAvail().x - spacing * static_cast<float>(count - 1)) /
                                  static_cast<float>(count));
    }

    std::string ObjectLabel(const PaintObject& object) {
        if (!object.name.empty()) {
            return object.name;
        }
        const char* const kind = object.shape.kind == ShapeKind::Marking ? MarkingKindName(object.shape.marking.kind)
                                                                         : ShapeKindName(object.shape.kind);
        return std::string(kind) + " " + std::to_string(object.id);
    }

    // Path markings follow a path that can be curved; the others take single points.
    bool MarkingFollowsPath(const MarkingKind kind) {
        return kind == MarkingKind::ParkingRow || kind == MarkingKind::CarPark || kind == MarkingKind::DoubleLine;
    }

    // Applies a control's change at once and commits it when the control is let go.
    void Track(bool& changed, bool& committed) {
        changed = changed || ImGui::IsItemEdited();
        committed = committed || ImGui::IsItemDeactivatedAfterEdit();
    }

    void Length(const char* label, float& value, const float min, const float max, bool& changed, bool& committed) {
        ImGui::DragFloat(label, &value, 0.01f, min, max, "%.2f m");
        Track(changed, committed);
    }

    void MarkingEditor(MarkingSpec& marking, bool& changed, bool& committed) {
        constexpr MarkingKind kKinds[] = {MarkingKind::ParkingRow, MarkingKind::CarPark, MarkingKind::Zebra,
                                          MarkingKind::Arrow, MarkingKind::DoubleLine, MarkingKind::Hatching,
                                          MarkingKind::Text};
        static const std::vector<Area> kPictures = [&] {
            std::vector<Area> pictures;
            for (const MarkingKind kind : kKinds) {
                pictures.push_back(MarkingPicture(kind, ArrowKind::Straight));
            }
            return pictures;
        }();
        ImGui::PushID("kinds");
        for (size_t i = 0; i < std::size(kKinds); ++i) {
            NextInRow(i, 44.0f);
            const char* const name = MarkingKindName(kKinds[i]);
            if (PictureButton(name, kPictures[i], marking.kind == kKinds[i], name) && marking.kind != kKinds[i]) {
                marking.kind = kKinds[i];
                committed = true;
            }
        }
        ImGui::PopID();
        ImGui::TextUnformatted(MarkingKindName(marking.kind));
        Length("Line width", marking.lineWidth, 0.02f, 2.0f, changed, committed);

        switch (marking.kind) {
        case MarkingKind::ParkingRow:
        case MarkingKind::CarPark:
            Length("Bay width", marking.stallWidth, 1.0f, 20.0f, changed, committed);
            Length("Bay length", marking.stallDepth, 1.0f, 30.0f, changed, committed);
            ImGui::DragFloat("Bay angle", &marking.stallAngleDegrees, 0.5f, 30.0f, 90.0f, "%.0f°");
            Track(changed, committed);
            ImGui::Checkbox("Line along the front", &marking.closeFront);
            Track(changed, committed);
            if (marking.kind == MarkingKind::CarPark) {
                ImGui::DragInt("Double rows", &marking.rowPairs, 0.05f, 1, 20);
                Track(changed, committed);
                Length("Aisle width", marking.aisleWidth, 2.0f, 30.0f, changed, committed);
            }
            break;
        case MarkingKind::Zebra:
            Length("Stripe width", marking.stripeWidth, 0.1f, 5.0f, changed, committed);
            Length("Stripe gap", marking.stripeGap, 0.1f, 5.0f, changed, committed);
            Length("Stripe length", marking.stripeLength, 0.5f, 20.0f, changed, committed);
            break;
        case MarkingKind::Arrow: {
            struct ArrowChoice {
                ArrowKind arrow;
                const char* name;
            };
            constexpr ArrowChoice kArrows[] = {
                {ArrowKind::Straight, "Straight"}, {ArrowKind::Left, "Left"}, {ArrowKind::Right, "Right"},
                {ArrowKind::StraightLeft, "Straight or left"}, {ArrowKind::StraightRight, "Straight or right"},
                {ArrowKind::LeftRight, "Left or right"}, {ArrowKind::UTurn, "U-turn"},
            };
            static const std::vector<Area> kArrowPictures = [&] {
                std::vector<Area> pictures;
                for (const ArrowChoice& choice : kArrows) {
                    pictures.push_back(MarkingPicture(MarkingKind::Arrow, choice.arrow));
                }
                return pictures;
            }();
            ImGui::PushID("arrows");
            for (size_t i = 0; i < std::size(kArrows); ++i) {
                NextInRow(i, 44.0f);
                if (PictureButton(kArrows[i].name, kArrowPictures[i], marking.arrow == kArrows[i].arrow, kArrows[i].name) &&
                    marking.arrow != kArrows[i].arrow) {
                    marking.arrow = kArrows[i].arrow;
                    committed = true;
                }
            }
            ImGui::PopID();
            Length("Head width", marking.arrowWidth, 0.2f, 10.0f, changed, committed);
            break;
        }
        case MarkingKind::DoubleLine:
            Length("Space between", marking.lineSpacing, 0.02f, 5.0f, changed, committed);
            ImGui::DragFloat("Dash", &marking.dashLength, 0.05f, 0.0f, 50.0f, marking.dashLength > 0.0f ? "%.2f m" : "solid");
            Track(changed, committed);
            if (marking.dashLength > 0.0f) {
                Length("Gap", marking.gapLength, 0.0f, 50.0f, changed, committed);
            }
            break;
        case MarkingKind::Hatching:
            Length("Stripe spacing", marking.hatchSpacing, 0.1f, 20.0f, changed, committed);
            ImGui::DragFloat("Stripe angle", &marking.hatchAngleDegrees, 0.5f, -90.0f, 90.0f, "%.0f°");
            Track(changed, committed);
            ImGui::Checkbox("Outline", &marking.hatchOutline);
            Track(changed, committed);
            ImGui::SameLine();
            ImGui::Checkbox("Chevrons", &marking.chevron);
            Track(changed, committed);
            break;
        case MarkingKind::Text: {
            char text[128];
            std::snprintf(text, sizeof(text), "%s", marking.text.c_str());
            if (ImGui::InputText("Text", text, sizeof(text))) {
                marking.text = text;
            }
            Track(changed, committed);
            Length("Letter height", marking.textHeight, 0.2f, 20.0f, changed, committed);
            ImGui::DragFloat("Letter spacing", &marking.letterSpacing, 0.01f, 0.0f, 2.0f, "%.2f");
            Track(changed, committed);
            break;
        }
        }
    }

    // Whether going from one version of an object to the other is cheap enough to show every
    // frame: the same cut, and nothing the service has to work out again per cell (the soft edge
    // and worn paint).
    bool CheapChange(const PaintObject& a, const PaintObject& b) {
        return SameShape(a, b) && a.look.featherMeters == b.look.featherMeters &&
               a.look.patchBlendMeters == b.look.patchBlendMeters && a.shape.wear == b.shape.wear;
    }

}

GroundPanelTab::GroundPanelTab(SC4PlopAndPaintDirector* director,
                               DecalRepository* decals,
                               FavoritesRepository* favorites,
                               cIGZPersistResourceManager* pRM,
                               cIGZImGuiService* imguiService)
    : PanelTab(director, nullptr, nullptr, favorites, imguiService)
    , decals_(decals)
    , pRM_(pRM)
    , stampTab_(decals ? std::make_unique<DecalPanelTab>(director, decals, favorites, pRM, imguiService) : nullptr) {
    const PaintGridChoice grid = director->GetGridChoice(&PaintGridChoices::ground);
    settings_.showGrid = *grid.show;
    settings_.snapPointsToGrid = *grid.snap;
    settings_.gridStepMeters = *grid.step;
}

void GroundPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    hoverPreview_.Clear();
    thumbnails_.OnDeviceReset();
    if (stampTab_) {
        stampTab_->OnDeviceReset(deviceGeneration);
    }
}

void GroundPanelTab::OnShutdown() {
    thumbnails_.Clear();
    hoverPreview_.Clear();
    if (stampTab_) {
        stampTab_->OnShutdown();
    }
}

void GroundPanelTab::SettingsChanged_() {
    settingsDirty_ = true;
}

void GroundPanelTab::FlushSettings_() {
    if (!settingsDirty_) {
        return;
    }
    // While a slider is dragged the preview follows every so often, not every frame: shape
    // changes cut the whole preview again. A change that only restyles it, such as a colour or
    // the opacity, follows at once. Letting go sends the last value at once.
    const auto now = std::chrono::steady_clock::now();
    const bool restyleOnly = [&] {
        ToolSettings shapeOnly = settings_;
        shapeOnly.look = pushedSettings_.look;
        PaintObject before;
        PaintObject after;
        before.look = pushedSettings_.look;
        after.look = settings_.look;
        before.shape.kind = after.shape.kind = settings_.tool == Tool::Line ? ShapeKind::Line : ShapeKind::Area;
        return shapeOnly == pushedSettings_ && CheapChange(before, after);
    }();
    if (!restyleOnly && ImGui::IsAnyItemActive() && now - lastSettingsPush_ < std::chrono::milliseconds(150)) {
        return;
    }
    settingsDirty_ = false;
    lastSettingsPush_ = now;
    pushedSettings_ = settings_;
    if (director_->IsGroundPainting()) {
        director_->UpdateGroundPainting(settings_);
    }
}

void GroundPanelTab::SelectTool_(const Tool tool) {
    stamping_ = false;
    settings_.tool = tool;
    ReleaseImGuiInputCapture_();
    pushedSettings_ = settings_;
    director_->StartGroundPainting(settings_);
}

void GroundPanelTab::FinishTexturePick_() {
    const std::optional<uint32_t> picked = std::exchange(pickedTexture_, std::nullopt);
    // Paints with it, from Colour too, whose colour is kept for going back.
    const auto use = [&](Look& look, LookEditorState& state) {
        if (look.textures.empty() && !state.wantsTexture) {
            state.plainColour = look.color;
            look.color = {1.0f, 1.0f, 1.0f};
        }
        state.wantsTexture = true;
        bool committed = false;
        UseTexture_(look, *picked, false, committed);
        // As the Stamp picker does: the library shows its family, the base or overlay ID without
        // the wealth, state and zoom variants.
        std::snprintf(textureFilter_, sizeof(textureFilter_), "%04X", *picked >> 16);
        packFilter_.Clear();
        favouriteTexturesOnly_ = false;
        kindFilter_ = {};
    };
    if (picked && pickForSelection_) {
        Document* const document = director_->GetGroundDocument();
        if (document && editingId_ != 0 && document->Find(editingId_)) {
            use(editing_.look, selectionLook_);
            document->Update({editing_});
        }
    }
    else if (picked) {
        use(settings_.look, toolLook_);
    }
    // Not when something else was started instead.
    if (!stamping_ && !director_->IsAnyToolActive()) {
        SelectTool_(pickResumeTool_);
    }
}

void GroundPanelTab::OnRender() {
    const GroundPainterInputControl* painter = director_->GetGroundPainter();
    if (painter && director_->IsGroundPainting() && !settingsDirty_) {
        // Keys and the middle mouse button in the city may have changed the grid and the brush.
        const ToolSettings& current = painter->Settings();
        settings_.showGrid = current.showGrid;
        settings_.snapPointsToGrid = current.snapPointsToGrid;
        settings_.snapToPaint = current.snapToPaint;
        settings_.gridStepMeters = current.gridStepMeters;
        settings_.brushRadius = current.brushRadius;
        if (current.look != settings_.look && painter->LookPickCount() != lookPickCount_) {
            // Copied from painted ground in the city.
            settings_.look = current.look;
            settings_.wear = current.wear;
            settings_.roughness = current.roughness;
            toolLook_ = {};
            toolLook_.wantsTexture = !settings_.look.textures.empty();
        }
        lookPickCount_ = painter->LookPickCount();
    }
    if (pickingTexture_ && !director_->IsDecalPicking()) {
        pickingTexture_ = false;
        FinishTexturePick_();
    }
    if (!opened_) {
        opened_ = true;
        // The first time: start with Area, so the tab shows what painting takes. Not over another
        // tool in use.
        if (director_->IsGroundPaintAvailable() && director_->GetGroundDocument() && !director_->IsAnyToolActive()) {
            SelectTool_(Tool::Area);
        }
    }
    ImGui::PushID("tools");
    RenderToolRow_();
    ImGui::PopID();
    // Both modes list the same textures, so the search goes along when the mode changes.
    if (stampTab_ && stamping_ != shownStamping_) {
        if (stamping_) {
            stampTab_->SetSearch(textureFilter_, packFilter_.Selected());
        }
        else {
            std::snprintf(textureFilter_, sizeof(textureFilter_), "%s", stampTab_->SearchText());
            packFilter_.Select(stampTab_->SelectedPack());
        }
    }
    shownStamping_ = stamping_;

    if (stamping_) {
        ImGui::Separator();
        if (stampTab_) {
            stampTab_->OnRender();
        }
        return;
    }

    Document* const document = director_->GetGroundDocument();
    if (!director_->IsGroundPaintAvailable() || !document) {
        ImGui::TextWrapped("Ground painting needs SC4RenderServices with terrain paint and "
                           "EnableCustomTerrainDecalRenderer=true in SC4RenderServices.ini.");
        return;
    }

    RenderStatus_();
    const bool painting = director_->IsGroundPainting();
    if (painting && settings_.tool == Tool::Select) {
        ImGui::PushID("selection");
        RenderSelection_(*document);
        ImGui::PopID();
    }
    else if (painting) {
        bool changed = false;
        bool committed = false;
        ImGui::PushID("shape");
        RenderShapeSettings_();
        ImGui::PopID();
        if (settings_.tool != Tool::Erase) {
            ImGui::SeparatorText("Paint with");
            if (GroundPainterInputControl* armable = director_->GetGroundPainter()) {
                if (ImGui::SmallButton(armable->LookPickArmed() ? "Click painted ground..." : "Copy look")) {
                    armable->ArmLookPick();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Copy the look of ground that you painted.\nClick this button, then click the ground in the city.\n"
                                      "You can also Alt+click the ground in the city.");
                }
            }
            ImGui::PushID("look");
            RenderLookEditor_(settings_.look, settings_.tool == Tool::Line, toolLook_, changed, committed);
            ImGui::PopID();
        }
        if (settings_.tool != Tool::Erase) {
            RenderFinish_(settings_.look, settings_.wear, settings_.tool == Tool::Marking ? nullptr : &settings_.roughness,
                          changed, committed);
        }
        if (changed || committed) {
            SettingsChanged_();
        }
    }
    if (painting) {
        RenderGrid_();
    }

    ImGui::PushID("layers");
    RenderLayers_(*document);
    ImGui::PopID();
    FlushSettings_();
    thumbnails_.ProcessLoadQueue([this](const uint32_t instanceId) {
        ImGuiTexture texture;
        ImVec2 size{};
        decals::LoadDecalTexture(pRM_, imguiService_, instanceId, texture, size);
        return texture;
    });
}

void GroundPanelTab::RenderToolRow_() {
    const bool painting = director_->IsGroundPainting();
    const bool available = director_->IsGroundPaintAvailable() && director_->GetGroundDocument();
    // The buttons wrap in a narrow panel, with a wider gap between the groups.
    size_t index = 0;
    const auto place = [&](const char* label, const bool firstOfGroup) {
        NextInRow(index++, ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f,
                  firstOfGroup ? kGroupGap : -1.0f);
    };
    const auto toolButtons = [&](const auto& tools) {
        bool first = true;
        for (const ToolInfo& info : tools) {
            place(info.label, std::exchange(first, false));
            const bool active = painting && !stamping_ && settings_.tool == info.tool;
            ImGui::BeginDisabled(!available);
            if (ToggleButton(info.label, active)) {
                // Clicking the tool in use puts it away.
                if (active) {
                    director_->StopGroundPainting();
                }
                else {
                    SelectTool_(info.tool);
                }
            }
            ImGui::EndDisabled();
            Tooltip(info.tip);
        }
    };

    toolButtons(kPaintTools);
    toolButtons(kEditTools);
    place("Stamp decals", true);
    if (ToggleButton("Stamp decals", stamping_)) {
        stamping_ = !stamping_;
        if (stamping_) {
            director_->StopGroundPainting();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Place, select and remove single texture decals.");
    }
}

void GroundPanelTab::RenderStatus_() {
    const GroundPainterInputControl* painter = director_->GetGroundPainter();
    const bool painting = painter && director_->IsGroundPainting();
    const std::string text =
        painting          ? painter->StatusText()
        : pickingTexture_ ? "Click a texture in the city. You can click a decal, or the base or overlay texture of a "
                            "lot. If textures overlap, hold Alt and scroll to change between them. Press Esc to cancel."
                          : "Click a tool above. Use Area, Line, Brush or Marking to paint the ground. Use Select to "
                            "change the paint. Use Erase to remove it.";

    static const std::string kNoMeasure;
    const std::string& measure = painting ? painter->MeasureText() : kNoMeasure;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float wrap = ImGui::GetContentRegionAvail().x - style.WindowPadding.x * 2.0f;
    // Room for a measure line always while painting, so the box does not jump as it comes and goes.
    constexpr const char* kStopHint = "Press Esc once to cancel the shape. Press Esc again to stop. Right-drag to move the camera.";
    const float height = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap).y +
                         (painting ? ImGui::GetTextLineHeightWithSpacing() + style.ItemSpacing.y * 2.0f +
                                         ImGui::CalcTextSize(kStopHint, nullptr, false, wrap).y
                                   : 0.0f) +
                         style.WindowPadding.y * 2.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    if (ImGui::BeginChild("##status", ImVec2(0, height), true, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::TextWrapped("%s", text.c_str());
        if (painting) {
            ImGui::TextColored(kAttentionColor, "%s", measure.empty() ? " " : measure.c_str());
            Hint(kStopHint);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void GroundPanelTab::RenderFinish_(Look& look, float& wear, float* roughness, bool& changed, bool& committed) {
    ImGui::PushID("finish");
    ImGui::SeparatorText("Finish");
    // Three short sliders on one line, each saying what it is in its value.
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - 2.0f * spacing) / 3.0f;
    float opacity = look.opacity * 100.0f;
    ImGui::SetNextItemWidth(width);
    if (ImGui::SliderFloat("##opacity", &opacity, 0.0f, 100.0f, "Opacity %.0f%%")) {
        look.opacity = opacity / 100.0f;
    }
    Track(changed, committed);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width);
    ImGui::DragFloat("##softEdge", &look.featherMeters, 0.05f, 0.0f, 32.0f,
                     look.featherMeters > 0.0f ? "Soft edge %.2f m" : "Sharp edge");
    Track(changed, committed);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The distance from the edge over which the paint fades out. Drag to the right for a soft edge.");
    }
    ImGui::SameLine();
    float worn = wear * 100.0f;
    ImGui::SetNextItemWidth(width);
    if (ImGui::SliderFloat("##worn", &worn, 0.0f, 90.0f, worn > 0.0f ? "Worn %.0f%%" : "Not worn")) {
        wear = worn / 100.0f;
    }
    Track(changed, committed);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Remove patches of paint, as on an old road.");
    }

    // A rough edge is not for markings, whose lines should stay crisp.
    if (roughness) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##rough", roughness, 0.02f, 0.0f, 16.0f, *roughness > 0.0f ? "Rough edge %.2f m" : "Smooth edge");
        Track(changed, committed);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Make the outline uneven and natural, for example for grass, dirt or gravel.\n"
                              "Drag to the right to set how far the edge moves in and out.");
        }
    }
    int layer = static_cast<int>(look.layer);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##layer", &layer, "Over lots and networks\0Under lots and networks\0Over shadows\0")) {
        look.layer = static_cast<PaintLayer>(layer);
        committed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Over lots and networks: the paint covers lot and road textures, and shadows fall on it.\n"
                          "Under lots and networks: lot and road textures cover the paint, as if it were the ground.\n"
                          "Over shadows: the paint covers the shadows of the game as well.\n"
                          "Buildings, props and cars stay on top of the paint in every case.");
    }
    // Rarely needed, but a value that is set still shows while this is closed.
    char advanced[48];
    std::snprintf(advanced, sizeof(advanced), look.depthOffset < 0 ? "Advanced###advanced" : "Advanced: depth %d###advanced",
                  look.depthOffset);
    if (ImGui::TreeNode(advanced)) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt("##depth", &look.depthOffset, -1, 8, look.depthOffset < 0 ? "Depth: default" : "Depth %d");
        Track(changed, committed);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Increase this value if the paint flickers or goes under lot textures.\n"
                              "At 4 or more, the paint shows on top of the shadows of the game.");
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void GroundPanelTab::RenderGrid_() {
    ImGui::PushID("grid");
    ImGui::SeparatorText("Grid and snapping");
    const bool wasShown = settings_.showGrid;
    const bool wasSnapping = settings_.snapPointsToGrid;
    bool changed = false;
    changed |= ImGui::Checkbox("Show grid", &settings_.showGrid);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show the grid (G).");
    }
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Snap to grid", &settings_.snapPointsToGrid);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Snap points to the grid (Shift+S).");
    }
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Snap to paint", &settings_.snapToPaint);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Snap points to the paint (M). Points snap to corners, edges and the middle of sides.\n"
                          "Points also snap to lines parallel or square to the last edge.\n"
                          "Hold Shift for 15° steps from the last point.");
    }
    ImGui::TextDisabled("Grid step (m)");
    ImGui::SameLine();
    const bool presetChosen = RenderPresetButtons_("GridStep", settings_.gridStepMeters, {2.0f, 4.0f, 8.0f, 16.0f});
    changed |= presetChosen;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##step", &settings_.gridStepMeters, 1.0f, 16.0f, "Every %.0f m");
    if (presetChosen || ImGui::IsItemDeactivatedAfterEdit() || settings_.showGrid != wasShown ||
        settings_.snapPointsToGrid != wasSnapping) {
        director_->SaveGridChoice(&PaintGridChoices::ground, PaintGridChoice{.step = settings_.gridStepMeters,
                                                                             .show = settings_.showGrid,
                                                                             .snap = settings_.snapPointsToGrid});
    }
    Tooltip("The grid step. Press Ctrl+[ or Ctrl+] to change it.");
    ImGui::PopID();
    if (changed) {
        SettingsChanged_();
    }
}

void GroundPanelTab::RenderShapeSettings_() {
    bool changed = false;
    const Tool tool = settings_.tool;
    switch (tool) {
    case Tool::Marking:
        ImGui::SeparatorText("Marking");
        break;
    case Tool::Erase:
        ImGui::SeparatorText("Eraser");
        break;
    case Tool::Brush:
        ImGui::SeparatorText("Brush");
        break;
    default:
        ImGui::SeparatorText("Shape");
        break;
    }

    if (tool == Tool::Marking) {
        bool committed = false;
        MarkingEditor(settings_.marking, changed, committed);
        changed |= committed;
        if (MarkingFollowsPath(settings_.marking.kind)) {
            bool curve = settings_.drawStyle == DrawStyle::Curve;
            changed |= ImGui::Checkbox("Curve through the clicks", &curve);
            settings_.drawStyle = curve ? DrawStyle::Curve : DrawStyle::Clicks;
        }
    }
    else {
        // The rest on one line: switches first, then the values sharing what is left.
        const bool brush = tool == Tool::Brush || (tool == Tool::Erase && settings_.eraseWithBrush);
        if (tool == Tool::Erase) {
            changed |= ImGui::Checkbox("Brush", &settings_.eraseWithBrush);
            Tooltip("Remove paint with a round brush, not with a shape.");
            ImGui::SameLine();
        }
        if (tool == Tool::Brush) {
            changed |= ImGui::Checkbox("Join", &settings_.joinBrushStrokes);
            Tooltip("When a new stroke touches a stroke with the same look, the two become one.\n"
                    "The list then shows one object for that area.");
            ImGui::SameLine();
        }
        const int count =
            1 + (tool == Tool::Area ? 1 : 0) + (tool == Tool::Line ? (settings_.dashLength > 0.0f ? 4 : 3) : 0);
        const float width = ShareOfLine(count);
        ImGui::SetNextItemWidth(width);
        if (brush) {
            changed |= ImGui::DragFloat("##brush", &settings_.brushRadius, 0.05f, 0.25f, 64.0f, "Brush %.2f m radius");
            Tooltip("The brush size. Press [ or ] to change it.");
        }
        else {
            int style = static_cast<int>(settings_.drawStyle);
            changed |= ImGui::Combo("##draw", &style, "Straight lines\0Curves\0Freehand\0Pen\0");
            settings_.drawStyle = static_cast<DrawStyle>(style);
            Tooltip("Draw straight lines between your clicks, a curve through your clicks, a freehand line, or use the pen.\nWith the pen, a click makes a corner. Hold the button and drag to make a curve.");
        }
        if (tool == Tool::Area) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(width);
            changed |= ImGui::DragFloat("##corners", &settings_.cornerRadius, 0.05f, 0.0f, 50.0f,
                                        settings_.cornerRadius > 0.0f ? "Round corners %.2f m" : "Sharp corners");
            Tooltip("Drag to the right to round the corners.");
        }
        if (tool == Tool::Line) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(width);
            changed |= ImGui::DragFloat("##width", &settings_.lineWidth, 0.01f, 0.02f, 50.0f, "Width %.2f m");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(width);
            int ends = static_cast<int>(settings_.lineEnds);
            changed |= ImGui::Combo("##ends", &ends, "Flat ends\0Round ends\0");
            settings_.lineEnds = static_cast<LineEnds>(ends);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(width);
            changed |= ImGui::DragFloat("##dashes", &settings_.dashLength, 0.05f, 0.0f, 50.0f,
                                        settings_.dashLength > 0.0f ? "Dashes %.2f m" : "Solid");
            Tooltip("Drag to the right to make a dashed line.");
            if (settings_.dashLength > 0.0f) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(width);
                changed |= ImGui::DragFloat("##gaps", &settings_.gapLength, 0.05f, 0.0f, 50.0f, "Gaps %.2f m");
            }
        }
    }

    if (changed) {
        SettingsChanged_();
    }
}

void GroundPanelTab::RenderLookEditor_(Look& look, const bool followsLine, LookEditorState& state, bool& changed,
                                       bool& committed) {
    const bool textured = state.wantsTexture || !look.textures.empty();
    if (ImGui::RadioButton("Color", !textured) && textured) {
        look.textures.clear();
        look.color = state.plainColour;
        state.wantsTexture = false;
        committed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Texture", textured) && !textured) {
        // The colour becomes the tint: start the texture as it is.
        state.plainColour = look.color;
        look.color = {1.0f, 1.0f, 1.0f};
        state.wantsTexture = true;
        committed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Pick texture")) {
        pickForSelection_ = &look == &editing_.look;
        pickResumeTool_ = settings_.tool;
        pickedTexture_.reset();
        ReleaseImGuiInputCapture_();
        pickingTexture_ = director_->StartDecalPicking([this](const uint32_t id) { pickedTexture_ = id; });
    }
    Tooltip("Click a texture in the city to paint with it.\n"
            "You can click a decal, or the base or overlay texture of a lot.\n"
            "If textures overlap, hold Alt and scroll to change between them.\n"
            "Press Esc to cancel.\n"
            "To copy the look of ground that you painted, use Copy look.");

    if (textured && followsLine) {
        ImGui::SameLine();
        ImGui::Checkbox("Follow the line", &look.followPath);
        Track(changed, committed);
        Tooltip("The texture goes along the line and fills its width. Use this for a track or a kerb.");
    }

    RenderPalettes_(look, state, committed);

    if (!textured) {
        for (size_t i = 0; i < std::size(kSwatches); ++i) {
            const Swatch& swatch = kSwatches[i];
            ImGui::PushID(static_cast<int>(i));
            if (i > 0) {
                ImGui::SameLine();
            }
            if (ImGui::ColorButton(swatch.name, ImVec4(swatch.r, swatch.g, swatch.b, 1.0f), 0, ImVec2(22, 22))) {
                look.color = {swatch.r, swatch.g, swatch.b};
                committed = true;
            }
            ImGui::PopID();
        }
        ImGui::SameLine();
        ImGui::ColorEdit3("Other color##paint", look.color.data(), ImGuiColorEditFlags_NoInputs);
        Track(changed, committed);
        return;
    }

    RenderChosenTextures_(look, changed, committed);
    if (!look.textures.empty()) {
        RenderTextureLayout_(look, followsLine && look.followPath, changed, committed);
        ImGui::TextDisabled("Size (m)");
        ImGui::SameLine();
        if (RenderPresetButtons_("TextureSize", look.textureSizeMeters, {8.0f, 16.0f, 32.0f, 64.0f})) {
            changed = true;
            committed = true;
        }
        if (look.textures.size() > 1) {
            const bool patches = look.mix == TextureMix::Patches;
            const float width = ShareOfLine(patches ? 3 : 1);
            ImGui::SetNextItemWidth(width);
            int mix = static_cast<int>(look.mix);
            if (ImGui::Combo("##mix", &mix, "Mix by 16 m square\0Mix in soft patches\0")) {
                look.mix = static_cast<TextureMix>(mix);
                committed = true;
            }
            Tooltip("Choose soft patches for a smooth change from one texture to the next.\n"
                    "With soft patches, the first texture covers the shape.\n"
                    "The other textures show on top in natural patches.\n"
                    "The share of each texture sets its area.");
            if (patches) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(width);
                ImGui::DragFloat("##patchSize", &look.patchSizeMeters, 0.1f, 2.0f, 200.0f, "Patches %.1f m");
                Track(changed, committed);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(width);
                ImGui::DragFloat("##patchBlend", &look.patchBlendMeters, 0.05f, 0.0f, 32.0f,
                                 look.patchBlendMeters > 0.0f ? "Blend %.2f m" : "Sharp patches");
                Track(changed, committed);
                Tooltip("The distance over which the edge of each patch fades into the ground below it.");
            }
        }
    }
    RenderTextureLibrary_(look, committed);
}

void GroundPanelTab::RenderPalettes_(Look& look, LookEditorState& state, bool& committed) {
    if (!favorites_ || !ImGui::CollapsingHeader("Palettes")) {
        return;
    }
    ImGui::PushID("palettes");
    std::vector<PaintPalette>& palettes = favorites_->GetPaintPalettes();
    activePalette_ = palettes.empty() ? 0 : std::min(activePalette_, palettes.size() - 1);

    // Not a name that is already there: deleting palettes leaves gaps in the numbers.
    const auto makePalette = [&] {
        std::string newName;
        for (size_t number = palettes.size() + 1;; ++number) {
            newName = "Palette " + std::to_string(number);
            if (std::ranges::none_of(palettes, [&](const PaintPalette& other) { return other.name == newName; })) {
                break;
            }
        }
        palettes.push_back({.name = std::move(newName)});
        activePalette_ = palettes.size() - 1;
        favorites_->Save();
    };

    // One line: choose a palette and use it. What is done less often is behind "...".
    const float buttonGap = ImGui::GetStyle().ItemSpacing.x;
    const float buttonsWidth = palettes.empty()
        ? PanelUi::ButtonWidth("New") + buttonGap
        : PanelUi::ButtonWidth("Add this look") + PanelUi::ButtonWidth("Mix all") + PanelUi::ButtonWidth("...") + buttonGap * 3.0f;
    ImGui::SetNextItemWidth(-buttonsWidth);
    if (ImGui::BeginCombo("##palette", palettes.empty() ? "No palettes yet" : palettes[activePalette_].name.c_str())) {
        for (size_t i = 0; i < palettes.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(palettes[i].name.c_str(), i == activePalette_)) {
                activePalette_ = i;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (palettes.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("New")) {
            makePalette();
        }
        Tooltip("Make a palette. Palettes are the same in all your cities.");
        ImGui::PopID();
        return;
    }

    PaintPalette& palette = palettes[activePalette_];
    ImGui::SameLine();
    // Texture is chosen but no texture is picked yet: there is nothing to add.
    const bool nothingToAdd = look.textures.empty() && state.wantsTexture;
    ImGui::BeginDisabled(nothingToAdd);
    if (ImGui::SmallButton("Add this look")) {
        if (look.textures.empty() && std::ranges::find(palette.colours, look.color) == palette.colours.end()) {
            palette.colours.push_back(look.color);
        }
        for (const TextureChoice& choice : look.textures) {
            if (std::ranges::none_of(palette.textures, [&](const PaintPaletteTexture& entry) {
                    return entry.instanceId.value() == choice.instance;
                })) {
                palette.textures.push_back({rfl::Hex<uint32_t>(choice.instance), choice.weight});
            }
        }
        favorites_->Save();
    }
    ImGui::EndDisabled();
    Tooltip(nothingToAdd ? "First pick a texture." : "Add the color or the textures that you paint with now to this palette.");

    ImGui::SameLine();
    ImGui::BeginDisabled(palette.textures.size() < 2);
    if (ImGui::SmallButton("Mix all")) {
        if (look.textures.empty()) {
            state.plainColour = look.color;
            look.color = {1.0f, 1.0f, 1.0f};
        }
        look.textures.clear();
        for (const PaintPaletteTexture& entry : palette.textures) {
            if (look.textures.size() < kTerrainPaintMaxTextures) {
                look.textures.push_back(TextureChoice{entry.instanceId.value(), entry.weight});
            }
        }
        chosenTexture_ = 0;
        state.wantsTexture = true;
        committed = true;
    }
    ImGui::EndDisabled();
    Tooltip(palette.textures.size() < 2 ? "This palette needs two or more textures."
                                        : "Paint with all textures of this palette as a mix, up to 8 textures.");

    ImGui::SameLine();
    if (ImGui::SmallButton("...")) {
        ImGui::OpenPopup("paletteMenu");
    }
    Tooltip("Make a palette, or rename or delete this one.");
    bool newRequested = false;
    bool deleteConfirmed = false;
    if (ImGui::BeginPopup("paletteMenu")) {
        newRequested = ImGui::Selectable("New palette");
        ImGui::Separator();
        char name[64];
        std::snprintf(name, sizeof(name), "%s", palette.name.c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        if (ImGui::InputText("##paletteName", name, sizeof(name)) && name[0] != 0) {
            palette.name = name;
            favorites_->Save();
        }
        Tooltip("The name of this palette. Click to change it.");
        if (ImGui::SmallButton("Delete this palette")) {
            ImGui::OpenPopup("deletePalette");
        }
        deleteConfirmed = PanelUi::ConfirmDeletePopup("deletePalette", palette.name.c_str());
        if (deleteConfirmed) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (palette.colours.empty() && palette.textures.empty()) {
        Hint("This palette is empty. Choose a color or a texture, then click Add this look.");
    }

    // At most two rows of tiles, which scroll: a big palette does not push the rest down.
    const ImGuiStyle& tileStyle = ImGui::GetStyle();
    const float tileHeight = kPaletteTileSize + tileStyle.FramePadding.y * 2.0f + tileStyle.ItemSpacing.y;
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, tileHeight * 2.0f));
    const bool hasTiles = !palette.colours.empty() || !palette.textures.empty();
    const bool tilesOpen = hasTiles && ImGui::BeginChild("##paletteTiles", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY);

    // Tiles on one line until they reach the side of the panel.
    float tileRight = 0.0f;
    const auto nextTile = [&](const float width) {
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float limit = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        if (tileRight > 0.0f && tileRight + spacing + width <= limit) {
            ImGui::SameLine();
        }
    };

    std::optional<size_t> removeColour;
    for (size_t i = 0; tilesOpen && i < palette.colours.size(); ++i) {
        const std::array<float, 3> colour = palette.colours[i];
        ImGui::PushID(static_cast<int>(i));
        nextTile(22.0f);
        if (ImGui::ColorButton("##color", ImVec4(colour[0], colour[1], colour[2], 1.0f), ImGuiColorEditFlags_NoTooltip,
                               ImVec2(22, 22))) {
            look.textures.clear();
            look.color = colour;
            state.plainColour = colour;
            state.wantsTexture = false;
            committed = true;
        }
        tileRight = ImGui::GetItemRectMax().x;
        Tooltip("Click to paint with this color. Right-click for more.");
        if (ImGui::BeginPopupContextItem("tileMenu")) {
            if (ImGui::MenuItem("Remove from palette")) {
                removeColour = i;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    std::optional<size_t> removeTexture;
    for (size_t i = 0; tilesOpen && i < palette.textures.size(); ++i) {
        const uint32_t id = palette.textures[i].instanceId.value();
        ImGui::PushID(static_cast<int>(id));
        const bool inUse = std::ranges::find(look.textures, id, &TextureChoice::instance) != look.textures.end();
        nextTile(kPaletteTileSize + ImGui::GetStyle().FramePadding.x * 2.0f);
        if (TextureTile_(id, kPaletteTileSize, inUse)) {
            if (look.textures.empty()) {
                state.plainColour = look.color;
                look.color = {1.0f, 1.0f, 1.0f};
            }
            state.wantsTexture = true;
            UseTexture_(look, id, ImGui::GetIO().KeyCtrl, committed);
        }
        tileRight = ImGui::GetItemRectMax().x;
        Tooltip("Click to paint with this texture. Ctrl+click to add it to the mix or remove it from the mix. "
                "Right-click for more.");
        if (ImGui::BeginPopupContextItem("tileMenu")) {
            if (ImGui::MenuItem("Remove from palette")) {
                removeTexture = i;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    if (hasTiles) {
        ImGui::EndChild();
    }

    if (removeColour) {
        palette.colours.erase(palette.colours.begin() + static_cast<std::ptrdiff_t>(*removeColour));
        favorites_->Save();
    }
    if (removeTexture) {
        palette.textures.erase(palette.textures.begin() + static_cast<std::ptrdiff_t>(*removeTexture));
        favorites_->Save();
    }
    if (deleteConfirmed) {
        palettes.erase(palettes.begin() + static_cast<std::ptrdiff_t>(activePalette_));
        favorites_->Save();
    }
    if (newRequested) {
        makePalette();
    }
    ImGui::PopID();
}

void GroundPanelTab::RenderTextureLayout_(Look& look, const bool following, bool& changed, bool& committed) {
    ImGui::ColorEdit3("##tint", look.color.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    Track(changed, committed);
    Tooltip("The tint. White does not change the texture.");
    ImGui::SameLine();
    const float width = ShareOfLine(following ? 1 : 4);
    ImGui::SetNextItemWidth(width);
    ImGui::DragFloat("##size", &look.textureSizeMeters, 0.1f, 0.5f, 256.0f,
                     following ? "Repeats every %.1f m" : "Size %.1f m");
    Track(changed, committed);
    Tooltip(following ? "The length of one repeat of the texture along the line." : "The size of one copy of the texture.");
    if (following) {
        return;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width);
    ImGui::DragFloat("##turn", &look.textureRotationDegrees, 0.5f, -360.0f, 360.0f, "Turn %.0f°");
    Track(changed, committed);
    constexpr const char* kShiftTip = "Move the texture to align it with a kerb or a lot.\n"
                                      "You can also use the Select tool. Hold Shift and drag the painted ground in the city.";
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width);
    ImGui::DragFloat("##shiftX", &look.textureOffset[0], 0.05f, -256.0f, 256.0f, "Shift x %.2f m");
    Track(changed, committed);
    Tooltip(kShiftTip);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width);
    ImGui::DragFloat("##shiftZ", &look.textureOffset[1], 0.05f, -256.0f, 256.0f, "Shift z %.2f m");
    Track(changed, committed);
    Tooltip(kShiftTip);
}

void GroundPanelTab::RenderChosenTextures_(Look& look, bool& changed, bool& committed) {
    if (look.textures.empty()) {
        ImGui::TextColored(kAttentionColor, "Pick a texture below.");
        return;
    }
    const bool mixed = look.textures.size() > 1;
    const bool patches = mixed && look.mix == TextureMix::Patches;
    chosenTexture_ = std::min(chosenTexture_, look.textures.size() - 1);

    float total = 0.0f;
    for (const TextureChoice& choice : look.textures) {
        total += std::max(0.0f, choice.weight);
    }
    const auto shareOf = [&](const TextureChoice& choice) {
        return total > 0.0f ? 100.0f * std::max(0.0f, choice.weight) / total : 0.0f;
    };
    const auto nameOf = [&](const TextureChoice& choice) {
        const std::string* file = decals_ ? decals_->OriginalFile(choice.instance) : nullptr;
        char id[16];
        std::snprintf(id, sizeof(id), "0x%08X", choice.instance);
        return file ? *file : std::string(id);
    };
    // A small label on the corner of the tile drawn last.
    const auto tileLabel = [](const char* text, const bool bottomRight) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 at = bottomRight ? ImVec2(max.x - size.x - 3.0f, max.y - size.y - 2.0f) : ImVec2(min.x + 3.0f, min.y + 2.0f);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(ImVec2(at.x - 2.0f, at.y - 1.0f), ImVec2(at.x + size.x + 2.0f, at.y + size.y + 1.0f),
                            IM_COL32(0, 0, 0, 190), 2.0f);
        draw->AddText(at, IM_COL32(255, 255, 255, 255), text);
    };

    // A strip of tiles, and the details of one: the same height however many textures are mixed.
    std::optional<size_t> removed;
    const float tileWidth = kThumbSize + ImGui::GetStyle().FramePadding.x * 2.0f;
    for (size_t i = 0; i < look.textures.size(); ++i) {
        const TextureChoice& choice = look.textures[i];
        ImGui::PushID(static_cast<int>(i));
        NextInRow(i, tileWidth);
        if (TextureTile_(choice.instance, kThumbSize, mixed && i == chosenTexture_)) {
            chosenTexture_ = i;
        }
        if (mixed) {
            char share[8];
            std::snprintf(share, sizeof(share), "%.0f%%", shareOf(choice));
            tileLabel(share, true);
        }
        if (patches && i == 0) {
            tileLabel("Base", false);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\n%s", nameOf(choice).c_str(), mixed ? "Click to change its share. Right-click to remove it." : "Right-click to remove it.");
        }
        if (ImGui::BeginPopupContextItem("chosenMenu")) {
            if (ImGui::MenuItem("Remove")) {
                removed = i;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    TextureChoice& chosen = look.textures[chosenTexture_];
    if (mixed) {
        ImGui::TextUnformatted(nameOf(chosen).c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        ImGui::DragFloat("##share", &chosen.weight, 0.05f, 0.0f, 100.0f, "Share %.2f");
        Track(changed, committed);
        ImGui::SameLine();
        ImGui::Text("%.0f%%", shareOf(chosen));
    }
    else {
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(nameOf(chosen).c_str());
    }
    if (mixed) {
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("Remove")) {
        removed = chosenTexture_;
    }
    if (!mixed) {
        ImGui::EndGroup();
    }
    if (patches && chosenTexture_ > 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Make base")) {
            std::rotate(look.textures.begin(), look.textures.begin() + static_cast<std::ptrdiff_t>(chosenTexture_),
                        look.textures.begin() + static_cast<std::ptrdiff_t>(chosenTexture_) + 1);
            chosenTexture_ = 0;
            committed = true;
        }
        Tooltip("The base texture covers the whole shape. The other textures lie on it in patches.");
    }
    if (removed) {
        look.textures.erase(look.textures.begin() + static_cast<std::ptrdiff_t>(*removed));
        committed = true;
    }
}

bool GroundPanelTab::TextureTile_(const uint32_t instanceId, const float size, const bool inUse) {
    bool clicked;
    if (const auto texture = thumbnails_.Get(instanceId); texture && *texture) {
        clicked = ImGui::ImageButton("##t", reinterpret_cast<ImTextureID>(*texture), ImVec2(size, size));
    }
    else {
        thumbnails_.Request(instanceId);
        clicked = ImGui::Button("##t", ImVec2(size + ImGui::GetStyle().FramePadding.x * 2.0f,
                                              size + ImGui::GetStyle().FramePadding.y * 2.0f));
    }
    if (inUse) {
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                            ImGui::GetColorU32(ImGuiCol_CheckMark), 3.0f, 0, 3.0f);
    }
    if (favorites_ && favorites_->IsDecalFavorite(instanceId)) {
        const ImVec2 corner = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddText(ImVec2(corner.x + 3.0f, corner.y + 1.0f), IM_COL32(255, 215, 0, 255), "*");
    }
    return clicked;
}

void GroundPanelTab::UseTexture_(Look& look, const uint32_t instanceId, const bool addToMix, bool& committed) {
    const auto it = std::ranges::find(look.textures, instanceId, &TextureChoice::instance);
    if (!addToMix) {
        look.textures = {TextureChoice{instanceId, 1.0f}};
    }
    else if (it != look.textures.end()) {
        look.textures.erase(it);
    }
    else if (look.textures.size() < kTerrainPaintMaxTextures) {
        look.textures.push_back(TextureChoice{instanceId, 1.0f});
    }
    if (look.variationSeed == 0) {
        look.variationSeed = instanceId;
    }
    std::erase(recentTextures_, instanceId);
    recentTextures_.insert(recentTextures_.begin(), instanceId);
    if (recentTextures_.size() > 8) {
        recentTextures_.resize(8);
    }
    committed = true;
}

void GroundPanelTab::IndexLibrary_() {
    const std::vector<uint32_t>& ids = decals_->GetInstanceIds();
    if (ids.size() == indexedLibrarySize_) {
        return;
    }
    indexedLibrarySize_ = ids.size();
    shownValid_ = false;

    // Every pack that ships a texture counts, from the catalog's rows alone: asking which one the
    // game loaded it from takes a file lookup per texture, which is for tooltips, not for thousands.
    const std::vector<TexturePack>& packs = decals_->Packs();
    const PackCatalogRepository* catalog = director_->GetPackCatalog();
    static const std::string kNone;
    library_.clear();
    library_.reserve(ids.size());
    packFilter_.Begin(catalog);
    textureSuggester_.Clear();
    for (const uint32_t id : ids) {
        LibraryEntry entry{.id = id, .rows = decals_->PackCandidates(id)};
        packFilter_.Count(entry.rows);
        char hex[9];
        std::snprintf(hex, sizeof(hex), "%08X", id);
        entry.searchText = hex;
        for (const PackCatalogRepository::TextureRow& row : entry.rows) {
            const auto pack = static_cast<int32_t>(row[2]);
            const std::string* file = catalog ? catalog->File(row[1]) : nullptr;
            if (entry.packs.empty()) {
                entry.sortPack = &packs[static_cast<size_t>(pack)].name;
                entry.sortFile = file ? file : &kNone;
            }
            if (std::ranges::find(entry.packs, pack) == entry.packs.end()) {
                entry.packs.push_back(pack);
                entry.searchText += ' ' + packs[static_cast<size_t>(pack)].name;
            }
            if (file) {
                entry.searchText += ' ' + *file;
            }
        }
        entry.searchText = TextSearch::Normalize(entry.searchText);
        textureSuggester_.Add(entry.searchText);
        library_.push_back(std::move(entry));
    }

    // A pack's textures together, by original file, then ID; those not in the catalog last.
    std::ranges::sort(library_, {}, [](const LibraryEntry& entry) {
        return std::tuple(entry.packs.empty(), entry.sortPack ? std::string_view(*entry.sortPack) : std::string_view{},
                          entry.sortFile ? std::string_view(*entry.sortFile) : std::string_view{}, entry.id);
    });
}

const std::vector<uint32_t>& GroundPanelTab::ShownTextures_() {
    IndexLibrary_();
    const auto* favourites = favorites_ ? &favorites_->GetFavoriteDecalIds() : nullptr;
    const size_t favouriteCount = favourites ? favourites->size() : 0;
    const std::string filter = textureFilter_;
    const TextSearch::Query query(filter);
    if (shownValid_ && filter == shownFilter_ && favouriteTexturesOnly_ == shownFavouritesOnly_ &&
        favouriteCount == shownFavouriteCount_ && packFilter_.Selected() == shownPack_) {
        return shownTextures_;
    }
    shownValid_ = true;
    shownFilter_ = filter;
    shownFavouritesOnly_ = favouriteTexturesOnly_;
    shownFavouriteCount_ = favouriteCount;
    shownPack_ = packFilter_.Selected();

    shownTextures_.clear();
    for (const LibraryEntry& entry : library_) {
        const bool inPack = packFilter_.Passes(entry.rows);
        if (!inPack ||
            (favouriteTexturesOnly_ && (!favourites || !favourites->contains(entry.id))) ||
            !kindFilter_.Allows(*decals_, entry.id) ||
            !query.Matches(entry.searchText)) {
            continue;
        }
        shownTextures_.push_back(entry.id);
    }
    return shownTextures_;
}

void GroundPanelTab::RenderPackFilter_() {
    IndexLibrary_();
    packFilter_.Render("##pack", "textures");
}

void GroundPanelTab::TextureTooltip_(const uint32_t instanceId) {
    // After a short wait, so that moving over the grid does not load every texture on the way.
    const bool showPreview = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
    const std::string* file = decals_->OriginalFile(instanceId);
    const TexturePack* pack = decals_->PackOf(instanceId);
    ImGui::BeginTooltip();
    if (showPreview) {
        hoverPreview_.Render(pRM_, imguiService_, instanceId);
    }
    if (file && pack) {
        ImGui::TextUnformatted(file->c_str());
        ImGui::TextDisabled("%s, by %s", pack->name.c_str(), pack->author.c_str());
        // Texture mods often replace the same texture: the game shows whichever loads last.
        std::vector<uint32_t> others;
        for (const PackCatalogRepository::TextureRow& row : decals_->PackCandidates(instanceId)) {
            if (&decals_->Packs()[row[2]] != pack && std::ranges::find(others, row[2]) == others.end()) {
                others.push_back(row[2]);
            }
        }
        if (!others.empty()) {
            ImGui::TextDisabled("Also in %zu other pack%s", others.size(), others.size() == 1 ? "" : "s");
        }
    }
    ImGui::TextDisabled("0x%08X", instanceId);
    ImGui::TextUnformatted("Click to paint with this texture.\nCtrl+click to add it to the mix or remove it from the mix.\nRight-click to add or remove it as a favorite.");
    ImGui::EndTooltip();
}

void GroundPanelTab::RenderTextureLibrary_(Look& look, bool& committed) {
    if (!decals_ || decals_->Count() == 0) {
        ImGui::TextDisabled("No ground textures found (type 0x7AB50E44, group 0x0986135E).");
        return;
    }

    const auto inUse = [&](const uint32_t id) {
        return std::ranges::find(look.textures, id, &TextureChoice::instance) != look.textures.end();
    };
    const auto tileClicked = [&](const uint32_t id) {
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && favorites_) {
            favorites_->ToggleDecalFavorite(id);
        }
        if (ImGui::IsItemHovered()) {
            TextureTooltip_(id);
        }
        return io.KeyCtrl;
    };

    if (!recentTextures_.empty()) {
        ImGui::TextDisabled("Recently used");
        for (size_t i = 0; i < recentTextures_.size(); ++i) {
            const uint32_t id = recentTextures_[i];
            ImGui::PushID(static_cast<int>(id));
            NextInRow(i, kThumbSize + ImGui::GetStyle().FramePadding.x * 2.0f);
            const bool clicked = TextureTile_(id, kThumbSize, inUse(id));
            const bool ctrl = tileClicked(id);
            ImGui::PopID();
            if (clicked) {
                UseTexture_(look, id, ctrl, committed);
                break;
            }
        }
    }

    RenderPackFilter_();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Search name, pack or ID", textureFilter_, sizeof(textureFilter_));
    PanelUi::Tooltip(PanelUi::kSearchHelp);
    ImGui::Checkbox("Favorites only", &favouriteTexturesOnly_);
    ImGui::SameLine();
    if (kindFilter_.Render(*decals_)) {
        shownValid_ = false;
    }

    const std::vector<uint32_t>& shown = ShownTextures_();
    if (shown.empty()) {
        if (PanelUi::NoResults("No textures match this search and these filters.")) {
            textureFilter_[0] = 0;
            packFilter_.Clear();
            favouriteTexturesOnly_ = false;
            kindFilter_ = {};
            shownValid_ = false;
        }
        else if (const auto suggestion = textureSuggester_.Suggest(textureFilter_); suggestion && PanelUi::DidYouMean(*suggestion)) {
            std::snprintf(textureFilter_, sizeof(textureFilter_), "%s", suggestion->c_str());
        }
        return;
    }
    Hint((std::to_string(shown.size()) + " textures. Click a texture to paint with it. Ctrl+click to mix textures. "
                                         "Right-click to set a favorite.").c_str());

    // About five rows. Drag its bottom edge for more or fewer.
    const ImGuiStyle& gridStyle = ImGui::GetStyle();
    const float gridHeight = 5.0f * (kPickerThumbSize + gridStyle.FramePadding.y * 2.0f + gridStyle.ItemSpacing.y) +
                             gridStyle.WindowPadding.y * 2.0f;
    if (ImGui::BeginChild("##textures", ImVec2(0, gridHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY)) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float tile = kPickerThumbSize + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
        const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / tile));
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>((shown.size() + columns - 1) / columns),
                      kPickerThumbSize + style.FramePadding.y * 2.0f + style.ItemSpacing.y);
        std::optional<std::pair<uint32_t, bool>> picked;
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                for (int col = 0; col < columns; ++col) {
                    const size_t index = static_cast<size_t>(row * columns + col);
                    if (index >= shown.size()) {
                        break;
                    }
                    if (col > 0) {
                        ImGui::SameLine();
                    }
                    const uint32_t id = shown[index];
                    ImGui::PushID(static_cast<int>(id));
                    const bool clicked = TextureTile_(id, kPickerThumbSize, inUse(id));
                    const bool ctrl = tileClicked(id);
                    if (clicked) {
                        picked = std::pair{id, ctrl};
                    }
                    ImGui::PopID();
                }
            }
        }
        if (picked) {
            UseTexture_(look, picked->first, picked->second, committed);
        }
    }
    ImGui::EndChild();
}

void GroundPanelTab::RenderSelection_(Document& document) {
    if (document.Selection().empty()) {
        editingId_ = 0;
        return;
    }

    const uint32_t first = document.Selection().front();
    RenderSelectionActions_(document);
    if (document.Selection().empty() || document.Selection().front() != first) {
        editingId_ = 0;
        return;
    }
    RenderTransform_(document);
    RenderInspector_(document, first);

    const std::vector<uint32_t> selection = document.Selection();
    if (!selection.empty()) {
        RenderShapeTools_(document, selection);
        RenderPlanting_(selection.front());
    }
}

void GroundPanelTab::RenderInspector_(Document& document, const uint32_t first) {
    const PaintObject* const object = document.Find(first);
    if (!object || document.Selection().size() != 1) {
        editingId_ = 0;
        return;
    }
    // Takes in what changed elsewhere, such as an undo or a point dragged in the city, so that the
    // next edit here does not put the old values back. Not while a control here is in use.
    const bool newObject = editingId_ != first;
    if (newObject || (!ImGui::IsAnyItemActive() && document.Revision() != editingRevision_)) {
        editingId_ = first;
        editing_ = *object;
        editingRevision_ = document.Revision();
    }
    if (newObject) {
        selectionLook_ = {};
        // Painted with a texture: its colour is the tint; the plain colour starts white.
        selectionLook_.wantsTexture = !object->look.textures.empty();
    }

    bool changed = false;
    bool committed = false;
    char name[64];
    std::snprintf(name, sizeof(name), "%s", editing_.name.c_str());
    if (ImGui::InputTextWithHint("Name", ObjectLabel(*object).c_str(), name, sizeof(name))) {
        editing_.name = name;
    }
    Track(changed, committed);

    ShapeSpec& shape = editing_.shape;
    if (shape.kind != ShapeKind::Merged) {
        ImGui::SeparatorText(shape.kind == ShapeKind::Marking ? "Marking" : "Shape");
    }
    // What only the point that was clicked last can have: a curve, or its own corner radius.
    const auto renderPointRadius = [&] {
        if (shape.pathStyle != PathStyle::Straight) {
            return;
        }
        const GroundPainterInputControl* const painter = director_->GetGroundPainter();
        const std::optional<size_t> picked = painter ? painter->PickedPoint() : std::nullopt;
        if (!picked || *picked >= shape.points.size()) {
            ImGui::TextDisabled("Click a point in the city to change only that point.");
            return;
        }
        const Vec2 handle = *picked < shape.handles.size() ? shape.handles[*picked] : Vec2{};
        const Vec2 inHandle = InHandleOf(shape, *picked);
        const bool curve = handle != Vec2{} || inHandle != Vec2{};
        if (ImGui::SmallButton(curve ? "Make corner" : "Make curve")) {
            const Vec2 smooth = curve ? Vec2{} : SmoothHandle(shape.points, *picked, shape.kind == ShapeKind::Area);
            SetPointHandles(shape, *picked, smooth, {-smooth.x, -smooth.z});
            committed = true;
        }
        Tooltip(curve ? "Change the point that you clicked last to a sharp corner."
                      : "Change the point that you clicked last to a smooth curve. Then drag its handles in the city.");
        if (curve && !(inHandle == Vec2{-handle.x, -handle.z})) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Join handles")) {
                const Vec2 outgoing = handle != Vec2{} ? handle : Vec2{-inHandle.x, -inHandle.z};
                SetPointHandles(shape, *picked, outgoing, {-outgoing.x, -outgoing.z});
                committed = true;
            }
            Tooltip("Make the two handles of this point a straight line again, so that the curve is smooth.");
        }
        if (curve) {
            return;
        }
        const bool own = *picked < shape.pointRadii.size() && shape.pointRadii[*picked] >= 0.0f;
        float radius = own ? shape.pointRadii[*picked] : shape.cornerRadius;
        if (ImGui::DragFloat("This corner", &radius, 0.05f, 0.0f, 50.0f, radius > 0.0f ? "%.2f m" : "sharp")) {
            shape.pointRadii.resize(shape.points.size(), -1.0f);
            shape.pointRadii[*picked] = radius;
        }
        Track(changed, committed);
        Tooltip("Round only the corner that you clicked last in the city.");
    };
    switch (shape.kind) {
    case ShapeKind::Area:
        ImGui::DragFloat("Round corners", &shape.cornerRadius, 0.05f, 0.0f, 50.0f, shape.cornerRadius > 0.0f ? "%.2f m" : "no");
        Track(changed, committed);
        renderPointRadius();
        break;
    case ShapeKind::Line: {
        const float width = ShareOfLine(shape.dashLength > 0.0f ? 3 : 2);
        ImGui::SetNextItemWidth(width);
        ImGui::DragFloat("##width", &shape.width, 0.01f, 0.02f, 50.0f, "Width %.2f m");
        Track(changed, committed);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(width);
        ImGui::DragFloat("##dashes", &shape.dashLength, 0.05f, 0.0f, 50.0f, shape.dashLength > 0.0f ? "Dashes %.2f m" : "Solid");
        Track(changed, committed);
        if (shape.dashLength > 0.0f) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(width);
            ImGui::DragFloat("##gaps", &shape.gapLength, 0.05f, 0.0f, 50.0f, "Gaps %.2f m");
            Track(changed, committed);
        }
        renderPointRadius();
        break;
    }
    case ShapeKind::Brush:
        ImGui::DragFloat(shape.outline.empty() ? "Brush size" : "Brush size (last stroke)", &shape.brushRadius, 0.05f,
                         0.25f, 64.0f, "%.2f m radius");
        Track(changed, committed);
        break;
    case ShapeKind::Marking:
        ImGui::PushID("marking");
        MarkingEditor(shape.marking, changed, committed);
        ImGui::PopID();
        break;
    case ShapeKind::Merged:
        break;
    }
    ImGui::SeparatorText("Paint with");
    ImGui::PushID("look");
    RenderLookEditor_(editing_.look, shape.kind == ShapeKind::Line, selectionLook_, changed, committed);
    ImGui::PopID();
    RenderFinish_(editing_.look, shape.wear, shape.kind == ShapeKind::Marking ? nullptr : &shape.roughness, changed,
                  committed);

    if (committed) {
        document.Update({editing_});
        editingRevision_ = document.Revision();
    }
    else if (changed) {
        // Shown while the control is held: a new style at once, a new cut every so often.
        const auto now = std::chrono::steady_clock::now();
        if (CheapChange(editing_, *object) || now - lastLivePreview_ >= std::chrono::milliseconds(60)) {
            lastLivePreview_ = now;
            document.ShowChanged(editing_);
        }
    }
}

void GroundPanelTab::RenderSelectionActions_(Document& document) {
    const std::vector<uint32_t> selection = document.Selection();
    const uint32_t first = selection.front();
    ImGui::Text("%zu selected", selection.size());

    // The order works on one shape: it would otherwise move only the first of several.
    const bool one = selection.size() == 1;
    const auto orderButton = [&](const char* label, const char* tip, void (Document::*move)(uint32_t)) {
        ImGui::SameLine();
        ImGui::BeginDisabled(!one);
        if (ImGui::SmallButton(label)) {
            (document.*move)(first);
        }
        ImGui::EndDisabled();
        Tooltip(one ? tip : "Select one shape to change its order.");
    };
    orderButton("Up", "Draw it over the shape above it.", &Document::MoveUp);
    orderButton("Down", "Draw it under the shape below it.", &Document::MoveDown);
    orderButton("Top", "Draw it over all other shapes.", &Document::MoveToTop);
    orderButton("Bottom", "Draw it under all other shapes.", &Document::MoveToBottom);

    if (ImGui::SmallButton("Copy")) {
        document.Duplicate(selection, {4.0f, 4.0f});
    }
    Tooltip("Add a copy next to it.");
    ImGui::SameLine();
    const bool grouped = std::ranges::any_of(selection, [&](const uint32_t id) {
        const PaintObject* const object = document.Find(id);
        return object && object->group != 0;
    });
    if (grouped) {
        if (ImGui::SmallButton("Ungroup")) document.Ungroup(selection);
    }
    else {
        ImGui::BeginDisabled(selection.size() < 2);
        if (ImGui::SmallButton("Group")) document.Group(selection);
        ImGui::EndDisabled();
        Tooltip(selection.size() < 2 ? "Select two or more shapes. Hold Ctrl and click to add a shape."
                                     : "Select these shapes as one from now on.");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selection.size() < 2);
    if (ImGui::SmallButton("Merge")) document.Merge(selection);
    ImGui::EndDisabled();
    Tooltip(selection.size() < 2 ? "Select two or more shapes. Hold Ctrl and click to add a shape."
                                 : "Join the selected shapes into one.\nThe new shape gets the look of the top shape.\nYou cannot edit their points after this.");
    ImGui::SameLine(0.0f, kGroupGap);
    if (ImGui::SmallButton("Delete")) {
        document.Remove(selection);
    }
    Tooltip("Remove it from the city. Press Ctrl+Z to bring it back.");
}

void GroundPanelTab::RenderPlanting_(const uint32_t objectId) {
    if (!ImGui::CollapsingHeader("Props and flora on it")) {
        return;
    }
    ImGui::PushID("planting");
    const RecentPaintHistory& recent = director_->GetRecentPaintHistory();
    if (recent.Empty()) {
        ImGui::TextWrapped("First, start to paint a prop, a prop family or flora in its own tab. "
                           "Then come back here. You can also press F over painted ground in Line or Polygon mode.");
        ImGui::PopID();
        return;
    }
    const RecentPaintEntry& entry = recent.Entries().front();
    const std::string name = entry.name.empty() ? std::string(entry.kind == RecentPaintEntry::Kind::Prop ? "props" : "flora")
                                                : entry.name;
    ImGui::TextWrapped("With %s, the last you painted:", name.c_str());
    if (ImGui::SmallButton("Fill it")) {
        director_->PlantOnGround(objectId, false);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The painter opens in Polygon mode and uses this shape as the sketch.\n"
                          "Holes stay empty. Press - or + to change the density. Press Enter to place the items.");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Plant along it")) {
        director_->PlantOnGround(objectId, true);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The painter opens in Line mode along this line, or around the edge of this shape.\n"
                          "Press - or + to change the spacing. Press Enter to place the items.");
    }
    Hint("In Line or Polygon mode of the prop and flora painters, press F over painted ground to do the same.");
    ImGui::PopID();
}

void GroundPanelTab::RenderShapeTools_(Document& document, const std::vector<uint32_t>& selection) {
    const Bounds bounds = document.SelectionBounds();
    const Vec2 size{bounds.max.x - bounds.min.x, bounds.max.z - bounds.min.z};
    const Vec2 middle{(bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.z + bounds.max.z) * 0.5f};
    if (selection.front() != toolsFor_) {
        // A new selection: repeats start just clear of it.
        toolsFor_ = selection.front();
        repeatStep_ = {std::ceil(size.x + 1.0f), std::ceil(size.z + 1.0f)};
    }
    if (!ImGui::CollapsingHeader("Border, repeat, mirror")) {
        return;
    }
    ImGui::PushID("shapeTools");

    const float fieldWidth = ShareOfLine(3);
    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::DragFloat("##borderWidth", &borderWidth_, 0.02f, 0.05f, 50.0f, "%.2f m wide");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fieldWidth);
    int side = static_cast<int>(borderSide_);
    if (ImGui::Combo("##borderSide", &side, "outside\0inside\0on the edge\0")) {
        borderSide_ = static_cast<BorderSide>(side);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Add border")) {
        document.AddBorder(selection, borderWidth_, borderSide_);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add a new shape along the edge, for example a kerb, a gravel edge or a beach.\n"
                          "It starts with the same look. You can then give it a different look.");
    }

    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::DragInt("##columns", &repeatColumns_, 0.05f, 1, 50, "%d across");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::DragInt("##rows", &repeatRows_, 0.05f, 1, 50, "%d down");
    ImGui::SameLine();
    ImGui::BeginDisabled(repeatColumns_ * repeatRows_ < 2);
    if (ImGui::SmallButton("Repeat")) {
        document.Repeat(selection, repeatColumns_, repeatRows_, {repeatStep_.x, 0.0f}, {0.0f, repeatStep_.z});
    }
    ImGui::EndDisabled();
    Tooltip(repeatColumns_ * repeatRows_ < 2 ? "Set more than 1 across or down." : "Add the copies.");
    ImGui::SetNextItemWidth(fieldWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    ImGui::DragFloat2("Step##repeat", &repeatStep_.x, 0.05f, -500.0f, 500.0f, "%.2f m");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The distance from each copy to the last copy, across (x) and down (z).\n"
                          "You can make %zu copies at most.", Document::kMaxRepeats);
    }

    constexpr float kQuarter = std::numbers::pi_v<float> * 0.5f;
    // A copy is mirrored in the right or the bottom edge, so it lies alongside, touching.
    if (ImGui::SmallButton("Flip left-right")) {
        document.Mirror(selection, mirrorCopy_ ? Vec2{bounds.max.x, middle.z} : middle, kQuarter, mirrorCopy_);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Flip top-bottom")) {
        document.Mirror(selection, mirrorCopy_ ? Vec2{middle.x, bounds.max.z} : middle, 0.0f, mirrorCopy_);
    }
    ImGui::SameLine();
    ImGui::Checkbox("as a copy", &mirrorCopy_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Keep the original and add a mirrored copy next to it.\n"
                          "Use this for symmetric layouts.");
    }
    ImGui::PopID();
}

void GroundPanelTab::RenderTransform_(Document& document) {
    // The two controls show the change while dragged and make it when let go.
    bool active = false;
    bool done = false;
    const float halfWidth = ShareOfLine(2);
    ImGui::SetNextItemWidth(halfWidth);
    ImGui::DragFloat("##turn", &turnDegrees_, 0.5f, -180.0f, 180.0f, "Turn %.1f deg");
    const bool turnStarted = ImGui::IsItemActivated();
    active |= ImGui::IsItemActive();
    done |= ImGui::IsItemDeactivated();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(halfWidth);
    ImGui::DragFloat("##scale", &scalePercent_, 0.25f, 10.0f, 1000.0f, "Scale %.0f %%");
    const bool scaleStarted = ImGui::IsItemActivated();
    active |= ImGui::IsItemActive();
    done |= ImGui::IsItemDeactivated();

    if (turnStarted || scaleStarted) {
        const Bounds bounds = document.SelectionBounds();
        transformPivot_ = {(bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.z + bounds.max.z) * 0.5f};
    }
    const Placement placement{
        .pivot = transformPivot_,
        .rotationRadians = turnDegrees_ * std::numbers::pi_v<float> / 180.0f,
        .scale = std::max(0.1f, scalePercent_ / 100.0f),
    };
    if (active) {
        document.ShowSelectionTransformed(placement);
    }
    else if (done) {
        if (turnDegrees_ != 0.0f || scalePercent_ != 100.0f) {
            const std::vector<uint32_t> selection = document.Selection();
            document.Transform(selection, placement);
        }
        // Also puts back locked ones, which were shown turned but are not.
        document.ShowSelectionMoved({});
        turnDegrees_ = 0.0f;
        scalePercent_ = 100.0f;
    }
}

void GroundPanelTab::RenderLayers_(Document& document) {
    const auto& objects = document.Objects();
    char header[64];
    std::snprintf(header, sizeof(header), "Painted in this city (%zu)", objects.size());
    ImGui::SeparatorText(header);

    ImGui::BeginDisabled(!document.CanUndo());
    if (ImGui::SmallButton("Undo")) {
        document.Undo();
    }
    ImGui::EndDisabled();
    Tooltip("Press Ctrl+Z, or the back mouse button.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!document.CanRedo());
    if (ImGui::SmallButton("Redo")) {
        document.Redo();
    }
    ImGui::EndDisabled();
    Tooltip("Press Ctrl+Y, or the forward mouse button.");
    if (objects.empty()) {
        ImGui::TextDisabled("Nothing painted yet.");
        return;
    }

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH;
    // As tall as its rows need, up to the rest of the panel; never fewer than about five rows, so
    // a short panel scrolls instead of squeezing the list.
    const float row = ImGui::GetTextLineHeightWithSpacing();
    const float needed = row * (static_cast<float>(objects.size()) + 1.5f);
    const float height = std::min(needed, std::max(ImGui::GetContentRegionAvail().y, row * 6.5f));
    if (!ImGui::BeginTable("##layers", 4, kFlags, ImVec2(0, height))) {
        return;
    }
    ImGui::TableSetupColumn("Shown", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Locked", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("##look", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Name (top first)", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    // Top first, as they lie in the city. Edits are collected and applied after the loop, which
    // reorders the list.
    std::optional<PaintObject> changed;
    std::optional<std::pair<uint32_t, bool>> clicked;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(objects.size()));
    while (clipper.Step()) {
        for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex) {
            const PaintObject& object = objects[objects.size() - 1 - static_cast<size_t>(rowIndex)];
            ImGui::PushID(static_cast<int>(object.id));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool shown = !object.hidden;
            if (ImGui::Checkbox("##shown", &shown)) {
                changed = object;
                changed->hidden = !shown;
            }
            ImGui::TableNextColumn();
            bool locked = object.locked;
            if (ImGui::Checkbox("##locked", &locked)) {
                changed = object;
                changed->locked = locked;
            }
            ImGui::TableNextColumn();
            // What it is painted with: its texture, or its colour. A click selects, as on the name.
            const float swatch = ImGui::GetTextLineHeight();
            bool swatchClicked;
            if (!object.look.textures.empty()) {
                swatchClicked = TextureTile_(object.look.textures.front().instance, swatch, false);
            }
            else {
                const auto& c = object.look.color;
                swatchClicked = ImGui::ColorButton("##color", ImVec4(c[0], c[1], c[2], 1.0f), ImGuiColorEditFlags_NoTooltip,
                                                   ImVec2(swatch, swatch));
            }
            ImGui::TableNextColumn();
            const bool selected = ImGui::Selectable(ObjectLabel(object).c_str(), document.IsSelected(object.id),
                                                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
            if (selected || swatchClicked) {
                clicked = std::pair{object.id, ImGui::GetIO().KeyCtrl};
            }
            // What kind of shape, and its group, quietly after the name.
            std::string detail = object.name.empty() ? "" : ShapeKindName(object.shape.kind);
            if (object.shape.kind == ShapeKind::Marking && !object.name.empty()) {
                detail = MarkingKindName(object.shape.marking.kind);
            }
            if (object.group != 0) {
                detail += (detail.empty() ? "" : ", ") + std::string("group ") + std::to_string(object.group);
            }
            if (object.shape.wear > 0.0f) {
                detail += detail.empty() ? "worn" : ", worn";
            }
            if (UsesPatches(object.look)) {
                detail += detail.empty() ? "patches" : ", patches";
            }
            if (!detail.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", detail.c_str());
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();

    if (changed) {
        document.Update({*changed});
    }
    if (clicked) {
        document.Select(clicked->first, clicked->second);
        if (settings_.tool != Tool::Select || !director_->IsGroundPainting()) {
            SelectTool_(Tool::Select);
        }
    }
}
