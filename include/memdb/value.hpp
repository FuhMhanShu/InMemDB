#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace memdb {

class Document;

/// @brief Dynamisch typisierter Feldwert eines Dokuments.
///
/// Kapselt per `std::variant` alle unterstützten Typen (null, bool, int64,
/// double, string, binary, verschachteltes Document, Array). Der Typ eines
/// Feldes kann sich pro Dokument und über die Zeit unterscheiden (schema-los).
class Value {
public:
    /// @brief Unterstützte Werttypen.
    enum class Type { Null, Bool, Int, Double, String, Binary, Document, Array };

    /// @brief Erzeugt einen `null`-Wert.
    Value();
    Value(std::nullptr_t);
    Value(bool value);
    Value(int value);
    Value(long value);
    Value(long long value);
    Value(unsigned value);
    Value(unsigned long value);
    Value(unsigned long long value);
    Value(double value);
    Value(const char* value);
    Value(std::string value);
    /// @brief Binary-Wert (Bytes).
    Value(std::vector<std::uint8_t> value);
    /// @brief Array-Wert (Liste von Values).
    Value(std::vector<Value> value);
    /// @brief Verschachteltes Dokument.
    Value(std::shared_ptr<Document> value);

    /// @brief Liefert den aktuellen Typ.
    Type type() const;

    bool isNull() const;
    bool isBool() const;
    bool isInt() const;
    bool isDouble() const;
    /// @brief true, wenn Int oder Double.
    bool isNumber() const;
    bool isString() const;
    bool isBinary() const;
    bool isDocument() const;
    bool isArray() const;

    /// @throws TypeError wenn der Typ nicht passt.
    bool asBool() const;
    /// @brief Nur für Int; wirft sonst TypeError.
    /// @throws TypeError
    std::int64_t asInt() const;
    /// @brief Int wird zu double konvertiert.
    /// @throws TypeError
    double asDouble() const;
    /// @throws TypeError
    const std::string& asString() const;
    /// @throws TypeError
    const std::vector<std::uint8_t>& asBinary() const;
    /// @throws TypeError
    std::shared_ptr<Document> asDocument() const;
    /// @throws TypeError
    const std::vector<Value>& asArray() const;

private:
    using Storage = std::variant<std::nullptr_t, bool, std::int64_t, double, std::string,
                                 std::vector<std::uint8_t>, std::shared_ptr<Document>,
                                 std::vector<Value>>;
    Storage storage_;

    friend const Storage& storageOf(const Value& v);
};

/// @brief Tiefenvergleich zweier Werte (Zahlen werden über Int/Double hinweg verglichen).
bool equal(const Value& a, const Value& b);
/// @brief Operator< für Value; Zahlen und Strings sind vergleichbar.
bool operator==(const Value& a, const Value& b);
bool operator!=(const Value& a, const Value& b);

/// @brief Dreiseitiger Vergleich (-1/0/1) für Zahlen und Strings.
/// @return `std::nullopt`, wenn die Typen nicht geordnet vergleichbar sind.
std::optional<int> compare(const Value& a, const Value& b);

/// @brief Debug-/JSON-nahe String-Darstellung.
std::string toString(const Value& v);

} // namespace memdb
