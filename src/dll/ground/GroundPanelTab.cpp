#include "GroundPanelTab.hpp"

#include <algorithm>
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

namespace {
    constexpr float kThumbSize = 40.0f;
    constexpr float kPickerThumbSize = 56.0f;

    struct ToolInfo {
        Tool tool;
        const char* label;
        const char* tip;
    };

    // Tools that paint, then tools that change what is painted.
    constexpr ToolInfo kPaintTools[] = {
        {Tool::Area, "Area", "Paint an area of any shape."},
        {Tool::Line, "Line", "Paint a line: a path, a kerb, a road marking line."},
        {Tool::Brush, "Brush", "Paint freely with a round brush."},
        {Tool::Marking, "Marking", "Road markings: parking, zebra crossings, arrows, double lines, hatching, text."},
    };
    constexpr ToolInfo kEditTools[] = {
        {Tool::Select, "Select", "Pick painted ground to move, turn, reshape, restyle, copy or delete it."},
        {Tool::Erase, "Erase", "Rub out painted ground."},
    };

    // Colours that road markings and ground paint often need, one click away.
    struct Swatch {
        const char* name;
        float r, g, b;
    };
    constexpr Swatch kSwatches[] = {
        {"Marking white", 0.95f, 0.95f, 0.92f}, {"Marking yellow", 0.98f, 0.80f, 0.12f},
        {"Bus lane red", 0.70f, 0.18f, 0.15f},  {"Cycle lane green", 0.25f, 0.55f, 0.30f},
        {"Disabled bay blue", 0.15f, 0.40f, 0.75f}, {"Asphalt grey", 0.32f, 0.32f, 0.33f},
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

    // Keeps a row of fixed-size buttons inside the panel: the next one goes on this line if it fits.
    void NextInRow(const size_t index, const float width) {
        if (index == 0) {
            return;
        }
        const float right = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width;
        if (right < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) {
            ImGui::SameLine();
        }
    }

    // Shows the active button pressed.
    bool ToggleButton(const char* label, const bool active) {
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        const bool clicked = ImGui::Button(label);
        if (active) {
            ImGui::PopStyleColor();
        }
        return clicked;
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

    std::string Upper(std::string text) {
        std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return text;
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
    settings_.showGrid = director->GetDefaultShowGridOverlay();
    settings_.snapPointsToGrid = director->GetDefaultSnapPointsToGrid();
    settings_.gridStepMeters = director->GetDefaultGridStepMeters();
}

void GroundPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    thumbnails_.OnDeviceReset();
    if (stampTab_) {
        stampTab_->OnDeviceReset(deviceGeneration);
    }
}

void GroundPanelTab::OnShutdown() {
    thumbnails_.Clear();
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

void GroundPanelTab::OnRender() {
    const GroundPainterInputControl* painter = director_->GetGroundPainter();
    if (painter && director_->IsGroundPainting() && !settingsDirty_) {
        // Keys and the middle mouse button in the city may have changed the grid and the brush.
        const ToolSettings& current = painter->Settings();
        settings_.showGrid = current.showGrid;
        settings_.snapPointsToGrid = current.snapPointsToGrid;
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
    ImGui::PushID("tools");
    RenderToolRow_();
    ImGui::PopID();
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
        if (settings_.tool != Tool::Erase) {
            ImGui::SeparatorText("Paint with");
            if (GroundPainterInputControl* armable = director_->GetGroundPainter()) {
                if (ImGui::SmallButton(armable->LookPickArmed() ? "Click painted ground..." : "Copy look")) {
                    armable->ArmLookPick();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Paint like something already painted: click this, then click it in the city.\n"
                                      "Alt+click in the city does the same.");
                }
            }
            ImGui::PushID("look");
            RenderLookEditor_(settings_.look, settings_.tool == Tool::Line, toolLook_, changed, committed);
            ImGui::PopID();
        }
        ImGui::PushID("shape");
        RenderShapeSettings_();
        ImGui::PopID();
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
    const auto toolButtons = [&](const auto& tools) {
        for (const ToolInfo& info : tools) {
            const bool active = painting && !stamping_ && settings_.tool == info.tool;
            if (ToggleButton(info.label, active)) {
                // Clicking the tool in use puts it away.
                if (active) {
                    director_->StopGroundPainting();
                }
                else {
                    SelectTool_(info.tool);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", info.tip);
            }
            ImGui::SameLine();
        }
    };

    toolButtons(kPaintTools);
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    toolButtons(kEditTools);
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ToggleButton("Stamp decals", stamping_)) {
        stamping_ = !stamping_;
        if (stamping_) {
            director_->StopGroundPainting();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Place single texture decals, pick them and strip them.");
    }
}

void GroundPanelTab::RenderStatus_() {
    const GroundPainterInputControl* painter = director_->GetGroundPainter();
    const bool painting = painter && director_->IsGroundPainting();
    const std::string text = painting ? painter->StatusText()
                                      : "Pick a tool above. Area, Line, Brush and Marking paint the ground; Select "
                                        "changes what was painted; Erase rubs it out.";

    const ImGuiStyle& style = ImGui::GetStyle();
    const float wrap = ImGui::GetContentRegionAvail().x - style.WindowPadding.x * 2.0f;
    const float height = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap).y +
                         ImGui::GetTextLineHeightWithSpacing() * (painting ? 1.0f : 0.0f) + style.WindowPadding.y * 2.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    if (ImGui::BeginChild("##status", ImVec2(0, height), true, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::TextWrapped("%s", text.c_str());
        if (painting) {
            ImGui::TextDisabled("Esc cancels the shape being drawn, then stops. Right-drag pans the camera.");
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
        ImGui::SetTooltip("How far in from its edge the paint fades out. Drag to the right for a soft edge.");
    }
    ImGui::SameLine();
    float worn = wear * 100.0f;
    ImGui::SetNextItemWidth(width);
    if (ImGui::SliderFloat("##worn", &worn, 0.0f, 90.0f, worn > 0.0f ? "Worn %.0f%%" : "Not worn")) {
        wear = worn / 100.0f;
    }
    Track(changed, committed);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Patches of paint missing, as on an old road.");
    }

    // The second row: a rough edge (not for markings, whose lines should stay crisp) and depth.
    const float halfWidth = (ImGui::GetContentRegionAvail().x - spacing) / 2.0f;
    if (roughness) {
        ImGui::SetNextItemWidth(halfWidth);
        ImGui::DragFloat("##rough", roughness, 0.02f, 0.0f, 16.0f, *roughness > 0.0f ? "Rough edge %.2f m" : "Smooth edge");
        Track(changed, committed);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("A natural, uneven outline that wanders this far in and out: grass, dirt, gravel.\n"
                              "Drag to the right.");
        }
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(roughness ? halfWidth : -1.0f);
    ImGui::SliderInt("##depth", &look.depthOffset, -1, 8, look.depthOffset < 0 ? "Depth: default" : "Depth %d");
    Track(changed, committed);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Raise it when the paint flickers or hides under lot textures.\n"
                          "4 and up draws over the game's shadows.");
    }
    ImGui::PopID();
}

void GroundPanelTab::RenderGrid_() {
    ImGui::PushID("grid");
    bool changed = false;
    changed |= ImGui::Checkbox("Grid", &settings_.showGrid);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show the grid (G).");
    }
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Snap", &settings_.snapPointsToGrid);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Snap points to the grid (Shift+S).");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##step", &settings_.gridStepMeters, 1.0f, 16.0f, "Every %.0f m");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Grid step (Ctrl+[ and Ctrl+]).");
    }
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

    if (tool == Tool::Area || tool == Tool::Line || (tool == Tool::Erase && !settings_.eraseWithBrush)) {
        int style = static_cast<int>(settings_.drawStyle);
        changed |= ImGui::Combo("Draw", &style, "Straight lines between clicks\0Curve through clicks\0Freehand (drag)\0");
        settings_.drawStyle = static_cast<DrawStyle>(style);
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
    if (tool == Tool::Area) {
        changed |= ImGui::DragFloat("Round corners", &settings_.cornerRadius, 0.05f, 0.0f, 50.0f,
                                    settings_.cornerRadius > 0.0f ? "%.2f m" : "no");
    }
    if (tool == Tool::Line) {
        changed |= ImGui::DragFloat("Width", &settings_.lineWidth, 0.01f, 0.02f, 50.0f, "%.2f m");
        int ends = static_cast<int>(settings_.lineEnds);
        changed |= ImGui::Combo("Ends", &ends, "Flat\0Round\0");
        settings_.lineEnds = static_cast<LineEnds>(ends);
        changed |= ImGui::DragFloat("Dashes", &settings_.dashLength, 0.05f, 0.0f, 50.0f,
                                    settings_.dashLength > 0.0f ? "%.2f m long" : "none (solid)");
        if (settings_.dashLength > 0.0f) {
            changed |= ImGui::DragFloat("Gaps", &settings_.gapLength, 0.05f, 0.0f, 50.0f, "%.2f m");
        }
    }
    if (tool == Tool::Erase) {
        changed |= ImGui::Checkbox("Erase with a brush", &settings_.eraseWithBrush);
    }
    if (tool == Tool::Brush || (tool == Tool::Erase && settings_.eraseWithBrush)) {
        changed |= ImGui::DragFloat("Brush size ([ ])", &settings_.brushRadius, 0.05f, 0.25f, 64.0f, "%.2f m radius");
    }
    if (tool == Tool::Brush) {
        changed |= ImGui::Checkbox("Join overlapping strokes", &settings_.joinBrushStrokes);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("A stroke over one painted the same way becomes part of it,\n"
                              "so a patch of ground stays one object in the list.");
        }
    }

    if (changed) {
        SettingsChanged_();
    }
}

