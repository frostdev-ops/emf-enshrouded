// fog_check.h.
#include "core/fog_check.h"

#include <algorithm>
#include <cmath>

namespace emf::core {

namespace {
constexpr float kMajority = 0.75f;

int rank(uint32_t type) {
    switch (type) {
        case EMF_FOG_DEATH: return 4;
        case EMF_FOG_DANGEROUS: return 3;
        case EMF_FOG_TEMPORARY: return 2;
        case EMF_FOG_NONE: return 1;
        default: return 0;  // mixed, unknown
    }
}
}  // namespace

FogVerdict fog_classify(const FogReading& r) {
    FogVerdict v;
    if (!r.loaded) return v;
    for (int i = 1; i <= 16; ++i)
        if (static_cast<uint32_t>(i) > r.progress && std::isfinite(r.w[i])) v.deadly += r.w[i];
    if (r.w[17] >= kMajority) {
        v.type = EMF_FOG_TEMPORARY;
    } else if (r.w[0] - r.w[17] >= kMajority) {
        v.type = EMF_FOG_NONE;
    } else {
        v.type = EMF_FOG_MIXED;
        for (int i = 1; i <= 16; ++i)
            if (r.w[i] >= kMajority) {
                v.type = static_cast<uint32_t>(i) > r.progress ? EMF_FOG_DEATH : EMF_FOG_DANGEROUS;
                break;
            }
    }
    return v;
}

FogVerdict fog_worst(const std::vector<FogVerdict>& all) {
    FogVerdict out;
    out.type = EMF_FOG_UNKNOWN;
    int best = -1;
    for (const FogVerdict& v : all) {
        out.deadly = std::max(out.deadly, v.deadly);
        if (v.type == EMF_FOG_UNKNOWN || v.type == EMF_FOG_MIXED) continue;
        if (rank(v.type) > best) {
            best = rank(v.type);
            out.type = v.type;
        }
    }
    if (best < 0 && !all.empty()) out.type = EMF_FOG_NONE;  // only edges: as clear as they read
    if (out.deadly >= kFogDeadlyRefuse) out.type = EMF_FOG_DEATH;
    return out;
}

std::vector<std::array<double, 3>> fog_points(double x, double y, double z) {
    return {{x, y + 1, z}, {x, y + 2, z}, {x + 3, y + 1, z}, {x - 3, y + 1, z}, {x, y + 1, z + 3}, {x, y + 1, z - 3}};
}

}  // namespace emf::core
