#include "memdb/import_bson.hpp"

#include "import_util.hpp"
#include "memdb/exceptions.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace memdb {

namespace {

std::uint32_t rdU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::int32_t rdI32(const std::uint8_t* p) { return static_cast<std::int32_t>(rdU32(p)); }

std::int64_t rdI64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | p[i];
    return static_cast<std::int64_t>(v);
}

double rdF64(const std::uint8_t* p) {
    std::uint64_t bits = static_cast<std::uint64_t>(rdI64(p));
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

struct BsonReader {
    const std::uint8_t* p;
    const std::uint8_t* end;

    void need(std::size_t n) const {
        if (static_cast<std::size_t>(end - p) < n)
            throw Error("BSON: unerwartetes Dateiende");
    }
    std::string cstring() {
        const char* s = reinterpret_cast<const char*>(p);
        std::size_t n = 0;
        while (p + n < end && p[n] != 0)
            ++n;
        if (p + n >= end)
            throw Error("BSON: CString ohne NUL-Terminator");
        std::string out(s, n);
        p += n + 1;
        return out;
    }
    std::string string() { // int32 len (inkl. NUL) + bytes + NUL
        need(4);
        std::int32_t len = rdI32(p);
        p += 4;
        if (len < 1)
            throw Error("BSON: ungueltige String-Laenge");
        need(static_cast<std::size_t>(len));
        std::string out(reinterpret_cast<const char*>(p), static_cast<std::size_t>(len) - 1);
        if (p[len - 1] != 0)
            throw Error("BSON: String ohne NUL-Terminator");
        p += len;
        return out;
    }
};

std::shared_ptr<Document> parseDoc(BsonReader& r);

Value parseElement(std::uint8_t type, BsonReader& r) {
    switch (type) {
        case 0x01: { // double
            r.need(8);
            double d = rdF64(r.p);
            r.p += 8;
            return Value(d);
        }
        case 0x02: // string
            return Value(r.string());
        case 0x03: // embedded document
            return Value(parseDoc(r));
        case 0x04: { // array: Dokument mit Keys "0","1",...
            auto sub = parseDoc(r);
            std::vector<std::pair<long long, Value>> items;
            sub->forEachField([&](const std::string& k, const Value& v) {
                try {
                    items.emplace_back(std::stoll(k), v);
                } catch (...) {
                    throw Error("BSON: Array-Key '" + k + "' ist keine Zahl");
                }
            });
            std::sort(items.begin(), items.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<Value> arr;
            arr.reserve(items.size());
            for (auto& [k, v] : items)
                arr.push_back(std::move(v));
            return Value(std::move(arr));
        }
        case 0x05: { // binary
            r.need(4);
            std::int32_t len = rdI32(r.p);
            r.p += 4;
            if (len < 0)
                throw Error("BSON: ungueltige Binary-Laenge");
            r.need(1);
            std::uint8_t subtype = *r.p++;
            r.need(static_cast<std::size_t>(len));
            const std::uint8_t* b = r.p;
            std::size_t n = static_cast<std::size_t>(len);
            if (subtype == 0x02) { // alter Binary-Subtyp: 4 Bytes wiederholte Laenge vorne
                if (n < 4)
                    throw Error("BSON: Binary-Subtyp 2 zu kurz");
                b += 4;
                n -= 4;
            }
            r.p += static_cast<std::size_t>(len);
            return Value(std::vector<std::uint8_t>(b, b + n));
        }
        case 0x06: // undefined
        case 0x0A: // null
        case 0x7F: // max key
        case 0xFF: // min key
            return Value(nullptr);
        case 0x07: { // ObjectId: 12 Bytes
            r.need(12);
            Value v(std::vector<std::uint8_t>(r.p, r.p + 12));
            r.p += 12;
            return v;
        }
        case 0x08: { // bool
            r.need(1);
            Value v(*r.p != 0);
            r.p += 1;
            return v;
        }
        case 0x09: // UTC datetime (ms)
        case 0x11: // timestamp
        case 0x12: { // int64
            r.need(8);
            std::int64_t v = rdI64(r.p);
            r.p += 8;
            return Value(v);
        }
        case 0x10: { // int32
            r.need(4);
            std::int32_t v = rdI32(r.p);
            r.p += 4;
            return Value(static_cast<std::int64_t>(v));
        }
        case 0x13: { // decimal128: 16 Bytes
            r.need(16);
            Value v(std::vector<std::uint8_t>(r.p, r.p + 16));
            r.p += 16;
            return v;
        }
        case 0x0B: { // regex: pattern + options
            std::string pattern = r.cstring();
            std::string options = r.cstring();
            return Value("/" + pattern + "/" + options);
        }
        case 0x0C: { // dbpointer: string + 12 Bytes -> Binary(12)
            (void)r.string();
            r.need(12);
            Value v(std::vector<std::uint8_t>(r.p, r.p + 12));
            r.p += 12;
            return v;
        }
        case 0x0D: // code
        case 0x0E: // symbol
            return Value(r.string());
        case 0x0F: { // code w/ scope: total int32, string, doc
            r.need(4);
            std::int32_t total = rdI32(r.p);
            if (total < 4)
                throw Error("BSON: ungueltige code_w_scope-Laenge");
            r.p += 4;
            std::string code = r.string();
            auto scope = parseDoc(r);
            auto doc = std::make_shared<Document>();
            doc->set("code", Value(std::move(code)));
            doc->set("scope", Value(std::move(scope)));
            return Value(std::move(doc));
        }
        default: {
            const std::string hex = "0123456789ABCDEF";
            throw Error("BSON: unbekannter Elementtyp 0x" + std::string(1, hex[type / 16]) +
                        std::string(1, hex[type % 16]));
        }
    }
}

std::shared_ptr<Document> parseDoc(BsonReader& r) {
    r.need(4);
    std::int32_t len = rdI32(r.p);
    if (len < 5)
        throw Error("BSON: ungueltige Dokument-Laenge");
    const std::uint8_t* docEnd = r.p + len;
    if (docEnd > r.end)
        throw Error("BSON: Dokument ragt ueber das Dateiende hinaus");
    r.p += 4;
    auto doc = std::make_shared<Document>();
    while (r.p < docEnd) {
        std::uint8_t type = *r.p++;
        if (type == 0x00)
            break;
        std::string name = r.cstring();
        doc->set(name, parseElement(type, r));
    }
    r.p = docEnd; // robust gegen fehlenden Terminator
    return doc;
}

} // namespace

struct BsonImportSource::Impl {
    std::string path;
    std::string tableNameOpt;
    bool parsed = false;
    std::string tableName;
    std::vector<ColumnInfo> columns;
    std::vector<Document> rows;

    void parse() {
        if (parsed)
            return;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            throw Error("BSON-Datei nicht lesbar: " + path);
        std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
        BsonReader r{data.data(), data.data() + data.size()};
        while (r.p < r.end) {
            auto doc = parseDoc(r);
            Document d;
            doc->forEachField([&](const std::string& k, const Value& v) { d.set(k, v); });
            rows.push_back(std::move(d));
        }
        detail::collectColumns(rows, columns);
        if (!tableNameOpt.empty()) {
            tableName = tableNameOpt;
        } else {
            tableName = std::filesystem::path(path).stem().string();
            if (tableName.empty())
                tableName = "bson";
        }
        parsed = true;
    }
};

BsonImportSource::BsonImportSource(const std::string& path, std::string tableName)
    : impl_(std::make_unique<Impl>()) {
    impl_->path = path;
    impl_->tableNameOpt = std::move(tableName);
}

BsonImportSource::~BsonImportSource() = default;

std::vector<TableInfo> BsonImportSource::tables() const {
    impl_->parse();
    TableInfo t;
    t.name = impl_->tableName;
    t.columns = impl_->columns;
    return {std::move(t)};
}

void BsonImportSource::readRows(
    const std::string& table,
    const std::function<void(const std::vector<Value>&)>& rowCb) const {
    impl_->parse();
    if (table != impl_->tableName)
        throw Error("BSON: unbekannte Tabelle '" + table + "' (erwartet: '" + impl_->tableName + "')");
    for (const auto& r : impl_->rows)
        rowCb(detail::rowToValues(r, impl_->columns));
}

} // namespace memdb
