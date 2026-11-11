// Markers (API v33): emf/markers (marker_net.h).
#include "core/marker_net.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace emf::core {

void MarkerSender::log_once(bool* told, const std::string& line) {
    if (*told || !log_) return;
    *told = true;
    log_(line + " (logged once)");
}

std::vector<std::string> MarkerSender::batches(const std::string& owner, const std::vector<EmfMarker>& markers) {
    std::vector<std::string> out;
    std::vector<EmfMarker> chunk;
    size_t bytes = 0;
    const size_t overhead = encode_markers(owner, {}, {}).size() + 8;
    for (const EmfMarker& m : markers) {
        const size_t one = encode_markers(owner, {m}, {}).size() - overhead + 8 + 1;  // its object and a comma
        if (!chunk.empty() && (chunk.size() >= kMarkersPerNetMessage || overhead + bytes + one > kMarkerNetBudget)) {
            out.push_back(encode_markers(owner, chunk, {}));
            chunk.clear();
            bytes = 0;
        }
        chunk.push_back(m);
        bytes += one;
    }
    if (!chunk.empty()) out.push_back(encode_markers(owner, chunk, {}));
    return out;
}

uint32_t MarkerSender::send(const std::string& owner, uint32_t player, const EmfMarker* markers, uint32_t count) {
    if (!markers || !count || owner.empty()) return 0;
    std::vector<EmfMarker> ok;
    std::vector<std::string> texts;  // the cut texts (ok[i].text points into them)
    texts.reserve(std::min<uint32_t>(count, kMarkersPerSend));
    bool bad = false;
    for (uint32_t i = 0; i < count && ok.size() < kMarkersPerSend; ++i) {
        const EmfMarker& m = markers[i];
        if (!marker_id_ok(m.id) || !marker_numbers_ok(m)) {
            bad = true;
            continue;
        }
        texts.push_back(marker_cut_utf8(m.text ? std::string(m.text) : std::string(), kMarkerTextBytes));
        EmfMarker c = m;
        c.icon = 0;  // a picture handle means nothing on another game
        ok.push_back(c);
    }
    for (size_t i = 0; i < ok.size(); ++i) ok[i].text = texts[i].empty() ? nullptr : texts[i].c_str();
    std::vector<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(m_);
        sent_ = true;
        if (bad) log_once(&told_bad_, "marker_send: " + owner + " sent a marker with a bad id or numbers; it was left out");
        if (count > kMarkersPerSend)
            log_once(&told_cut_, "marker_send: " + owner + " sent " + std::to_string(count) +
                                     " markers in one call; the first " + std::to_string(kMarkersPerSend) + " are sent");
        if (ok.empty()) return 0;
        for (const EmfMarker& m : ok) {
            const Key k{player, owner, m.id};
            if (m.entity) {
                Follow f;
                f.entity = m.entity;
                f.sent[0] = m.x;
                f.sent[1] = m.y;
                f.sent[2] = m.z;
                follow_[k] = f;
            } else {
                follow_.erase(k);
            }
            if (stored_.size() < kMarkersStoredMax || stored_.count(k)) {
                Stored st;
                st.m = m;
                st.m.id = nullptr;
                st.text = m.text ? m.text : "";
                st.m.text = nullptr;
                stored_[k] = std::move(st);
            } else {
                log_once(&told_full_, "marker_send: more than " + std::to_string(kMarkersStoredMax) +
                                          " markers kept for players; a player who links up later misses the rest");
            }
        }
        messages = batches(owner, ok);
    }
    uint32_t reached = 0;
    for (const auto& data : messages) reached = std::max(reached, send_ ? send_(player, data) : 0u);
    return reached;
}

uint32_t MarkerSender::unsend(const std::string& owner, uint32_t player, const char* id) {
    if (!marker_id_ok(id) || owner.empty()) return 0;
    {
        std::lock_guard<std::mutex> lock(m_);
        auto hit = [&](const Key& k) {
            return std::get<1>(k) == owner && std::get<2>(k) == id && (player == 0 || std::get<0>(k) == player);
        };
        for (auto it = follow_.begin(); it != follow_.end();) it = hit(it->first) ? follow_.erase(it) : std::next(it);
        for (auto it = stored_.begin(); it != stored_.end();) it = hit(it->first) ? stored_.erase(it) : std::next(it);
    }
    return send_ ? send_(player, encode_markers(owner, {}, {id})) : 0;
}

