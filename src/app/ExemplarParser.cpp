#include "ExemplarParser.hpp"
#include "FiraMono.hpp"
#include "LTextReader.h"
#include "ThumbnailRenderer.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_set>

#include "raylib.h"
#include "spdlog/spdlog.h"

#include <stb_image.h>

#include "BuiltinPropFamilyNames.hpp"
#include "S3DStructures.h"
#include "Utils.hpp"

namespace {
    // Icon dimensions: first 44px is greyscale locked icon, second 44px is the color icon we want
    constexpr uint32_t kIconSkipWidth = 44; // Skip the first 44 pixels (greyscale locked icon)
    constexpr uint32_t kIconCropWidth = 44; // Extract the next 44 pixels (color icon)

    // Decode PNG data to RGBA32 pixel data, extracting the second 44-pixel wide icon
    // Returns empty vector on failure
    struct DecodedImage {
        std::vector<std::byte> pixels; // RGBA32 data
        uint32_t width = 0;
        uint32_t height = 0;
    };

    Font getThumbnailFailureFont() {
        struct FontHolder {
            Font font{};
            bool loaded = false;

            ~FontHolder() {
                if (loaded && IsFontValid(font)) {
                    UnloadFont(font);
                }
            }
        };

        static FontHolder holder;
        static bool initialized = false;
        if (!initialized) {
            holder.font = LoadFontFromMemory(".ttf", FiraMonoBold, static_cast<int>(std::size(FiraMonoBold)), 64, nullptr, 0);
            holder.loaded = IsFontValid(holder.font);
            if (!holder.loaded) {
                spdlog::warn("Failed to load embedded Fira Mono font, using raylib default font");
            }
            initialized = true;
        }

        return holder.loaded ? holder.font : GetFontDefault();
    }

    PreRendered makeRenderFailedThumbnail(const uint32_t size, const DBPF::Tgi* modelTgi = nullptr) {
        constexpr Color background{0, 0, 0, 255};
        constexpr Color text{245, 245, 235, 245};

        Image image = GenImageColor(static_cast<int>(size), static_cast<int>(size), BLANK);
        const Font font = getThumbnailFailureFont();
        const int lineGap = std::max(4, static_cast<int>(size / 28));
        const int padding = std::max(4, static_cast<int>(size / 28));
        const Rectangle textRect{
            static_cast<float>(padding),
            static_cast<float>(padding),
            static_cast<float>(size - padding * 2),
            static_cast<float>(size - padding * 2)
        };

        char typeBuffer[16] = "????????";
        char groupBuffer[16] = "????????";
        char instanceBuffer[16] = "????????";
        if (modelTgi) {
            std::snprintf(typeBuffer, sizeof(typeBuffer), "%08X", modelTgi->type);
            std::snprintf(groupBuffer, sizeof(groupBuffer), "%08X", modelTgi->group);
            std::snprintf(instanceBuffer, sizeof(instanceBuffer), "%08X", modelTgi->instance);
        }

        const char* lines[4] = {
            "TGI",
            typeBuffer,
            groupBuffer,
            instanceBuffer
        };
        constexpr int kValueFontCandidates[] = {64, 56, 48, 40, 36, 32, 28, 24, 22, 20, 18, 16, 14, 12};
        constexpr int kHeaderFontCandidates[] = {28, 24, 22, 20, 18, 16, 14, 12};
        int headerFontSize = kHeaderFontCandidates[0];
        int valueFontSize = kValueFontCandidates[0];
        constexpr float kHeaderSpacing = 1.0f;
        constexpr float kValueSpacing = 1.0f;
        Vector2 lineSizes[4]{};
        float contentHeight = 0.0f;

        bool foundFit = false;
        for (const int candidateValueFontSize : kValueFontCandidates) {
            int candidateHeaderFontSize = kHeaderFontCandidates[std::size(kHeaderFontCandidates) - 1];
            for (const int headerCandidate : kHeaderFontCandidates) {
                if (headerCandidate <= candidateValueFontSize - 6) {
                    candidateHeaderFontSize = headerCandidate;
                    break;
                }
            }

            contentHeight = 0.0f;
            lineSizes[0] = MeasureTextEx(font, lines[0], static_cast<float>(candidateHeaderFontSize), kHeaderSpacing);
            contentHeight += lineSizes[0].y;
            float maxLineWidth = lineSizes[0].x;
            for (int i = 1; i < 4; ++i) {
                lineSizes[i] = MeasureTextEx(font, lines[i], static_cast<float>(candidateValueFontSize), kValueSpacing);
                contentHeight += lineSizes[i].y;
                maxLineWidth = std::max(maxLineWidth, lineSizes[i].x);
            }
            contentHeight += static_cast<float>(lineGap) * 3.0f;

            const bool fitsWidth = maxLineWidth <= textRect.width;
            const bool fitsHeight = contentHeight <= textRect.height;
            if (fitsWidth && fitsHeight) {
                valueFontSize = candidateValueFontSize;
                headerFontSize = candidateHeaderFontSize;
                foundFit = true;
                break;
            }
        }

        if (!foundFit) {
            headerFontSize = kHeaderFontCandidates[std::size(kHeaderFontCandidates) - 1];
            valueFontSize = kValueFontCandidates[std::size(kValueFontCandidates) - 1];
            contentHeight = 0.0f;
            lineSizes[0] = MeasureTextEx(font, lines[0], static_cast<float>(headerFontSize), kHeaderSpacing);
            contentHeight += lineSizes[0].y;
            for (int i = 1; i < 4; ++i) {
                lineSizes[i] = MeasureTextEx(font, lines[i], static_cast<float>(valueFontSize), kValueSpacing);
                contentHeight += lineSizes[i].y;
            }
            contentHeight += static_cast<float>(lineGap) * 3.0f;
        }

        float currentY = textRect.y + (textRect.height - contentHeight) * 0.5f;
        for (int i = 0; i < 4; ++i) {
            const float fontSize = static_cast<float>(i == 0 ? headerFontSize : valueFontSize);
            const float spacing = i == 0 ? kHeaderSpacing : kValueSpacing;
            const Vector2 linePos{
                textRect.x + (textRect.width - lineSizes[i].x) * 0.5f,
                currentY
            };
            ImageDrawTextEx(&image, font, lines[i], linePos, fontSize, spacing, text);
            currentY += lineSizes[i].y;
            if (i < 3) {
                currentY += static_cast<float>(lineGap);
            }
        }

        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

        std::vector<std::byte> pixels(static_cast<size_t>(size) * size * 4);
        std::memcpy(pixels.data(), image.data, pixels.size());
        UnloadImage(image);

        PreRendered preview;
        preview.width = size;
        preview.height = size;
        preview.data = rfl::Bytestring(std::move(pixels));
        return preview;
    }

