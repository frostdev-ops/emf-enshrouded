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
    if (lim.has_sides && (x < lim.box_min_x || x > lim.box_max_x || z < lim.box_min_z || z > lim.box_max_z)) {
        if (why) *why = "that spot is outside the playable area";  // the game would send a player there to their altar
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
        if (r.fog_phase) {
            if (fog_step(r, now, &a))
                done.emplace_back(std::move(r), std::move(a));
            else
                keep.push_back(std::move(r));
            continue;
        }
        if (!d_.ray || !d_.ray(world, a.x, a.z, r.search.ray_start(), r.search.ray_end(), &ray)) {
            a.why = "the ground could not be read here";
        } else {
            const GroundSearch::Out o = r.search.on_ray(ray, now);
            if (o.step == GroundSearch::Step::Wait) {
                keep.push_back(std::move(r));
                continue;
            }
            if (o.step == GroundSearch::Step::Accept) {
                // the ground is known: the shroud there next (same tick)
                r.fog_phase = true;
                r.ground_y = o.y;
                r.fog_start = now;
                // water over it: the landing is its surface (the shroud is read there)
                WaterReading wr;
                if (d_.water && d_.water(world, a.x, o.y + kWaterProbeUp, a.z, kWaterMaxDown, &wr) && wr.loaded &&
                    wr.water && wr.surface_y - o.y >= kWaterLandDepth && wr.surface_y - o.y <= kWaterMaxDepth) {
                    r.water = true;
                    r.water_y = wr.surface_y;
                }
                r.points = fog_points(a.x, r.water ? r.water_y : o.y, a.z);
                if (r.water) {  // and at the bed: a player who sinks ends there
                    const auto bed = fog_points(a.x, o.y, a.z);
                    r.points.insert(r.points.end(), bed.begin(), bed.end());
                }
                r.verdicts.assign(r.points.size(), FogVerdict{});
                if (fog_step(r, now, &a))
                    done.emplace_back(std::move(r), std::move(a));
                else
                    keep.push_back(std::move(r));
                continue;
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

bool GroundService::fog_step(Req& r, uint64_t now, GroundAnswer* a) {
    a->status = EMF_GROUND_OK;
    a->y = r.ground_y;
    a->water = r.water;
    a->water_y = r.water_y;
    a->fog = EMF_FOG_UNKNOWN;
    bool all = true;
    for (size_t i = 0; i < r.points.size(); ++i) {
        if (r.verdicts[i].type != EMF_FOG_UNKNOWN) continue;
        FogReading f;
        if (!d_.fog || !d_.fog(r.world, r.points[i][0], r.points[i][1], r.points[i][2], &f)) return true;  // not on this build
        if (!f.loaded) {
            all = false;
            continue;
        }
        r.verdicts[i] = fog_classify(f);
    }
    if (!all && now - r.fog_start < kFogLoadMs) return false;
    const FogVerdict w = fog_worst(r.verdicts);
    a->deadly = w.deadly;
    // out of time with points still unknown: deadly if what was read says so, else unknown
    a->fog = all || w.type == EMF_FOG_DEATH ? w.type : EMF_FOG_UNKNOWN;
    return true;
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
