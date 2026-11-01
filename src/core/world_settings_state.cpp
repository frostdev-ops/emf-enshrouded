#include "world_settings_state.h"

#include <cstring>

namespace emf::core::ws {
namespace {

constexpr uint64_t kSyncEveryMs = 1000;

std::string label(const Field& f) { return f.alias ? std::string(f.alias) + " (" + f.key + ")" : f.key; }

}  // namespace

void State::queue(const Change& c, uint32_t session) {
    std::lock_guard<std::mutex> l(m_);
    queue_.push_back({c, session});
}

bool State::has_base() const {
    std::lock_guard<std::mutex> l(m_);
    return base_session_ != 0;
}

double State::base_value(const Field& f) const {
    std::lock_guard<std::mutex> l(m_);
    return read(base_.data(), f);
}

std::map<std::string, double> State::persisted() const {
    std::lock_guard<std::mutex> l(m_);
    return persisted_;
}

void State::set_persisted(std::map<std::string, double> values, uint32_t session) {
    std::lock_guard<std::mutex> l(m_);
    persisted_ = std::move(values);
    persisted_session_ = session;
}

void State::require_plausible_base(bool on) {
    std::lock_guard<std::mutex> l(m_);
    require_plausible_ = on;
}

bool State::writes_allowed(uint32_t session, std::string* why) const {
    std::lock_guard<std::mutex> l(m_);
    if (!off_ || off_session_ != session) return true;
    if (why) *why = "changes are off for this world: its settings do not look as EMF knows them (" + off_why_ +
                    "); EMF needs an update for this game version";
    return false;
}

State::Step State::on_server_step(uint8_t* live, uint32_t session, uint64_t now_ms) {
    Step out;
    std::lock_guard<std::mutex> l(m_);
    std::array<uint8_t, kStructSize> copy;
    std::memcpy(copy.data(), live, kStructSize);
    const bool first = session != base_session_;
    if (first) {  // a new session: its own values are the base; what persisted for this world goes on top
        std::memcpy(base_.data(), live, kStructSize);
        base_session_ = session;
        pending_sync_ = false;
        if (persisted_session_ != session) {  // kept values of another world (its file was read in another session)
            persisted_.clear();
            persisted_session_ = session;
        }
        std::string why;
        off_ = require_plausible_ && !plausible(live, &why);
        off_session_ = session;
        if (off_) {
            off_why_ = why;
            out.lines.push_back("world settings: changes are off for this world: its settings do not look as EMF "
                                "knows them (" + why + "); EMF needs an update for this game version");
        }
    }
    if (off_ && off_session_ == session) {  // nothing is written into a struct EMF cannot read right
        if (!queue_.empty())
            out.lines.push_back("world settings: changes are off for this world (this game version): nothing changed");
        queue_.clear();
        return out;
    }
    if (first) {
        for (const auto& [key, value] : persisted_)
            if (const Field* f = find(key)) {
                write(copy.data(), *f, value);
                out.lines.push_back("world settings: " + label(*f) + " = " + text(*f, value) + " (kept for this world)");
            }
    }
    std::vector<Queued> todo;
    todo.swap(queue_);
    auto persisted = persisted_;
    for (const auto& q : todo) {
        if (q.session != session) {
            out.lines.push_back("world settings: a change for another session was dropped (the world changed)");
            continue;
        }
        const Change& c = q.change;
        if (c.reset) {
            for (const auto& f : fields())
                if (!c.field || c.field == &f) {
                    write(copy.data(), f, read(base_.data(), f));
                    if (c.persist) persisted.erase(f.key);
                }
        } else if (c.field) {
            write(copy.data(), *c.field, c.value);
            if (c.persist) persisted[c.field->key] = c.value;
        }
    }
    if (!equal(copy.data(), live)) {
        std::string why;
        if (!crash_safe(copy.data(), &why)) {
            out.lines.push_back("world settings: refused, nothing changed: " + why);
        } else {
            for (const auto& f : fields()) {
                const double was = read(live, f), now = read(copy.data(), f);
                if (was == now) continue;
                out.changed.emplace_back(f.key, now);
                if (!first || !persisted_.count(f.key))
                    out.lines.push_back("world settings: " + label(f) + " = " + text(f, now) + " (was " + text(f, was) + ")");
            }
            std::memcpy(live, copy.data(), kStructSize);
            persisted_ = persisted;
            out.wrote = true;
            pending_sync_ = true;
        }
    } else {
        persisted_ = persisted;  // a persist of the value it has already, or a reset persist
    }
    if (pending_sync_ && (!synced_once_ || now_ms - last_sync_ >= kSyncEveryMs)) {
        out.sync = true;
        pending_sync_ = false;
        synced_once_ = true;
        last_sync_ = now_ms;
    }
    return out;
}

}  // namespace emf::core::ws
