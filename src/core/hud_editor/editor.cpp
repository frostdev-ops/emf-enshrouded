#include "core/hud_editor/editor.h"

#include <algorithm>
#include <cctype>

#include "core/profiles.h"

namespace emf::core::hud {

namespace {

constexpr const char* kPrefix = "hud_editor.";

bool truthy(const std::string& v) { return v == "1" || v == "true" || v == "on" || v == "yes"; }

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool same_snapshot(const HudSnapshot& a, const HudSnapshot& b) {
    if (a.active != b.active) return false;
    for (size_t i = 0; i < kHudElementCount; ++i) {
        const ElementEdit &x = a.edits[i], &y = b.edits[i];
        if (x.x_percent != y.x_percent || x.y_percent != y.y_percent || x.scale != y.scale || x.hidden != y.hidden ||
            x.outline != y.outline)
            return false;
    }
    return true;
}

}  // namespace

HudEditor::HudEditor(HudEditorDeps deps) : deps_(std::move(deps)) {}

uint64_t HudEditor::now_locked() const { return deps_.now_ms ? deps_.now_ms() : 0; }

void HudEditor::log_locked(const std::string& line) {
    if (deps_.log) deps_.log(line);
}

HudLayout& HudEditor::active_locked() { return layouts_[active_slug_]; }
const HudLayout& HudEditor::active_locked() const { return layouts_.at(active_slug_); }

void HudEditor::read_settings_locked() {
    if (!deps_.settings) return;
    auto get = [&](const char* key) { return deps_.settings->get(kEmfSettingsId, std::string(kPrefix) + key); };
    enabled_ = truthy(get("enabled"));
    show_outlines_ = truthy(get("show_outlines"));
    allow_other_ = truthy(get("allow_with_shroudhud"));
    settings_version_ = deps_.settings->version();
}

void HudEditor::select_layout_locked(const std::string& wanted, bool /*first*/) {
    const std::string slug = profile_slug(trim_profile_name(wanted));
    auto it = layouts_.find(slug);
    if (it == layouts_.end()) {
        const std::string w = lower(wanted);
        for (auto i = layouts_.begin(); i != layouts_.end(); ++i)
            if (lower(i->second.name) == w) it = i;
    }
    if (it != layouts_.end() && !wanted.empty()) {
        active_slug_ = it->first;
        return;
    }
    if (layouts_.find("default") == layouts_.end()) {
        HudLayout d;
        d.name = kDefaultLayoutName;
        layouts_["default"] = d;
    }
    active_slug_ = "default";
    if (!wanted.empty() && slug != "default" && warned_layout_ != wanted) {
        warned_layout_ = wanted;
        log_locked("HUD editor: layout \"" + wanted + "\" is not in emf\\hud; using \"Default\"");
    }
}

GateInput HudEditor::gate_input_locked() const {
    GateInput in = deps_.gate;
    in.enabled = enabled_;
    in.other_hud_mod = other_mod_;
    in.allow_with_other = allow_other_;
    in.fault_code = fault_;
    in.hook_error = hook_error_;
    return in;
}

void HudEditor::update_gate_locked(bool log_change) {
    gate_ = decide_gate(gate_input_locked());
    if (gate_.state == GateState::On && !hooked_ && hook_error_.empty()) {
        if (!deps_.install_hooks) {
            hooked_ = true;
        } else {
            std::string why;
            if (deps_.install_hooks(&why))
                hooked_ = true;
            else
                hook_error_ = why.empty() ? "unknown error" : why;
        }
        gate_ = decide_gate(gate_input_locked());
    }
    if (!gate_known_ || gate_.state != logged_state_) {
        const bool first = !gate_known_;
        gate_known_ = true;
        logged_state_ = gate_.state;
        if (log_change && !first)
            log_locked(gate_.state == GateState::On ? "HUD editor: on" : "HUD editor: off (" + gate_.message + ")");
    }
    publish_locked();
}

void HudEditor::publish_locked() {
    HudSnapshot next;
    next.active = gate_.state == GateState::On && hooked_;
    const HudLayout& l = active_locked();
    for (size_t i = 0; i < kHudElementCount; ++i) {
        ElementEdit& e = next.edits[i];
        e.x_percent = l.elements[i].x;
        e.y_percent = l.elements[i].y;
        e.scale = l.elements[i].scale;
        e.hidden = l.elements[i].hidden;
        e.outline = false;
    }
    if (same_snapshot(*box_.current(), next)) return;
    next.serial = ++serial_;
    box_.publish(next, now_locked());
}

void HudEditor::write_active_locked() {
    const HudLayout copy = active_locked();
    const std::wstring dir = deps_.dir;
    const uint32_t build = deps_.build_number;
    auto log = deps_.log;
    auto job = [dir, copy, build, log] {
        std::string err;
        if (!write_layout_file(dir, copy, build, &err) && log)
            log("HUD editor: could not write emf\\hud\\" + profile_slug(copy.name) + ".json (" + err + ")");
    };
    dirty_ = false;
    if (deps_.run)
        deps_.run(job);
    else
        job();
}

void HudEditor::mark_changed_locked(Save save) {
    if (save == Save::Now) {
        write_active_locked();
    } else {
        dirty_ = true;
        last_change_ = now_locked();
    }
}

void HudEditor::start() {
    std::lock_guard<std::mutex> lock(mu_);
    read_settings_locked();
    layouts_.clear();
    for (LayoutFile& f : read_layout_dir(deps_.dir)) {
        if (!f.error.empty()) log_locked("HUD editor: emf\\hud\\" + f.slug + ".json: " + f.error);
        for (const std::string& n : f.notes) log_locked("HUD editor: emf\\hud\\" + f.slug + ".json: " + n);
        if (f.layout) layouts_[f.slug] = std::move(*f.layout);
    }
    const std::string wanted = deps_.settings ? deps_.settings->get(kEmfSettingsId, std::string(kPrefix) + "active_layout")
                                              : std::string();
    select_layout_locked(wanted, true);
    other_mod_ = deps_.other_hud_mod ? deps_.other_hud_mod() : std::string();
    last_conflict_check_ = now_locked();
    fault_ = deps_.fault_code ? deps_.fault_code() : 0;
    update_gate_locked(false);
    if (gate_.state == GateState::On)
        log_locked("HUD editor: on (layout \"" + active_locked().name + "\", " + std::to_string(kHudElementCount) + " parts)");
    else
        log_locked("HUD editor: off (" + gate_.message + ")");
}

void HudEditor::tick() {
    std::lock_guard<std::mutex> lock(mu_);
    if (deps_.settings && deps_.settings->version() != settings_version_) {
        read_settings_locked();
        select_layout_locked(deps_.settings->get(kEmfSettingsId, std::string(kPrefix) + "active_layout"), false);
    }
    const uint64_t now = now_locked();
    if (now - last_conflict_check_ >= kConflictCheckMs) {
        other_mod_ = deps_.other_hud_mod ? deps_.other_hud_mod() : std::string();
        last_conflict_check_ = now;
    }
    fault_ = deps_.fault_code ? deps_.fault_code() : 0;
    update_gate_locked(true);
    if (dirty_ && now - last_change_ >= kSaveDelayMs) write_active_locked();
    if (deps_.scale_state && !scale_off_logged_ && deps_.scale_state().off) {
        scale_off_logged_ = true;
        log_locked(
            "HUD editor: scale is off until the game restarts (the game's transform stack did not match at a close); move "
            "and hide go on");
    }
}

HudView HudEditor::view() const {
    std::lock_guard<std::mutex> lock(mu_);
    HudView v;
    v.gate = gate_;
    v.layout_name = active_locked().name;
    for (const auto& [slug, l] : layouts_) v.layouts.push_back(l.name);
    std::sort(v.layouts.begin(), v.layouts.end(), [](const std::string& a, const std::string& b) {
        const std::string x = lower(a), y = lower(b);
        return x != y ? x < y : a < b;
    });
    v.elements = active_locked().elements;
    for (size_t i = 0; i < kHudElementCount; ++i)
        if (deps_.live) v.live[i] = deps_.live(i);
    if (deps_.scale_state) v.scale = deps_.scale_state();
    v.scale_works = v.scale.available && !v.scale.off;
    if (!v.scale.available)
        v.scale_note = "Scale is not available on this game build; move and hide work.";
    else if (v.scale.off)
        v.scale_note =
            "Scale stopped for this session: the game's drawing did not match what EMF expects (see emf_core.log). Move "
            "and hide still work.";
    return v;
}

void HudEditor::set_offset(size_t element, float x_percent, float y_percent, Save save) {
    if (element >= kHudElementCount) return;
    std::lock_guard<std::mutex> lock(mu_);
    ElementLayout& e = active_locked().elements[element];
    e.x = clamp_offset(x_percent);
    e.y = clamp_offset(y_percent);
    publish_locked();
    mark_changed_locked(save);
}

void HudEditor::nudge(size_t element, float dx_percent, float dy_percent, Save save) {
    if (element >= kHudElementCount) return;
    float x, y;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const ElementLayout& e = active_locked().elements[element];
        x = e.x + dx_percent;
        y = e.y + dy_percent;
    }
    set_offset(element, x, y, save);
}

void HudEditor::set_scale(size_t element, float scale, Save save) {
    if (element >= kHudElementCount) return;
    std::lock_guard<std::mutex> lock(mu_);
    active_locked().elements[element].scale = clamp_scale(scale);
    publish_locked();
    mark_changed_locked(save);
}

void HudEditor::set_hidden(size_t element, bool hidden, Save save) {
    if (element >= kHudElementCount) return;
    std::lock_guard<std::mutex> lock(mu_);
    active_locked().elements[element].hidden = hidden;
    publish_locked();
    mark_changed_locked(save);
}

void HudEditor::reset(size_t element, Save save) {
    if (element >= kHudElementCount) return;
    std::lock_guard<std::mutex> lock(mu_);
    active_locked().elements[element] = ElementLayout{};
    publish_locked();
    mark_changed_locked(save);
}

void HudEditor::flush() {
    std::lock_guard<std::mutex> lock(mu_);
    if (dirty_) write_active_locked();
}

}  // namespace emf::core::hud
