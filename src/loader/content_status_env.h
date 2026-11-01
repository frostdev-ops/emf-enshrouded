#pragma once
// How EML content went at this start, as the loader tells EMF's core: the process's environment variable
// EMF_CONTENT_STATUS (never a file, so a state left from an earlier start is never taken for this one's), set before
// the core starts. One line:  <kind> TAB <mods> TAB <ms> TAB <message>
//   kind  off       content is off ([content] mode=on only: the core shows why)
//         current   the shadow copy from an earlier build is used (mods and ms are that build's)
//         built     built at this start
//         failed    EMF owns the content but it could not be built (or held): the game runs without it (and the
//                   core backs up the saves)
//         failed_again  the same failure as at an earlier start with the same files: the warning, no second backup
// The message is cut to 512 bytes (on a UTF-8 letter's boundary). The loader sets or clears the variable at every
// start, so a value a parent process passed on is never taken; the core clears it once read.
// The core turns it into the player's lines (src/core/content_status.h).
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace emf::loader {

constexpr const wchar_t* kContentStatusVariable = L"EMF_CONTENT_STATUS";
constexpr size_t kContentStatusMaxMessage = 512;

struct ContentStatus {
    std::string kind;
    size_t mods = 0;
    uint64_t ms = 0;
    std::string message;
};

inline std::string content_status_text(const ContentStatus& s) {
    std::string msg;
    bool space = false;  // a run of tabs and line breaks becomes one space
    for (const char c : s.message) {
        if (c == '\t' || c == '\r' || c == '\n') {
            if (!space) msg += ' ';
            space = true;
        } else {
            msg += c;
            space = false;
        }
    }
    if (msg.size() > kContentStatusMaxMessage) {
        size_t n = kContentStatusMaxMessage;
        while (n > 0 && (static_cast<unsigned char>(msg[n]) & 0xC0) == 0x80) --n;  // not inside a UTF-8 letter
        msg.resize(n);
    }
    return s.kind + "\t" + std::to_string(s.mods) + "\t" + std::to_string(s.ms) + "\t" + msg;
}

inline std::optional<ContentStatus> parse_content_status(std::string_view text) {
    const size_t a = text.find('\t');
    const size_t b = a == std::string_view::npos ? a : text.find('\t', a + 1);
    const size_t c = b == std::string_view::npos ? b : text.find('\t', b + 1);
    if (c == std::string_view::npos || a == 0) return std::nullopt;
    const auto number = [](std::string_view s, uint64_t* out) {
        if (s.empty() || s.size() > 19) return false;
        uint64_t v = 0;
        for (const char ch : s) {
            if (ch < '0' || ch > '9') return false;
            v = v * 10 + static_cast<uint64_t>(ch - '0');
        }
        *out = v;
        return true;
    };
    ContentStatus s;
    uint64_t mods = 0;
    if (!number(text.substr(a + 1, b - a - 1), &mods) || !number(text.substr(b + 1, c - b - 1), &s.ms)) return std::nullopt;
    s.kind = std::string(text.substr(0, a));
    s.mods = static_cast<size_t>(mods);
    s.message = std::string(text.substr(c + 1));
    return s;
}

}  // namespace emf::loader
