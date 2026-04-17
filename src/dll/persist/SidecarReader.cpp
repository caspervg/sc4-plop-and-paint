#include "SidecarReader.hpp"

#include <cstring>
#include <format>
#include <vector>

class cIGZVariant;

#include "cIGZIStream.h"
#include "SidecarFormat.hpp"

namespace PlopAndPaint::Sidecar
{
    namespace
    {
        // In-memory LE byte cursor for parsing chunk payloads after reading
        // them off the stream. Using a buffer-per-chunk keeps error handling
        // local: if a chunk is malformed we can drop it without poisoning the
        // global stream position.
        class ByteReader
        {
        public:
            ByteReader(const uint8_t* data, const size_t size) noexcept
                : data_(data), size_(size), pos_(0) {}

            [[nodiscard]] size_t Remaining() const noexcept { return size_ - pos_; }
            [[nodiscard]] bool Eof() const noexcept { return pos_ >= size_; }

            bool GetUint8(uint8_t& out) noexcept
            {
                if (Remaining() < 1) return false;
                out = data_[pos_++];
                return true;
            }

            bool GetUint16(uint16_t& out) noexcept { return CopyLE_(&out, sizeof(out)); }
            bool GetUint32(uint32_t& out) noexcept { return CopyLE_(&out, sizeof(out)); }
            bool GetFloat32(float& out) noexcept { return CopyLE_(&out, sizeof(out)); }

            bool GetBytes(void* out, const size_t size) noexcept
            {
                if (Remaining() < size) return false;
                if (size > 0) {
                    std::memcpy(out, data_ + pos_, size);
                }
                pos_ += size;
                return true;
            }

            bool Skip(const size_t size) noexcept
            {
                if (Remaining() < size) return false;
                pos_ += size;
                return true;
            }

        private:
            bool CopyLE_(void* out, const size_t size) noexcept
            {
                if (Remaining() < size) return false;
                std::memcpy(out, data_ + pos_, size);
                pos_ += size;
                return true;
            }

            const uint8_t* data_;
            size_t size_;
            size_t pos_;
        };

        bool ReadChunkPayload_(cIGZIStream& in, const uint32_t size, std::vector<uint8_t>& out)
        {
            out.clear();
            if (size == 0) return true;
            out.resize(size);
            if (!in.GetVoid(out.data(), size)) return false;
            return in.GetError() == 0;
        }

        void ReadFieldInto_(const uint32_t tag,
                            ByteReader& fieldValue,
                            DecalEntry& entry)
        {
            switch (tag) {
            case kFieldWorldPos: {
                WorldPos pos{};
                if (fieldValue.GetFloat32(pos.x)
                    && fieldValue.GetFloat32(pos.y)
                    && fieldValue.GetFloat32(pos.z)) {
                    entry.worldPos = pos;
                }
                break;
            }
            case kFieldTextureKey: {
                TextureKey key{};
                if (fieldValue.GetUint32(key.type)
                    && fieldValue.GetUint32(key.group)
                    && fieldValue.GetUint32(key.instance)) {
                    entry.textureKey = key;
                }
                break;
            }
            case kFieldUvSubrect: {
                UvSubrect uv{};
                uint32_t modeRaw = 0;
                if (fieldValue.GetFloat32(uv.u1)
                    && fieldValue.GetFloat32(uv.v1)
                    && fieldValue.GetFloat32(uv.u2)
                    && fieldValue.GetFloat32(uv.v2)
                    && fieldValue.GetUint32(modeRaw)) {
                    uv.mode = (modeRaw == static_cast<uint32_t>(UvMode::ClipSubrect))
                                  ? UvMode::ClipSubrect
                                  : UvMode::StretchSubrect;
                    entry.uvSubrect = uv;
                }
                break;
            }
            case kFieldDecalInfo: {
                DecalInfoSnapshot info{};
                if (fieldValue.GetFloat32(info.centerU)
                    && fieldValue.GetFloat32(info.centerV)
                    && fieldValue.GetFloat32(info.radiusU)
                    && fieldValue.GetFloat32(info.radiusV)
                    && fieldValue.GetFloat32(info.rotationRadians)
                    && fieldValue.GetFloat32(info.scaleX)
                    && fieldValue.GetFloat32(info.scaleY)) {
                    entry.decalInfo = info;
                }
                break;
            }
            case kFieldOpacity: {
                float opacity = 0.0f;
                if (fieldValue.GetFloat32(opacity)) {
                    entry.opacity = opacity;
                }
                break;
            }
            case kFieldUserFlags: {
                uint32_t flags = 0;
                if (fieldValue.GetUint32(flags)) {
                    entry.userFlags = flags;
                }
                break;
            }
            default: {
                // Preserve the raw value bytes so they round-trip through this DLL.
                std::vector<uint8_t> raw(fieldValue.Remaining());
                if (!raw.empty()) {
                    fieldValue.GetBytes(raw.data(), raw.size());
                }
                entry.unknownFields.push_back(UnknownField{tag, std::move(raw)});
                break;
            }
            }
        }

