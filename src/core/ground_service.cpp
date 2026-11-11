// Markers step 2 (ground_service.h).
#include "core/ground_service.h"

namespace emf::core {

bool GroundService::request(EmfWorld* world, double x, double z, Callback cb, std::string* why) {
    std::string w;
    if (!world || !cb) {
        if (why) *why = "no world or no callback";
        return false;
    }
    if (!ground_spot_ok(x, z, &w)) {
        if (why) *why = w;
        return false;
    }
    if (reqs_.size() >= kGroundRequestsMax) {
        if (why) *why = "64 ground searches are waiting already";
        return false;
    }
    GroundLimits lim;
    if (!d_.limits || !d_.limits(world, &lim, &w)) {
        if (why) *why = w.empty() ? "the world's limits cannot be read" : w;
        return false;
    }
    reqs_.push_back(Req{world, GroundSearch(x, z, lim, d_.now_ms ? d_.now_ms() : 0), std::move(cb)});
    return true;
}

void GroundService::on_server_tick(EmfWorld* world) {
    if (reqs_.empty()) return;
    const uint64_t now = d_.now_ms ? d_.now_ms() : 0;
    std::vector<Req> keep;
    std::vector<std::pair<Req, GroundAnswer>> done;
    for (Req& r : reqs_) {
        if (r.world != world) {
            keep.push_back(std::move(r));
            continue;
        }
        RayResult ray;
        GroundAnswer a;
        a.x = r.search.x();
        a.z = r.search.z();
        if (!d_.ray || !d_.ray(world, a.x, a.z, r.search.ray_start(), r.search.ray_end(), &ray)) {
            a.why = "the ground could not be read here";
        } else {
            const GroundSearch::Out o = r.search.on_ray(ray, now);
            if (o.step == GroundSearch::Step::Wait) {
                keep.push_back(std::move(r));
                continue;
            }
            if (o.step == GroundSearch::Step::Accept) {
                a.status = EMF_GROUND_OK;
                a.y = o.y;
            } else if (o.step == GroundSearch::Step::Hop) {  // a plain search moves nobody: the caller may hop
                a.status = EMF_GROUND_NOT_LOADED;
                a.hop_y = o.y;
                a.why = "that place is not loaded yet (nobody is near it)";
            } else {
                a.why = o.why.empty() ? "no ground found there" : o.why;
            }
        }
        done.emplace_back(std::move(r), std::move(a));
    }
    reqs_ = std::move(keep);
    for (auto& [r, a] : done) r.cb(world, a);
}

void GroundService::world_reset() {
    std::vector<Req> old = std::move(reqs_);
    reqs_.clear();
    for (Req& r : old) {
        GroundAnswer a;
        a.x = r.search.x();
        a.z = r.search.z();
        a.why = "the world changed";
        r.cb(r.world, a);
    }
}

}  // namespace emf::core
