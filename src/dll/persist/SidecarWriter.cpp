#include "SidecarWriter.hpp"

#include <cstring>
#include <vector>

class cIGZVariant;

#include "cIGZOStream.h"
#include "SidecarFormat.hpp"

namespace PlopAndPaint::Sidecar
{
    namespace
    {
        // Tiny little-endian byte sink used to measure chunk / entry sizes
        // before emitting them to the real stream. Keeps everything host-order
        // since SC4 is always LE x86.
        class ByteSink
        {
        public:
            void PutUint8(const uint8_t value) { buffer_.push_back(value); }
            void PutUint16(const uint16_t value) { Raw(&value, sizeof(value)); }
            void PutUint32(const uint32_t value) { Raw(&value, sizeof(value)); }
            void PutFloat32(const float value) { Raw(&value, sizeof(value)); }

            void PutBytes(const void* data, const size_t size)
            {
                if (data && size > 0) {
                    Raw(data, size);
                }
            }

            [[nodiscard]] const std::vector<uint8_t>& Buffer() const noexcept { return buffer_; }
            [[nodiscard]] uint32_t Size() const noexcept { return static_cast<uint32_t>(buffer_.size()); }

        private:
            void Raw(const void* data, const size_t size)
            {
                const auto* bytes = static_cast<const uint8_t*>(data);
                buffer_.insert(buffer_.end(), bytes, bytes + size);
            }

            std::vector<uint8_t> buffer_;
        };

        void WriteField_(ByteSink& sink, const uint32_t tag, const void* value, const uint32_t valueBytes)
        {
            sink.PutUint32(tag);
            sink.PutUint32(valueBytes);
            sink.PutBytes(value, valueBytes);
        }

        void WriteWorldPos_(ByteSink& sink, const WorldPos& pos)
        {
            ByteSink tmp;
            tmp.PutFloat32(pos.x);
            tmp.PutFloat32(pos.y);
            tmp.PutFloat32(pos.z);
            WriteField_(sink, kFieldWorldPos, tmp.Buffer().data(), tmp.Size());
        }

        void WriteTextureKey_(ByteSink& sink, const TextureKey& key)
        {
            ByteSink tmp;
            tmp.PutUint32(key.type);
            tmp.PutUint32(key.group);
            tmp.PutUint32(key.instance);
            WriteField_(sink, kFieldTextureKey, tmp.Buffer().data(), tmp.Size());
        }

        void WriteUvSubrect_(ByteSink& sink, const UvSubrect& uv)
        {
            ByteSink tmp;
            tmp.PutFloat32(uv.u1);
            tmp.PutFloat32(uv.v1);
            tmp.PutFloat32(uv.u2);
            tmp.PutFloat32(uv.v2);
            tmp.PutUint32(static_cast<uint32_t>(uv.mode));
            WriteField_(sink, kFieldUvSubrect, tmp.Buffer().data(), tmp.Size());
        }

        void WriteDecalInfo_(ByteSink& sink, const DecalInfoSnapshot& info)
        {
            ByteSink tmp;
            tmp.PutFloat32(info.centerU);
            tmp.PutFloat32(info.centerV);
            tmp.PutFloat32(info.radiusU);
            tmp.PutFloat32(info.radiusV);
            tmp.PutFloat32(info.rotationRadians);
            tmp.PutFloat32(info.scaleX);
            tmp.PutFloat32(info.scaleY);
            WriteField_(sink, kFieldDecalInfo, tmp.Buffer().data(), tmp.Size());
        }

        uint32_t CountKnownFields_(const DecalEntry& entry)
        {
            uint32_t count = 1; // worldPos is always present
            if (entry.textureKey) ++count;
            if (entry.uvSubrect) ++count;
            if (entry.decalInfo) ++count;
            if (entry.opacity) ++count;
            if (entry.userFlags) ++count;
            return count;
        }

        void WriteEntry_(ByteSink& parent, const DecalEntry& entry)
        {
            ByteSink body;

            const uint32_t knownFieldCount = CountKnownFields_(entry);
            const uint32_t unknownFieldCount = static_cast<uint32_t>(entry.unknownFields.size());
            body.PutUint32(knownFieldCount + unknownFieldCount);

            WriteWorldPos_(body, entry.worldPos);

            if (entry.textureKey) {
                WriteTextureKey_(body, *entry.textureKey);
            }
            if (entry.uvSubrect) {
                WriteUvSubrect_(body, *entry.uvSubrect);
            }
            if (entry.decalInfo) {
                WriteDecalInfo_(body, *entry.decalInfo);
            }
            if (entry.opacity) {
                ByteSink tmp;
                tmp.PutFloat32(*entry.opacity);
                WriteField_(body, kFieldOpacity, tmp.Buffer().data(), tmp.Size());
            }
            if (entry.userFlags) {
                ByteSink tmp;
                tmp.PutUint32(*entry.userFlags);
                WriteField_(body, kFieldUserFlags, tmp.Buffer().data(), tmp.Size());
            }

            for (const auto& unknown : entry.unknownFields) {
                WriteField_(body,
                            unknown.tag,
                            unknown.bytes.empty() ? nullptr : unknown.bytes.data(),
                            static_cast<uint32_t>(unknown.bytes.size()));
            }

            parent.PutUint32(body.Size());
            parent.PutBytes(body.Buffer().data(), body.Size());
        }

        void BuildDecalListChunk_(ByteSink& chunk, const SidecarDocument& doc)
        {
            chunk.PutUint32(static_cast<uint32_t>(doc.decals.size()));
            for (const auto& entry : doc.decals) {
                WriteEntry_(chunk, entry);
            }
        }
    }

    bool WriteSidecarDocument(cIGZOStream& out, const SidecarDocument& document)
    {
        if (!out.SetUint32(kSidecarMagic)) return false;
        if (!out.SetUint16(document.versionMajor != 0 ? document.versionMajor : kSidecarVersionMajor)) return false;
        if (!out.SetUint16(document.versionMinor)) return false;
        if (!out.SetUint32(document.flags)) return false;

        const uint32_t knownChunkCount = 1; // kChunkDecalList
        const uint32_t unknownChunkCount = static_cast<uint32_t>(document.unknownChunks.size());
        if (!out.SetUint32(knownChunkCount + unknownChunkCount)) return false;

        {
            ByteSink chunk;
            BuildDecalListChunk_(chunk, document);
            if (!out.SetUint32(kChunkDecalList)) return false;
            if (!out.SetUint32(chunk.Size())) return false;
            if (chunk.Size() > 0 && !out.SetVoid(chunk.Buffer().data(), chunk.Size())) return false;
        }

        for (const auto& unknown : document.unknownChunks) {
            const uint32_t payloadSize = static_cast<uint32_t>(unknown.bytes.size());
            if (!out.SetUint32(unknown.tag)) return false;
            if (!out.SetUint32(payloadSize)) return false;
            if (payloadSize > 0 && !out.SetVoid(unknown.bytes.data(), payloadSize)) return false;
        }

        return out.GetError() == 0;
    }
}
