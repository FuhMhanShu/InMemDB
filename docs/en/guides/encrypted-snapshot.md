# Encrypted binary snapshots (FA-19) – implemented in v0.9

| Field | Value |
|---|---|
| Status | implemented (v0.9) |
| Reference | requirements FA-19 (optional), NFA-06 (no external dependencies) |

## API

```cpp
db.saveToFileEncrypted("backup.enc", "password");
Database db2 = Database::loadFromFileEncrypted("backup.enc", "password");
```

`CryptoError` on: empty password, wrong password, corrupted file (HMAC check), wrong
magic. `Error` on IO failures, `JsonError` on invalid decrypted content.

## File format (binary, little-endian)

```
offset 0   : magic 8 bytes  "INMEMDB\x01"
offset 8   : salt 16 bytes (random per file)
offset 24  : PBKDF2 iterations uint32 LE (600,000 from v0.44; v≤0.43: 100,000 – the value is versioned in the file header, old files still load)
offset 28  : ChaCha20 nonce 12 bytes (random per file)
offset 40  : ciphertext (ChaCha20, length = JSON length)
last 32    : tag = HMAC-SHA256(macKey, all preceding bytes)
```

- **Key derivation:** PBKDF2-HMAC-SHA256(password, salt, **600,000** – OWASP recommendation from v0.44, finding M4; before: 100,000) → 64 bytes:
  32 bytes encryption key (ChaCha20), 32 bytes MAC key.
- **Encryption:** ChaCha20 (RFC 8439), counter starts at 1.
- **Integrity:** encrypt-then-MAC; tag comparison in constant time.
- Payload is the normal JSON snapshot (`toJson()`), incl. index definitions.

## Implementation without external dependencies
`src/crypto.cpp`: SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256, ChaCha20, randomness
via `std::random_device` – all self-implemented and tested against known test vectors
(SHA-256("abc"), HMAC RFC 4231 TC1, ChaCha20 RFC 8439 keystream and encryption vector,
PBKDF2-HMAC-SHA256 c=1/c=2).

## Security notes / limitations (v0.9)
- Password-based; strength depends on the password (PBKDF2 slows down offline attacks).
- No zeroization of sensitive keys in memory; not a FIPS/Common-Criteria certified module.
- No key management (no KEK/DEK hierarchy) – outlook.
