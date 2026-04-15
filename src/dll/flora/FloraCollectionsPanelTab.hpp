#pragma once

#include <string>
#include <vector>

#include "../common/PanelTab.hpp"
#include "../paint/PaintSettings.hpp"
#include "../thumbnail/ThumbnailCache.hpp"
#include "FloraRepository.hpp"
#include "public/ImGuiTexture.h"

class FloraCollectionsPanelTab : public PanelTab {
public:
    FloraCollectionsPanelTab(SC4PlopAndPaintDirector* director,
                             FloraRepository* flora,
                             FavoritesRepository* favorites,
                             cIGZImGuiService* imguiService);

    ~FloraCollectionsPanelTab() override = default;

    [[nodiscard]] const char* GetTabName() const override;
    void OnRender() override;
    void OnShutdown() override { thumbnailCache_.Clear(); }
    void OnDeviceReset(uint32_t deviceGeneration) override;

private:
    struct PendingPaintState {
        uint32_t typeId{0};
        RecentPaintEntry::SourceKind sourceKind{RecentPaintEntry::SourceKind::FloraFamily};
        uint64_t sourceId{0};
        std::string name;
        std::string detail;
        PropPaintSettings settings{};
        bool open{false};
    };

    [[nodiscard]] ImGuiTexture LoadFloraTexture_(uint64_t key) const;
    void RenderFilterUI_();
    void BuildFilteredCollectionIndices_(const std::vector<FloraRepository::FloraCollection>& collections,
                                         std::vector<size_t>& filteredIndices) const;
    void RenderCollectionsTable_(const std::vector<FloraRepository::FloraCollection>& collections,
                                 const std::vector<size_t>& filteredIndices);
    void RenderSelectedCollectionPanel_(const std::vector<FloraRepository::FloraCollection>& collections);
    void RenderPaintModal_();
    void QueuePaintForCollection_(const FloraRepository::FloraCollection& collection);
    void RenderFavoriteButton_(const Flora& flora, const char* idSuffix = "") const;
    [[nodiscard]] bool RenderFloraPills_(const Flora& flora, bool startOnNewLine) const;
    [[nodiscard]] const FloraRepository::FloraCollection* GetSelectedCollection_(
        const std::vector<FloraRepository::FloraCollection>& collections) const;
    [[nodiscard]] uint64_t BuildCollectionKey_(FloraRepository::CollectionType type, uint32_t id) const;

    FloraRepository* flora_;
    ThumbnailCache<uint64_t> thumbnailCache_{};
    uint32_t lastDeviceGeneration_{0};
    uint64_t selectedCollectionKey_{0};

    char nameFilterBuf_[256]{};
    char iidFilterBuf_[64]{};
    int typeFilter_{0};
    bool favoritesOnly_{false};

    PendingPaintState pendingPaint_{};
};
