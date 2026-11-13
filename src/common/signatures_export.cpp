// EMF's engine signatures as a pattern file (signatures_export.h).
#include "common/signatures_export.h"

#include <cstdio>

#include "common/signatures.h"

namespace emf {

std::string export_signature_patterns() {
    std::string out =
        "# EMF's engine signatures: the game functions EMF finds by their bytes, one per line in EMF's pattern format\n"
        "# (docs/modding/reference.md, v14 find_pattern; src/common/pattern_file.h). MIT licence (EMF contributors): use\n"
        "# them in any loader. Generated from src/common/signatures.cpp by `emf_sigcheck --export`: do not edit by hand.\n"
        "# Check them against a game exe without starting the game:\n"
        "#   emf_sigcheck --patterns emf_signatures.patterns \"<game>\\enshrouded.exe\"\n"
        "# Each pattern must match exactly once in the exe's .text. `offset=N ; rel32`: the function is the target of the\n"
        "# call whose rel32 is at match + N. The comment above a line gives each known build's RVA of the function\n"
        "# (\"s\": the dedicated server's exe; \"client only\": the server has no such function).\n"
        "# Known builds (the game's \"Build version\", the exe's sha256):\n";
    char b[256];
    for (const KnownBuild& k : known_builds()) {
        std::snprintf(b, sizeof b, "#   %u%s %s\n", k.number, k.server ? "s" : " ", k.sha256);
        out += b;
    }
    const auto builds = known_builds();
    for (const SigDef& def : all_signatures()) {
        out += "\n# " + std::string(def.name) + ": " + (def.required ? "required" : "optional") + ";";
        bool client_only = false;
        for (size_t i = 0; i < builds.size(); ++i) {
            if (absent_on(def, static_cast<int>(i))) {
                client_only = client_only || builds[i].server;
                continue;
            }
            if (!rva_checked(def, static_cast<int>(i))) continue;
            std::snprintf(b, sizeof b, " %u%s=0x%x", builds[i].number, builds[i].server ? "s" : "", def.rva[i]);
            out += b;
        }
        if (client_only) out += " (client only)";
        if (def.call_at >= 0 && def.call_target_pattern)
            out += "\n# the call's target starts with: " + std::string(def.call_target_pattern);
        out += "\n" + std::string(def.name) + " = " + def.pattern;
        if (def.call_at >= 0) {
            std::snprintf(b, sizeof b, " ; offset=%d ; rel32", def.call_at + 1);
            out += b;
        }
        out += "\n";
    }
    return out;
}

}  // namespace emf
