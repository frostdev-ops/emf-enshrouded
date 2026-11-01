#pragma once
// "Play without EMF mods" and "Play without any mods" (EMF.exe's Play menu): a one-shot flag file, emf\play_once.txt,
// that EMF.exe writes just before it asks Steam to start the game. The loader reads it and deletes it before it does
// anything else, so it counts for one game start only, and only within kPlayOnceMaxAge seconds of being written: a
// start that never happened (Steam did not start the game, the PC went off) never leaves EMF off later. A flag the
// loader cannot delete is not obeyed either (it would count again at the next start). The dedicated server ignores it.
//
//   emf_play_once=1
//   mode=no-emf      EMF's core and its mods do not start; the DLL mods in dinput8-chain.ini do (other tools)
//   mode=no-mods     neither EMF nor the DLL mods in dinput8-chain.ini start
//   written=<seconds since 1970, UTC>
//
// Header-only and free of Windows calls, so the tests run it with the file reads injected.
#include <cstdint>
#include <optional>
#include <string>

namespace emf::loader {

enum class PlayOnce { Normal, NoEmf, NoMods };

constexpr int64_t kPlayOnceMaxAge = 600;     // ten minutes: a cold Steam start with an update can take a while
constexpr int64_t kPlayOnceClockSlack = 120;  // "written" this far after now still counts (the clock was set back)
constexpr const wchar_t* kPlayOnceFile = L"emf\\play_once.txt";
// In EMF's loader (exported, like emf_loader_identity): EMF.exe offers the Play menu only for a loader that has it.
#define EMF_PLAY_ONCE_MARKER "EMF-PLAY-ONCE-1"

struct PlayOnceRead {
    PlayOnce mode = PlayOnce::Normal;
    std::string why;  // the loader's log line ("" when there was no flag)
};

inline std::string play_once_text(PlayOnce mode, int64_t now) {
    return std::string("emf_play_once=1\r\nmode=") + (mode == PlayOnce::NoMods ? "no-mods" : "no-emf") +
           "\r\nwritten=" + std::to_string(now) + "\r\n";
}

inline std::string play_once_age(int64_t seconds) {
    const int64_t minutes = seconds / 60, hours = minutes / 60, days = hours / 24;
    if (minutes < 120) return std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
    if (hours < 48) return std::to_string(hours) + " hours";
    return std::to_string(days) + " days";
}

inline PlayOnceRead parse_play_once(const std::string& text, int64_t now) {
    PlayOnceRead r;
    std::string version, mode, written;
    size_t from = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    while (from < text.size()) {
        size_t to = text.find('\n', from);
        if (to == std::string::npos) to = text.size();
        std::string line = text.substr(from, to - from);
        from = to + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "emf_play_once") version = value;
        else if (key == "mode") mode = value;
        else if (key == "written") written = value;
    }
    const PlayOnce wanted = mode == "no-emf" ? PlayOnce::NoEmf : mode == "no-mods" ? PlayOnce::NoMods : PlayOnce::Normal;
    const bool digits = !written.empty() && written.size() <= 15 && written.find_first_not_of("0123456789") == std::string::npos;
    if (version != "1" || wanted == PlayOnce::Normal || !digits) {
        r.why = "emf\\play_once.txt is not a flag this EMF knows: ignored";
        return r;
    }
    const int64_t at = std::stoll(written);
    if (at > now + kPlayOnceClockSlack) {
        r.why = "emf\\play_once.txt says it was written later than now (did the PC's clock change?): ignored";
        return r;
    }
    if (now - at > kPlayOnceMaxAge) {
        r.why = "emf\\play_once.txt was written " + play_once_age(now - at) +
                " ago, too long ago for this start (EMF.exe's Play asked for a start that did not happen then): ignored";
        return r;
    }
    r.mode = wanted;
    r.why = wanted == PlayOnce::NoEmf
                ? "this start is without EMF mods (Play without EMF mods in EMF.exe): EMF's core and its mods do not start; "
                  "the DLL mods in dinput8-chain.ini do. The next start loads EMF again."
                : "this start is without mods (Play without any mods in EMF.exe): neither EMF nor the DLL mods in "
                  "dinput8-chain.ini start. The next start loads them again.";
    return r;
}

// The flag, read and deleted before anything else. `read()` gives the file's bytes (nullopt when there is none);
// `remove()` deletes it (false when it could not). A flag that stays would count again: then EMF starts as usual.
template <class Read, class Remove>
PlayOnceRead take_play_once(Read read, Remove remove, int64_t now) {
    const std::optional<std::string> text = read();
    if (!text) return {};
    const bool removed = remove();
    PlayOnceRead r = parse_play_once(*text, now);
    if (!removed && r.mode != PlayOnce::Normal) {
        r.mode = PlayOnce::Normal;
        r.why = "could not delete emf\\play_once.txt, so EMF starts as usual (a flag that stays would count at every "
                "start): delete the file by hand";
    } else if (r.mode == PlayOnce::Normal) {
        r.why += removed ? ", and deleted" : "; EMF could not delete it: delete the file by hand";
    }
    return r;
}

struct StartPlan {
    bool chain = true;  // the DLL mods in dinput8-chain.ini
    bool core = true;   // emf_core.dll (and its Vulkan layer, which is the same DLL)
};

inline StartPlan start_plan(PlayOnce mode) {
    StartPlan p;
    p.core = mode == PlayOnce::Normal;
    p.chain = mode != PlayOnce::NoMods;
    return p;
}

}  // namespace emf::loader