    std::optional<DBPF::Tgi> tgiFromProperty(const Exemplar::Property* prop, uint32_t defaultType) {
        if (!prop || prop->values.size() < 3) {
            return std::nullopt;
        }

        auto type = prop->GetScalarAs<uint32_t>(0);
        auto group = prop->GetScalarAs<uint32_t>(1);
        auto instance = prop->GetScalarAs<uint32_t>(2);
        if (!group || !instance) {
            return std::nullopt;
        }

        uint32_t typeValue = type.value_or(defaultType);
        if (typeValue == 0) {
            typeValue = defaultType;
        }

        return DBPF::Tgi{typeValue, *group, *instance};
    }

    std::optional<std::string> loadLocalizedText(const DbpfIndexService* indexService, const DBPF::Tgi& tgi) {
        if (!indexService) {
            return std::nullopt;
        }

        const auto data = indexService->loadEntryData(tgi);
        if (!data || data->empty()) {
            spdlog::trace("Failed to load localized text {}: no data", tgi.ToString());
            return std::nullopt;
        }

        auto parsed = LText::Parse(std::span(data->data(), data->size()));
        if (!parsed.has_value()) {
            spdlog::trace("Failed to parse LText {}: {}", tgi.ToString(), parsed.error().message);
            return std::nullopt;
        }

        auto text = parsed->ToUtf8();
        if (text.empty()) {
            return std::nullopt;
        }
        else {
            spdlog::trace("Loaded localized text {}: {}", tgi.ToString(), text);
        }

        return text;
    }

    DecodedImage decodePngToRgba32(const std::vector<uint8_t>& pngData) {
        DecodedImage result;

        if (pngData.empty()) {
            spdlog::trace("decodePngToRgba32: empty pngData");
            return result;
        }

        spdlog::trace("decodePngToRgba32: attempting to decode {} bytes", pngData.size());

        int width, height, channels;
        unsigned char* pixels = stbi_load_from_memory(
            pngData.data(),
            static_cast<int>(pngData.size()),
            &width, &height, &channels, 4 // Force RGBA output
        );

        if (!pixels) {
            spdlog::warn("Failed to decode PNG ({} bytes): {}", pngData.size(), stbi_failure_reason());
            return result;
        }

        spdlog::trace("decodePngToRgba32: decoded {}x{} image with {} channels", width, height, channels);

        // Check if image is wide enough to have the second icon
        if (static_cast<uint32_t>(width) < kIconSkipWidth + kIconCropWidth) {
            spdlog::trace("decodePngToRgba32: image too narrow ({}px), need at least {}px",
                          width, kIconSkipWidth + kIconCropWidth);
            stbi_image_free(pixels);
            return result;
        }

        constexpr auto cropWidth = kIconCropWidth;
        const auto cropHeight = static_cast<uint32_t>(height);

        // Copy the second 44-pixel region and swap R/B channels in a single pass
        const size_t croppedDataSize = static_cast<size_t>(cropWidth) * cropHeight * 4;
        result.pixels.resize(croppedDataSize);

        for (uint32_t y = 0; y < cropHeight; ++y) {
            const size_t srcOffset = (static_cast<size_t>(y) * width + kIconSkipWidth) * 4;
            const size_t dstOffset = static_cast<size_t>(y) * cropWidth * 4;
            const auto* src = pixels + srcOffset;
            auto* dst = result.pixels.data() + dstOffset;
            for (uint32_t x = 0; x < cropWidth; ++x) {
                const size_t px = x * 4;
                dst[px + 0] = static_cast<std::byte>(src[px + 2]); // B <- R
                dst[px + 1] = static_cast<std::byte>(src[px + 1]); // G
                dst[px + 2] = static_cast<std::byte>(src[px + 0]); // R <- B
                dst[px + 3] = static_cast<std::byte>(src[px + 3]); // A
            }
        }

        result.width = cropWidth;
        result.height = cropHeight;

        stbi_image_free(pixels);
        return result;
    }
}

namespace fs = std::filesystem;

ExemplarParser::ExemplarParser(const PropertyMapper& mapper,
                               const DbpfIndexService* indexService,
                               const bool renderThumbnails,
                               const uint32_t thumbnailSize)
    : propertyMapper_(mapper)
      , indexService_(indexService)
      , thumbnailSize_(thumbnailSize)
      , pidExemplarType_(mapper.propertyId(kExemplarType))
      , pidItemName_(mapper.propertyId(kItemName))
      , pidUserVisibleNameKey_(mapper.propertyId(kUserVisibleNameKey))
      , pidExemplarName_(mapper.propertyId(kExemplarName))
      , pidItemDescriptionKey_(mapper.propertyId(kItemDescriptionKey))
      , pidItemDescription_(mapper.propertyId(kItemDescription))
      , pidOccupantGroups_(mapper.propertyId(kOccupantGroups))
      , pidBuildingPropFamily_(mapper.propertyId(kBuildingPropFamily))
      , pidItemIcon_(mapper.propertyId(kItemIcon))
      , pidLotConfigSize_(mapper.propertyId(kLotConfigSize))
      , pidGrowthStage_(mapper.propertyId(kGrowthStage))
      , pidLotConfigZoneType_(mapper.propertyId(kLotConfigZoneType))
      , pidLotConfigWealthType_(mapper.propertyId(kLotConfigWealthType))
      , pidLotConfigPurposeType_(mapper.propertyId(kLotConfigPurposeType))
      , pidOccupantSize_(mapper.propertyId(kOccupantSize))
      , pidNighttimeStateChange_(mapper.propertyId(kNighttimeStateChange))
      , pidPropTimeOfDay_(mapper.propertyId(kPropTimeOfDay))
      , pidSimulatorDateStart_(mapper.propertyId(kSimulatorDateStart))
      , pidSimulatorDateDuration_(mapper.propertyId(kSimulatorDateDuration))
      , pidSimulatorDateInterval_(mapper.propertyId(kSimulatorDateInterval))
      , pidPropRandomChance_(mapper.propertyId(kPropRandomChance))
      , pidFloraWild_(mapper.propertyId(kFloraWild))
      , pidFloraFamily_(mapper.propertyId(kFloraFamily))
      , pidFloraClusterType_(mapper.propertyId(kFloraClusterType))
      , optBuilding_(mapper.propertyOptionId(kExemplarType, kExemplarTypeBuilding))
      , optLotConfig_(mapper.propertyOptionId(kExemplarType, kExemplarTypeLotConfig))
      , optProp_(mapper.propertyOptionId(kExemplarType, kExemplarTypeProp))
      , optFlora_(mapper.propertyOptionId(kExemplarType, kExemplarTypeFlora)) {
    if (renderThumbnails && indexService_) {
        thumbnailRenderer_ = std::make_unique<thumb::ThumbnailRenderer>(*indexService_);
    }
}