void MarkerSender::refresh(const std::function<bool(uint32_t, double out[3])>& pos_of) {
    std::map<std::pair<uint32_t, std::string>, std::vector<MarkerPosition>> moved;  // (player, owner) -> positions
    {
        std::lock_guard<std::mutex> lock(m_);
        for (auto& [k, f] : follow_) {
            double p[3];
            if (!pos_of(f.entity, p) || !std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) continue;
            const double dx = p[0] - f.sent[0], dy = p[1] - f.sent[1], dz = p[2] - f.sent[2];
            if (dx * dx + dy * dy + dz * dz < kMarkerRefreshMoveM * kMarkerRefreshMoveM) continue;
            f.sent[0] = p[0];
            f.sent[1] = p[1];
            f.sent[2] = p[2];
            auto st = stored_.find(k);  // a replay sends the newest place
            if (st != stored_.end()) {
                st->second.m.x = p[0];
                st->second.m.y = p[1];
                st->second.m.z = p[2];
            }
            moved[{std::get<0>(k), std::get<1>(k)}].push_back(MarkerPosition{std::get<2>(k), p[0], p[1], p[2]});
        }
    }
    for (const auto& [who, list] : moved)
        for (size_t i = 0; i < list.size(); i += kMarkersPerNetMessage) {
            const size_t end = std::min(list.size(), i + kMarkersPerNetMessage);
            if (send_) send_(who.first, encode_positions(who.second, std::vector<MarkerPosition>(list.begin() + i, list.begin() + end)));
        }
}

void MarkerSender::replay(uint32_t player) {
    if (!player) return;
    std::vector<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(m_);
        std::map<std::string, std::vector<EmfMarker>> by_owner;  // the player's own and every player's
        for (auto& [k, st] : stored_) {
            if (std::get<0>(k) != player && std::get<0>(k) != 0) continue;
            EmfMarker m = st.m;
            m.id = std::get<2>(k).c_str();
            m.text = st.text.empty() ? nullptr : st.text.c_str();
            by_owner[std::get<1>(k)].push_back(m);
        }
        for (const auto& [owner, list] : by_owner)
            for (auto& data : batches(owner, list)) messages.push_back(std::move(data));
    }
    for (const auto& data : messages)
        if (send_) send_(player, data);
}

void MarkerSender::forget(uint32_t player) {
    std::lock_guard<std::mutex> lock(m_);
    for (auto it = follow_.begin(); it != follow_.end();) it = std::get<0>(it->first) == player ? follow_.erase(it) : std::next(it);
    for (auto it = stored_.begin(); it != stored_.end();) it = std::get<0>(it->first) == player ? stored_.erase(it) : std::next(it);
}

void MarkerSender::reset() {
    std::lock_guard<std::mutex> lock(m_);
    follow_.clear();
    stored_.clear();
    sent_ = false;
}

bool MarkerSender::any_sent() const {
    std::lock_guard<std::mutex> lock(m_);
    return sent_;
}

bool MarkerReceiver::on_message(const std::string& data, MarkerStore& store) {
    DecodedMarkers d;
    if (!decode_markers(data, &d)) {
        if (!told_ && log_) {
            told_ = true;
            log_("markers: a message on " + std::string(kMarkersChannel) + " could not be read; dropped (logged once)");
        }
        return false;
    }
    for (const DecodedMarker& m : d.markers) store.set(d.owner, m.as_marker(), true);
    for (const std::string& id : d.removes) store.remove(d.owner, id, true);
    for (const MarkerPosition& p : d.positions) store.move(d.owner, p.id, p.x, p.y, p.z);
    return true;
}

void MarkerReceiver::reset(MarkerStore& store) { store.clear_received(); }

}  // namespace emf::core
