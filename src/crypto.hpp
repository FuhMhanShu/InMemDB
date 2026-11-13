#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace memdb::detail::crypto {

/// @brief SHA-256 (einmalig, One-Shot).
std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t len);

/// @brief HMAC-SHA-256 (One-Shot).
std::array<std::uint8_t, 32> hmacSha256(const std::uint8_t* key, std::size_t keyLen,
                                        const std::uint8_t* msg, std::size_t msgLen);

/// @brief PBKDF2-HMAC-SHA256 zur Passwort-basierten Schlüsselableitung.
std::vector<std::uint8_t> pbkdf2HmacSha256(const std::string& password, const std::uint8_t* salt,
                                           std::size_t saltLen, std::uint32_t iterations, std::size_t dkLen);

/// @brief ChaCha20 (RFC 8439): XOR von `len` Bytes mit dem Keystream (counter startet bei `counter`).
void chacha20Xor(std::uint8_t* out, const std::uint8_t* in, std::size_t len, const std::uint8_t key[32],
                 std::uint32_t counter, const std::uint8_t nonce[12]);

/// @brief Kryptografisch brauchbare Zufallsbytes (std::random_device).
std::vector<std::uint8_t> randomBytes(std::size_t n);

/// @brief Konstantzeit-Vergleich (true = gleich).
bool constantTimeEqual(const std::uint8_t* a, const std::uint8_t* b, std::size_t len);

} // namespace memdb::detail::crypto
