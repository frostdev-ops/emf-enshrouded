// The shared signature file (docs/modding/emf_signatures.patterns): EMF's engine signatures in the mod pattern format,
// for other loaders (one job after a game update instead of one per loader).
#include <doctest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "common/pattern_file.h"
#include "common/signatures.h"
#include "common/signatures_export.h"

TEST_CASE("signatures export: every signature as a pattern line, parsed back without errors, with its RVAs") {
    const std::string text = emf::export_signature_patterns();
    const emf::PatternFile pf = emf::parse_pattern_file(text);
    CHECK(pf.errors.empty());
    REQUIRE(pf.entries.size() == emf::all_signatures().size());
    for (size_t i = 0; i < pf.entries.size(); ++i) {
        const emf::SigDef& def = emf::all_signatures()[i];
        CAPTURE(def.name);
        CHECK(pf.entries[i].name == def.name);
        CHECK(pf.entries[i].query.bytes == def.pattern);
        if (def.call_at >= 0) {  // the function is the target of the E8 call at match + call_at
            CHECK(pf.entries[i].query.follow_rel32);
            CHECK(pf.entries[i].query.offset == def.call_at + 1);
        } else {
            CHECK_FALSE(pf.entries[i].query.follow_rel32);
            CHECK(pf.entries[i].query.offset == 0);
        }
    }
    CHECK(text.find("MIT") != std::string::npos);
    CHECK(text.find("5d9ef2f140b8ed49e30dbee655841b89d08baec20f799a1817e171a18a850c77") != std::string::npos);  // a known build
    CHECK(text.find("1287369=0x") != std::string::npos);  // an RVA on it
}

TEST_CASE("signatures export: the committed docs/modding/emf_signatures.patterns is the table as it is now") {
    std::ifstream in(std::string(EMF_SOURCE_DIR) + "/docs/modding/emf_signatures.patterns", std::ios::binary);
    REQUIRE_MESSAGE(in, "write it with: build\\msvc\\emf_sigcheck.exe --export docs\\modding\\emf_signatures.patterns");
    std::stringstream s;
    s << in.rdbuf();
    std::string file = s.str();
    std::string clean;
    for (char c : file)
        if (c != '\r') clean += c;  // a checkout with CRLF line ends
    CHECK_MESSAGE(clean == emf::export_signature_patterns(),
                  "the signatures changed: build\\msvc\\emf_sigcheck.exe --export docs\\modding\\emf_signatures.patterns");
}
