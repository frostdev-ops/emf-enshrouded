// Markers (API v33): emf/markers (marker_net.h).
#include "core/marker_net.h"

#include <cmath>
#include <utility>
#include <vector>

namespace emf::core {

uint32_t MarkerSender::send(const std::string& owner, uint32_t player, const EmfMarker* markers, uint32_t count) {
    if (!markers || !count || owner.empty()) return 0;
    std::vector<EmfMarker> ok;
    for (uint32_t i = 0; i < count && ok.size() < kMarkersPerSend; ++i)
        if (markers[i].size >= sizeof(EmfMarker) && marker_id_ok(markers[i].id)) ok.push_back(markers[i]);
    if (ok.empty()) return 0;
    std::vector<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(m_);
        sent_ = true;
        if (count > kMarkersPerSend && !told_cut_ && log_) {
            told_cut_ = true;
            log_("marker_send: " + owner + " sent " + std::to_string(count) + " markers in one call; the first " +
                 std::to_string(kMarkersPerSend) + " are sent (logged once)");
        }
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
        }
        for (size_t i = 0; i < ok.size(); i += kMarkersPerNetMessage) {
            const size_t end = std::min(ok.size(), i + kMarkersPerNetMessage);
            messages.push_back(encode_markers(owner, std::vector<EmfMarker>(ok.begin() + i, ok.begin() + end), {}));
        }
    }
    uint32_t reached = 0;
    for (const auto& data : messages) reached = std::max(reached, send_ ? send_(player, data) : 0u);
    return reached;
}

uint32_t MarkerSender::unsend(const std::string& owner, uint32_t player, const char* id) {
    if (!marker_id_ok(id) || owner.empty()) return 0;
    {
        std::lock_guard<std::mutex> lock(m_);
        for (auto it = follow_.begin(); it != follow_.end();) {
            const bool hit = std::get<1>(it->first) == owner && std::get<2>(it->first) == id &&
                             (player == 0 || std::get<0>(it->first) == player);
            it = hit ? follow_.erase(it) : std::next(it);
        }
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
            moved[{std::get<0>(k), std::get<1>(k)}].push_back(MarkerPosition{std::get<2>(k), p[0], p[1], p[2]});
        }
    }
    for (const auto& [who, list] : moved)
        for (size_t i = 0; i < list.size(); i += kMarkersPerNetMessage) {
            const size_t end = std::min(list.size(), i + kMarkersPerNetMessage);
            if (send_) send_(who.first, encode_positions(who.second, std::vector<MarkerPosition>(list.begin() + i, list.begin() + end)));
        }
}

void MarkerSender::forget(uint32_t player) {
    std::lock_guard<std::mutex> lock(m_);
    for (auto it = follow_.begin(); it != follow_.end();) it = std::get<0>(it->first) == player ? follow_.erase(it) : std::next(it);
}

void MarkerSender::reset() {
    std::lock_guard<std::mutex> lock(m_);
    follow_.clear();
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
