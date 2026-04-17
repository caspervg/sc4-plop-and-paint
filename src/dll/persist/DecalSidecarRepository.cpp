#include "DecalSidecarRepository.hpp"

#include <bit>
#include <utility>

#include "terrain/ClippedTerrainDecalRenderer.hpp"

namespace PlopAndPaint::Sidecar
{
    DecalSidecarRepository::PosKey DecalSidecarRepository::MakeKey_(const WorldPos& pos) noexcept
    {
        return PosKey{
            std::bit_cast<uint32_t>(pos.x),
            std::bit_cast<uint32_t>(pos.y),
            std::bit_cast<uint32_t>(pos.z),
        };
    }

    void DecalSidecarRepository::Clear() noexcept
    {
        entries_.clear();
        unknownChunks_.clear();
        loadedVersionMajor_ = 0;
        loadedVersionMinor_ = 0;
        loadedFlags_ = 0;
    }

    void DecalSidecarRepository::LoadFromDocument(SidecarDocument document)
    {
        entries_.clear();
        entries_.reserve(document.decals.size());
        for (auto& entry : document.decals) {
            const auto key = MakeKey_(entry.worldPos);
            entries_.insert_or_assign(key, std::move(entry));
        }
        unknownChunks_ = std::move(document.unknownChunks);
        loadedVersionMajor_ = document.versionMajor;
        loadedVersionMinor_ = document.versionMinor;
        loadedFlags_ = document.flags;
    }

    SidecarDocument DecalSidecarRepository::BuildDocument() const
    {
        SidecarDocument doc;
        doc.versionMajor = loadedVersionMajor_ != 0 ? loadedVersionMajor_ : 0;
        doc.versionMinor = loadedVersionMinor_;
        doc.flags = loadedFlags_;
        doc.decals = SnapshotEntries();
        doc.unknownChunks = unknownChunks_;
        return doc;
    }

    void DecalSidecarRepository::SetEntry(const DecalEntry& entry)
    {
        const auto key = MakeKey_(entry.worldPos);
        entries_.insert_or_assign(key, entry);
    }

    bool DecalSidecarRepository::RemoveAt(const WorldPos& pos) noexcept
    {
        return entries_.erase(MakeKey_(pos)) > 0;
    }

    std::optional<DecalEntry> DecalSidecarRepository::FindByPos(const WorldPos& pos) const
    {
        const auto it = entries_.find(MakeKey_(pos));
        if (it == entries_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::vector<DecalEntry> DecalSidecarRepository::SnapshotEntries() const
    {
        std::vector<DecalEntry> out;
        out.reserve(entries_.size());
        for (const auto& [_, entry] : entries_) {
            out.push_back(entry);
        }
        return out;
    }

    UvSubrect DecalSidecarRepository::FromRenderer(const TerrainDecal::OverlayUvSubrect& src) noexcept
    {
        UvSubrect out;
        out.u1 = src.u1;
        out.v1 = src.v1;
        out.u2 = src.u2;
        out.v2 = src.v2;
        out.mode = (src.mode == TerrainDecal::OverlayUvMode::ClipSubrect)
                       ? UvMode::ClipSubrect
                       : UvMode::StretchSubrect;
        return out;
    }

    TerrainDecal::OverlayUvSubrect DecalSidecarRepository::ToRenderer(const UvSubrect& src) noexcept
    {
        TerrainDecal::OverlayUvSubrect out;
        out.u1 = src.u1;
        out.v1 = src.v1;
        out.u2 = src.u2;
        out.v2 = src.v2;
        out.mode = (src.mode == UvMode::ClipSubrect)
                       ? TerrainDecal::OverlayUvMode::ClipSubrect
                       : TerrainDecal::OverlayUvMode::StretchSubrect;
        return out;
    }
}