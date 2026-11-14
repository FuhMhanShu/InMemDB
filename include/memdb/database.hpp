#pragma once

#include "collection.hpp"
#include "metrics.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace memdb {

/// @brief Oberste Einheit: verwaltet benannte Collections (FA-01/FA-02).
///
/// Reines In-Memory: Daten gehen beim Prozessende verloren, außer sie werden
/// per `saveToFile`/`toJson` gesichert (FA-14). Nicht kopierbar.
class Database {
public:
    /// @param name Optionaler Name der Database (steht im JSON-Snapshot).
    explicit Database(std::string name = {});
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;

    /// @brief Name der Database.
    const std::string& name() const;

    /// @brief Neue Collection anlegen.
    /// @throws CollectionExistsError wenn der Name bereits vergeben ist.
    Collection createCollection(const std::string& name);
    /// @brief Bestehende Collection holen (Handle).
    /// @throws CollectionNotFoundError wenn nicht vorhanden.
    Collection collection(const std::string& name) const;
    /// @brief true, wenn die Collection existiert.
    bool hasCollection(const std::string& name) const;
    /// @brief Namen aller Collections (sortiert).
    std::vector<std::string> collectionNames() const;
    /// @brief Aggregierte Metriken ueber alle Collections (v0.22), siehe `Collection::metrics()`.
    DatabaseMetrics metrics() const;
    /// @brief Metriken einer einzelnen Collection (v0.22).
    CollectionMetrics metrics(const std::string& collectionName) const;
    /// @brief Collection inkl. aller Dokumente löschen; false, wenn nicht vorhanden.
    bool dropCollection(const std::string& name);
    /// @brief Alle Collections löschen.
    void clear();

    /// @brief Gesamte Database als JSON-String (FA-14), inkl. Index-Definitionen
    /// pro Collection. Format siehe `docs/snapshot-json.md`.
    std::string toJson() const;
    /// @brief Lädt JSON in diese Database: fehlende Collections werden angelegt,
    /// bestehende behalten ihre Index-Definitionen, Dokumente werden ersetzt.
    /// @throws JsonError bei Parse-/Strukturfehlern.
    void fromJson(std::string_view json);
    /// @brief Neue Database aus JSON-String; `name` überschreibt ggf. den Namen aus dem JSON.
    /// @throws JsonError
    static Database fromJson(std::string_view json, std::string name);
    /// @brief Speichert `toJson()` in eine Datei.
    /// @throws Error bei IO-Fehlern.
    void saveToFile(const std::string& path) const;
    /// @brief Lädt eine Database aus einer JSON-Datei (Name aus der Datei).
    /// @throws Error bei IO-Fehlern; JsonError bei ungültigem JSON.
    static Database loadFromFile(const std::string& path);

    /// @brief Speichert die gesamte Database binär VERSCHLÜSSELT (FA-19).
    ///
    /// Passwortbasiert (PBKDF2-HMAC-SHA256, 100.000 Iterationen, zufälliges Salt),
    /// Verschlüsselung ChaCha20 (RFC 8439), Integrität/Authentizität per
    /// HMAC-SHA256 (encrypt-then-MAC). Ohne externe Abhängigkeiten.
    /// @throws CryptoError bei leerem Passwort; Error bei IO-Fehlern.
    void saveToFileEncrypted(const std::string& path, const std::string& password) const;
    /// @brief Lädt eine mit `saveToFileEncrypted` gespeicherte Database.
    /// @throws CryptoError bei falschem Passwort oder beschädigter Datei;
    ///         Error bei IO-Fehlern; JsonError bei ungültigem Inhalt.
    static Database loadFromFileEncrypted(const std::string& path, const std::string& password);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace memdb
