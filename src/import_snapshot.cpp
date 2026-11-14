#include "memdb/import_snapshot.hpp"

#include "import_util.hpp"
#include "json.hpp"
#include "memdb/exceptions.hpp"

#include <fstream>
#include <iterator>

namespace memdb {

namespace {

std::vector<Document> toDocuments(const std::vector<Value>& arr, const std::string& collName) {
    std::vector<Document> docs;
    docs.reserve(arr.size());
    for (const Value& e : arr) {
        if (!e.isDocument())
            throw JsonError("Snapshot: Dokument in '" + collName + "' muss ein Objekt sein");
        Document d;
        e.asDocument()->forEachField([&](const std::string& k, const Value& v) { d.set(k, v); });
        docs.push_back(std::move(d));
    }
    return docs;
}

} // namespace

struct InmemdbSnapshotSource::Impl {
    std::string path;
    bool parsed = false;
    struct Table {
        TableInfo info;
        std::vector<Document> rows;
    };
    std::vector<Table> tables;

    void parse() {
        if (parsed)
            return;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            throw Error("Snapshot-Datei nicht lesbar: " + path);
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        Value root = detail::parseJson(data);
        if (!root.isDocument())
            throw JsonError("Snapshot: Wurzel muss ein Objekt sein");
        const Value* colls = root.asDocument()->find("collections");
        if (!colls || !colls->isDocument())
            throw JsonError("Snapshot: Feld 'collections' fehlt oder ist kein Objekt");

        for (const auto& [name, cv] : colls->asDocument()->fields()) {
            Table t;
            t.info.name = name;
            if (cv.isArray()) {
                t.rows = toDocuments(cv.asArray(), name);
            } else if (cv.isDocument()) {
                const Document& cd = *cv.asDocument();
                if (const Value* ix = cd.find("indexes")) {
                    if (!ix->isArray())
                        throw JsonError("Snapshot '" + name + "': 'indexes' muss ein Array sein");
                    for (const Value& e : ix->asArray()) {
                        if (!e.isDocument())
                            throw JsonError("Snapshot '" + name + "': Index-Eintrag muss Objekt sein");
                        const Document& ed = *e.asDocument();
                        const Value* f = ed.find("field");
                        const Value* u = ed.find("unique");
                        if (!f || !f->isString())
                            throw JsonError("Snapshot '" + name + "': Index-Eintrag ohne 'field'");
                        t.info.indexes.push_back(
                            IndexInfo{f->asString(), u && u->isBool() && u->asBool()});
                    }
                }
                if (const Value* s = cd.find("schema")) {
                    Schema sch = detail::parseJsonSchema(*s);
                    for (const auto& [p, fs] : sch.fields()) {
                        ColumnInfo c;
                        c.name = p;
                        c.type = sourceTypeFromValueType(fs.type());
                        c.notNull = !fs.nullable();
                        t.info.columns.push_back(std::move(c));
                    }
                }
                const Value* docs = cd.find("documents");
                if (!docs || !docs->isArray())
                    throw JsonError("Snapshot '" + name + "': 'documents' fehlt oder ist kein Array");
                t.rows = toDocuments(docs->asArray(), name);
            } else {
                throw JsonError("Snapshot '" + name + "': Collection muss Array oder Objekt sein");
            }
            if (t.info.columns.empty())
                detail::collectColumns(t.rows, t.info.columns);
            tables.push_back(std::move(t));
        }
        parsed = true;
    }
};

InmemdbSnapshotSource::InmemdbSnapshotSource(const std::string& path)
    : impl_(std::make_unique<Impl>()) {
    impl_->path = path;
}

InmemdbSnapshotSource::~InmemdbSnapshotSource() = default;

std::vector<TableInfo> InmemdbSnapshotSource::tables() const {
    impl_->parse();
    std::vector<TableInfo> out;
    out.reserve(impl_->tables.size());
    for (const auto& t : impl_->tables)
        out.push_back(t.info);
    return out;
}

void InmemdbSnapshotSource::readRows(
    const std::string& table,
    const std::function<void(const std::vector<Value>&)>& rowCb) const {
    impl_->parse();
    for (const auto& t : impl_->tables) {
        if (t.info.name == table) {
            for (const auto& r : t.rows)
                rowCb(detail::rowToValues(r, t.info.columns));
            return;
        }
    }
    throw Error("Snapshot: unbekannte Collection '" + table + "'");
}

} // namespace memdb
