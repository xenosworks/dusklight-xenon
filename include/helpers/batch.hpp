#pragma once

#include <dolphin/gx/GXEnum.h>
#include <dolphin/types.h>

namespace batch {

struct LeafTemplate {
    static constexpr u32 kMaxVtx = 192;
    static constexpr u32 kMaxPosRefs = 64;

    struct Vtx {
        u8 pos;
        u8 nrm;
        u8 clr;
        u8 tex;
    };
    // GX_QUADS when the display list's strips make no more vertices as quads: a batch of quads
    // takes Aurora's prebuilt indices, where a new triangle list has its own built. Else
    // GX_TRIANGLES.
    GXPrimitive prim = GX_TRIANGLES;
    Vtx vtx[kMaxVtx];
    u16 vtxCount = 0;
    u8 posRefs[kMaxPosRefs];
    u8 posRefCount = 0;
};

void decode_leaf_template(const u8* dl, u32 size, LeafTemplate& out);

}  // namespace batch
