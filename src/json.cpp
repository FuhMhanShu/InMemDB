#include "json.hpp"

#include "memdb/exceptions.hpp"

#include <cerrno>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

namespace memdb::detail {

namespace {

std::string hexEncode(const std::vector<std::uint8_t>& data) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (auto b : data) {
        out += digits[b >> 4];
        out += digits[b & 0x0F];
    }
    return out;
}

int hexVal(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

std::vector<std::uint8_t> hexDecode(const std::string& s) {
    if (s.size() % 2 != 0)
        throw JsonError("ungueltiger Hex-String (Laenge ungerade)");
    std::vector<std::uint8_t> out;
    out.reserve(s.size() / 2);
    for (std::size_t i = 0; i < s.size(); i += 2) {
        int hi = hexVal(s[i]);
        int lo = hexVal(s[i + 1]);
        if (hi < 0 || lo < 0)
            throw JsonError("ungueltiger Hex-String");
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return out;
}

struct Parser {
    std::string_view s;
    std::size_t pos = 0;

    explicit Parser(std::string_view sv) : s(sv) {}

    [[noreturn]] void fail(const std::string& msg) const {
        throw JsonError("JSON-Parsefehler bei Offset " + std::to_string(pos) + ": " + msg);
    }

    void skipWs() {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
            ++pos;
    }

    char peek() {
        skipWs();
        if (pos >= s.size())
            fail("unerwartetes Ende");
        return s[pos];
    }

    bool consume(char c) {
        skipWs();
        if (pos < s.size() && s[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }

    void expect(char c) {
        if (!consume(c))
            fail(std::string("erwartet '") + c + "'");
    }

    void expectLit(const char* lit) {
        skipWs();
        std::string_view l(lit);
        if (s.substr(pos, l.size()) != l)
            fail(std::string("erwartet '") + lit + "'");
        pos += l.size();
    }

    std::uint32_t parseHex4() {
        if (pos + 4 > s.size())
            fail("unvollstaendiges \\u-Escape");
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            int h = hexVal(s[pos + i]);
            if (h < 0)
                fail("ungueltiges \\u-Escape");
            v = (v << 4) | static_cast<std::uint32_t>(h);
        }
        pos += 4;
        return v;
    }

    static void appendUtf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (pos >= s.size())
                fail("ungueltiger String (fehlendes '\"')");
            // Fast-Path: kompletter Lauf bis '"' oder '\' auf einmal anhaengen
            std::size_t runStart = pos;
            while (pos < s.size() && s[pos] != '"' && s[pos] != '\\')
                ++pos;
            out.append(s.data() + runStart, pos - runStart);
            if (pos >= s.size())
                fail("ungueltiger String (fehlendes '\"')");
            char c = s[pos++];
            if (c == '"')
                break;
            if (c == '\\') {
                if (pos >= s.size())
                    fail("ungueltiges Escape");
                char e = s[pos++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        std::uint32_t cp = parseHex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (pos + 1 < s.size() && s[pos] == '\\' && s[pos + 1] == 'u') {
                                pos += 2;
                                std::uint32_t lo = parseHex4();
                                if (lo >= 0xDC00 && lo <= 0xDFFF)
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                else
                                    fail("ungueltiges Surrogat-Paar");
                            } else {
                                fail("ungueltiges Surrogat-Paar");
                            }
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: fail("ungueltiges Escape");
                }
            } else {
                out += c;
            }
        }
        return out;
    }

    Value parseNumber() {
        skipWs();
        std::size_t start = pos;
        if (pos < s.size() && s[pos] == '-')
            ++pos;
        bool isDouble = false;
        while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.' ||
                                   s[pos] == 'e' || s[pos] == 'E' || s[pos] == '+' || s[pos] == '-')) {
            if (s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E')
                isDouble = true;
            ++pos;
        }
        if (pos == start)
            fail("erwartet Zahl, String, Array, Objekt, true/false/null");
        std::size_t n = pos - start;
        char buf[64];
        if (n >= sizeof(buf))
            fail("Zahl zu lang");
        std::memcpy(buf, s.data() + start, n);
        buf[n] = '\0';
        if (!isDouble) {
            std::int64_t v = 0;
            auto r = std::from_chars(buf, buf + n, v);
            if (r.ec == std::errc() && r.ptr == buf + n)
                return Value(v);
            errno = 0;
            char* end = nullptr;
            long long fb = std::strtoll(buf, &end, 10);
            if (errno == 0 && end && *end == '\0')
                return Value(static_cast<std::int64_t>(fb));
        } else {
            // Locale-unabhaengig parsen (std::from_chars, nie strtod: bei de_DE
            // waere sonst '.' kein Dezimaltrennzeichen mehr). Fallbacks:
            // Hex-Float (0x1.8p1) sowie inf/nan (nicht JSON, aber tolerant).
            double d = 0.0;
            auto r = std::from_chars(buf, buf + n, d);
            if (r.ec == std::errc() && r.ptr == buf + n)
                return Value(d);
            r = std::from_chars(buf, buf + n, d, std::chars_format::hex);
            if (r.ec == std::errc() && r.ptr == buf + n)
                return Value(d);
            std::string lower(buf, n);
            for (auto& c : lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            bool neg = false;
            std::size_t off = 0;
            if (!lower.empty() && (lower[0] == '+' || lower[0] == '-')) {
                neg = lower[0] == '-';
                off = 1;
            }
            if (lower.compare(off, 3, "inf") == 0 || lower.compare(off, 8, "infinity") == 0)
                return Value(neg ? -std::numeric_limits<double>::infinity()
                                 : std::numeric_limits<double>::infinity());
            if (lower.compare(off, 3, "nan") == 0)
                return Value(std::numeric_limits<double>::quiet_NaN());
        }
        fail(std::string("ungueltige Zahl '") + buf + "'");
    }

    Value parseArray() {
        expect('[');
        std::vector<Value> items;
        if (consume(']'))
            return Value(std::move(items));
        while (true) {
            items.push_back(parseValue());
            if (consume(','))
                continue;
            expect(']');
            break;
        }
        return Value(std::move(items));
    }

    Value parseObject() {
        expect('{');
        Document doc;
        if (consume('}'))
            return Value(std::make_shared<Document>(std::move(doc)));
        while (true) {
            if (peek() != '"')
                fail("erwartet String-Schluessel");
            std::string key = parseString();
            expect(':');
            Value v = parseValue();
            doc.set(std::move(key), std::move(v));
            if (consume(','))
                continue;
            expect('}');
            break;
        }
        if (doc.fieldCount() == 1 && doc.contains("$binary")) {
            const Value* v = doc.find("$binary");
            if (v->isString())
                return Value(hexDecode(v->asString()));
        }
        return Value(std::make_shared<Document>(std::move(doc)));
    }

    Value parseValue() {
        char c = peek();
        switch (c) {
            case 'n': expectLit("null"); return Value(nullptr);
            case 't': expectLit("true"); return Value(true);
            case 'f': expectLit("false"); return Value(false);
            case '"': return Value(parseString());
            case '[': return parseArray();
            case '{': return parseObject();
            default: return parseNumber();
        }
    }
};

} // namespace

std::string writeJsonString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

std::string writeJsonDocument(const Document& doc) {
    std::string out = "{";
    bool first = true;
    for (const auto& [key, value] : doc.fields()) {
        if (!first)
            out += ",";
        first = false;
        out += writeJsonString(key);
        out += ":";
        out += writeJson(value);
    }
    out += "}";
    return out;
}

std::string writeJson(const Value& v) {
    switch (v.type()) {
        case Value::Type::Null:
            return "null";
        case Value::Type::Bool:
            return v.asBool() ? "true" : "false";
        case Value::Type::Int:
            return std::to_string(v.asInt());
        case Value::Type::Double: {
            double d = v.asDouble();
            if (std::isnan(d) || std::isinf(d))
                return "null";
            // std::to_chars: kuerzeste Round-Trip-Darstellung und
            // locale-unabhaengig (snprintf "%.17g" wuerde bei de_DE ein
            // Komma als Dezimaltrennzeichen liefern).
            char buf[64];
            auto r = std::to_chars(buf, buf + sizeof(buf), d);
            std::string num(buf, r.ptr);
            if (num.find_first_of(".eE") == std::string::npos)
                num += ".0";
            return num;
        }
        case Value::Type::String:
            return writeJsonString(v.asString());
        case Value::Type::Binary:
            return "{\"$binary\":\"" + hexEncode(v.asBinary()) + "\"}";
        case Value::Type::Document: {
            auto d = v.asDocument();
            return d ? writeJsonDocument(*d) : "null";
        }
        case Value::Type::Array: {
            std::string out = "[";
            bool first = true;
            for (const auto& item : v.asArray()) {
                if (!first)
                    out += ",";
                first = false;
                out += writeJson(item);
            }
            out += "]";
            return out;
        }
    }
    return "null";
}

Value parseJson(std::string_view s) {
    Parser p(s);
    Value v = p.parseValue();
    p.skipWs();
    if (p.pos != s.size())
        p.fail("unerwartete Daten nach dem JSON-Wert");
    return v;
}

namespace {

std::string writeJsonFields(const Schema& schema) {
    std::string out = "{";
    bool first = true;
    for (const auto& [path, fs] : schema.fields()) {
        if (!first)
            out += ",";
        first = false;
        std::string entry = "{\"type\":" + writeJsonString(typeName(fs.type())) +
                            ",\"nullable\":" + (fs.nullable() ? "true" : "false");
        if (fs.elementType())
            entry += ",\"element\":" + writeJsonString(typeName(*fs.elementType()));
        if (fs.sub())
            entry += ",\"fields\":" + writeJsonFields(*fs.sub());
        entry += "}";
        out += writeJsonString(path) + ":" + entry;
    }
    out += "}";
    return out;
}

Schema parseJsonSchemaFields(const Value& fieldsValue) {
    if (!fieldsValue.isDocument())
        throw JsonError("Schema-'fields' muss ein Objekt sein");
    Schema schema;
    for (const auto& [path, fsv] : fieldsValue.asDocument()->fields()) {
        if (!fsv.isDocument())
            throw JsonError("Schema-Feld '" + path + "' muss ein Objekt sein");
        const Document& fd = *fsv.asDocument();
        const Value* t = fd.find("type");
        if (!t || !t->isString())
            throw JsonError("Schema-Feld '" + path + "' ohne 'type'");
        FieldSchema& fs = schema.add(path, typeFromName(t->asString()));
        const Value* n = fd.find("nullable");
        if (n && n->isBool())
            fs.setNullable(n->asBool());
        const Value* e = fd.find("element");
        if (e && e->isString())
            fs.elements(typeFromName(e->asString()));
        const Value* sub = fd.find("fields");
        if (sub)
            fs.subSchema(parseJsonSchemaFields(*sub));
    }
    return schema;
}

} // namespace

std::string writeJsonSchema(const Schema& schema) { return "{\"fields\":" + writeJsonFields(schema) + "}"; }

Schema parseJsonSchema(const Value& v) {
    if (!v.isDocument())
        throw JsonError("Schema-JSON muss ein Objekt sein");
    const Value* fields = v.asDocument()->find("fields");
    if (!fields)
        throw JsonError("Schema-JSON: 'fields' fehlt");
    return parseJsonSchemaFields(*fields);
}

} // namespace memdb::detail