void GroundPanelTab::RenderLookEditor_(Look& look, const bool followsLine, LookEditorState& state, bool& changed,
                                       bool& committed) {
    const bool textured = state.wantsTexture || !look.textures.empty();
    if (ImGui::RadioButton("Colour", !textured) && textured) {
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
        ImGui::ColorEdit3("Other colour##paint", look.color.data(), ImGuiColorEditFlags_NoInputs);
        Track(changed, committed);
    }
    else {
        RenderChosenTextures_(look, changed, committed);
        RenderTextureLibrary_(look, committed);
        if (!look.textures.empty()) {
            ImGui::ColorEdit3("Tint", look.color.data(), ImGuiColorEditFlags_NoInputs);
            Track(changed, committed);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("White leaves the texture as it is.");
            }
            if (followsLine) {
                ImGui::Checkbox("Follow the line", &look.followPath);
                Track(changed, committed);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("The texture runs along the line and spans its width, like a track or a kerb.");
                }
            }
            const bool following = followsLine && look.followPath;
            ImGui::DragFloat(following ? "Repeats every" : "Texture size", &look.textureSizeMeters, 0.1f, 0.5f, 256.0f,
                             "%.1f m");
            Track(changed, committed);
            if (!following) {
                ImGui::DragFloat("Texture turn", &look.textureRotationDegrees, 0.5f, -360.0f, 360.0f, "%.0f°");
                Track(changed, committed);
                ImGui::DragFloat2("Texture shift", look.textureOffset.data(), 0.05f, -256.0f, 256.0f, "%.2f m");
                Track(changed, committed);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Moves the texture, to line it up with a kerb or a lot.\n"
                                      "With Select, Shift+drag the painted ground in the city to move its texture.");
                }
            }
            if (look.textures.size() > 1) {
                int mix = static_cast<int>(look.mix);
                if (ImGui::Combo("Mix", &mix, "By 16 m square\0In soft patches\0")) {
                    look.mix = static_cast<TextureMix>(mix);
                    committed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("In soft patches: the first texture covers the shape and the others lie on it\n"
                                      "in natural, blended patches, each by its share.");
                }
                if (look.mix == TextureMix::Patches) {
                    ImGui::DragFloat("Patch size", &look.patchSizeMeters, 0.1f, 2.0f, 200.0f, "%.1f m");
                    Track(changed, committed);
                    ImGui::DragFloat("Patch blend", &look.patchBlendMeters, 0.05f, 0.0f, 32.0f,
                                     look.patchBlendMeters > 0.0f ? "%.2f m" : "sharp");
                    Track(changed, committed);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How far the edge of each patch fades into what lies under it.");
                    }
                }
            }
        }
    }
}

