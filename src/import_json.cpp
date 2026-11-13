#include "memdb/import_json.hpp"

#include "import_util.hpp"
#include "json.hpp"
#include "memdb/exceptions.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace memdb {

namespace {

Document valueToDocument(const Value& v, const std::string& ctx) {
    if (!v.isDocument())
        throw JsonError(ctx + ": erwartet ein JSON-Objekt");
    Document d;
    v.asDocument()->forEachField([&](const std::string& k, const Value& val) { d.set(k, val); });
    return d;
}

} // namespace

struct JsonImportSource::Impl {
    std::string path;
    JsonImportOptions opts;
    bool parsed = false;
    std::string tableName;
    std::vector<ColumnInfo> columns;
    std::vector<Document> rows;

    void parseLines(const std::string& data) {
        std::size_t pos = 0;
        std::size_t lineNo = 0;
        while (pos <= data.size()) {
            std::size_t nl = data.find('\n', pos);
            std::string line = data.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            ++lineNo;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            auto b = line.find_first_not_of(" \t");
            if (b != std::string::npos) {
                auto e = line.find_last_not_of(" \t");
                Value v = detail::parseJson(line.substr(b, e - b + 1));
                rows.push_back(valueToDocument(v, "JSONL Zeile " + std::to_string(lineNo)));
            }
            if (nl == std::string::npos)
                break;
            pos = nl + 1;
        }
    }

    void parse() {
        if (parsed)
            return;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            throw Error("JSON-Datei nicht lesbar: " + path);
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        rows.clear();
        if (opts.mode == JsonMode::Array) {
            Value v = detail::parseJson(data);
            if (!v.isArray())
                throw JsonError("JSON: erwartet ein Array von Objekten");
            for (const Value& e : v.asArray())
                rows.push_back(valueToDocument(e, "JSON-Array-Element"));
        } else if (opts.mode == JsonMode::Lines) {
            parseLines(data);
        } else {
            try {
                Value v = detail::parseJson(data);
                if (v.isArray()) {
                    for (const Value& e : v.asArray())
                        rows.push_back(valueToDocument(e, "JSON-Array-Element"));
                } else {
                    rows.push_back(valueToDocument(v, "JSON"));
                }
            } catch (const JsonError&) {
                parseLines(data); // JSONL-Fallback
            }
        }

        detail::collectColumns(rows, columns);

        if (!opts.tableName.empty()) {
            tableName = opts.tableName;
        } else {
            tableName = std::filesystem::path(path).stem().string();
            if (tableName.empty())
                tableName = "json";
        }
        parsed = true;
    }
};

JsonImportSource::JsonImportSource(const std::string& path, JsonImportOptions opts)
    : impl_(std::make_unique<Impl>()) {
    impl_->path = path;
    impl_->opts = std::move(opts);
}

JsonImportSource::~JsonImportSource() = default;

const JsonImportOptions& JsonImportSource::options() const { return impl_->opts; }

std::vector<TableInfo> JsonImportSource::tables() const {
    impl_->parse();
    TableInfo t;
    t.name = impl_->tableName;
    t.columns = impl_->columns;
    return {std::move(t)};
}

void JsonImportSource::readRows(
    const std::string& table,
    const std::function<void(const std::vector<Value>&)>& rowCb) const {
    impl_->parse();
    if (table != impl_->tableName)
        throw Error("JSON: unbekannte Tabelle '" + table + "' (erwartet: '" + impl_->tableName + "')");
    for (const auto& r : impl_->rows)
        rowCb(detail::rowToValues(r, impl_->columns));
}

} // namespace memdb
