#include "crypto.hpp"

#include <algorithm>
#include <cstring>
#include <random>

namespace memdb::detail::crypto {

namespace {

inline std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

const std::uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

void sha256Compress(std::uint32_t h[8], const std::uint8_t block[64]) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16) |
               (std::uint32_t(block[i * 4 + 2]) << 8) | std::uint32_t(block[i * 4 + 3]);
    for (int i = 16; i < 64; ++i) {
        std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        std::uint32_t ch = (e & f) ^ (~e & g);
        std::uint32_t t1 = hh + S1 + ch + SHA256_K[i] + w[i];
        std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        std::uint32_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

inline std::uint32_t rotl(std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

void chachaQuarterRound(std::uint32_t& a, std::uint32_t& b, std::uint32_t& c, std::uint32_t& d) {
    a += b; d ^= a; d = rotl(d, 16);
    c += d; b ^= c; b = rotl(b, 12);
    a += b; d ^= a; d = rotl(d, 8);
    c += d; b ^= c; b = rotl(b, 7);
}

void chacha20Block(std::uint8_t out[64], const std::uint8_t key[32], std::uint32_t counter,
                   const std::uint8_t nonce[12]) {
    std::uint32_t state[16];
    state[0] = 0x61707865; state[1] = 0x3320646e; state[2] = 0x79622d32; state[3] = 0x6b206574;
    for (int i = 0; i < 8; ++i)
        state[4 + i] = (std::uint32_t(key[i * 4]) | (std::uint32_t(key[i * 4 + 1]) << 8) |
                        (std::uint32_t(key[i * 4 + 2]) << 16) | (std::uint32_t(key[i * 4 + 3]) << 24));
    state[12] = counter;
    for (int i = 0; i < 3; ++i)
        state[13 + i] = (std::uint32_t(nonce[i * 4]) | (std::uint32_t(nonce[i * 4 + 1]) << 8) |
                         (std::uint32_t(nonce[i * 4 + 2]) << 16) | (std::uint32_t(nonce[i * 4 + 3]) << 24));
    std::uint32_t x[16];
    std::memcpy(x, state, sizeof(state));
    for (int i = 0; i < 10; ++i) {
        chachaQuarterRound(x[0], x[4], x[8], x[12]);
        chachaQuarterRound(x[1], x[5], x[9], x[13]);
        chachaQuarterRound(x[2], x[6], x[10], x[14]);
        chachaQuarterRound(x[3], x[7], x[11], x[15]);
        chachaQuarterRound(x[0], x[5], x[10], x[15]);
        chachaQuarterRound(x[1], x[6], x[11], x[12]);
        chachaQuarterRound(x[2], x[7], x[8], x[13]);
        chachaQuarterRound(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; ++i) {
        std::uint32_t v = x[i] + state[i];
        out[i * 4] = static_cast<std::uint8_t>(v);
        out[i * 4 + 1] = static_cast<std::uint8_t>(v >> 8);
        out[i * 4 + 2] = static_cast<std::uint8_t>(v >> 16);
        out[i * 4 + 3] = static_cast<std::uint8_t>(v >> 24);
    }
}

} // namespace

std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t len) {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<std::uint8_t> msg(data, data + len);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56)
        msg.push_back(0);
    std::uint64_t bitLen = static_cast<std::uint64_t>(len) * 8;
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<std::uint8_t>((bitLen >> (8 * i)) & 0xFF));
    for (std::size_t off = 0; off < msg.size(); off += 64)
        sha256Compress(h, &msg[off]);
    std::array<std::uint8_t, 32> out;
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<std::uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(h[i]);
    }
    return out;
}

std::array<std::uint8_t, 32> hmacSha256(const std::uint8_t* key, std::size_t keyLen,
                                        const std::uint8_t* msg, std::size_t msgLen) {
    std::uint8_t keyBlock[64];
    std::memset(keyBlock, 0, sizeof(keyBlock));
    if (keyLen > 64) {
        auto kh = sha256(key, keyLen);
        std::memcpy(keyBlock, kh.data(), 32);
    } else {
        std::memcpy(keyBlock, key, keyLen);
    }
    std::uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = keyBlock[i] ^ 0x36;
        opad[i] = keyBlock[i] ^ 0x5C;
    }
    std::vector<std::uint8_t> inner(ipad, ipad + 64);
    inner.insert(inner.end(), msg, msg + msgLen);
    auto innerHash = sha256(inner.data(), inner.size());
    std::vector<std::uint8_t> outer(opad, opad + 64);
    outer.insert(outer.end(), innerHash.begin(), innerHash.end());
    return sha256(outer.data(), outer.size());
}

std::vector<std::uint8_t> pbkdf2HmacSha256(const std::string& password, const std::uint8_t* salt,
                                           std::size_t saltLen, std::uint32_t iterations, std::size_t dkLen) {
    std::vector<std::uint8_t> out;
    out.reserve(dkLen);
    std::uint32_t blockIndex = 1;
    while (out.size() < dkLen) {
        std::vector<std::uint8_t> msg(salt, salt + saltLen);
        msg.push_back(static_cast<std::uint8_t>(blockIndex >> 24));
        msg.push_back(static_cast<std::uint8_t>(blockIndex >> 16));
        msg.push_back(static_cast<std::uint8_t>(blockIndex >> 8));
        msg.push_back(static_cast<std::uint8_t>(blockIndex));
        auto u = hmacSha256(reinterpret_cast<const std::uint8_t*>(password.data()), password.size(),
                            msg.data(), msg.size());
        std::vector<std::uint8_t> t(u.begin(), u.end());
        for (std::uint32_t i = 1; i < iterations; ++i) {
            u = hmacSha256(reinterpret_cast<const std::uint8_t*>(password.data()), password.size(), u.data(),
                           u.size());
            for (int j = 0; j < 32; ++j)
                t[j] ^= u[j];
        }
        out.insert(out.end(), t.begin(), t.end());
        ++blockIndex;
    }
    out.resize(dkLen);
    return out;
}

void chacha20Xor(std::uint8_t* out, const std::uint8_t* in, std::size_t len, const std::uint8_t key[32],
                 std::uint32_t counter, const std::uint8_t nonce[12]) {
    std::size_t off = 0;
    while (off < len) {
        std::uint8_t block[64];
        chacha20Block(block, key, counter++, nonce);
        std::size_t n = std::min<std::size_t>(64, len - off);
        for (std::size_t i = 0; i < n; ++i)
            out[off + i] = in[off + i] ^ block[i];
        off += n;
    }
}

std::vector<std::uint8_t> randomBytes(std::size_t n) {
    std::random_device rd;
    std::vector<std::uint8_t> out(n);
    for (auto& b : out)
        b = static_cast<std::uint8_t>(rd());
    return out;
}

bool constantTimeEqual(const std::uint8_t* a, const std::uint8_t* b, std::size_t len) {
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < len; ++i)
        diff |= static_cast<std::uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

} // namespace memdb::detail::crypto
