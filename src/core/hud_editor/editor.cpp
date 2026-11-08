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
    hotkeys_.set_keys(key_setting_vk(get("key_page")), key_setting_vk(get("key_reset")));
    settings_version_ = deps_.settings->version();
}

// The settings changed elsewhere (the page, `set emf ...`, the ini): follow them.
void HudEditor::refresh_settings_locked() {
    if (!deps_.settings || deps_.settings->version() == settings_version_) return;
    read_settings_locked();
    select_layout_locked(deps_.settings->get(kEmfSettingsId, std::string(kPrefix) + "active_layout"), false);
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
        if (dirty_ && active_slug_ != it->first && layouts_.count(active_slug_)) write_active_locked();  // its own file
        active_slug_ = it->first;
        return;
    }
    if (layouts_.find("default") == layouts_.end()) {
        HudLayout d;
        d.name = kDefaultLayoutName;
        layouts_["default"] = d;
    }
    if (dirty_ && active_slug_ != "default" && layouts_.count(active_slug_)) write_active_locked();
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
    const uint64_t now = now_locked();
    const bool outlines_on = show_outlines_ && page_seen_ && now - last_page_frame_ < kPageGoneMs;
    const HudLayout& l = active_locked();
    for (size_t i = 0; i < kHudElementCount; ++i) {
        ElementEdit& e = next.edits[i];
        e.x_percent = l.elements[i].x;
        e.y_percent = l.elements[i].y;
        e.scale = l.elements[i].scale;
        e.hidden = l.elements[i].hidden;
        e.outline = outlines_on && (i != selected_ || (now / kBlinkMs) % 2 == 0);
    }
    if (same_snapshot(*box_.current(), next)) return;
    next.serial = ++serial_;
    box_.publish(next, now_locked());
}

void HudEditor::write_active_locked() { write_layout_locked(active_locked()); }

