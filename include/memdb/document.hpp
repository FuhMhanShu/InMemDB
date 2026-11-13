#pragma once

#include "document_id.hpp"
#include "value.hpp"

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace memdb {

class Collection;
namespace detail {
struct CollectionState;
}

/// @brief Ein Dokument: schema-lose Menge von Feld → Wert mit eindeutiger ID.
///
/// Felder sind jederzeit und pro Dokument beliebig erweiterbar: hinzufügen,
/// Typ ändern, umbenennen und entfernen sind zur Laufzeit möglich. Der Zugriff
/// auf verschachtelte Dokumente erfolgt per Punktnotation (`findPath("a.b")`).
class Document {
public:
    /// @brief Leeres Dokument.
    Document();
    /// @brief Dokument aus Feldliste, z. B. `Document{{"name", "Ada"}, {"age", 36}}`.
    Document(std::initializer_list<std::pair<const std::string, Value>> fields);
    Document(const Document& other);
    Document(Document&& other) noexcept;
    Document& operator=(const Document& other);
    Document& operator=(Document&& other) noexcept;
    ~Document();

    /// @brief ID des Dokuments (von der Collection vergeben).
    DocumentId id() const;

    /// @brief Anzahl der Felder.
    std::size_t fieldCount() const;
    /// @brief true, wenn das Feld existiert.
    bool contains(std::string_view key) const;

    /// @brief Zeiger auf den Wert des Feldes oder `nullptr`, wenn nicht vorhanden.
    const Value* find(std::string_view key) const;
    /// @brief Nicht-konstante Variante von `find` (In-Place-Änderung / Herausbewegen).
    Value* findMutable(std::string_view key);
    /// @brief Referenz auf den Wert; legt das Feld notfalls als `null` an.
    Value& operator[](const std::string& key);
    /// @brief Wert des Feldes.
    /// @throws Error wenn das Feld fehlt.
    const Value& at(std::string_view key) const;

    /// @brief Feld setzen oder überschreiben (auch mit anderem Typ).
    void set(std::string key, Value value);
    /// @brief Feld entfernen; true, wenn es existierte.
    bool remove(std::string_view key);
    /// @brief Feld umbenennen; false, wenn Quelle fehlt oder Ziel belegt ist.
    bool rename(std::string_view from, std::string_view to);

    /// @brief Zugriff auf verschachteltes Feld per Punktnotation (`"adresse.plz"`);
    /// `nullptr`, wenn der Pfad nicht existiert.
    const Value* findPath(std::string_view dotPath) const;

    /// @brief Kopie aller Felder.
    std::map<std::string, Value> fields() const;
    /// @brief Iteration über alle Felder ohne Kopie (z. B. für Validierung).
    void forEachField(const std::function<void(const std::string&, const Value&)>& f) const;

    /// @brief Vergleich der Felder (ID wird nicht verglichen).
    bool operator==(const Document& other) const;
    bool operator!=(const Document& other) const { return !(*this == other); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void setId(DocumentId id);

    friend class Collection;
    friend struct detail::CollectionState;
};

} // namespace memdb
