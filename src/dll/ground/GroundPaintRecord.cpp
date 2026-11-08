#include "GroundPaintRecord.hpp"

#include "cGZPersistResourceKey.h"
#include "cIGZPersistDBSegment.h"
#include "cISC4DBSegment.h"
#include "cISC4DBSegmentIStream.h"
#include "cISC4DBSegmentOStream.h"
#include "../utils/Logger.h"

namespace Ground::SaveRecord {
    namespace {
        constexpr uint32_t FourCC(const char a, const char b, const char c, const char d) {
            return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
        }

        constexpr uint32_t kMagic = FourCC('P', 'N', 'P', 'G');
        constexpr uint32_t kVersion = 1;
        constexpr uint32_t kMaxPayloadBytes = 64u * 1024u * 1024u;
        // Type shared with Plop and Paint's other save records, if any come; group and instance
        // name the ground paint.
        const cGZPersistResourceKey kRecordKey(0x8F1E6D6Au, FourCC('P', 'N', 'P', 'G'), 0x00000001u);
    }

    bool Write(cISC4DBSegment& segment, const std::span<const char> payload) {
        if (payload.empty()) {
            if (cIGZPersistDBSegment* const db = segment.AsIGZPersistDBSegment()) {
                db->DeleteRecord(kRecordKey);
            }
            return true;
        }

        cISC4DBSegmentOStream* stream = nullptr;
        if (!segment.OpenOStream(kRecordKey, &stream, true) || !stream) {
            LOG_WARN("Ground paint: could not open the save record for writing");
            return false;
        }
        const bool ok = stream->SetUint32(kMagic) && stream->SetUint32(kVersion) &&
                        stream->SetUint32(static_cast<uint32_t>(payload.size())) &&
                        stream->SetVoid(payload.data(), static_cast<uint32_t>(payload.size())) &&
                        stream->GetError() == 0;
        segment.CloseOStream(stream);
        if (!ok) {
            LOG_WARN("Ground paint: writing the save record failed");
        }
        return ok;
    }

    std::optional<std::vector<char>> Read(cISC4DBSegment& segment) {
        cISC4DBSegmentIStream* stream = nullptr;
        if (!segment.OpenIStream(kRecordKey, &stream) || !stream) {
            return std::nullopt;
        }

        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t size = 0;
        std::vector<char> payload;
        bool ok = stream->GetUint32(magic) && stream->GetUint32(version) && stream->GetUint32(size) &&
                  magic == kMagic && version == kVersion && size <= kMaxPayloadBytes;
        if (ok) {
            payload.resize(size);
            ok = size == 0 || stream->GetVoid(payload.data(), size);
        }
        segment.CloseIStream(stream);

        if (!ok) {
            LOG_WARN("Ground paint: the save record is unreadable (magic {:08X}, version {}, {} bytes)", magic, version, size);
            return std::nullopt;
        }
        return payload;
    }
}