void HudEditor::write_layout_locked(const HudLayout& l) {
    const HudLayout copy = l;
    const std::wstring dir = deps_.dir;
    const uint32_t build = deps_.build_number;
    auto log = deps_.log;
    const std::string slug = profile_slug(copy.name);
    const bool set_aside = refused_.erase(slug) > 0;  // a file EMF could not read: kept before the first write over it
    auto job = [dir, copy, build, log, slug, set_aside] {
        if (set_aside) {
            std::string kept;
            if (set_aside_layout_file(dir, slug, &kept)) {
                if (!kept.empty() && log) log("HUD editor: emf\\hud\\" + slug + ".json could not be read; kept as " + kept);
            } else if (log) {
                log("HUD editor: could not keep emf\\hud\\" + slug + ".json before writing a new one");
            }
        }
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

bool HudEditor::could_turn_on_locked() const {
    return enabled_ && deps_.gate.build_known && !deps_.gate.server_exe && fault_ == 0;
}

void HudEditor::start() {
    std::unique_lock<std::mutex> lock(mu_);
    read_settings_locked();
    layouts_.clear();
    for (LayoutFile& f : read_layout_dir(deps_.dir)) {
        if (!f.error.empty()) log_locked("HUD editor: emf\\hud\\" + f.slug + ".json: " + f.error);
        for (const std::string& n : f.notes) log_locked("HUD editor: emf\\hud\\" + f.slug + ".json: " + n);
        if (f.layout)
            layouts_[f.slug] = std::move(*f.layout);
        else
            refused_.insert(f.slug);
    }
    const std::string wanted = deps_.settings ? deps_.settings->get(kEmfSettingsId, std::string(kPrefix) + "active_layout")
                                              : std::string();
    select_layout_locked(wanted, true);
    hotkeys_.poll(deps_.key_down);  // a key held now does not fire later
    fault_ = deps_.fault_code ? deps_.fault_code() : 0;
    if (could_turn_on_locked() && deps_.other_hud_mod) {  // only while it could turn on; outside the lock (it can be slow)
        lock.unlock();
        const std::string mod = deps_.other_hud_mod();
        lock.lock();
        other_mod_ = mod;
        other_checked_ = true;
        last_conflict_check_ = now_locked();
    }
    update_gate_locked(false);
    if (gate_.state == GateState::On)
        log_locked("HUD editor: on (layout \"" + active_locked().name + "\", " + std::to_string(kHudElementCount) + " parts)");
    else
        log_locked("HUD editor: off (" + gate_.message + ")");
}

void HudEditor::tick() {
    std::unique_lock<std::mutex> lock(mu_);
    refresh_settings_locked();
    const uint64_t now = now_locked();
    fault_ = deps_.fault_code ? deps_.fault_code() : 0;
    if (could_turn_on_locked() && deps_.other_hud_mod &&
        (!other_checked_ || now - last_conflict_check_ >= kConflictCheckMs)) {
        lock.unlock();  // the process check can be slow: never under the editor's lock
        const std::string mod = deps_.other_hud_mod();
        lock.lock();
        other_mod_ = mod;
        other_checked_ = true;
        last_conflict_check_ = now;
    }
    update_gate_locked(true);
    if (dirty_ && now - last_change_ >= kSaveDelayMs) write_active_locked();
    if (deps_.scale_state && !scale_off_logged_ && deps_.scale_state().off) {
        scale_off_logged_ = true;
        log_locked(
            "HUD editor: scale is off until the game restarts (the game's transform stack did not match at a close); move "
            "and hide go on");
    }
    const Hotkeys::Pressed pressed = hotkeys_.poll(deps_.key_down);
    if (pressed.reset) {
        const LayoutResult res = reset_all_locked();
        log_locked("HUD editor: the reset key put every part back (" + res.message + ")");
    }
    const bool toggle = pressed.page && deps_.toggle_page;
    lock.unlock();
    if (toggle) deps_.toggle_page();  // outside the lock: it reaches into the console
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

std::string HudEditor::find_layout_locked(const std::string& name_or_slug) const {
    const std::string wanted = trim_profile_name(name_or_slug);
    if (wanted.empty()) return "";
    const std::string slug = profile_slug(wanted);
    if (layouts_.count(slug)) return slug;
    const std::string low = lower(wanted);
    for (const auto& [key, l] : layouts_)
        if (lower(l.name) == low || key == low) return key;
    return "";
}

std::string HudEditor::layout_list_locked() const {
    std::vector<std::string> names;
    for (const auto& [slug, l] : layouts_) names.push_back(l.name);
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        const std::string x = lower(a), y = lower(b);
        return x != y ? x < y : a < b;
    });
    std::string out;
    for (size_t i = 0; i < names.size(); ++i) out += (i ? ", " : "") + names[i];
    return out;
}

LayoutResult HudEditor::not_found_locked(const std::string& name) const {
    return {false, "no layout \"" + name + "\"; layouts: " + layout_list_locked()};
}

void HudEditor::set_active_setting_locked(const std::string& name) {
    if (!deps_.settings) return;
    // The slug, not the name: the ini cannot hold every name (a ';' cuts a value, a trailing '%' is dropped, the file is
    // in the code page). select_layout_locked resolves slugs.
    deps_.settings->set(kEmfSettingsId, std::string(kPrefix) + "active_layout", profile_slug(name));
    // our own write: the next tick must not re-resolve it as a change from elsewhere
    settings_version_ = deps_.settings->version();
}

void HudEditor::use_default_locked() {
    if (!layouts_.count("default")) {
        HudLayout d;
        d.name = kDefaultLayoutName;
        layouts_["default"] = d;
    }
    active_slug_ = "default";
}

LayoutResult HudEditor::reset_all_locked() {
    HudLayout& a = active_locked();
    if (a.is_default()) return {true, "the HUD is already the game's own"};
    const bool backup = active_slug_ != "before-reset";
    if (backup) {
        HudLayout before = a;
        before.name = kBeforeResetName;
        layouts_["before-reset"] = before;
        write_layout_locked(before);
    }
    HudLayout& now_active = active_locked();
    now_active.elements = {};
    publish_locked();
    write_active_locked();
    if (!backup) return {true, "every part is back where the game draws it"};
    return {true,
            "every part is back where the game draws it; the layout before is saved as \"Before reset\" (hud.layout load "
            "\"Before reset\" brings it back)"};
}

LayoutResult HudEditor::reset_all() {
    std::lock_guard<std::mutex> lock(mu_);
    return reset_all_locked();
}

LayoutResult HudEditor::save_as(const std::string& name) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string trimmed = trim_profile_name(name);
    const std::string problem = profile_name_problem(trimmed);
    if (!problem.empty()) return {false, problem};
    const std::string slug = profile_slug(trimmed);
    const bool existed = layouts_.count(slug) > 0;
    if (dirty_) write_active_locked();  // a pending edit belongs to the layout being left
    HudLayout copy = active_locked();
    copy.name = trimmed;
    layouts_[slug] = copy;
    active_slug_ = slug;
    dirty_ = false;
    write_layout_locked(layouts_[slug]);
    set_active_setting_locked(trimmed);
    publish_locked();
    return {true, "saved as \"" + trimmed + "\"" + (existed ? " (replaced the layout of that name)" : "")};
}

LayoutResult HudEditor::load(const std::string& name_or_slug) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string key = find_layout_locked(name_or_slug);
    if (key.empty()) return not_found_locked(name_or_slug);
    if (dirty_) write_active_locked();  // a pending save belongs to the layout being left
    active_slug_ = key;
    set_active_setting_locked(layouts_[key].name);
    publish_locked();
    return {true, "layout \"" + layouts_[key].name + "\" is in use"};
}

LayoutResult HudEditor::remove(const std::string& name_or_slug) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string key = find_layout_locked(name_or_slug);
    if (key.empty()) return not_found_locked(name_or_slug);
    const std::string name = layouts_[key].name;
    const std::wstring dir = deps_.dir;
    auto log = deps_.log;
    auto job = [dir, key, log] {
        std::string err;
        if (!delete_layout_file(dir, key, &err) && log)
            log("HUD editor: could not delete emf\\hud\\" + key + ".json (" + err + ")");
    };
    if (deps_.run)
        deps_.run(job);
    else
        job();
    const bool was_active = key == active_slug_;
    layouts_.erase(key);
    if (!was_active) return {true, "layout \"" + name + "\" deleted"};
    dirty_ = false;
    use_default_locked();
    set_active_setting_locked(layouts_["default"].name);
    publish_locked();
    return {true, "layout \"" + name + "\" deleted; \"Default\" is in use"};
}

void HudEditor::page_frame(size_t selected) {
    std::lock_guard<std::mutex> lock(mu_);
    refresh_settings_locked();
    const uint64_t now = now_locked();
    page_seen_ = true;
    last_page_frame_ = now;
    selected_ = selected < kHudElementCount ? selected : 0;
    publish_locked();
    if (dirty_ && now - last_change_ >= kSaveDelayMs) write_active_locked();
}

}  // namespace emf::core::hud