ExemplarParser::~ExemplarParser() = default;

std::optional<ExemplarType> ExemplarParser::getExemplarType(const Exemplar::Record& exemplar) const {
    if (!pidExemplarType_) {
        return std::nullopt;
    }

    const auto* prop = findProperty(exemplar, *pidExemplarType_);
    if (!prop || prop->values.empty()) {
        return std::nullopt;
    }

    const auto exemplarTypeOpt = prop->GetScalarAs<uint32_t>();
    if (!exemplarTypeOpt) {
        return std::nullopt;
    }

    if (optBuilding_ && *exemplarTypeOpt == *optBuilding_) return ExemplarType::Building;
    if (optLotConfig_ && *exemplarTypeOpt == *optLotConfig_) return ExemplarType::LotConfig;
    if (optProp_ && *exemplarTypeOpt == *optProp_) return ExemplarType::Prop;
    if (optFlora_ && *exemplarTypeOpt == *optFlora_) return ExemplarType::Flora;
    return std::nullopt;
}

std::optional<ParsedBuildingExemplar> ExemplarParser::parseBuilding(const Exemplar::Record& exemplar,
                                                                    const DBPF::Tgi& tgi) const {
    ParsedBuildingExemplar parsedBuildingExemplar;
    parsedBuildingExemplar.tgi = tgi;

    parsedBuildingExemplar.name = "";
    parsedBuildingExemplar.description = "";

    if (pidItemName_) {
        if (auto* prop = findProperty(exemplar, *pidItemName_)) {
            if (auto name = prop->GetScalarAs<std::string>()) {
                parsedBuildingExemplar.name = *name;
            }
        }
    }

    if (parsedBuildingExemplar.name.empty() && pidUserVisibleNameKey_) {
        if (auto* prop = findProperty(exemplar, *pidUserVisibleNameKey_)) {
            if (auto tgiKey = tgiFromProperty(prop, kTypeIdLText)) {
                if (auto localized = loadLocalizedText(indexService_, *tgiKey)) {
                    parsedBuildingExemplar.name = resolveLTextTags_(*localized, exemplar);
                }
            }
        }
    }

    if (parsedBuildingExemplar.name.empty() && pidExemplarName_) {
        if (auto* prop = findProperty(exemplar, *pidExemplarName_)) {
            if (auto name = prop->GetScalarAs<std::string>()) {
                parsedBuildingExemplar.name = *name;
            }
        }
    }

    if (pidItemDescriptionKey_) {
        if (auto* prop = findProperty(exemplar, *pidItemDescriptionKey_)) {
            if (auto tgiKey = tgiFromProperty(prop, kTypeIdLText)) {
                if (auto localized = loadLocalizedText(indexService_, *tgiKey)) {
                    parsedBuildingExemplar.description = resolveLTextTags_(*localized, exemplar);
                }
            }
        }
    }

    if (parsedBuildingExemplar.description.empty() && pidItemDescription_) {
        if (auto* prop = findProperty(exemplar, *pidItemDescription_)) {
            if (auto description = prop->GetScalarAs<std::string>()) {
                parsedBuildingExemplar.description = *description;
            }
        }
    }

    if (pidOccupantGroups_) {
        if (auto* prop = findProperty(exemplar, *pidOccupantGroups_)) {
            if (prop->IsNumericList()) {
                for (size_t i = 0; i < prop->values.size(); ++i) {
                    if (auto v = prop->GetScalarAs<uint32_t>(i)) {
                        parsedBuildingExemplar.occupantGroups.push_back(*v);
                    }
                }
            }
        }
    }

    // Extract building family IDs
    if (pidBuildingPropFamily_) {
        if (auto* prop = findProperty(exemplar, *pidBuildingPropFamily_)) {
            for (size_t i = 0; i < prop->values.size(); ++i) {
                if (auto familyId = prop->GetScalarAs<uint32_t>(i)) {
                    parsedBuildingExemplar.familyIds.push_back(*familyId);
                }
            }
        }
    }

    if (pidItemIcon_) {
        if (auto* prop = findProperty(exemplar, *pidItemIcon_)) {
            if (const auto iconInstance = prop->GetScalarAs<uint32_t>()) {
                parsedBuildingExemplar.iconTgi = DBPF::Tgi{
                    kTypeIdPNG,
                    kLotIconGroup,
                    *iconInstance
                };
            }
        }
    }

    parsedBuildingExemplar.modelTgi = resolveModelTgi_(exemplar, tgi);

    return parsedBuildingExemplar;
}

