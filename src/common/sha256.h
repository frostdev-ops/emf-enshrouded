#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace emf {

// SHA-256 (FIPS 180-4), EMF's own: no Windows bcrypt. EMF's core starts while the game is starting up, and bcrypt's
// first use there deadlocked against the game's main thread (tests/test_game_imports.cpp says more).
class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t size);
    std::string hex();  // the lowercase hex digest; finishes the hash (update after it does nothing)

private:
    void block(const unsigned char* p);
    uint32_t h_[8];
    unsigned char buf_[64];
    size_t buf_len_ = 0;
    uint64_t total_ = 0;
    bool done_ = false;
};

// Lowercase hex SHA-256. sha256_file: empty string when the file cannot be read.
std::string sha256_hex(const void* data, size_t size);
std::string sha256_file(const std::wstring& path);

}  // namespace emf
