#pragma once

#include <string>
#include <vector>

#include "../common/PanelTab.hpp"
#include "../packs/PackFilter.hpp"
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
    void OnShutdown() override {
        thumbnailCache_.Clear();
        previewCache_.Clear();
    }
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
    void IndexPacks_(const std::vector<FloraRepository::FloraCollection>& collections);
    void RenderFilterUI_();
    void RenderResultLine_(size_t shown, size_t total);
    void ClearFilters_();
    void BuildFilteredCollectionIndices_(const std::vector<FloraRepository::FloraCollection>& collections,
                                         std::vector<size_t>& filteredIndices) const;
    void RenderCollectionsTable_(const std::vector<FloraRepository::FloraCollection>& collections,
                                 const std::vector<size_t>& filteredIndices, float height);
    void RenderMemberTable_(const FloraRepository::FloraCollection& collection, float height);
    void RenderSelectedCollectionPanel_(const std::vector<FloraRepository::FloraCollection>& collections);
    void RenderPaintModal_();
    void QueuePaintForCollection_(const FloraRepository::FloraCollection& collection);
    void RenderFavoriteButton_(const Flora& flora) const;
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
    PackFilter packFilter_{};
    // By collection index, for packFilter_.
    std::vector<std::vector<PackCatalogRepository::ExemplarRow>> collectionPacks_{};
    size_t indexedPackCount_{SIZE_MAX};
    // By collection index.
    std::vector<std::string> collectionTexts_{};
    TextSearch::Suggester suggester_{};

    PendingPaintState pendingPaint_{};
};
