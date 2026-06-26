#include "LotTextureStripSidecar.hpp"

#include "cIGZIStream.h"
#include "cIGZOStream.h"
#include "cIGZPersistDBSegment.h"

namespace lottex::sidecar {

namespace {
    struct Header {
        uint32_t magic{kMagic};
        uint16_t versionMajor{kVersionMajor};
        uint16_t versionMinor{kVersionMinor};
        uint32_t flags{0};
        uint32_t chunkCount{1};
    };

    struct ChunkHeader {
        uint32_t tag{kChunkTag};
        uint32_t payloadBytes{0};
        uint32_t recordSize{0};
        uint32_t recordCount{0};
    };

    // On-disk record size: 2 int32 + 2 uint32 + 4 float32 = 8 * 4 bytes.
    constexpr uint32_t kRecordSize = 8 * 4;
}

ReadResult Read(cIGZIStream& in) {
    ReadResult result{};

    Header header{};
    if (!in.GetUint32(header.magic) ||
        !in.GetUint16(header.versionMajor) ||
        !in.GetUint16(header.versionMinor) ||
        !in.GetUint32(header.flags) ||
        !in.GetUint32(header.chunkCount)) {
        result.error = "truncated lot-texture-strip sidecar header";
        return result;
    }
    if (header.magic != kMagic) {
        result.error = "invalid lot-texture-strip sidecar magic";
        return result;
    }
    if (header.versionMajor != kVersionMajor) {
        result.error = "unsupported lot-texture-strip sidecar major version";
        return result;
    }
    if (header.chunkCount != 1) {
        result.error = "unexpected lot-texture-strip chunk count";
        return result;
    }

    ChunkHeader chunk{};
    if (!in.GetUint32(chunk.tag) ||
        !in.GetUint32(chunk.payloadBytes) ||
        !in.GetUint32(chunk.recordSize) ||
        !in.GetUint32(chunk.recordCount)) {
        result.error = "truncated lot-texture-strip chunk header";
        return result;
    }
    if (chunk.tag != kChunkTag) {
        result.error = "unexpected lot-texture-strip chunk tag";
        return result;
    }
    if (chunk.recordSize < kRecordSize ||
        chunk.payloadBytes != chunk.recordCount * chunk.recordSize) {
        result.error = "lot-texture-strip payload size mismatch";
        return result;
    }

    result.records.reserve(chunk.recordCount);
    for (uint32_t i = 0; i < chunk.recordCount; ++i) {
        StripRecord record{};
        if (!in.GetSint32(record.lotCellX) ||
            !in.GetSint32(record.lotCellZ) ||
            !in.GetUint32(record.lotConfigID) ||
            !in.GetUint32(record.normalizedIID) ||
            !in.GetFloat32(record.minX) ||
            !in.GetFloat32(record.minZ) ||
            !in.GetFloat32(record.maxX) ||
            !in.GetFloat32(record.maxZ)) {
            result.error = "truncated lot-texture-strip record";
            result.records.clear();
            return result;
        }
        // Skip any extra bytes a newer record layout may carry.
        for (uint32_t extra = kRecordSize; extra < chunk.recordSize; extra += 4) {
            uint32_t discard = 0;
            if (!in.GetUint32(discard)) {
                result.error = "truncated lot-texture-strip record padding";
                result.records.clear();
                return result;
            }
        }
        result.records.push_back(record);
    }

    result.ok = in.GetError() == 0;
    if (!result.ok && result.error.empty()) {
        result.error = "stream read error";
    }
    return result;
}

bool Write(cIGZOStream& out, const std::vector<StripRecord>& records) {
    const Header header{};
    const ChunkHeader chunk{
        .tag = kChunkTag,
        .payloadBytes = static_cast<uint32_t>(records.size()) * kRecordSize,
        .recordSize = kRecordSize,
        .recordCount = static_cast<uint32_t>(records.size()),
    };

    if (!out.SetUint32(header.magic) ||
        !out.SetUint16(header.versionMajor) ||
        !out.SetUint16(header.versionMinor) ||
        !out.SetUint32(header.flags) ||
        !out.SetUint32(header.chunkCount) ||
        !out.SetUint32(chunk.tag) ||
        !out.SetUint32(chunk.payloadBytes) ||
        !out.SetUint32(chunk.recordSize) ||
        !out.SetUint32(chunk.recordCount)) {
        return false;
    }

    for (const StripRecord& record : records) {
        if (!out.SetSint32(record.lotCellX) ||
            !out.SetSint32(record.lotCellZ) ||
            !out.SetUint32(record.lotConfigID) ||
            !out.SetUint32(record.normalizedIID) ||
            !out.SetFloat32(record.minX) ||
            !out.SetFloat32(record.minZ) ||
            !out.SetFloat32(record.maxX) ||
            !out.SetFloat32(record.maxZ)) {
            return false;
        }
    }

    return out.GetError() == 0;
}

bool DeleteRecord(cIGZPersistDBSegment* const dbSegment) {
    return dbSegment ? dbSegment->DeleteRecord(kKey) : false;
}

}  // namespace lottex::sidecar
