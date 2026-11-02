#include "helpers/batch.hpp"

#include "dusk/logging.h"

#include <aurora/dl.hpp>
#include <dolphin/gx/GXEnum.h>

#include <algorithm>

namespace batch {

static LeafTemplate::Vtx vertex_at(const aurora::gx::dl::DrawCmd& draw, u16 elem) {
    LeafTemplate::Vtx v;
    v.pos = draw.attr_idx(elem, GX_VA_POS);
    v.nrm = draw.attr_idx(elem, GX_VA_NRM);
    v.clr = draw.attr_idx(elem, GX_VA_CLR0);
    v.tex = draw.attr_idx(elem, GX_VA_TEX0);
    return v;
}

void decode_leaf_template(const u8* dl, u32 size, LeafTemplate& out) {
    out.prim = GX_TRIANGLES;
    out.vtxCount = 0;
    out.posRefCount = 0;
    bool posSeen[256] = {};
    // The strips again as quads, which Aurora splits (q0, q1, q2) and (q2, q3, q0)
    // (gx/command_processor.cpp, prepare_idx_buffer).
    LeafTemplate::Vtx quads[LeafTemplate::kMaxVtx];
    u16 quadCount = 0;
    bool quadsFit = true;

    static constexpr GXVtxDescList kLeafDesc[] = {
        {GX_VA_POS, GX_INDEX8},
        {GX_VA_NRM, GX_INDEX8},
        {GX_VA_CLR0, GX_INDEX8},
        {GX_VA_TEX0, GX_INDEX8},
        {GX_VA_NULL, GX_NONE},
    };

    aurora::gx::dl::Reader reader{dl, size, kLeafDesc};
    while (const auto cmd = reader.next()) {
        if (cmd->kind == aurora::gx::dl::Command::Kind::Passthrough) {
            if (cmd->data[0] != GX_NOP) {
                DuskLog.fatal("decode_leaf_template: unexpected opcode {:#x}", cmd->data[0]);
            }
            continue;
        }
        if (cmd->kind != aurora::gx::dl::Command::Kind::Draw) {
            DuskLog.fatal("decode_leaf_template: unexpected pre-optimized draw");
        }

        const auto& draw = cmd->draw;
        bool overflow = false;
        const bool expanded =
            aurora::gx::dl::expand_triangles(draw.prim, draw.vtxCount, [&](u16 i0, u16 i1, u16 i2) {
                if (overflow || out.vtxCount + 3 > LeafTemplate::kMaxVtx) {
                    overflow = true;
                    return;
                }
                for (const u16 elem : {i0, i1, i2}) {
                    LeafTemplate::Vtx& v = out.vtx[out.vtxCount++];
                    v = vertex_at(draw, elem);
                    if (!posSeen[v.pos]) {
                        posSeen[v.pos] = true;
                        if (out.posRefCount >= LeafTemplate::kMaxPosRefs) {
                            overflow = true;
                            return;
                        }
                        out.posRefs[out.posRefCount++] = v.pos;
                    }
                }
            });
        if (!expanded) {
            DuskLog.fatal("decode_leaf_template: untriangulable draw (prim {:#x}, {} verts)",
                static_cast<u32>(draw.prim), draw.vtxCount);
        }
        if (overflow) {
            DuskLog.fatal("decode_leaf_template: template overflow ({} verts, {} positions)",
                out.vtxCount, out.posRefCount);
        }

        quadsFit = quadsFit && draw.prim == GX_TRIANGLESTRIP;
        for (u16 v = 2; quadsFit && v < draw.vtxCount; v += 2) {
            if (quadCount + 4 > LeafTemplate::kMaxVtx) {
                quadsFit = false;
                break;
            }
            // expand_triangles' pair (a, b, v) and (v, b, v + 1) is the quad (v, a, b, v + 1); a
            // strip's last odd triangle, the quad (a, b, v, v), whose second triangle has no area.
            const u16 a = static_cast<u16>(v - 2);
            const u16 b = static_cast<u16>(v - 1);
            const bool pair = v + 1 < draw.vtxCount;
            const u16 corners[4] = {
                pair ? v : a, pair ? a : b, pair ? b : v, pair ? static_cast<u16>(v + 1) : v};
            for (const u16 elem : corners) {
                quads[quadCount++] = vertex_at(draw, elem);
            }
        }
    }
    if (reader.failed()) {
        DuskLog.fatal("decode_leaf_template: failed to walk display list");
    }
    if (quadsFit && quadCount <= out.vtxCount) {
        std::copy_n(quads, quadCount, out.vtx);
        out.vtxCount = quadCount;
        out.prim = GX_QUADS;
    }
}

}  // namespace batch
