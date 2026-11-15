#include "memdb/document.hpp"
#include "memdb/exceptions.hpp"
#include "memdb/value.hpp"

#include <charconv>
#include <cmath>
#include <sstream>

namespace memdb {

const Value::Storage& storageOf(const Value& v) { return v.storage_; }

Value::Value() : storage_(nullptr) {}
Value::Value(std::nullptr_t) : storage_(nullptr) {}
Value::Value(bool value) : storage_(value) {}
Value::Value(int value) : storage_(static_cast<std::int64_t>(value)) {}
Value::Value(long value) : storage_(static_cast<std::int64_t>(value)) {}
Value::Value(long long value) : storage_(static_cast<std::int64_t>(value)) {}
Value::Value(unsigned value) : storage_(static_cast<std::int64_t>(value)) {}
Value::Value(unsigned long value) : storage_(static_cast<std::int64_t>(value)) {}
Value::Value(unsigned long long value) : storage_(static_cast<std::int64_t>(value)) {}
Value::Value(double value) : storage_(value) {}
Value::Value(const char* value) : storage_(std::string(value)) {}
Value::Value(std::string value) : storage_(std::move(value)) {}
Value::Value(std::vector<std::uint8_t> value) : storage_(std::move(value)) {}
Value::Value(std::vector<Value> value) : storage_(std::move(value)) {}
Value::Value(std::shared_ptr<Document> value) : storage_(std::move(value)) {}

Value::Type Value::type() const {
    switch (storage_.index()) {
        case 0: return Type::Null;
        case 1: return Type::Bool;
        case 2: return Type::Int;
        case 3: return Type::Double;
        case 4: return Type::String;
        case 5: return Type::Binary;
        case 6: return Type::Document;
        case 7: return Type::Array;
        default: return Type::Null;
    }
}

bool Value::isNull() const { return std::holds_alternative<std::nullptr_t>(storage_); }
bool Value::isBool() const { return std::holds_alternative<bool>(storage_); }
bool Value::isInt() const { return std::holds_alternative<std::int64_t>(storage_); }
bool Value::isDouble() const { return std::holds_alternative<double>(storage_); }
bool Value::isNumber() const { return isInt() || isDouble(); }
bool Value::isString() const { return std::holds_alternative<std::string>(storage_); }
bool Value::isBinary() const { return std::holds_alternative<std::vector<std::uint8_t>>(storage_); }
bool Value::isDocument() const { return std::holds_alternative<std::shared_ptr<Document>>(storage_); }
bool Value::isArray() const { return std::holds_alternative<std::vector<Value>>(storage_); }

bool Value::asBool() const {
    if (!isBool())
        throw TypeError("asBool(): Wert ist kein Bool");
    return std::get<bool>(storage_);
}

std::int64_t Value::asInt() const {
    if (!isInt())
        throw TypeError("asInt(): Wert ist kein Int");
    return std::get<std::int64_t>(storage_);
}

double Value::asDouble() const {
    if (isInt())
        return static_cast<double>(std::get<std::int64_t>(storage_));
    if (isDouble())
        return std::get<double>(storage_);
    throw TypeError("asDouble(): Wert ist keine Zahl");
}

const std::string& Value::asString() const {
    if (!isString())
        throw TypeError("asString(): Wert ist kein String");
    return std::get<std::string>(storage_);
}

const std::vector<std::uint8_t>& Value::asBinary() const {
    if (!isBinary())
        throw TypeError("asBinary(): Wert ist kein Binary");
    return std::get<std::vector<std::uint8_t>>(storage_);
}

std::shared_ptr<Document> Value::asDocument() const {
    if (!isDocument())
        throw TypeError("asDocument(): Wert ist kein Document");
    return std::get<std::shared_ptr<Document>>(storage_);
}

const std::vector<Value>& Value::asArray() const {
    if (!isArray())
        throw TypeError("asArray(): Wert ist kein Array");
    return std::get<std::vector<Value>>(storage_);
}

std::vector<Value>& Value::asArray() {
    if (!isArray())
        throw TypeError("asArray(): Wert ist kein Array");
    return std::get<std::vector<Value>>(storage_);
}

bool equal(const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber()) {
        if (a.isInt() && b.isInt())
            return a.asInt() == b.asInt();
        return a.asDouble() == b.asDouble();
    }
    if (a.type() != b.type())
        return false;
    switch (a.type()) {
        case Value::Type::Null:
            return true;
        case Value::Type::Bool:
            return a.asBool() == b.asBool();
        case Value::Type::String:
            return a.asString() == b.asString();
        case Value::Type::Binary:
            return a.asBinary() == b.asBinary();
        case Value::Type::Array: {
            const auto& x = a.asArray();
            const auto& y = b.asArray();
            if (x.size() != y.size())
                return false;
            for (std::size_t i = 0; i < x.size(); ++i)
                if (!equal(x[i], y[i]))
                    return false;
            return true;
        }
        case Value::Type::Document: {
            auto x = a.asDocument();
            auto y = b.asDocument();
            if (!x || !y)
                return x == y;
            return *x == *y;
        }
        default:
            return false;
    }
}

bool operator==(const Value& a, const Value& b) { return equal(a, b); }
bool operator!=(const Value& a, const Value& b) { return !equal(a, b); }

std::optional<int> compare(const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber()) {
        if (a.isInt() && b.isInt()) {
            auto x = a.asInt();
            auto y = b.asInt();
            return x < y ? -1 : (x > y ? 1 : 0);
        }
        double x = a.asDouble();
        double y = b.asDouble();
        if (x < y)
            return -1;
        if (x > y)
            return 1;
        return 0;
    }
    if (a.isString() && b.isString())
        return a.asString().compare(b.asString());
    return std::nullopt;
}

std::string toString(const Value& v) {
    std::ostringstream os;
    switch (v.type()) {
        case Value::Type::Null:
            os << "null";
            break;
        case Value::Type::Bool:
            os << (v.asBool() ? "true" : "false");
            break;
        case Value::Type::Int:
            os << v.asInt();
            break;
        case Value::Type::Double: {
            // locale-unabhaengig (os << double wuerde bei de_DE ein Komma
            // liefern); kuerzeste Round-Trip-Darstellung wie im JSON-Writer.
            double d = v.asDouble();
            if (std::isnan(d) || std::isinf(d)) {
                os << (std::isnan(d) ? "nan" : (d < 0 ? "-inf" : "inf"));
                break;
            }
            char buf[64];
            auto r = std::to_chars(buf, buf + sizeof(buf), d);
            os << std::string(buf, r.ptr);
            break;
        }
        case Value::Type::String:
            os << '"' << v.asString() << '"';
            break;
        case Value::Type::Binary:
            os << "<binary " << v.asBinary().size() << " bytes>";
            break;
        case Value::Type::Array: {
            os << '[';
            bool first = true;
            for (const auto& item : v.asArray()) {
                if (!first)
                    os << ", ";
                first = false;
                os << toString(item);
            }
            os << ']';
            break;
        }
        case Value::Type::Document: {
            auto d = v.asDocument();
            os << (d ? "{...}" : "null");
            break;
        }
    }
    return os.str();
}

} // namespace memdb
