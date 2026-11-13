#include <memdb/memdb.hpp>

#include <iostream>

using namespace memdb;

int main() {
    Database db("geheim-db");
    Collection konten = db.createCollection("konten");
    konten.createIndex("iban", true);
    konten.insert({{"iban", "DE89370400440532013000"}, {"saldo", 1500}, {"inhaber", "Ada"}});
    konten.insert({{"iban", "DE02120300000000202051"}, {"saldo", 42}});

    const std::string path = "inmemdb_encrypted_demo.bin";
    db.saveToFileEncrypted(path, "s3cret-Passwort!");
    std::cout << "verschlüsselt gespeichert: " << path << "\n";

    Database geladen = Database::loadFromFileEncrypted(path, "s3cret-Passwort!");
    std::cout << "geladen: name=" << geladen.name() << ", konten=" << geladen.collection("konten").size()
              << ", index iban: " << std::boolalpha << geladen.collection("konten").hasIndex("iban") << "\n";
    auto ada = geladen.collection("konten").findOne(Field("inhaber") == "Ada");
    if (ada)
        std::cout << "Ada: saldo=" << ada->at("saldo").asInt() << "\n";

    try {
        Database::loadFromFileEncrypted(path, "falsch");
    } catch (const CryptoError& e) {
        std::cout << "falsches Passwort erkannt: " << e.what() << "\n";
    }
    std::remove(path.c_str());
    return 0;
}
