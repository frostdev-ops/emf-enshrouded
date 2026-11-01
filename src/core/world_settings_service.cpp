#include "world_settings_service.h"

#include <cstdio>

#include "common/seh.h"

namespace emf::core::ws {
namespace {

constexpr const char* kNoWorld = "only the host or the dedicated server changes world settings (no world is hosted here)";

bool unknown(std::string_view key, std::string* why) {
    if (why) *why = "\"" + std::string(key) + "\" is not a world setting (world.list shows them)";
    return false;
}

// The live struct is game memory: read it guarded.
bool read_live(const uint8_t* live, uint8_t (&out)[kStructSize]) { return live && emf::seh_read(live, out, kStructSize); }

}  // namespace

Service::Service(State& state, ServiceDeps deps) : state_(state), deps_(std::move(deps)) {}

bool Service::can_sync() const { return !deps_.can_sync || deps_.can_sync(); }

bool Service::may_queue(bool persist, std::string* why) const {
    if (!deps_.live || !deps_.live()) {
        if (why) *why = kNoWorld;
        return false;
    }
    if (!state_.writes_allowed(deps_.session ? deps_.session() : 0, why)) return false;
    if (persist && deps_.can_persist && !deps_.can_persist()) {
        if (why)
            *why = "it cannot be kept: EMF has not seen this world's save load, so its kept settings cannot be saved "
                   "yet (try again in a moment, or without persist)";
        return false;
    }
    return true;
}

bool Service::get(std::string_view key, double* value, std::string* why) const {
    const Field* f = find(key);
    if (!f) return unknown(key, why);
    uint8_t copy[kStructSize];
    if (!read_live(deps_.live ? deps_.live() : nullptr, copy)) {
        if (why) *why = kNoWorld;
        return false;
    }
    if (value) *value = read(copy, *f);
    return true;
}

bool Service::queue_checked(const Field& f, double value, bool persist, std::string* why) {
    if (!may_queue(persist, why)) return false;
    char b[48];
    std::snprintf(b, sizeof b, "%.9g", value);
    const Parsed p = parse(f, b);
    if (!p.ok) {
        if (why) *why = p.error;
        return false;
    }
    state_.queue({&f, p.value, false, persist}, deps_.session ? deps_.session() : 0);
    return true;
}

bool Service::set(std::string_view key, double value, bool persist, std::string* why) {
    const Field* f = find(key);
    return f ? queue_checked(*f, value, persist, why) : unknown(key, why);
}

bool Service::set_text(std::string_view key, std::string_view value, bool persist, std::string* why) {
    const Field* f = find(key);
    if (!f) return unknown(key, why);
    const Parsed p = parse(*f, value);
    if (!p.ok) {
        if (why) *why = p.error;
        return false;
    }
    return queue_checked(*f, p.value, persist, why);
}

bool Service::reset(std::string_view key, bool persist, std::string* why) {
    const Field* f = nullptr;
    if (!key.empty() && key != "all") {
        f = find(key);
        if (!f) return unknown(key, why);
    }
    if (!may_queue(persist, why)) return false;
    state_.queue({f, 0, true, persist}, deps_.session ? deps_.session() : 0);
    return true;
}

std::vector<Service::Row> Service::rows() const {
    std::vector<Row> out;
    uint8_t copy[kStructSize];
    if (!read_live(deps_.live ? deps_.live() : nullptr, copy)) return out;
    const bool base = state_.has_base();
    for (const auto& f : fields()) {
        const double now = read(copy, f);
        const double world = base ? state_.base_value(f) : now;
        out.push_back({&f, now, world, now != world});
    }
    return out;
}

bool Service::snapshot(uint8_t (&out)[kStructSize]) const { return read_live(deps_.live ? deps_.live() : nullptr, out); }

void Service::on_changed(std::function<void(const std::string&, double)> cb) {
    std::lock_guard<std::mutex> l(cb_m_);
    callbacks_.push_back(std::move(cb));
}

void Service::notify(const State::Step& step) {
    if (step.changed.empty()) return;
    std::vector<std::function<void(const std::string&, double)>> cbs;
    {
        std::lock_guard<std::mutex> l(cb_m_);
        cbs = callbacks_;
    }
    for (const auto& [key, value] : step.changed)
        for (const auto& cb : cbs) cb(key, value);
}

}  // namespace emf::core::ws
