// Markers step 2 (ground_search.h).
#include "core/ground_search.h"

#include <algorithm>
#include <cmath>

namespace emf::core {

namespace {
constexpr double kAgreeM = 0.5, kBelowBoxM = 4.0, kBelowStartM = 1.0, kHopBelowBoxM = 8.0;
constexpr uint64_t kGiveUpMs = 20000;  // ground that never passes the checks (past kGroundLoadMs)
}  // namespace

bool ground_spot_ok(double x, double z, std::string* why) {
    if (std::isfinite(x) && std::isfinite(z) && x >= 0 && x <= kGroundWorldSize && z >= 0 && z <= kGroundWorldSize)
        return true;
    if (why) *why = "that spot is outside the world";
    return false;
}

GroundSearch::GroundSearch(double x, double z, GroundLimits limits, uint64_t now_ms)
    : x_(x), z_(z), lim_(limits), start_ms_(now_ms) {}

GroundSearch::Out GroundSearch::on_ray(const RayResult& r, uint64_t now_ms) {
    Out o;
    if (!r.loaded) {
        if (hopped_) {
            if (now_ms - hopped_ms_ >= kGroundAfterHopMs) o.step = Step::Back;
            return o;
        }
        if (hop_asked_) return o;
        if (now_ms - start_ms_ >= kGroundLoadMs) {
            hop_asked_ = true;
            o.step = Step::Hop;
            o.y = std::min(lim_.box_top - kHopBelowBoxM, lim_.grid_top);
        }
        return o;
    }
    if (!r.hit) {
        o.step = Step::Fail;
        o.why = "there is no ground under that spot";
        return o;
    }
    const bool ok = std::isfinite(r.y) && r.y < lim_.box_top - kBelowBoxM && r.y < ray_start() - kBelowStartM;
    if (!ok) {
        have_candidate_ = false;
        if (now_ms - start_ms_ >= kGiveUpMs) {
            o.step = hopped_ ? Step::Back : Step::Fail;
            o.why = "the ground there is out of reach";
        }
        return o;
    }
    if (have_candidate_ && std::fabs(r.y - candidate_) <= kAgreeM) {
        o.step = Step::Accept;
        o.y = r.y;
        return o;
    }
    have_candidate_ = true;
    candidate_ = r.y;
    if (now_ms - start_ms_ >= kGiveUpMs) {  // two readings never agree: no endless search
        o.step = hopped_ ? Step::Back : Step::Fail;
        o.why = "the ground there keeps changing";
    }
    return o;
}

GroundSearch::Out GroundSearch::on_hopped(bool moved, uint64_t now_ms) {
    Out o;
    if (!moved) {
        o.step = Step::Fail;
        o.why = "could not move there";
        return o;
    }
    hopped_ = true;
    hopped_ms_ = now_ms;
    have_candidate_ = false;
    return o;
}

}  // namespace emf::core
