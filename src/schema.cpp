#include "memdb/schema.hpp"

#include "memdb/document.hpp"
#include "memdb/exceptions.hpp"

#include <map>

namespace memdb {

FieldSchema& Schema::add(std::string path, Value::Type type) {
    auto it = fields_.find(path);
    if (it != fields_.end()) {
        it->second = FieldSchema(path, type);
        return it->second;
    }
    FieldSchema fs(path, type);
    return fields_.emplace(std::move(path), std::move(fs)).first->second;
}

std::string typeName(Value::Type t) {
    switch (t) {
        case Value::Type::Null: return "null";
        case Value::Type::Bool: return "bool";
        case Value::Type::Int: return "int";
        case Value::Type::Double: return "double";
        case Value::Type::String: return "string";
        case Value::Type::Binary: return "binary";
        case Value::Type::Document: return "document";
        case Value::Type::Array: return "array";
    }
    return "unknown";
}

Value::Type typeFromName(const std::string& name) {
    if (name == "null") return Value::Type::Null;
    if (name == "bool") return Value::Type::Bool;
    if (name == "int") return Value::Type::Int;
    if (name == "double") return Value::Type::Double;
    if (name == "string") return Value::Type::String;
    if (name == "binary") return Value::Type::Binary;
    if (name == "document") return Value::Type::Document;
    if (name == "array") return Value::Type::Array;
    throw JsonError("unbekannter Typname im Schema: " + name);
}

namespace {

void checkValue(const std::string& path, const Value& v, Value::Type expected, bool nullable,
                const FieldSchema* fs) {
    if (v.isNull()) {
        if (!nullable)
            throw SchemaError("Feld '" + path + "' darf nicht null sein (notNull)");
        return;
    }
    bool ok = false;
    switch (expected) {
        case Value::Type::Bool: ok = v.isBool(); break;
        case Value::Type::Int: ok = v.isInt(); break;
        case Value::Type::Double: ok = v.isNumber(); break; // Int ist bei Double erlaubt
        case Value::Type::String: ok = v.isString(); break;
        case Value::Type::Binary: ok = v.isBinary(); break;
        case Value::Type::Document:
            ok = v.isDocument();
            if (ok && fs && fs->sub() && v.asDocument())
                validateDocumentAgainstSchema(*v.asDocument(), *fs->sub());
            break;
        case Value::Type::Array:
            ok = v.isArray();
            if (ok && fs && fs->elementType())
                for (const auto& el : v.asArray())
                    checkValue(path + "[]", el, *fs->elementType(), true, nullptr);
            break;
        case Value::Type::Null: ok = false; break;
    }
    if (!ok)
        throw SchemaError("Feld '" + path + "': Typ-Mismatch, erwartet " + typeName(expected) +
                          ", ist " + typeName(v.type()));
}

void validateFields(const std::map<std::string, Value>& fields, const Schema& schema,
                    const std::string& prefix) {
    for (const auto& [key, value] : fields) {
        const FieldSchema* fs = schema.find(key);
        if (!fs)
            throw SchemaError("Feld '" + prefix + key + "' ist nicht im Schema deklariert");
        checkValue(prefix + key, value, fs->type(), fs->nullable(), fs);
    }
    for (const auto& [key, fs] : schema.fields())
        if (!fs.nullable()) {
            auto it = fields.find(key);
            if (it == fields.end() || it->second.isNull())
                throw SchemaError("Feld '" + prefix + key + "' fehlt oder ist null (notNull)");
        }
}

} // namespace

void validateDocumentAgainstSchema(const Document& doc, const Schema& schema) {
    validateFields(doc.fields(), schema, "");
}

} // namespace memdb