std::optional<ParsedLotConfigExemplar> ExemplarParser::parseLotConfig(
    const Exemplar::Record& exemplar,
    const DBPF::Tgi& tgi,
    const std::unordered_map<uint32_t, ParsedBuildingExemplar>& buildingMap,
    const std::unordered_map<uint32_t, std::vector<uint32_t>>& familyToBuildingsMap) const {
    ParsedLotConfigExemplar parsedLotConfigExemplar;
    parsedLotConfigExemplar.tgi = tgi;
    parsedLotConfigExemplar.buildingInstanceId = 0;
    parsedLotConfigExemplar.buildingFamilyId = 0;
    parsedLotConfigExemplar.isFamilyReference = false;

    if (pidExemplarName_) {
        if (auto* prop = findProperty(exemplar, *pidExemplarName_)) {
            if (auto name = prop->GetScalarAs<std::string>()) {
                parsedLotConfigExemplar.name = *name;
            }
        }
    }

    if (pidLotConfigSize_) {
        if (auto* prop = findProperty(exemplar, *pidLotConfigSize_)) {
            if (prop->IsNumericList() && prop->values.size() >= 2) {
                auto width = prop->GetScalarAs<uint8_t>(0);
                auto height = prop->GetScalarAs<uint8_t>(1);
                if (width && height) {
                    parsedLotConfigExemplar.lotSize = {*width, *height};
                }
            }
        }
    }

    // Scan through the lot objects property ID range to find the building
    for (uint32_t propID = kPropertyLotObjectsStart;
         propID <= kPropertyLotObjectsEnd;
         propID++) {
        if (auto* prop = findProperty(exemplar, propID)) {
            if (prop->values.size() >= 13) {
                auto objectType = prop->GetScalarAs<uint32_t>(kLotObjectIndexType);
                if (objectType && *objectType == kLotConfigObjectTypeBuilding) {
                    // Rep 13 (index 12) contains either:
                    // - Building IID (for most ploppables by Maxis, and most custom content)
                    // - Family ID (for all growables by Maxis, and very rarely custom content)
                    // We determine which by checking if it matches a known building first
                    auto rep13Value = prop->GetScalarAs<uint32_t>(kLotObjectIndexIID);

                    if (rep13Value) {
                        // First, check if this is a known building instance ID
                        auto buildingIt = buildingMap.find(*rep13Value);
                        if (buildingIt != buildingMap.end()) {
                            // Direct building IID reference
                            parsedLotConfigExemplar.buildingInstanceId = *rep13Value;
                        }
                        else {
                            // Not a known building - check if it's a family ID
                            auto famIt = familyToBuildingsMap.find(*rep13Value);
                            if (famIt != familyToBuildingsMap.end() && !famIt->second.empty()) {
                                // This is a family reference
                                parsedLotConfigExemplar.isFamilyReference = true;
                                parsedLotConfigExemplar.buildingFamilyId = *rep13Value;
                                // Use the first building from this family
                                parsedLotConfigExemplar.buildingInstanceId = famIt->second.front();
                            }
                            else {
                                // Unknown reference - could be a building we haven't seen yet
                                // or a family with no members. Store it as a potential IID.
                                parsedLotConfigExemplar.buildingInstanceId = *rep13Value;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }

    // We need either a valid building ID or a family reference with a resolved building
    if (!parsedLotConfigExemplar.buildingInstanceId) {
        return std::nullopt;
    }

    if (pidGrowthStage_) {
        if (auto* prop = findProperty(exemplar, *pidGrowthStage_)) {
            if (auto v = prop->GetScalarAs<uint8_t>()) {
                parsedLotConfigExemplar.growthStage = *v;
            }
        }
    }

    if (pidLotConfigZoneType_) {
        if (auto* prop = findProperty(exemplar, *pidLotConfigZoneType_)) {
            if (auto v = prop->GetScalarAs<uint8_t>()) {
                parsedLotConfigExemplar.zoneType = *v;
            }
        }
    }

    if (pidLotConfigWealthType_) {
        if (auto* prop = findProperty(exemplar, *pidLotConfigWealthType_)) {
            if (auto v = prop->GetScalarAs<uint8_t>()) {
                parsedLotConfigExemplar.wealthType = *v;
            }
        }
    }

    if (pidLotConfigPurposeType_) {
        if (auto* prop = findProperty(exemplar, *pidLotConfigPurposeType_)) {
            if (auto v = prop->GetScalarAs<uint8_t>()) {
                parsedLotConfigExemplar.purposeType = *v;
            }
        }
    }

    return parsedLotConfigExemplar;
}

std::optional<ParsedPropExemplar> ExemplarParser::parseProp(const Exemplar::Record& exemplar,
                                                            const DBPF::Tgi& tgi) const {
    ParsedPropExemplar parsedPropExemplar;
    parsedPropExemplar.tgi = tgi;
    parsedPropExemplar.visibleName = "";
    parsedPropExemplar.exemplarName = "";
    parsedPropExemplar.modelTgi = std::nullopt;

    if (pidUserVisibleNameKey_) {
        if (auto* prop = findProperty(exemplar, *pidUserVisibleNameKey_)) {
            if (auto tgiKey = tgiFromProperty(prop, kTypeIdLText)) {
                if (auto localized = loadLocalizedText(indexService_, *tgiKey)) {
                    auto resolvedUVNK = resolveLTextTags_(*localized, exemplar);
                    resolvedUVNK = SanitizeString(resolvedUVNK);
                    parsedPropExemplar.visibleName = std::move(resolvedUVNK);
                }
            }
        }
    }

    if (pidExemplarName_) {
        if (auto* prop = findProperty(exemplar, *pidExemplarName_)) {
            if (const auto name = prop->GetScalarAs<std::string>()) {
                parsedPropExemplar.exemplarName = SanitizeString(*name);
            }
        }
    }

    if (pidOccupantSize_) {
        if (auto* prop = findProperty(exemplar, *pidOccupantSize_)) {
            if (prop->IsNumericList()) {
                if (prop->values.size() >= 3) {
                    auto width = prop->GetScalarAs<float>(0);
                    auto height = prop->GetScalarAs<float>(1);
                    auto depth = prop->GetScalarAs<float>(2);
                    if (width && height && depth) {
                        parsedPropExemplar.width = *width;
                        parsedPropExemplar.height = *height;
                        parsedPropExemplar.depth = *depth;
                    }
                    else {
                        spdlog::warn("Failed to parse occupant size for {} at {}",
                                     parsedPropExemplar.exemplarName,
                                     tgi.ToString());
                    }
                }
            }
        }
    }

    if (pidBuildingPropFamily_) {
        if (auto* prop = findProperty(exemplar, *pidBuildingPropFamily_)) {
            for (size_t i = 0; i < prop->values.size(); ++i) {
                if (auto familyId = prop->GetScalarAs<uint32_t>(i)) {
                    parsedPropExemplar.familyIds.push_back(*familyId);
                }
            }
        }
    }

    if (pidNighttimeStateChange_) {
        if (auto* prop = findProperty(exemplar, *pidNighttimeStateChange_)) {
            if (const auto value = prop->GetScalarAs<uint8_t>()) {
                parsedPropExemplar.nighttimeStateChange = (*value != 0);
            }
        }
    }

    if (pidPropTimeOfDay_) {
        if (auto* prop = findProperty(exemplar, *pidPropTimeOfDay_)) {
            if (prop->values.size() >= 2) {
                const auto startHour = prop->GetScalarAs<float>(0);
                const auto endHour = prop->GetScalarAs<float>(1);
                if (startHour && endHour) {
                    parsedPropExemplar.timeOfDay = PropTimeOfDay{*startHour, *endHour};
                }
            }
        }
    }

    if (pidSimulatorDateStart_) {
        if (auto* prop = findProperty(exemplar, *pidSimulatorDateStart_)) {
            if (prop->values.size() >= 2) {
                const auto month = prop->GetScalarAs<uint8_t>(0);
                const auto day = prop->GetScalarAs<uint8_t>(1);
                if (month && day) {
                    parsedPropExemplar.simulatorDateStart = SimulatorDateStart{*month, *day};
                }
            }
        }
    }

    if (pidSimulatorDateDuration_) {
        if (auto* prop = findProperty(exemplar, *pidSimulatorDateDuration_)) {
            if (const auto value = prop->GetScalarAs<uint32_t>()) {
                parsedPropExemplar.simulatorDateDuration = *value;
            }
        }
    }

    if (pidSimulatorDateInterval_) {
        if (auto* prop = findProperty(exemplar, *pidSimulatorDateInterval_)) {
            if (const auto value = prop->GetScalarAs<uint32_t>()) {
                parsedPropExemplar.simulatorDateInterval = *value;
            }
        }
    }

    if (pidPropRandomChance_) {
        if (auto* prop = findProperty(exemplar, *pidPropRandomChance_)) {
            if (const auto value = prop->GetScalarAs<uint8_t>()) {
                parsedPropExemplar.randomChance = *value;
            }
        }
    }

    parsedPropExemplar.modelTgi = resolveModelTgi_(exemplar, tgi);

    if (parsedPropExemplar.modelTgi.has_value()) {
        if (const auto bounds = loadModelBounds_(*parsedPropExemplar.modelTgi)) {
            parsedPropExemplar.minX = (*bounds)[0];
            parsedPropExemplar.maxX = (*bounds)[1];
            parsedPropExemplar.minY = (*bounds)[2];
            parsedPropExemplar.maxY = (*bounds)[3];
            parsedPropExemplar.minZ = (*bounds)[4];
            parsedPropExemplar.maxZ = (*bounds)[5];
            parsedPropExemplar.hasModelBounds = true;
        }
    }

    if (!parsedPropExemplar.hasModelBounds &&
        parsedPropExemplar.width > 0.0f &&
        parsedPropExemplar.height > 0.0f &&
        parsedPropExemplar.depth > 0.0f) {
        parsedPropExemplar.minX = -parsedPropExemplar.width * 0.5f;
        parsedPropExemplar.maxX = parsedPropExemplar.width * 0.5f;
        parsedPropExemplar.minY = 0.0f;
        parsedPropExemplar.maxY = parsedPropExemplar.height;
        parsedPropExemplar.minZ = -parsedPropExemplar.depth * 0.5f;
        parsedPropExemplar.maxZ = parsedPropExemplar.depth * 0.5f;
    }

    return parsedPropExemplar;
}

std::optional<ParsedFloraExemplar> ExemplarParser::parseFlora(const Exemplar::Record& exemplar,
                                                              const DBPF::Tgi& tgi) const {
    // Only parse MMP flora (Flora: Wild == false). Skip wild/god-mode flora.
    if (pidFloraWild_) {
        if (const auto* prop = findProperty(exemplar, *pidFloraWild_)) {
            if (const auto wild = prop->GetScalarAs<bool>()) {
                if (*wild) {
                    spdlog::trace("Skipping wild flora at {}", tgi.ToString());
                    return std::nullopt;
                }
            }
        }
    }

    ParsedFloraExemplar parsed;
    parsed.tgi = tgi;

    if (pidUserVisibleNameKey_) {
        if (const auto* prop = findProperty(exemplar, *pidUserVisibleNameKey_)) {
            if (auto tgiKey = tgiFromProperty(prop, kTypeIdLText)) {
                if (auto localized = loadLocalizedText(indexService_, *tgiKey)) {
                    parsed.visibleName = SanitizeString(resolveLTextTags_(*localized, exemplar));
                }
            }
        }
    }

    if (pidExemplarName_) {
        if (const auto* prop = findProperty(exemplar, *pidExemplarName_)) {
            if (const auto name = prop->GetScalarAs<std::string>()) {
                parsed.exemplarName = SanitizeString(*name);
            }
        }
    }

    if (pidOccupantSize_) {
        if (const auto* prop = findProperty(exemplar, *pidOccupantSize_)) {
            if (prop->IsNumericList() && prop->values.size() >= 3) {
                const auto w = prop->GetScalarAs<float>(0);
                const auto h = prop->GetScalarAs<float>(1);
                const auto d = prop->GetScalarAs<float>(2);
                if (w && h && d) {
                    parsed.width = *w;
                    parsed.height = *h;
                    parsed.depth = *d;
                }
            }
        }
    }

    if (pidFloraFamily_) {
        if (const auto* prop = findProperty(exemplar, *pidFloraFamily_)) {
            for (size_t i = 0; i < prop->values.size(); ++i) {
                if (const auto familyId = prop->GetScalarAs<uint32_t>(i)) {
                    parsed.familyIds.push_back(*familyId);
                }
            }
        }
    }

    if (pidFloraClusterType_) {
        if (const auto* prop = findProperty(exemplar, *pidFloraClusterType_)) {
            if (const auto value = prop->GetScalarAs<uint32_t>()) {
                if (*value != 0) {
                    parsed.clusterNextType = *value;
                }
            }
        }
    }

    parsed.modelTgi = resolveModelTgi_(exemplar, tgi);

    if (parsed.modelTgi.has_value()) {
        if (const auto bounds = loadModelBounds_(*parsed.modelTgi)) {
            parsed.minX = (*bounds)[0];
            parsed.maxX = (*bounds)[1];
            parsed.minY = (*bounds)[2];
            parsed.maxY = (*bounds)[3];
            parsed.minZ = (*bounds)[4];
            parsed.maxZ = (*bounds)[5];
            parsed.hasModelBounds = true;
        }
    }

    if (!parsed.hasModelBounds &&
        parsed.width > 0.0f && parsed.height > 0.0f && parsed.depth > 0.0f) {
        parsed.minX = -parsed.width * 0.5f;
        parsed.maxX =  parsed.width * 0.5f;
        parsed.minY = 0.0f;
        parsed.maxY = parsed.height;
        parsed.minZ = -parsed.depth * 0.5f;
        parsed.maxZ =  parsed.depth * 0.5f;
    }

    return parsed;
}

Flora ExemplarParser::floraFromParsed(const ParsedFloraExemplar& parsed) const {
    Flora flora;
    flora.instanceId = parsed.tgi.instance;
    flora.groupId    = parsed.tgi.group;
    flora.exemplarName = parsed.exemplarName;
    flora.visibleName  = parsed.visibleName;
    flora.width  = parsed.width;
    flora.height = parsed.height;
    flora.depth  = parsed.depth;
    flora.minX = parsed.minX;
    flora.maxX = parsed.maxX;
    flora.minY = parsed.minY;
    flora.maxY = parsed.maxY;
    flora.minZ = parsed.minZ;
    flora.maxZ = parsed.maxZ;
    flora.familyIds.reserve(parsed.familyIds.size());
    for (const auto fid : parsed.familyIds) {
        flora.familyIds.emplace_back(fid);
    }
    if (parsed.clusterNextType.has_value()) {
        flora.clusterNextType = rfl::Hex<uint32_t>(*parsed.clusterNextType);
    }

    if (parsed.modelTgi.has_value() && thumbnailRenderer_) {
        auto rendered = thumbnailRenderer_->renderModel(*parsed.modelTgi, thumbnailSize_);
        if (rendered.has_value() && !rendered->pixels.empty()) {
            PreRendered preview;
            preview.data = rfl::Bytestring(std::move(rendered->pixels));
            preview.width  = rendered->width;
            preview.height = rendered->height;
            flora.thumbnail = preview;
        }
        else {
            flora.thumbnail = makeRenderFailedThumbnail(thumbnailSize_, &*parsed.modelTgi);
        }
    }
    return flora;
}

std::optional<PropFamilyInfo> ExemplarParser::parsePropFamilyFromCohort(const Exemplar::Record& cohort) const {
    if (!pidBuildingPropFamily_) {
        return std::nullopt;
    }
    const auto* familyProp = findProperty(cohort, *pidBuildingPropFamily_);
    if (!familyProp || familyProp->values.empty()) {
        return std::nullopt;
    }

    const auto familyId = familyProp->GetScalarAs<uint32_t>(0);
    if (!familyId || *familyId == 0) {
        return std::nullopt;
    }

    std::optional<std::string> name;
    if (pidExemplarName_) {
        if (const auto* nameProp = findProperty(cohort, *pidExemplarName_)) {
            name = nameProp->GetScalarAs<std::string>();
        }
    }
    if ((!name || name->empty()) && pidItemName_) {
        if (const auto* itemNameProp = findProperty(cohort, *pidItemName_)) {
            name = itemNameProp->GetScalarAs<std::string>();
        }
    }

    std::string displayName;
    if (name && !name->empty()) {
        displayName = SanitizeString(*name);
    }
    if (displayName.empty()) {
        if (const auto it = kBuiltinPropFamilyNames.find(*familyId); it != kBuiltinPropFamilyNames.end()) {
            displayName = std::string(it->second);
        }
    }
    if (displayName.empty()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Family 0x%08X", *familyId);
        displayName = buf;
    }

    return PropFamilyInfo{
        rfl::Hex<uint32_t>(*familyId),
        std::move(displayName)
    };
}

Building ExemplarParser::buildingFromParsed(const ParsedBuildingExemplar& parsed) const {
    Building building;
    building.instanceId = parsed.tgi.instance;
    building.groupId = parsed.tgi.group;
    building.name = parsed.name;
    building.description = parsed.description;
    building.occupantGroups = std::unordered_set(parsed.occupantGroups.begin(), parsed.occupantGroups.end());
    building.thumbnail = std::nullopt;

    // Load icon if TGI is available and we have index service
    if (parsed.iconTgi.has_value() && indexService_) {
        spdlog::trace("buildingFromParsed: Loading icon for building {} (0x{:08X})",
                      parsed.name, parsed.tgi.instance);
        const auto pngData = indexService_->loadEntryData(*parsed.iconTgi);
        if (pngData.has_value() && !pngData->empty()) {
            spdlog::trace("buildingFromParsed: Got {} bytes of PNG data", pngData->size());
            // Decode PNG to RGBA32 pixel data
            auto decoded = decodePngToRgba32(*pngData);

            if (!decoded.pixels.empty()) {
                spdlog::trace("buildingFromParsed: Decoded to {}x{} RGBA", decoded.width, decoded.height);
                Icon icon;
                icon.data = rfl::Bytestring(std::move(decoded.pixels));
                icon.width = decoded.width;
                icon.height = decoded.height;
                building.thumbnail = icon;
            }
            else {
                spdlog::warn("buildingFromParsed: PNG decode returned empty pixels for {}", parsed.name);
            }
        }
        else {
            spdlog::warn("buildingFromParsed: No PNG data found for icon TGI 0x{:08X}/0x{:08X}/0x{:08X}",
                          parsed.iconTgi->type, parsed.iconTgi->group, parsed.iconTgi->instance);
        }
    }

    if (!building.thumbnail.has_value() && parsed.modelTgi.has_value() && thumbnailRenderer_) {
        auto rendered = thumbnailRenderer_->renderModel(*parsed.modelTgi, thumbnailSize_);
        if (rendered.has_value() && !rendered->pixels.empty()) {
            PreRendered preview;
            preview.data = rfl::Bytestring(std::move(rendered->pixels));
            preview.width = rendered->width;
            preview.height = rendered->height;
            building.thumbnail = preview;
        }
        else {
            spdlog::debug("Thumbnail render failed for building {} ({})",
                          parsed.name, parsed.modelTgi->ToString());
            building.thumbnail = makeRenderFailedThumbnail(thumbnailSize_, &*parsed.modelTgi);
        }
    }

    return building;
}

Lot ExemplarParser::lotFromParsed(const ParsedLotConfigExemplar& parsed) const {
    Lot lot;
    lot.instanceId = parsed.tgi.instance;
    lot.groupId = parsed.tgi.group;
    lot.name = parsed.name;
    lot.sizeX = parsed.lotSize.first;
    lot.sizeZ = parsed.lotSize.second;
    lot.minCapacity = parsed.capacity.has_value() ? parsed.capacity->first : 0;
    lot.maxCapacity = parsed.capacity.has_value() ? parsed.capacity->second : 0;
    lot.growthStage = parsed.growthStage.has_value() ? parsed.growthStage.value() : 0;
    lot.zoneType = parsed.zoneType;
    lot.wealthType = parsed.wealthType;
    lot.purposeType = parsed.purposeType;
    return lot;
}

Prop ExemplarParser::propFromParsed(const ParsedPropExemplar& parsed) const {
    Prop prop;
    prop.instanceId = parsed.tgi.instance;
    prop.groupId = parsed.tgi.group;
    prop.exemplarName = parsed.exemplarName;
    prop.visibleName = parsed.visibleName;
    prop.width = parsed.width;
    prop.height = parsed.height;
    prop.depth = parsed.depth;
    prop.minX = parsed.minX;
    prop.maxX = parsed.maxX;
    prop.minY = parsed.minY;
    prop.maxY = parsed.maxY;
    prop.minZ = parsed.minZ;
    prop.maxZ = parsed.maxZ;
    prop.familyIds.reserve(parsed.familyIds.size());
    for (const auto familyId : parsed.familyIds) {
        prop.familyIds.emplace_back(familyId);
    }
    prop.nighttimeStateChange = parsed.nighttimeStateChange;
    prop.timeOfDay = parsed.timeOfDay;
    prop.simulatorDateStart = parsed.simulatorDateStart;
    prop.simulatorDateDuration = parsed.simulatorDateDuration;
    prop.simulatorDateInterval = parsed.simulatorDateInterval;
    prop.randomChance = parsed.randomChance;

    if (parsed.modelTgi.has_value() && thumbnailRenderer_) {
        auto rendered = thumbnailRenderer_->renderModel(*parsed.modelTgi, thumbnailSize_);
        if (rendered.has_value() && !rendered->pixels.empty()) {
            PreRendered preview;
            preview.data = rfl::Bytestring(std::move(rendered->pixels));
            preview.width = rendered->width;
            preview.height = rendered->height;
            prop.thumbnail = preview;
        }
        else {
            spdlog::debug("Thumbnail render failed for prop {} ({})",
                          parsed.visibleName, parsed.modelTgi->ToString());
            prop.thumbnail = makeRenderFailedThumbnail(thumbnailSize_, &*parsed.modelTgi);
        }
    }
    return prop;
}

const Exemplar::Property* ExemplarParser::findProperty(
    const Exemplar::Record& exemplar,
    const uint32_t propertyId
) const {
    // If we have an index service, use recursive cohort following
    if (indexService_) {
        std::unordered_set<uint32_t> visitedCohorts;
        return findPropertyRecursive(exemplar, propertyId, visitedCohorts);
    }
    // Otherwise, just direct lookup
    return exemplar.FindProperty(propertyId);
}

const Exemplar::Property* ExemplarParser::findPropertyRecursive(
    const Exemplar::Record& exemplar,
    const uint32_t propertyId,
    std::unordered_set<uint32_t>& visitedCohorts
) const {
    // Check the current exemplar for the property
    if (auto* prop = exemplar.FindProperty(propertyId)) {
        return prop;
    }

    // If no index service, can't look up parent cohorts across files
    if (!indexService_) {
        return nullptr;
    }

    // Parent cohort is stored in the exemplar header, not as a property
    // Check if this exemplar has a parent cohort (instance != 0)
    if (exemplar.parent.instance == 0) {
        return nullptr;
    }

    // Prevent infinite loops
    if (visitedCohorts.contains(exemplar.parent.instance)) {
        return nullptr;
    }
    visitedCohorts.insert(exemplar.parent.instance);

    // Use the full parent TGI (type, group, instance) from the exemplar header
    const DBPF::Tgi& parentTgi = exemplar.parent;

    if (indexService_->containsTgi(parentTgi)) {
        // Use the index service's cached loader instead of opening files repeatedly
        auto parentExemplarResult = indexService_->loadExemplar(parentTgi);
        if (parentExemplarResult.has_value()) {
            const Exemplar::Record* parentExemplar = *parentExemplarResult;
            // Recursively search parent
            spdlog::trace("Searching parent cohort 0x{:08X}/0x{:08X}/0x{:08X} for property 0x{:08X}", parentTgi.type,
                          parentTgi.group, parentTgi.instance, propertyId);
            const auto prop = findPropertyRecursive(*parentExemplar, propertyId, visitedCohorts);
            if (prop != nullptr) {
                spdlog::trace("Found property 0x{:08X} in parent cohort: {}", propertyId, prop->ToString());
                return prop;
            }
            else {
                spdlog::trace("Property 0x{:08X} not found in parent cohort", propertyId);
            }
        }
        else {
            spdlog::warn("Failed to load parent cohort 0x{:08X}/0x{:08X}/0x{:08X}: {}",
                         parentTgi.type, parentTgi.group, parentTgi.instance, parentExemplarResult.error().message);
        }
    }

    return nullptr;
}

std::string ExemplarParser::resolveLTextTags_(std::string_view text,
                                              const Exemplar::Record& exemplar) const {
    auto formatValue = [](const Exemplar::Property& prop, char mode) -> std::optional<std::string> {
        if (prop.values.empty()) {
            return std::nullopt;
        }

        return std::visit([mode]<typename T>(const T& value) -> std::optional<std::string> {
            using V = std::decay_t<T>;
            if constexpr (std::is_same_v<V, std::string>) {
                if (mode == 'd') {
                    return value;
                }
                return std::nullopt;
            }
            else if constexpr (std::is_same_v<V, bool>) {
                if (mode == 'd') {
                    return value ? "1" : "0";
                }
                return std::nullopt;
            }
            else if constexpr (std::is_integral_v<V>) {
                if (mode == 'm') {
                    return std::string("§") + std::to_string(static_cast<int64_t>(value));
                }
                return std::to_string(static_cast<int64_t>(value));
            }
            else if constexpr (std::is_same_v<V, float>) {
                if (mode == 'm') {
                    const auto rounded = static_cast<int64_t>(std::llround(value));
                    return std::string("§") + std::to_string(rounded);
                }
                return std::to_string(value);
            }
            else {
                return std::nullopt;
            }
        }, prop.values.front());
    };

    std::string result;
    result.reserve(text.size());

    size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '#') {
            result.push_back(text[i]);
            ++i;
            continue;
        }

        const size_t end = text.find('#', i + 1);
        if (end == std::string_view::npos) {
            result.append(text.substr(i));
            break;
        }

        const auto token = text.substr(i + 1, end - i - 1);
        auto replaced = false;
        if (token.size() > 2 && token[1] == ':' && (token[0] == 'm' || token[0] == 'd')) {
            uint32_t propertyId = 0;
            const auto* begin = token.data() + 2;
            const auto* tokenEnd = token.data() + token.size();
            const auto [ptr, ec] = std::from_chars(begin, tokenEnd, propertyId, 16);
            if (ec == std::errc() && ptr == tokenEnd) {
                if (const auto* prop = findProperty(exemplar, propertyId)) {
                    if (auto formatted = formatValue(*prop, token[0])) {
                        result.append(*formatted);
                        replaced = true;
                    }
                    else {
                        spdlog::warn("LTEXT tag {} for property 0x{:08X} could not be formatted", token, propertyId);
                    }
                }
                else {
                    spdlog::warn("LTEXT tag {} references missing property 0x{:08X}", token, propertyId);
                }
            }
        }

        if (!replaced) {
            result.append(text.substr(i, end - i + 1));
        }
        i = end + 1;
    }

    return result;
}

std::optional<DBPF::Tgi> ExemplarParser::resolveModelTgi_(const Exemplar::Record& exemplar,
                                                          const DBPF::Tgi& exemplarTgi) const {
    constexpr auto kDesiredZoomLevel = 5;
    constexpr auto kDesiredRotation = 0; // South
    constexpr auto kDesiredZoomOffset = static_cast<uint32_t>(kDesiredZoomLevel - 1) * 0x100u;
    constexpr auto kDesiredRotationOffset = static_cast<uint32_t>(kDesiredRotation) * 0x10u;

    auto getU32 = [](const Exemplar::Property* prop, const size_t index) -> std::optional<uint32_t> {
        if (!prop || index >= prop->values.size()) {
            return std::nullopt;
        }
        return prop->GetScalarAs<uint32_t>(index);
    };

    auto tgiFromList = [&](const Exemplar::Property* prop) -> std::optional<DBPF::Tgi> {
        if (!prop || prop->values.size() < 3) {
            return std::nullopt;
        }
        auto type = getU32(prop, 0).value_or(kTypeIdS3D);
        if (type == 0) {
            type = kTypeIdS3D;
        }
        const auto group = getU32(prop, 1);
        const auto instance = getU32(prop, 2);
        if (!group || !instance) {
            return std::nullopt;
        }
        return DBPF::Tgi{type, *group, *instance};
    };

    if (const auto* rkt0 = findProperty(exemplar, kRkt0PropertyId)) {
        spdlog::trace("Found RKT0 for exemplar {}", exemplarTgi.ToString());
        // RKT0 -> One model for all zooms and rotation (True3D)
        if (const auto tgi = tgiFromList(rkt0)) {
            return tgi;
        }
    }

    if (const auto* rkt1 = findProperty(exemplar, kRkt1PropertyId)) {
        spdlog::trace("Found RKT1 for exemplar {}", exemplarTgi.ToString());
        // RKT1 -> The S3D tgi will point towards the Zoom 1, South version of the 20 possible models
        if (auto tgi = tgiFromList(rkt1)) {
            tgi->instance = tgi->instance + kDesiredZoomOffset + kDesiredRotationOffset;
            return tgi;
        }
    }

    if (const auto* rkt5 = findProperty(exemplar, kRkt5PropertyId)) {
        spdlog::trace("Found RKT5 for exemplar {}", exemplarTgi.ToString());
        if (auto tgi = tgiFromList(rkt5)) {
            constexpr uint32_t zoomOffset = static_cast<uint32_t>(kDesiredZoomLevel - 1) * 0x100u;
            constexpr uint32_t rotationOffset = static_cast<uint32_t>(kDesiredRotation) * 0x10u;
            tgi->instance = tgi->instance + zoomOffset + rotationOffset;
            return tgi;
        }
    }

    if (const auto* rkt3 = findProperty(exemplar, kRkt3PropertyId)) {
        spdlog::trace("Found RKT3 for exemplar {}", exemplarTgi.ToString());
        constexpr size_t index = 2 + static_cast<size_t>(kDesiredZoomLevel - 1);
        if (rkt3->values.size() > index) {
            auto type = getU32(rkt3, 0).value_or(kTypeIdS3D);
            if (type == 0) {
                type = kTypeIdS3D;
            }
            const auto group = getU32(rkt3, 1);
            const auto instance = getU32(rkt3, index);
            if (group && instance) {
                return DBPF::Tgi{type, *group, *instance};
            }
        }
    }

    if (const auto* rkt2 = findProperty(exemplar, kRkt2PropertyId)) {
        spdlog::trace("Found RKT2 for exemplar {}", exemplarTgi.ToString());
        constexpr size_t index = 2 + static_cast<size_t>(kDesiredZoomLevel - 1) * 4 +
            static_cast<size_t>(kDesiredRotation);
        if (rkt2->values.size() > index) {
            const auto instance = getU32(rkt2, index);
            if (instance) {
                return DBPF::Tgi{kTypeIdS3D, exemplarTgi.group, *instance};
            }
        }
    }

    if (const auto* rkt4 = findProperty(exemplar, kRkt4PropertyId)) {
        spdlog::trace("Found RKT4 for exemplar {}", exemplarTgi.ToString());
        constexpr size_t blockSize = 8;
        // Try state 0 (normal) first, then state 1 (special/timed prop).
        // Timed props might have a null TGI in state 0 and the real model in state 1
        for (const uint8_t targetState : {0u, 1u}) {
            for (size_t i = 0; i + blockSize - 1 < rkt4->values.size(); i += blockSize) {
                const auto state = getU32(rkt4, i);
                if (!state || *state != targetState) {
                    spdlog::trace("Skipping RKT4 block at index {} with state {} (looking for state {})",
                              i, state ? std::to_string(*state) : "null", targetState);
                    continue;
                }

                // Rep 5 is the Resource Key Type (R): 0x27812820=RKT0, 0x27812821=RKT1.
                // Rep 6-8 are the model's Type, Group and Instance.
                const auto rktValue = getU32(rkt4, i + 4);
                if (!rktValue) {
                    spdlog::trace("Skipping RKT4 block at index {} with invalid RKT value", i);
                    continue;
                }

                const auto type = getU32(rkt4, i + 5).value_or(kTypeIdS3D);
                const auto group = getU32(rkt4, i + 6);
                const auto instance = getU32(rkt4, i + 7);
                if (!group || !instance || !type || (*group==0 && *instance==0)) {
                    spdlog::trace("Skipping RKT4 block at index {} with invalid TGI", i);
                    continue;
                }
                const uint32_t finalType = type;
                const uint32_t finalGroup = *group;
                uint32_t finalInstance = *instance;

                // For RKT1 the stored I is the base (zoom 1, south), we should apply offsets
                // For RKT0 the TGI is already the exact model
                if (!rktValue || *rktValue == kRkt1PropertyId) {
                    finalInstance += kDesiredZoomOffset + kDesiredRotationOffset;
                }

                if (finalType && finalGroup && finalInstance) {
                    const auto finalTgi = DBPF::Tgi{finalType, finalGroup, finalInstance};
                    spdlog::trace("Final TGI for model: {}", finalTgi.ToString());
                    return finalTgi;
                }
            }
        }
    }

    spdlog::warn("Did not manage to resolve model TGI for exemplar {}", exemplarTgi.ToString());
    return std::nullopt;
}

std::optional<std::array<float, 6>> ExemplarParser::loadModelBounds_(const DBPF::Tgi& modelTgi) const {
    if (!indexService_) {
        return std::nullopt;
    }

    auto filePaths = indexService_->lookupFiles(modelTgi);
    if (filePaths.empty()) {
        return std::nullopt;
    }

    for (const auto& filePath : std::ranges::reverse_view(filePaths)) {
        auto* reader = indexService_->getReader(filePath);
        if (!reader) {
            continue;
        }

        auto record = reader->LoadS3D(modelTgi);
        if (!record.has_value()) {
            continue;
        }

        return std::array<float, 6>{
            record->bbMin.x,
            record->bbMax.x,
            record->bbMin.y,
            record->bbMax.y,
            record->bbMin.z,
            record->bbMax.z
        };
    }

    spdlog::warn("Unable to load model bounds for S3D model {}", modelTgi.ToString());
    return std::nullopt;
}

std::vector<std::byte> ExemplarParser::convertBgraToRgba_(const std::vector<std::byte>& pixels) {
    std::vector<std::byte> rgba;
    rgba.resize(pixels.size());
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        rgba[i + 0] = pixels[i + 2];
        rgba[i + 1] = pixels[i + 1];
        rgba[i + 2] = pixels[i + 0];
        rgba[i + 3] = pixels[i + 3];
    }
    return rgba;
}
