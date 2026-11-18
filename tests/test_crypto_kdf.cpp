// Tests v0.44 (M4): PBKDF2-Iterationen 600.000 (OWASP) im Dateiheader; Abwaertskompatibilitaet:
// alte Datei mit 100.000 Iterationen (header-versioniert) laedt weiterhin; Cap 10 Mio. greift.
#include <memdb/memdb.hpp>

#include "../src/crypto.hpp" // interne Krypto-Helfer, um ein echtes Altformat zu bauen

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;
#define CHECK(cond)                                                                              \
    do {                                                                                         \
        ++g_checks;                                                                              \
        if (!(cond)) {                                                                           \
            ++g_failures;                                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ")\n";        \
        }                                                                                        \
    } while (0)

using namespace memdb;

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::uint32_t getU32Le(const std::string& s, std::size_t off) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(s.data()) + off;
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}

void putU32Le(std::string& s, std::size_t off, std::uint32_t v) {
    s[off] = static_cast<char>(v & 0xFF);
    s[off + 1] = static_cast<char>((v >> 8) & 0xFF);
    s[off + 2] = static_cast<char>((v >> 16) & 0xFF);
    s[off + 3] = static_cast<char>((v >> 24) & 0xFF);
}

// Baut eine Datei exakt im verschluesselten Format (Magic|Salt16|Iterationen u32le|Nonce12|CT|Tag32),
// mit frei wählbarer Iterationszahl – so entsteht ein echtes "altes" v<=0.43-File mit 100.000.
void writeEncryptedFile(const std::string& path, const std::string& password, const std::string& json,
                        std::uint32_t iterations) {
    constexpr std::size_t kMagicLen = 8, kSaltLen = 16, kNonceLen = 12, kTagLen = 32;
    const char magic[kMagicLen] = {'I', 'N', 'M', 'E', 'M', 'D', 'B', 0x01};
    std::vector<std::uint8_t> salt(kSaltLen, 0xAB);   // deterministisch reicht fürs Testen
    std::vector<std::uint8_t> nonce(kNonceLen, 0xCD);
    auto keys = detail::crypto::pbkdf2HmacSha256(password, salt.data(), salt.size(), iterations, 64);
    const std::uint8_t* encKey = keys.data();
    const std::uint8_t* macKey = keys.data() + 32;
    std::vector<std::uint8_t> ct(json.size());
    detail::crypto::chacha20Xor(ct.data(), reinterpret_cast<const std::uint8_t*>(json.data()), json.size(),
                                encKey, 1, nonce.data());
    std::string file;
    file.append(magic, kMagicLen);
    file.append(reinterpret_cast<const char*>(salt.data()), kSaltLen);
    char it[4] = {static_cast<char>(iterations & 0xFF), static_cast<char>((iterations >> 8) & 0xFF),
                 static_cast<char>((iterations >> 16) & 0xFF), static_cast<char>((iterations >> 24) & 0xFF)};
    file.append(it, 4);
    file.append(reinterpret_cast<const char*>(nonce.data()), kNonceLen);
    file.append(reinterpret_cast<const char*>(ct.data()), ct.size());
    auto tag = detail::crypto::hmacSha256(macKey, 32, reinterpret_cast<const std::uint8_t*>(file.data()),
                                          file.size());
    file.append(reinterpret_cast<const char*>(tag.data()), kTagLen);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(file.data(), static_cast<std::streamsize>(file.size()));
}

} // namespace

int main() {
    const std::string path = "/tmp/kdf_test.enc";
    const std::string pathOld = "/tmp/kdf_test_old100k.enc";

    // 1) neue Dateien tragen 600.000 Iterationen im Header (offset 8 magic + 16 salt = 24)
    {
        Database db("kdf");
        db.createCollection("c").insert({{"x", 1}});
        db.saveToFileEncrypted(path, "geheim");
        std::string raw = slurp(path);
        CHECK(raw.size() > 40 + 32);
        CHECK(raw.compare(0, 8, std::string("INMEMDB\x01", 8)) == 0);
        CHECK(getU32Le(raw, 24) == 600000); // v0.44: OWASP-Empfehlung
        Database back = Database::loadFromFileEncrypted(path, "geheim");
        CHECK(back.collection("c").size() == 1);
    }

    // 2) Abwaertskompatibilitaet: echtes Altformat mit 100.000 Iterationen laedt weiterhin
    {
        Database db("alt");
        db.createCollection("c").insert({{"x", 42}});
        const std::string json = db.toJson();
        writeEncryptedFile(pathOld, "geheim", json, 100000);
        std::string raw = slurp(pathOld);
        CHECK(getU32Le(raw, 24) == 100000);
        Database back = Database::loadFromFileEncrypted(pathOld, "geheim"); // darf nicht werfen
        CHECK(back.collection("c").findOne(Field("x") == 42).has_value());
    }

    // 3) Cap greift: Iterationszahl > 10 Mio. in der Datei -> CryptoError (vor HMAC-Pruefung)
    {
        std::string raw = slurp(path);
        putU32Le(raw, 24, 20000000);
        std::ofstream(path + ".bad", std::ios::binary | std::ios::trunc).write(raw.data(), (std::streamsize)raw.size());
        bool threw = false;
        try {
            Database::loadFromFileEncrypted(path + ".bad", "geheim");
        } catch (const CryptoError& e) {
            threw = true;
            CHECK(std::string(e.what()).find("Iterationszahl") != std::string::npos);
        }
        CHECK(threw);
    }

    // 4) falsches Passwort weiterhin CryptoError
    {
        bool threw = false;
        try {
            Database::loadFromFileEncrypted(path, "falsch");
        } catch (const CryptoError&) {
            threw = true;
        }
        CHECK(threw);
    }

    std::remove(path.c_str());
    std::remove(pathOld.c_str());
    std::remove((path + ".bad").c_str());
    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
