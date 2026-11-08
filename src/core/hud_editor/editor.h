#pragma once
// The HUD editor: the state of the HUD layouts, the gate, the hooks (installed once), the edits published to the hooks as
// an immutable snapshot, and the saves. One mutex guards all state; the detours take no lock (they read the snapshot).
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "core/hud_editor/gate.h"
#include "core/hud_editor/hook.h"
#include "core/hud_editor/keys.h"
#include "core/hud_editor/layout.h"
#include "core/hud_editor/layout_store.h"
#include "core/hud_editor/scale.h"
#include "core/hud_editor/snapshot.h"
#include "core/settings.h"

namespace emf::core::hud {

struct HudEditorDeps {
    SettingsStore* settings = nullptr;                    // EMF's settings, hud_editor.* declared
    std::wstring dir;                                     // <game>\emf\hud
    uint32_t build_number = 0;                            // written into layout files
    GateInput gate;                                       // server_exe, build_known, build_label (the editor fills the rest)
    std::function<bool(std::string* why)> install_hooks;  // called once, when the gate first says On; empty = nothing to hook
    std::function<std::string()> other_hud_mod;           // "" or a loaded HUD mod's name
    std::function<uint32_t()> fault_code;                 // the hooks' fault, 0 = none
    std::function<LiveInfo(size_t)> live;
    std::function<ScaleState()> scale_state;              // empty = not available
    std::function<bool(uint32_t vk)> key_down;            // a key held now with the game in front
    std::function<void()> toggle_page;                    // opens (or closes) the console on the HUD page
    std::function<void(std::function<void()>)> run;       // file writes; empty = on the caller's thread
    std::function<uint64_t()> now_ms;
    std::function<void(const std::string&)> log;
};

struct HudView {
    Gate gate;
    std::string layout_name;
    std::vector<std::string> layouts;  // display names, sorted ignoring case, the active one always in it
    std::array<ElementLayout, kHudElementCount> elements{};
    std::array<LiveInfo, kHudElementCount> live{};
    ScaleState scale;           // from scale_state()
    bool scale_works = false;   // scale.available && !scale.off
    std::string scale_note;     // "" when it works, else the reason
};

enum class Save { Now, Later };
struct LayoutResult {
    bool ok = false;
    std::string message;
};

class HudEditor {
public:
    static constexpr uint64_t kSaveDelayMs = 500, kConflictCheckMs = 5000, kPageGoneMs = 300, kBlinkMs = 250;
    explicit HudEditor(HudEditorDeps deps);
    void start();
    void tick();
    const SnapshotBox<HudSnapshot>& snapshots() const { return box_; }
    HudView view() const;
    void set_offset(size_t element, float x_percent, float y_percent, Save save);
    void nudge(size_t element, float dx_percent, float dy_percent, Save save);
    void set_scale(size_t element, float scale, Save save);
    void set_hidden(size_t element, bool hidden, Save save);
    void reset(size_t element, Save save);
    void flush();  // a pending Later save now

    // Named layouts. Each result message is the reply text without the "hud: " prefix.
    LayoutResult reset_all();
    LayoutResult save_as(const std::string& name);
    LayoutResult load(const std::string& name_or_slug);
    LayoutResult remove(const std::string& name_or_slug);
    void page_frame(size_t selected);  // the HUD page is drawn this frame; the selected part blinks

private:
    // all with mu_ held
    uint64_t now_locked() const;
    void log_locked(const std::string& line);
    void read_settings_locked();
    void select_layout_locked(const std::string& wanted, bool first);
    HudLayout& active_locked();
    const HudLayout& active_locked() const;
    GateInput gate_input_locked() const;
    void update_gate_locked(bool log_change);
    void publish_locked();
    void mark_changed_locked(Save save);
    void write_active_locked();
    void write_layout_locked(const HudLayout& l);
    void refresh_settings_locked();
    std::string find_layout_locked(const std::string& name_or_slug) const;  // the map key, or ""
    std::string layout_list_locked() const;
    LayoutResult not_found_locked(const std::string& name) const;
    LayoutResult reset_all_locked();
    void use_default_locked();
    void set_active_setting_locked(const std::string& name);

    HudEditorDeps deps_;
    mutable std::mutex mu_;
    SnapshotBox<HudSnapshot> box_;
    uint64_t serial_ = 0;

    // settings
    bool enabled_ = true, show_outlines_ = true, allow_other_ = false;
    uint64_t settings_version_ = ~0ull;
    std::string warned_layout_;  // a missing layout name already logged

    // layouts, by slug
    std::map<std::string, HudLayout> layouts_;
    std::string active_slug_;

    // gate and hooks
    Gate gate_;
    bool gate_known_ = false;
    GateState logged_state_ = GateState::UnknownBuild;
    bool hooked_ = false;
    std::string hook_error_, other_mod_;
    uint32_t fault_ = 0;
    uint64_t last_conflict_check_ = 0;
    bool scale_off_logged_ = false;

    // keys and the page
    Hotkeys hotkeys_;
    bool page_seen_ = false;
    uint64_t last_page_frame_ = 0;
    size_t selected_ = 0;

    // saves
    bool dirty_ = false;
    uint64_t last_change_ = 0;
};

}  // namespace emf::core::hud
