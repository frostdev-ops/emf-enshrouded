#pragma once
// Which of the game's files the content loader builds the shadow copy from: never from data that EML or ShroudForge
// already patched in place. Both keep the vanilla index as <name>.kfc.bak and patch <name>.kfc; the .bak carries the
// same version string as the .kfc it was made from.
//   no .bak, or a .bak of another game build (another version string)   the base is <name>.kfc
//   the same version, size and container count                         not patched: the base is <name>.kfc
//   the same version but another size or container count               patched: the base is <name>.kfc.bak, and only
//                                                                        its containers are linked
#include <cstdint>
#include <optional>
#include <string>

#include "content_io.h"
#include "kfc_header.h"

namespace emf::loader {

struct BaseFile {
    std::optional<KfcHeader> header;
    uint64_t size = 0;
    bool exists = false;
};

struct BaseChoice {
    bool ok = false;
    bool use_bak = false;    // the base is <name>.kfc.bak
    uint32_t dat_count = 0;  // the base's containers: <name>_000.dat to <name>_(dat_count-1).dat
    std::string why;         // which base and why; when !ok, why there is none
};

constexpr uint32_t kMaxContainers = 10000;  // a header that claims more is not one EMF knows

// `file_name`: "enshrouded" (the game) or "enshrouded_server".
inline BaseChoice choose_base(const BaseFile& kfc, const BaseFile& bak, const std::string& file_name = "enshrouded") {
    BaseChoice c;
    const std::string k = file_name + ".kfc", b = file_name + ".kfc.bak";
    if (!kfc.exists) {
        c.why = k + " is not in the game folder";
        return c;
    }
    if (!kfc.header) {
        c.why = k + "'s header could not be read (a game build EMF does not know?)";
        return c;
    }
    const KfcHeader* base = &*kfc.header;
    if (bak.exists) {
        if (!bak.header) {
            c.why = b + "'s header could not be read, so EMF cannot tell whether " + k +
                    " was patched in place (by EML or ShroudForge)";
            return c;
        }
        if (bak.header->version != kfc.header->version) {
            c.why = "the base is " + k + " (" + b + " is from another game build)";
        } else if (bak.size == kfc.size && bak.header->containers == kfc.header->containers) {
            c.why = "the base is " + k + " (" + b + " is the same file)";
        } else {
            c.use_bak = true;
            base = &*bak.header;
            c.why = "the game's " + k + " was patched in place (by EML or ShroudForge): the base is " + b;
        }
    } else {
        c.why = "the base is " + k;
    }
    if (base->containers == 0 || base->containers > kMaxContainers) {
        c.use_bak = false;
        c.why = (base == &*kfc.header ? k : b) + " says it has " + std::to_string(base->containers) +
                " containers (not a game index EMF knows)";
        return c;
    }
    c.ok = true;
    c.dat_count = base->containers;
    c.why += " (" + std::to_string(c.dat_count) + " containers)";
    return c;
}

inline BaseFile read_base_file(const std::wstring& path) {
    BaseFile f;
    const cio::Stat s = cio::stat(path);
    if (!s.exists || s.dir) return f;
    f.exists = true;
    f.size = s.size;
    f.header = read_kfc_header(path);
    return f;
}

}  // namespace emf::loader