        bool ParseEntry_(ByteReader& entryBody, DecalEntry& entry, std::string& error)
        {
            uint32_t fieldCount = 0;
            if (!entryBody.GetUint32(fieldCount)) {
                error = "truncated entry (field count)";
                return false;
            }
            if (fieldCount > kMaxFieldsPerEntry) {
                error = std::format("entry field count {} exceeds cap {}", fieldCount, kMaxFieldsPerEntry);
                return false;
            }

            for (uint32_t i = 0; i < fieldCount; ++i) {
                uint32_t tag = 0;
                uint32_t valueBytes = 0;
                if (!entryBody.GetUint32(tag) || !entryBody.GetUint32(valueBytes)) {
                    error = "truncated field header";
                    return false;
                }
                if (valueBytes > kMaxFieldBytes) {
                    error = std::format("field {:08X} value bytes {} exceeds cap {}", tag, valueBytes, kMaxFieldBytes);
                    return false;
                }
                if (entryBody.Remaining() < valueBytes) {
                    error = std::format("field {:08X} value bytes {} exceeds remaining {}",
                                        tag, valueBytes, entryBody.Remaining());
                    return false;
                }

                // Carve out a sub-view for the value; makes per-field parsing
                // crash-safe even if a known field is silently truncated.
                std::vector<uint8_t> buf(valueBytes);
                if (valueBytes > 0) {
                    entryBody.GetBytes(buf.data(), valueBytes);
                }
                ByteReader fieldValue(buf.data(), buf.size());
                ReadFieldInto_(tag, fieldValue, entry);
            }

            return true;
        }

        bool ParseDecalList_(const std::vector<uint8_t>& payload,
                             SidecarDocument& doc,
                             std::string& error)
        {
            ByteReader chunk(payload.data(), payload.size());

            uint32_t entryCount = 0;
            if (!chunk.GetUint32(entryCount)) {
                error = "truncated DECL chunk (entry count)";
                return false;
            }
            if (entryCount > kMaxEntriesPerChunk) {
                error = std::format("DECL entry count {} exceeds cap {}", entryCount, kMaxEntriesPerChunk);
                return false;
            }

            doc.decals.reserve(doc.decals.size() + entryCount);
            for (uint32_t i = 0; i < entryCount; ++i) {
                uint32_t entryBytes = 0;
                if (!chunk.GetUint32(entryBytes)) {
                    error = std::format("DECL truncated at entry {} header", i);
                    return false;
                }
                if (entryBytes > kMaxEntryBytes) {
                    error = std::format("DECL entry {} size {} exceeds cap {}", i, entryBytes, kMaxEntryBytes);
                    return false;
                }
                if (chunk.Remaining() < entryBytes) {
                    error = std::format("DECL entry {} bytes {} exceed remaining {}",
                                        i, entryBytes, chunk.Remaining());
                    return false;
                }

                std::vector<uint8_t> entryBuf(entryBytes);
                if (entryBytes > 0) {
                    chunk.GetBytes(entryBuf.data(), entryBytes);
                }
                ByteReader entryReader(entryBuf.data(), entryBuf.size());

                DecalEntry entry;
                if (!ParseEntry_(entryReader, entry, error)) {

                    error = std::format("DECL entry {}: {}", i, error);
                    return false;
                }
                doc.decals.push_back(std::move(entry));
            }

            return true;
        }
    }

    ReadResult ReadSidecarDocument(cIGZIStream& in, SidecarDocument& out)
    {
        out = SidecarDocument{};

        uint32_t magic = 0;
        if (!in.GetUint32(magic) || in.GetError() != 0) {
            return {false, "failed to read magic"};
        }
        if (magic != kSidecarMagic) {
            return {false, std::format("bad magic: 0x{:08X}", magic)};
        }

        uint16_t major = 0;
        uint16_t minor = 0;
        uint32_t flags = 0;
        uint32_t chunkCount = 0;
        if (!in.GetUint16(major) || !in.GetUint16(minor) || !in.GetUint32(flags) || !in.GetUint32(chunkCount)
            || in.GetError() != 0) {
            return {false, "failed to read header"};
        }
        if (chunkCount > kMaxChunks) {
            return {false, std::format("chunk count {} exceeds cap {}", chunkCount, kMaxChunks)};
        }

        out.versionMajor = major;
        out.versionMinor = minor;
        out.flags = flags;

        for (uint32_t i = 0; i < chunkCount; ++i) {
            uint32_t tag = 0;
            uint32_t size = 0;
            if (!in.GetUint32(tag) || !in.GetUint32(size) || in.GetError() != 0) {
                return {false, std::format("failed to read chunk {} header", i)};
            }
            if (size > kMaxChunkBytes) {
                return {false, std::format("chunk {} size {} exceeds cap {}", i, size, kMaxChunkBytes)};
            }

            std::vector<uint8_t> payload;
            if (!ReadChunkPayload_(in, size, payload)) {
                return {false, std::format("failed to read chunk {} payload ({} bytes)", i, size)};
            }

            if (tag == kChunkDecalList) {
                std::string err;
                if (!ParseDecalList_(payload, out, err)) {
                    return {false, std::move(err)};
                }
            }
            else {
                out.unknownChunks.push_back(UnknownChunk{tag, std::move(payload)});
            }
        }

        return {true, {}};
    }
}
