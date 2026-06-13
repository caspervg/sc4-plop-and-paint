#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "../decals/DecalTextureLoader.hpp"
#include "ScenePickStrategy.hpp"
#include "public/cIGZTerrainDecalService.h"

class DecalRepository;

class DecalPickStrategy final : public ScenePickStrategy {
public:
    // `decalRepository` may be null; it is used to verify that picked lot
    // texture IDs resolve to a known zoom-4 texture.
    DecalPickStrategy(cIGZTerrainDecalService* decalService, const DecalRepository* decalRepository);
    ~DecalPickStrategy() override;

    [[nodiscard]] ScenePickMode Mode() const override;
    [[nodiscard]] float PickRadiusMeters() const override;
    [[nodiscard]] std::optional<ScenePickResult> Pick(const ScenePickContext& context) override;
    void SetHover(const std::optional<ScenePickResult>& result) override;
    void ClearHover() override;

    [[nodiscard]] uint32_t CandidateCount() const override;
    [[nodiscard]] uint32_t CandidateIndex() const override;
    void CycleCandidates(int32_t delta) override;

private:
    // One entry per pickable thing under the cursor, plus a stable key used to
    // reset the cycle offset when the stack changes.
    struct Candidate {
        PickedDecal picked{};
        uint64_t key{0};
    };

    [[nodiscard]] std::vector<Candidate> CollectCandidates_(const ScenePickContext& context) const;
    void AppendDecalCandidates_(const ScenePickContext& context, std::vector<Candidate>& candidates) const;
    void AppendLotTextureCandidates_(const ScenePickContext& context, std::vector<Candidate>& candidates) const;
    void SetHoveredDecal_(TerrainDecalId id);
    [[nodiscard]] uint32_t ResolveTextureInstance_(uint32_t rawInstanceId) const;
    [[nodiscard]] decals::TextureKind ClassifyTexture_(uint32_t instanceId) const;

    cIGZTerrainDecalService* decalService_{nullptr};
    const DecalRepository* decalRepository_{nullptr};
    // FSH alpha classification is expensive; memoize per texture instance.
    mutable std::unordered_map<uint32_t, decals::TextureKind> textureKindCache_{};
    TerrainDecalId hoveredDecalId_{};
    bool hasHoveredDecal_{false};
    uint8_t hoveredOriginalDrawMode_{0};

    uint32_t candidateCount_{0};
    int32_t cycleOffset_{0};
    std::vector<uint64_t> lastCandidateKeys_{};
};
