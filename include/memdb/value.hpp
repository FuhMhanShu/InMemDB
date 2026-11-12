#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace memdb {

class Document;

class Value {
public:
    enum class Type { Null, Bool, Int, Double, String, Binary, Document, Array };

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
    Value(std::vector<std::uint8_t> value);
    Value(std::vector<Value> value);
    Value(std::shared_ptr<Document> value);

    Type type() const;

    bool isNull() const;
    bool isBool() const;
    bool isInt() const;
    bool isDouble() const;
    bool isNumber() const;
    bool isString() const;
    bool isBinary() const;
    bool isDocument() const;
    bool isArray() const;

    bool asBool() const;
    std::int64_t asInt() const;
    double asDouble() const;
    const std::string& asString() const;
    const std::vector<std::uint8_t>& asBinary() const;
    std::shared_ptr<Document> asDocument() const;
    const std::vector<Value>& asArray() const;

private:
    using Storage = std::variant<std::nullptr_t, bool, std::int64_t, double, std::string,
                                 std::vector<std::uint8_t>, std::shared_ptr<Document>,
                                 std::vector<Value>>;
    Storage storage_;

    friend const Storage& storageOf(const Value& v);
};

bool equal(const Value& a, const Value& b);
bool operator==(const Value& a, const Value& b);
bool operator!=(const Value& a, const Value& b);
std::optional<int> compare(const Value& a, const Value& b);
std::string toString(const Value& v);

} // namespace memdb
