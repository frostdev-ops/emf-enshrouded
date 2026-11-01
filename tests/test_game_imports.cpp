// What the DLLs that run inside the game import. EMF's core starts while the game is still starting up (Steam's client
// initialises then), so the game-side DLLs must not use Windows' bcrypt: its first use takes a lock and loads a DLL,
// and with the Steam overlay's hooks that deadlocked against the game's main thread doing the same (the game hung
// before its window, 2026-11-01). EMF's own SHA-256 (src/common/sha256.cpp) replaces it there.
#include <doctest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "common/sha256.h"

namespace {
std::vector<unsigned char> read_all(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

template <class T>
bool get(const std::vector<unsigned char>& b, size_t at, T* out) {
    if (at > b.size() || b.size() - at < sizeof(T)) return false;
    std::memcpy(out, b.data() + at, sizeof(T));
    return true;
}

// The DLL names in a PE file's import directory (lowercase); empty when the file is missing or not a PE.
std::vector<std::string> imported_dlls(const std::string& path) {
    const auto b = read_all(path);
    std::vector<std::string> names;
    uint32_t pe = 0;
    if (!get(b, 0x3C, &pe)) return names;
    uint16_t sections = 0, opt_size = 0;
    if (!get(b, pe + 6, &sections) || !get(b, pe + 20, &opt_size)) return names;
    const size_t opt = pe + 24;
    uint32_t import_rva = 0;
    if (!get(b, opt + 112 + 8, &import_rva) || import_rva == 0) return names;  // PE32+: data directory 1
    const size_t sect = opt + opt_size;
    auto to_off = [&](uint32_t rva, size_t* off) {
        for (uint16_t i = 0; i < sections; ++i) {
            uint32_t va = 0, vsize = 0, raw = 0, rawsize = 0;
            const size_t s = sect + i * 40;
            if (!get(b, s + 8, &vsize) || !get(b, s + 12, &va) || !get(b, s + 16, &rawsize) || !get(b, s + 20, &raw))
                return false;
            if (rva >= va && rva < va + std::max(vsize, rawsize)) {
                *off = raw + (rva - va);
                return true;
            }
        }
        return false;
    };
    size_t d = 0;
    if (!to_off(import_rva, &d)) return names;
    for (;; d += 20) {
        uint32_t name_rva = 0, first_thunk = 0;
        if (!get(b, d + 12, &name_rva) || !get(b, d + 16, &first_thunk)) break;
        if (name_rva == 0 && first_thunk == 0) break;
        size_t n = 0;
        if (!to_off(name_rva, &n)) break;
        std::string s;
        while (n < b.size() && b[n]) s += static_cast<char>(std::tolower(b[n++]));
        names.push_back(s);
    }
    return names;
}
}  // namespace

TEST_CASE("game-side DLLs: emf_core.dll and the loader import no bcrypt.dll") {
    for (const char* rel : {"/dist/emf/emf_core.dll", "/dist/dinput8.dll"}) {
        const auto names = imported_dlls(std::string(EMF_BINARY_DIR) + rel);
        INFO(std::string(rel));
        REQUIRE_FALSE(names.empty());  // the file is there and its imports were read
        CHECK(std::find(names.begin(), names.end(), "kernel32.dll") != names.end());
        CHECK(std::find(names.begin(), names.end(), "bcrypt.dll") == names.end());
    }
}

TEST_CASE("sha256: the standard vectors, also when fed in pieces") {
    CHECK(emf::sha256_hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(emf::sha256_hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";  // 56 bytes: padding spills
    CHECK(emf::sha256_hex(two_blocks.data(), two_blocks.size()) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const std::string long_one = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    CHECK(emf::sha256_hex(long_one.data(), long_one.size()) ==
          "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    const std::string million(1000000, 'a');
    CHECK(emf::sha256_hex(million.data(), million.size()) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    for (size_t piece : {1u, 7u, 63u, 64u, 65u, 1000u}) {
        emf::Sha256 h;
        for (size_t at = 0; at < million.size(); at += piece) h.update(million.data() + at, std::min(piece, million.size() - at));
        INFO(piece);
        CHECK(h.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }
}
