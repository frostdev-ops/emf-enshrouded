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
    std::vector<std::pair<Req, GroundSearch::Out>> done;
    for (Req& r : reqs_) {
        if (r.world != world) {
            keep.push_back(std::move(r));
            continue;
        }
        RayResult ray;
        GroundSearch::Out o;
        if (!d_.ray || !d_.ray(world, r.search.x(), r.search.z(), r.search.ray_start(), r.search.ray_end(), &ray)) {
            o.step = GroundSearch::Step::Fail;
            o.why = "the ground could not be read here";
        } else {
            o = r.search.on_ray(ray, now);
            if (o.step == GroundSearch::Step::Hop || o.step == GroundSearch::Step::Back) {
                o.step = GroundSearch::Step::Fail;  // a plain search moves nobody
                o.why = "that place is not loaded yet (nobody is near it)";
            }
        }
        if (o.step == GroundSearch::Step::Wait)
            keep.push_back(std::move(r));
        else
            done.emplace_back(std::move(r), o);
    }
    reqs_ = std::move(keep);
    for (auto& [r, o] : done) {
        const bool ok = o.step == GroundSearch::Step::Accept;
        r.cb(world, ok, r.search.x(), ok ? o.y : 0.0, r.search.z(), ok ? std::string() : o.why);
    }
}

void GroundService::world_reset() {
    std::vector<Req> old = std::move(reqs_);
    reqs_.clear();
    for (Req& r : old) r.cb(r.world, false, r.search.x(), 0.0, r.search.z(), "the world changed");
}

}  // namespace emf::core
