#include "sha256.h"

#include <windows.h>

#include <cstring>
#include <vector>

namespace emf {
namespace {
constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
}  // namespace

Sha256::Sha256() {
    static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(h_, init, sizeof h_);
}

void Sha256::block(const unsigned char* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) |
               uint32_t(p[i * 4 + 3]);
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + s1 + ch + kK[i] + w[i];
        const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
}

void Sha256::update(const void* data, size_t size) {
    if (done_ || size == 0) return;
    const auto* p = static_cast<const unsigned char*>(data);
    total_ += size;
    if (buf_len_) {
        const size_t take = (size < 64 - buf_len_) ? size : 64 - buf_len_;
        std::memcpy(buf_ + buf_len_, p, take);
        buf_len_ += take;
        p += take;
        size -= take;
        if (buf_len_ < 64) return;
        block(buf_);
        buf_len_ = 0;
    }
    for (; size >= 64; p += 64, size -= 64) block(p);
    if (size) {
        std::memcpy(buf_, p, size);
        buf_len_ = size;
    }
}

std::string Sha256::hex() {
    if (!done_) {
        const uint64_t bits = total_ * 8;
        unsigned char pad[72] = {0x80};
        const size_t pad_len = (buf_len_ < 56) ? 56 - buf_len_ : 120 - buf_len_;
        for (int i = 0; i < 8; ++i) pad[pad_len + i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        update(pad, pad_len + 8);
        done_ = true;
    }
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint32_t v : h_)
        for (int s = 28; s >= 0; s -= 4) out += digits[(v >> s) & 15];
    return out;
}

std::string sha256_hex(const void* data, size_t size) {
    Sha256 h;
    h.update(data, size);
    return h.hex();
}

std::string sha256_file(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    Sha256 h;
    std::vector<unsigned char> buf(1 << 20);
    DWORD got = 0;
    bool good = true;
    for (;;) {
        if (!ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) {
            good = false;
            break;
        }
        if (got == 0) break;
        h.update(buf.data(), got);
    }
    CloseHandle(f);
    return good ? h.hex() : std::string{};
}

}  // namespace emf
