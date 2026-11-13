#pragma once

#include "value.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace memdb {

class Document;
class Schema;

/// @brief Deklaration eines Feldes im Collection-Schema (FA-20):
/// Datentyp als Metadatum plus Constraints (nullable, Unter-Schema, Array-Elementtyp).
class FieldSchema {
public:
    FieldSchema() = default;
    FieldSchema(std::string path, Value::Type type) : path_(std::move(path)), type_(type) {}

    /// @brief Feldpfad (einfache Ebene; Verschachtelung über Unter-Schema).
    const std::string& path() const { return path_; }
    /// @brief Deklarierter Datentyp.
    Value::Type type() const { return type_; }
    /// @brief true, wenn `null` erlaubt ist (Default true).
    bool nullable() const { return nullable_; }
    /// @brief Feld ist Pflicht und darf nicht null sein.
    FieldSchema& notNull() {
        nullable_ = false;
        return *this;
    }
    FieldSchema& setNullable(bool n) {
        nullable_ = n;
        return *this;
    }

    /// @brief Unter-Schema für Felder vom Typ Document (rekursive Prüfung).
    FieldSchema& subSchema(Schema sub);
    /// @brief Unter-Schema (nullptr, wenn keines).
    const Schema* sub() const { return sub_.get(); }

    /// @brief Elementtyp für Felder vom Typ Array (jedes Element wird geprüft).
    FieldSchema& elements(Value::Type t) {
        elementType_ = t;
        return *this;
    }
    std::optional<Value::Type> elementType() const { return elementType_; }

private:
    std::string path_;
    Value::Type type_;
    bool nullable_ = true;
    std::shared_ptr<Schema> sub_;
    std::optional<Value::Type> elementType_;
};

/// @brief Optionales, striktes Schema einer Collection (FA-20).
///
/// Ist ein Schema gesetzt, darf **kein Feld undefiniert** sein: jedes Feld jedes
/// Dokuments muss deklariert sein und den deklarierten Typ haben. Ohne Schema
/// bleibt die Collection schema-los (alles erlaubt).
class Schema {
public:
    Schema() = default;

    /// @brief Feld deklarieren (Builder-API): `schema.add("age", Value::Type::Int).notNull();`
    /// @return Referenz auf das FieldSchema (erneutes add aktualisiert den Eintrag).
    FieldSchema& add(std::string path, Value::Type type);

    bool contains(const std::string& path) const { return fields_.find(path) != fields_.end(); }
    const FieldSchema* find(const std::string& path) const {
        auto it = fields_.find(path);
        return it == fields_.end() ? nullptr : &it->second;
    }
    /// @brief Alle deklarierten Felder.
    const std::map<std::string, FieldSchema>& fields() const { return fields_; }
    std::size_t size() const { return fields_.size(); }

private:
    std::map<std::string, FieldSchema> fields_;
};

inline FieldSchema& FieldSchema::subSchema(Schema sub) {
    sub_ = std::make_shared<Schema>(std::move(sub));
    return *this;
}

/// @brief Name eines Werttyps (für JSON-Snapshot und Fehlermeldungen).
std::string typeName(Value::Type t);
/// @brief Parst einen Typnamen; wirft JsonError bei unbekanntem Namen.
Value::Type typeFromName(const std::string& name);

/// @brief Prüft ein Dokument gegen ein Schema (strikt: jedes Feld muss deklariert sein).
/// @throws SchemaError mit Feldpfad + erwartet/ist.
void validateDocumentAgainstSchema(const Document& doc, const Schema& schema);

} // namespace memdb
