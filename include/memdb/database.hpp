#pragma once

#include "collection.hpp"
#include "metrics.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace memdb {

class ReferenceIndex; // FA-18, siehe references.hpp

/// @brief Optionen für `Database::mergeCollections` (FA-24, v0.24).
struct MergeOptions {
    /// Quell-Collections nach erfolgreichem Merge löschen (Default: behalten).
    /// Achtung: Referenzen auf gelöschte Quellen werden nur umgeschrieben, wenn
    /// `mergeCollections` ein `ReferenceIndex*` übergeben bekommt, sonst dangling.
    bool dropSources = false;
    /// Herkunftsfeld: wird pro gemergtem Dokument auf den Quell-Collection-Namen
    /// gesetzt (überschreibt ein gleichnamiges Feld). Leer = kein Feld.
    std::string sourceField = "_source";
    /// Index-Definitionen der Quellen übernehmen, soweit im Ziel nicht vorhanden
    /// (Union; bestehende Ziel-Indizes bleiben). Default: true.
    bool mergeIndexes = true;
};

/// @brief Ergebnis von `Database::mergeCollections` (FA-24, v0.24).
struct MergeStats {
    std::size_t merged = 0;                       ///< Anzahl eingefügter Dokumente (gesamt)
    std::map<std::string, std::size_t> perSource; ///< eingefügte Dokumente pro Quell-Collection
    std::vector<std::string> adoptedIndexes;      ///< übernommene Index-Felder
    std::vector<std::string> droppedSources;      ///< gelöschte Quell-Collections
    std::size_t rewrittenReferences = 0;          ///< umgeschriebene from_key/to_key (nur mit ReferenceIndex*)
};

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

    /// @brief Mehrere Collections in eine Ziel-Collection zusammenführen (FA-24, v0.24).
    ///
    /// Jedes Quell-Dokument wird mit **neuer ID** in `target` eingefügt (IDs sind
    /// pro Collection vergeben, Kollisionen sind daher garantiert); sofern
    /// `MergeOptions::sourceField` nicht leer ist, wird das Feld auf den Namen
    /// der Quell-Collection gesetzt. Existiert `target` nicht, wird sie angelegt.
    /// Der Merge läuft unter exklusiven Locks aller beteiligten Collections und
    /// ist **atomar**: wirft ein Dokument `SchemaError` (FA-20) oder
    /// `UniqueConstraintError` (FA-10), werden alle bereits eingefügten Dokumente
    /// zurückgerollt und die Exception durchgereicht.
    ///
    /// @param target Ziel-Collection (wird angelegt, falls nicht vorhanden).
    /// @param sources Quell-Collections (müssen existieren; Duplikate werden ignoriert).
    /// @param opts Optionen (Quellen löschen, Herkunftsfeld, Indizes übernehmen).
    /// @param refs Optional: wenn gesetzt **und** `opts.dropSources`, werden die
    ///        `from_key`/`to_key` der Referenzen (altes `quelle#id` → `ziel#neueId`,
    ///        `quelle#` → `ziel#`) umgeschrieben, damit keine Dangling-Refs entstehen.
    /// @throws Error wenn `target` in `sources` vorkommt oder `sources` leer ist;
    ///         CollectionNotFoundError bei unbekannter Quelle;
    ///         SchemaError/UniqueConstraintError bei Verstoß (mit Rollback).
    MergeStats mergeCollections(const std::string& target, std::vector<std::string> sources,
                                MergeOptions opts = {}, ReferenceIndex* refs = nullptr);

    /// @brief Komfort-Wrapper über `mergeCollections` (FA-24, v0.25): **alle**
    /// Collections der Database (außer `target` und ggf. den Referenz-Speicher)
    /// in `target` zusammenführen. Existiert `target` nicht, wird sie angelegt.
    /// Übergibt man `refs`, wird dessen Speicher-Collection von den Quellen
    /// ausgenommen (sie wird weder gemergt noch bei `dropSources` gelöscht) und
    /// die Referenzen auf `target` umgeschrieben.
    /// @throws Error wenn keine Quell-Collections vorhanden sind.
    MergeStats mergeAllCollections(const std::string& target = "merged", MergeOptions opts = {},
                                   ReferenceIndex* refs = nullptr);

    /// @brief Gesamte Database als JSON-String (FA-14), inkl. Index-Definitionen
    /// pro Collection. Format siehe `docs/de/guides/snapshot-json.md`.
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
