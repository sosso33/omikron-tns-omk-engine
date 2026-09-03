#include "formats/mesh3do.h"
#include "formats/scx.h"
#include "platform/datafs.h"
#include <cstdio>
#include <cmath>
int main(int argc, char** argv) {
    const omk::DataFs fs(argv[1]);
    const auto d = fs.read(argv[2]);
    const auto st = omk::readScxStream(d);
    for (std::size_t i = 0; i < st.sprites.size(); ++i) {
        const auto& sp = st.sprites[i];
        if (!sp.model || sp.offset + sp.model > d.size()) continue;
        const std::span<const std::byte> mo(d.data() + sp.offset, sp.model);
        const auto h = omk::readHeader(mo);
        if (!h) continue;
        const auto vs = omk::readVertices(mo, *h);
        const auto qs = omk::readQuads(mo, *h);
        float lo = 1e9f, hi = -1e9f;
        for (const auto& v : vs) {
            for (int k = 0; k < 3; ++k) {
                if (v.pos[k] < lo) lo = v.pos[k];
                if (v.pos[k] > hi) hi = v.pos[k];
            }
        }
        std::printf("  [%2zu] %-24s %zu quads, %zu verts, extent %.1f..%.1f\n",
                    i, sp.name.c_str(), qs.size(), vs.size(), lo, hi);
    }
    return 0;
}
