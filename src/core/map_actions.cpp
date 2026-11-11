// Markers step 2 (map_actions.h).
#include "core/map_actions.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/bridge/json_out.h"
#include "core/json.h"

namespace emf::core {

namespace {

constexpr double kWorld = 10240.0;
constexpr uint64_t kWindowMs = 10000;

bool id_ok(const char* id) {
    if (!id) return false;
    const size_t n = std::strlen(id);
    if (n == 0 || n > 48) return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = id[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
              c == '-' || c == ':'))
            return false;
    }
    return true;
}

}  // namespace

void MapActions::log_once(const std::string& kind, const std::string& line) {
    LogFn log;
    {
        std::lock_guard<std::mutex> lock(m_);
        if (told_[kind]) return;
        told_[kind] = true;
        log = log_;
    }
    if (log) log(line + " (logged once)");
}

void MapActions::set_log(LogFn log) {
    std::lock_guard<std::mutex> lock(m_);
    log_ = std::move(log);
}

bool MapActions::add(const std::string& owner, const char* id, const char* label, uint32_t hold_ms,
                     EmfMapActionCallback cb, void* user) {
    if (!id_ok(id)) return false;
    if (!cb) return remove(owner, id);
    std::lock_guard<std::mutex> lock(m_);
    Action a{owner, id, label ? label : id, hold_ms ? std::min<uint32_t>(hold_ms, 10000) : 1000, cb, user};
    for (Action& x : actions_)
        if (x.id == a.id) {
            x = std::move(a);
            return true;
        }
    if (actions_.size() >= kMapActionsMax) return false;
    actions_.push_back(std::move(a));
    return true;
}

bool MapActions::remove(const std::string& owner, const char* id) {
    if (!id) return false;
    std::lock_guard<std::mutex> lock(m_);
    const auto it =
        std::find_if(actions_.begin(), actions_.end(), [&](const Action& a) { return a.id == id && a.owner == owner; });
    if (it == actions_.end()) return false;
    actions_.erase(it);
    return true;
}

size_t MapActions::remove_owner(const std::string& owner) {
    std::lock_guard<std::mutex> lock(m_);
    return std::erase_if(actions_, [&](const Action& a) { return a.owner == owner; });
}

bool MapActions::empty() const {
    std::lock_guard<std::mutex> lock(m_);
    return actions_.empty();
}

std::string MapActions::announce_message() const {
    std::vector<std::string> items;
    {
        std::lock_guard<std::mutex> lock(m_);
        for (const Action& a : actions_)
            items.push_back(bridge::JsonObj().str("i", a.id).str("l", a.label).num("h", a.hold_ms).text());
    }
    return bridge::JsonObj().raw("a", bridge::json_list(items)).text();
}

bool MapActions::on_done(EmfWorld* world, uint32_t player, bool admin, const std::string& data, uint64_t now_ms) {
    const json::ParseResult r = json::parse(data);
    const json::Value* id = r.value ? r.value->get("i") : nullptr;
    const json::Value* x = r.value ? r.value->get("x") : nullptr;
    const json::Value* z = r.value ? r.value->get("z") : nullptr;
    if (!id || !id->is(json::Value::Type::String) || !x || !x->is(json::Value::Type::Number) || !z ||
        !z->is(json::Value::Type::Number)) {
        log_once("junk", "map actions: a player's message could not be read; dropped");
        return false;
    }
    if (!std::isfinite(x->n) || !std::isfinite(z->n) || x->n < 0 || x->n > kWorld || z->n < 0 || z->n > kWorld) {
        log_once("spot", "map actions: a spot outside the world; dropped");
        return false;
    }
    EmfMapActionCallback cb = nullptr;
    void* user = nullptr;
    bool flood = false;
    {
        std::lock_guard<std::mutex> lock(m_);
        auto& times = recent_[player];
        while (!times.empty() && now_ms - times.front() >= kWindowMs) times.pop_front();
        flood = times.size() >= kMapDonePer10s;
        if (!flood) times.push_back(now_ms);
        for (const Action& a : actions_)
            if (a.id == id->s) {
                cb = a.cb;
                user = a.user;
            }
    }
    if (flood) {
        log_once("flood:" + std::to_string(player), "map actions: player " + std::to_string(player) +
                                                         " sent more than 20 in 10 s; the rest are dropped");
        return false;
    }
    if (!cb) {
        log_once("unknown", "map actions: a player asked for an action this server does not have (" + id->s + ")");
        return false;
    }
    EmfMapDone d{};
    d.size = sizeof d;
    d.player = player;
    d.admin = admin ? 1 : 0;
    d.x = x->n;
    d.z = z->n;
    d.action = id->s.c_str();
    cb(world, &d, user);
    return true;
}

void MapActions::forget(uint32_t player) {
    std::lock_guard<std::mutex> lock(m_);
    recent_.erase(player);
}

bool MapActionsClient::on_announce(const std::string& data) {
    const json::ParseResult r = json::parse(data);
    const json::Value* list = r.value ? r.value->get("a") : nullptr;
    if (!list || !list->is(json::Value::Type::Array)) return false;
    std::string first;
    uint32_t hold = 1000;
    for (const json::Value& v : list->arr) {
        const json::Value* id = v.get("i");
        if (!id || !id->is(json::Value::Type::String) || !id_ok(id->s.c_str())) continue;
        first = id->s;
        const json::Value* h = v.get("h");
        if (h && h->is(json::Value::Type::Number) && h->n >= 100 && h->n <= 10000) hold = static_cast<uint32_t>(h->n);
        break;
    }
    std::lock_guard<std::mutex> lock(m_);
    first_ = first;
    hold_ms_ = hold;
    return true;
}

bool MapActionsClient::announced() const {
    std::lock_guard<std::mutex> lock(m_);
    return !first_.empty();
}

std::string MapActionsClient::first_id() const {
    std::lock_guard<std::mutex> lock(m_);
    return first_;
}

uint32_t MapActionsClient::hold_ms() const {
    std::lock_guard<std::mutex> lock(m_);
    return hold_ms_;
}

std::string MapActionsClient::done(double x, double z) const {
    const std::string id = first_id();
    return id.empty() ? std::string() : done_message(id.c_str(), x, z);
}

std::string MapActionsClient::done_message(const char* id, double x, double z) {
    return bridge::JsonObj().str("i", id ? id : "").num("x", x).num("z", z).text();
}

void MapActionsClient::reset() {
    std::lock_guard<std::mutex> lock(m_);
    first_.clear();
    hold_ms_ = 1000;
}

}  // namespace emf::core
