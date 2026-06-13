#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "DBPFReader.h"
#include "DbpfIndexService.hpp"
#include "ParseTypes.h"

namespace thumb {
    struct LoadedModelHandle;
    class ModelFactory;

    struct RenderedImage {
        std::vector<std::byte> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    class ThumbnailRenderer {
    public:
        explicit ThumbnailRenderer(const DbpfIndexService& indexService);
        ~ThumbnailRenderer();

        ParseExpected<RenderedImage> renderModel(const DBPF::Tgi& tgi, uint32_t size);

    private:
        bool ensureInitialized_();
        ParseExpected<std::shared_ptr<LoadedModelHandle>> loadModel_(const DBPF::Tgi& tgi);
        std::optional<FSH::Record> loadTexture_(uint32_t inst, uint32_t group) const;

        const DbpfIndexService& indexService_;
        std::shared_ptr<ModelFactory> modelFactory_;
        std::unordered_map<DBPF::Tgi, std::shared_ptr<LoadedModelHandle>, DBPF::TgiHash> modelCache_;
        // Failed model TGIs mapped to the failure reason, so repeated lookups stay cheap
        // and report the original cause.
        std::unordered_map<DBPF::Tgi, std::string, DBPF::TgiHash> failedModels_;
        bool initialized_ = false;
    };
} // namespace thumb