void GroundPanelTab::RenderChosenTextures_(Look& look, bool& changed, bool& committed) {
    if (look.textures.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Pick a texture below.");
        return;
    }
    if (look.textures.size() > 1) {
        ImGui::TextWrapped(look.mix == TextureMix::Patches
                               ? "A mix: the first covers the shape, the others lie on it in patches, by their share."
                               : "A mix: each 16 m square of ground takes one of these, by its share.");
    }

    float total = 0.0f;
    for (const TextureChoice& choice : look.textures) {
        total += std::max(0.0f, choice.weight);
    }
    for (size_t i = 0; i < look.textures.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        TextureChoice& choice = look.textures[i];
        TextureTile_(choice.instance, kThumbSize, false);
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (const std::string* file = decals_ ? decals_->OriginalFile(choice.instance) : nullptr) {
            ImGui::TextUnformatted(file->c_str());
            ImGui::TextDisabled("%s  0x%08X", decals_->PackOf(choice.instance)->name.c_str(), choice.instance);
        }
        else {
            ImGui::Text("0x%08X", choice.instance);
        }
        if (look.textures.size() > 1) {
            ImGui::SetNextItemWidth(90.0f);
            ImGui::DragFloat("##share", &choice.weight, 0.05f, 0.0f, 100.0f, "share %.2f");
            Track(changed, committed);
            ImGui::SameLine();
            ImGui::Text("%.0f%%", total > 0.0f ? 100.0f * std::max(0.0f, choice.weight) / total : 0.0f);
            ImGui::SameLine();
        }
        const bool remove = ImGui::SmallButton("Remove");
        ImGui::EndGroup();
        ImGui::PopID();
        if (remove) {
            look.textures.erase(look.textures.begin() + static_cast<std::ptrdiff_t>(i));
            committed = true;
            break;
        }
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
    packCounts_.assign(packs.size() + 1, 0);
    for (const uint32_t id : ids) {
        LibraryEntry entry{.id = id};
        char hex[9];
        std::snprintf(hex, sizeof(hex), "%08X", id);
        entry.searchText = hex;
        for (const PackCatalogRepository::TextureRow& row : decals_->PackCandidates(id)) {
            const auto pack = static_cast<int32_t>(row[2]);
            const std::string* file = catalog ? catalog->File(row[1]) : nullptr;
            if (entry.packs.empty()) {
                entry.sortPack = &packs[static_cast<size_t>(pack)].name;
                entry.sortFile = file ? file : &kNone;
            }
            if (std::ranges::find(entry.packs, pack) == entry.packs.end()) {
                entry.packs.push_back(pack);
                entry.searchText += ' ' + Upper(packs[static_cast<size_t>(pack)].name);
                ++packCounts_[static_cast<size_t>(pack)];
            }
            if (file) {
                entry.searchText += ' ' + Upper(*file);
            }
        }
        if (entry.packs.empty()) {
            ++packCounts_.back();
        }
        library_.push_back(std::move(entry));
    }

    // A pack's textures together, by original file, then ID; those not in the catalog last.
    std::ranges::sort(library_, {}, [](const LibraryEntry& entry) {
        return std::tuple(entry.packs.empty(), entry.sortPack ? std::string_view(*entry.sortPack) : std::string_view{},
                          entry.sortFile ? std::string_view(*entry.sortFile) : std::string_view{}, entry.id);
    });

    packsByName_.clear();
    for (int32_t i = 0; i < static_cast<int32_t>(packs.size()); ++i) {
        if (packCounts_[static_cast<size_t>(i)] > 0) {
            packsByName_.push_back(i);
        }
    }
    std::ranges::sort(packsByName_, {}, [&](const int32_t i) { return packs[static_cast<size_t>(i)].name; });
}

const std::vector<uint32_t>& GroundPanelTab::ShownTextures_() {
    IndexLibrary_();
    const auto* favourites = favorites_ ? &favorites_->GetFavoriteDecalIds() : nullptr;
    const size_t favouriteCount = favourites ? favourites->size() : 0;
    const std::string filter = Upper(textureFilter_);
    if (shownValid_ && filter == shownFilter_ && favouriteTexturesOnly_ == shownFavouritesOnly_ &&
        favouriteCount == shownFavouriteCount_ && packFilter_ == shownPack_) {
        return shownTextures_;
    }
    shownValid_ = true;
    shownFilter_ = filter;
    shownFavouritesOnly_ = favouriteTexturesOnly_;
    shownFavouriteCount_ = favouriteCount;
    shownPack_ = packFilter_;

    shownTextures_.clear();
    for (const LibraryEntry& entry : library_) {
        const bool inPack = packFilter_ == kAllPacks ||
                            (packFilter_ == kNoPack ? entry.packs.empty()
                                                    : std::ranges::find(entry.packs, packFilter_) != entry.packs.end());
        if (!inPack ||
            (favouriteTexturesOnly_ && (!favourites || !favourites->contains(entry.id))) ||
            (!filter.empty() && entry.searchText.find(filter) == std::string::npos)) {
            continue;
        }
        shownTextures_.push_back(entry.id);
    }
    return shownTextures_;
}

void GroundPanelTab::RenderPackFilter_() {
    IndexLibrary_();
    const std::vector<TexturePack>& packs = decals_->Packs();
    const uint32_t unknown = packCounts_.empty() ? 0 : packCounts_.back();
    const auto label = [&](const int32_t pack) {
        char text[160];
        if (pack == kAllPacks) {
            std::snprintf(text, sizeof(text), "All packs (%zu)", library_.size());
        }
        else if (pack == kNoPack) {
            std::snprintf(text, sizeof(text), "Not in the catalog (%u)", unknown);
        }
        else {
            std::snprintf(text, sizeof(text), "%s (%u)", packs[static_cast<size_t>(pack)].name.c_str(),
                          packCounts_[static_cast<size_t>(pack)]);
        }
        return std::string(text);
    };

    ImGui::SetNextItemWidth(-1.0f);
    if (!ImGui::BeginCombo("##pack", label(packFilter_).c_str(), ImGuiComboFlags_HeightLarge)) {
        return;
    }
    const auto option = [&](const int32_t pack) {
        if (ImGui::Selectable(label(pack).c_str(), packFilter_ == pack)) {
            packFilter_ = pack;
        }
        if (pack >= 0 && ImGui::IsItemHovered()) {
            const TexturePack& info = packs[static_cast<size_t>(pack)];
            ImGui::SetTooltip("%s\n%s%s%s", info.author.c_str(), info.package.c_str(), info.package.empty() ? "" : "\n",
                              info.url.c_str());
        }
    };
    option(kAllPacks);
    for (const int32_t pack : packsByName_) {
        option(pack);
    }
    if (unknown > 0) {
        option(kNoPack);
    }
    ImGui::EndCombo();
}

void GroundPanelTab::TextureTooltip_(const uint32_t instanceId) const {
    const std::string* file = decals_->OriginalFile(instanceId);
    const TexturePack* pack = decals_->PackOf(instanceId);
    ImGui::BeginTooltip();
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
    ImGui::TextUnformatted("Click: paint with it. Ctrl+click: add it to the mix or take it out.\nRight-click: favourite.");
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
            if (i > 0) {
                ImGui::SameLine();
            }
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
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##filter", "Search name, pack or ID", textureFilter_, sizeof(textureFilter_));
    ImGui::SameLine();
    ImGui::Checkbox("Favourites only", &favouriteTexturesOnly_);

    const std::vector<uint32_t>& shown = ShownTextures_();
    ImGui::TextDisabled("%zu textures. Click to paint with one, Ctrl+click to mix several, right-click for a favourite.",
                        shown.size());

    if (ImGui::BeginChild("##textures", ImVec2(0, 240.0f), true)) {
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
    const std::vector<uint32_t>& selection = document.Selection();
    if (selection.empty()) {
        editingId_ = 0;
        return;
    }

    const uint32_t first = selection.front();
    RenderSelectionActions_(document);
    if (document.Selection().empty() || document.Selection().front() != first) {
        editingId_ = 0;
        return;
    }
    RenderTransform_(document);

    const PaintObject* const object = document.Find(first);
    if (!object || selection.size() != 1) {
        editingId_ = 0;
        return;
    }
    if (editingId_ != first || (!ImGui::IsAnyItemActive() && object->drawOrder != editing_.drawOrder)) {
        editingId_ = first;
        editing_ = *object;
        selectionLook_ = {};
        if (!object->look.textures.empty()) {
            // Painted with a texture: its colour is the tint; the plain colour starts white.
            selectionLook_.wantsTexture = true;
        }
    }

    bool changed = false;
    bool committed = false;
    char name[64];
    std::snprintf(name, sizeof(name), "%s", editing_.name.c_str());
    if (ImGui::InputTextWithHint("Name", ObjectLabel(*object).c_str(), name, sizeof(name))) {
        editing_.name = name;
    }
    Track(changed, committed);

    ImGui::SeparatorText("Paint with");
    ShapeSpec& shape = editing_.shape;
    ImGui::PushID("look");
    RenderLookEditor_(editing_.look, shape.kind == ShapeKind::Line, selectionLook_, changed, committed);
    ImGui::PopID();

    if (shape.kind != ShapeKind::Merged) {
        ImGui::SeparatorText(shape.kind == ShapeKind::Marking ? "Marking" : "Shape");
    }
    switch (shape.kind) {
    case ShapeKind::Area:
        ImGui::DragFloat("Round corners", &shape.cornerRadius, 0.05f, 0.0f, 50.0f, shape.cornerRadius > 0.0f ? "%.2f m" : "no");
        Track(changed, committed);
        break;
    case ShapeKind::Line:
        ImGui::DragFloat("Width", &shape.width, 0.01f, 0.02f, 50.0f, "%.2f m");
        Track(changed, committed);
        ImGui::DragFloat("Dashes", &shape.dashLength, 0.05f, 0.0f, 50.0f, shape.dashLength > 0.0f ? "%.2f m long" : "none (solid)");
        Track(changed, committed);
        if (shape.dashLength > 0.0f) {
            ImGui::DragFloat("Gaps", &shape.gapLength, 0.05f, 0.0f, 50.0f, "%.2f m");
            Track(changed, committed);
        }
        break;
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
    RenderFinish_(editing_.look, shape.wear, shape.kind == ShapeKind::Marking ? nullptr : &shape.roughness, changed,
                  committed);

    if (committed) {
        document.Update({editing_});
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
    ImGui::SameLine();
    if (ImGui::SmallButton("Up")) document.MoveUp(first);
    ImGui::SameLine();
    if (ImGui::SmallButton("Down")) document.MoveDown(first);
    ImGui::SameLine();
    if (ImGui::SmallButton("Top")) document.MoveToTop(first);
    ImGui::SameLine();
    if (ImGui::SmallButton("Bottom")) document.MoveToBottom(first);

    if (ImGui::SmallButton("Copy")) {
        document.Duplicate(selection, {4.0f, 4.0f});
    }
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
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selection.size() < 2);
    if (ImGui::SmallButton("Merge")) document.Merge(selection);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Make the selected shapes one, painted like the topmost.\nTheir points can no longer be edited.");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
        document.Remove(selection);
    }
}

void GroundPanelTab::RenderTransform_(Document& document) {
    // The two controls show the change while dragged and make it when let go.
    bool active = false;
    bool done = false;
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Turn", &turnDegrees_, 0.5f, -180.0f, 180.0f, "%.1f deg");
    const bool turnStarted = ImGui::IsItemActivated();
    active |= ImGui::IsItemActive();
    done |= ImGui::IsItemDeactivated();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Scale", &scalePercent_, 0.25f, 10.0f, 1000.0f, "%.0f %%");
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
    ImGui::SameLine();
    ImGui::BeginDisabled(!document.CanRedo());
    if (ImGui::SmallButton("Redo")) {
        document.Redo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+Z, Ctrl+Y, or the back and forward mouse buttons");
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
    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
        const PaintObject& object = *it;
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
        // What it is painted with: its texture, or its colour.
        const float swatch = ImGui::GetTextLineHeight();
        if (!object.look.textures.empty()) {
            TextureTile_(object.look.textures.front().instance, swatch, false);
        }
        else {
            const auto& c = object.look.color;
            ImGui::ColorButton("##colour", ImVec4(c[0], c[1], c[2], 1.0f), ImGuiColorEditFlags_NoTooltip,
                               ImVec2(swatch, swatch));
        }
        ImGui::TableNextColumn();
        const bool selected = ImGui::Selectable(ObjectLabel(object).c_str(), document.IsSelected(object.id),
                                                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
        if (selected) {
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
