#include "window_gate.h"

namespace emf::core {

WindowGate::Refusal WindowGate::open(WindowRequest r, const std::function<bool()>& overlay_ready, bool window_hooked,
                                     bool wheel_busy, std::optional<WindowRequest>* replaced) {
    std::lock_guard<std::mutex> l(m_);
    if (req_ && req_->id != r.id) return Refusal::OtherWindow;
    if (!overlay_ready || !overlay_ready()) return Refusal::NotReady;
    if (!window_hooked) return Refusal::NoWindow;  // no keyboard or mouse for it: it could not be closed
    if (wheel_busy) return Refusal::WheelBusy;
    if (req_ && replaced) *replaced = std::move(req_);
    const uint16_t vk = r.close_vk;
    req_ = std::move(r);
    ++version_;
    if (capture_) capture_(true, vk);
    return Refusal::None;
}

bool WindowGate::update(const WindowRequest& contents) {
    std::lock_guard<std::mutex> l(m_);
    if (!req_ || req_->id != contents.id) return false;
    WindowRequest next = contents;
    next.cb = req_->cb;
    next.user = req_->user;
    next.mod = req_->mod;
    next.world_kind = req_->world_kind;  // events keep going where the open asked
    const bool new_key = next.close_vk != req_->close_vk;
    req_ = std::move(next);
    ++version_;
    if (new_key && capture_) capture_(true, req_->close_vk);
    return true;
}

std::optional<WindowRequest> WindowGate::close_locked() {
    std::optional<WindowRequest> out = std::move(req_);
    req_.reset();
    ++version_;
    if (capture_) capture_(false, 0);
    return out;
}

bool WindowGate::close(const std::string& id, std::optional<WindowRequest>* closed) {
    std::lock_guard<std::mutex> l(m_);
    if (!req_ || req_->id != id) return false;
    auto out = close_locked();
    if (closed) *closed = std::move(out);
    return true;
}

std::optional<WindowRequest> WindowGate::finish() {
    std::lock_guard<std::mutex> l(m_);
    if (!req_) return std::nullopt;
    return close_locked();
}

std::optional<WindowRequest> WindowGate::turned_off() {
    std::lock_guard<std::mutex> l(m_);
    if (!req_) return std::nullopt;
    return close_locked();
}

std::optional<WindowRequest> WindowGate::close_mod(const std::string& mod) {
    std::lock_guard<std::mutex> l(m_);
    if (!req_ || req_->mod != mod) return std::nullopt;
    return close_locked();
}

bool WindowGate::busy() const {
    std::lock_guard<std::mutex> l(m_);
    return req_.has_value();
}

uint64_t WindowGate::version() const {
    std::lock_guard<std::mutex> l(m_);
    return version_;
}

std::optional<WindowRequest> WindowGate::current() const {
    std::lock_guard<std::mutex> l(m_);
    return req_;
}

}  // namespace emf::core
