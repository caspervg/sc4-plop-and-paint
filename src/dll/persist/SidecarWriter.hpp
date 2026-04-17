#pragma once

#include "DecalSidecarPayload.hpp"

class cIGZOStream;

namespace PlopAndPaint::Sidecar
{
    // Serializes a SidecarDocument to the given stream.
    //
    // Layout:
    //   [u32 magic = 'SP4D']
    //   [u16 versionMajor] [u16 versionMinor]
    //   [u32 flags]
    //   [u32 chunkCount]
    //   chunkCount × {
    //       [u32 tag]
    //       [u32 payloadBytes]
    //       [payloadBytes ...]
    //   }
    //
    // DecalList chunk payload:
    //   [u32 entryCount]
    //   entryCount × {
    //       [u32 entryBytes]        // byte length of the fieldCount + fields block
    //       [u32 fieldCount]
    //       fieldCount × {
    //           [u32 tag]
    //           [u32 valueBytes]
    //           [valueBytes ...]
    //       }
    //   }
    //
    // Returns true iff the stream reported no error after writing. On failure
    // the stream's position is unspecified; the caller should abort/discard
    // the record.
    bool WriteSidecarDocument(cIGZOStream& out, const SidecarDocument& document);
}