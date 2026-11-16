#include "memdb/import.hpp"

#include "memdb/references.hpp"

#include "memdb/exceptions.hpp"
#include "memdb/import_bson.hpp"
#include "memdb/import_csv.hpp"
#include "memdb/import_json.hpp"
#include "memdb/import_snapshot.hpp"
#include "memdb/import_sqldump.hpp"
#ifdef INMEMDB_HAS_SQLITE
#include "memdb/import_sqlite.hpp"
#endif

#include <algorithm>
#include <map>
#include <set>
#include <cctype>

namespace memdb {

namespace {

std::string toLower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string normalizeExt(std::string ext) {
    ext = toLower(std::move(ext));
    if (!ext.empty() && ext.front() != '.')
        ext.insert(ext.begin(), '.');
    return ext;
}

} // namespace

ImportRegistry& ImportRegistry::instance() {
    static ImportRegistry reg;
    return reg;
}

ImportRegistry::ImportRegistry() {
    registerSource("csv",
                   [](const std::string& p) {
                       return std::unique_ptr<ImportSource>(std::make_unique<CsvImportSource>(p));
                   },
                   {".csv"}, "CSV-Datei (Header-Zeile, ohne externe Abhaengigkeiten)");
    registerSource("json",
                   [](const std::string& p) {
                       return std::unique_ptr<ImportSource>(std::make_unique<JsonImportSource>(p));
                   },
                   {".json"}, "JSON (Array/einzelnes Objekt; JSONL-Fallback, Auto-Modus)");
    registerSource("jsonl",
                   [](const std::string& p) {
                       JsonImportOptions o;
                       o.mode = JsonMode::Lines;
                       return std::unique_ptr<ImportSource>(std::make_unique<JsonImportSource>(p, o));
                   },
                   {".jsonl", ".ndjson"}, "JSON Lines (ein Objekt pro Zeile)");
    registerSource("snapshot",
                   [](const std::string& p) {
                       return std::unique_ptr<ImportSource>(std::make_unique<InmemdbSnapshotSource>(p));
                   },
                   {".inmemdb.json"}, "InMemDB-JSON-Snapshot (FA-14, mit Schema/Indizes)");
    registerSource("sqldump",
                   [](const std::string& p) {
                       return std::unique_ptr<ImportSource>(std::make_unique<SqlDumpImportSource>(p));
                   },
                   {".sql", ".dump"}, "SQL-Dump (CREATE TABLE + INSERT, ohne externe Abhaengigkeiten)");
    registerSource("bson",
                   [](const std::string& p) {
                       return std::unique_ptr<ImportSource>(std::make_unique<BsonImportSource>(p));
                   },
                   {".bson"}, "BSON-Datei (z. B. mongodump)");
#ifdef INMEMDB_HAS_SQLITE
    registerSource("sqlite",
                   [](const std::string& p) {
                       return std::unique_ptr<ImportSource>(std::make_unique<SqliteImportSource>(p));
                   },
                   {".db", ".sqlite", ".sqlite3"}, "SQLite-Datei (INMEMDB_WITH_SQLITE=ON)");
#endif
}

void ImportRegistry::registerSource(std::string name, ImportSourceFactory factory,
                                    std::vector<std::string> extensions, std::string /*description*/) {
    name = toLower(std::move(name));
    if (name.empty() || !factory)
        throw Error("ImportRegistry::registerSource: Name/Facto leer");
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto it = byExtension_.begin(); it != byExtension_.end();)
        if (it->second == name)
            it = byExtension_.erase(it);
        else
            ++it;
    factories_[name] = std::move(factory);
    for (auto& ext : extensions)
        byExtension_[normalizeExt(std::move(ext))] = name;
}

void ImportRegistry::unregisterSource(const std::string& name) {
    std::string n = toLower(name);
    std::lock_guard<std::mutex> lk(mutex_);
    factories_.erase(n);
    for (auto it = byExtension_.begin(); it != byExtension_.end();)
        if (it->second == n)
            it = byExtension_.erase(it);
        else
            ++it;
}

bool ImportRegistry::hasSource(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mutex_);
    return factories_.count(toLower(name)) != 0;
}

std::vector<std::string> ImportRegistry::sourceNames() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<std::string> out;
    out.reserve(factories_.size());
    for (const auto& [n, f] : factories_)
        out.push_back(n);
    return out;
}

std::unique_ptr<ImportSource> ImportRegistry::create(const std::string& name,
                                                     const std::string& path) const {
    std::unique_lock<std::mutex> lk(mutex_);
    auto it = factories_.find(toLower(name));
    if (it == factories_.end())
        throw Error("ImportRegistry: unbekannte Quelle '" + name + "' (registriert: " +
                    [&] {
                        std::string s;
                        for (const auto& [n, f] : factories_) {
                            if (!s.empty())
                                s += ", ";
                            s += n;
                        }
                        return s;
                    }() +
                    ")");
    auto factory = it->second;
    lk.unlock();
    return factory(path);
}

std::string ImportRegistry::sourceForFile(const std::string& path) const {
    // Suffix-Matching auf dem ganzen Pfad (case-insensitiv), laengster Suffix gewinnt,
    // damit z. B. ".inmemdb.json" vor ".json" greift.
    std::string low = toLower(path);
    std::lock_guard<std::mutex> lk(mutex_);
    std::string bestName;
    std::size_t bestLen = 0;
    for (const auto& [ext, name] : byExtension_)
        if (low.size() >= ext.size() && ext.size() > bestLen &&
            low.compare(low.size() - ext.size(), std::string::npos, ext) == 0) {
            bestLen = ext.size();
            bestName = name;
        }
    if (bestName.empty()) {
        std::string known;
        for (const auto& [ext, name] : byExtension_) {
            if (!known.empty())
                known += ", ";
            known += ext;
        }
        throw Error("ImportRegistry: keine Quelle fuer Datei '" + path +
                    "' registriert (bekannte Endungen: " + known + ")");
    }
    return bestName;
}

std::unique_ptr<ImportSource> ImportRegistry::createForFile(const std::string& path) const {
    std::string name = sourceForFile(path);
    return create(name, path);
}

std::optional<Value::Type> sourceTypeToValueType(SourceType t) {
    switch (t) {
        case SourceType::Null: return Value::Type::Null;
        case SourceType::Integer: return Value::Type::Int;
        case SourceType::Real: return Value::Type::Double;
        case SourceType::Text: return Value::Type::String;
        case SourceType::Blob: return Value::Type::Binary;
        case SourceType::Bool: return Value::Type::Bool;
        case SourceType::Document: return Value::Type::Document;
        case SourceType::Array: return Value::Type::Array;
        case SourceType::Unknown: return std::nullopt;
    }
    return std::nullopt;
}

SourceType sourceTypeFromValueType(Value::Type t) {
    switch (t) {
        case Value::Type::Null: return SourceType::Null;
        case Value::Type::Bool: return SourceType::Bool;
        case Value::Type::Int: return SourceType::Integer;
        case Value::Type::Double: return SourceType::Real;
        case Value::Type::String: return SourceType::Text;
        case Value::Type::Binary: return SourceType::Blob;
        case Value::Type::Document: return SourceType::Document;
        case Value::Type::Array: return SourceType::Array;
    }
    return SourceType::Unknown;
}

ImportStats Importer::import(Database& db, const ImportSource& src, const ImportOptions& opts) {
    ImportStats stats;
    const std::vector<TableInfo> infos = src.tables(); // Kopie fuer FK-Pass (v0.28)
    for (const TableInfo& t : infos) {
        if (db.hasCollection(t.name)) {
            stats.skippedTables.push_back(t.name);
            continue;
        }
        Collection c = db.createCollection(t.name);

        std::vector<Document> batch;
        batch.reserve(opts.batchSize);
        std::size_t rows = 0;
        src.readRows(t.name, [&](const std::vector<Value>& row) {
            Document d;
            for (std::size_t i = 0; i < t.columns.size() && i < row.size(); ++i)
                d.set(t.columns[i].name, row[i]);
            if (opts.keepPrimaryKeyField && !opts.pkFieldName.empty())
                for (std::size_t i = 0; i < t.columns.size() && i < row.size(); ++i)
                    if (t.columns[i].primaryKey)
                        d.set(opts.pkFieldName, row[i]);
            batch.push_back(std::move(d));
            ++rows;
            if (batch.size() >= opts.batchSize) {
                c.insertBulk(std::move(batch));
                batch.clear();
                batch.reserve(opts.batchSize);
            }
        });
        if (!batch.empty())
            c.insertBulk(std::move(batch));

        if (opts.createIndexes)
            for (const auto& ix : t.indexes)
                c.createIndex(ix.column, ix.unique);

        if (opts.generateSchema) {
            Schema schema;
            bool ok = true;
            for (const auto& col : t.columns) {
                auto vt = sourceTypeToValueType(col.type);
                if (!vt) {
                    ok = false;
                    break;
                }
                schema.add(col.name, *vt).setNullable(!col.notNull);
            }
            if (ok && opts.keepPrimaryKeyField && !opts.pkFieldName.empty()) {
                for (const auto& col : t.columns)
                    if (col.primaryKey)
                        if (auto vt = sourceTypeToValueType(col.type))
                            schema.add(opts.pkFieldName, *vt).notNull();
            }
            if (ok) {
                try {
                    c.setSchema(std::move(schema));
                } catch (const SchemaError&) {
                    stats.skippedSchemas.push_back(t.name);
                }
            } else {
                stats.skippedSchemas.push_back(t.name);
            }
        }

        stats.rowsPerTable[t.name] = rows;
        stats.totalRows += rows;
    }

    // v0.28: Fremdschlüssel als FA-18-Referenzen importieren (ueber pk-Feld bzw. refColumn matchen)
    if (opts.importForeignKeys) {
        ReferenceIndex refs(db); // legt __references an, falls nicht vorhanden
        std::set<std::string> imported;
        for (const auto& [n, r] : stats.rowsPerTable)
            imported.insert(n);
        auto pkColumnOf = [&](const std::string& table) -> std::string {
            for (const auto& ti : infos)
                if (ti.name == table)
                    for (const auto& c : ti.columns)
                        if (c.primaryKey)
                            return c.name;
            return "";
        };
        for (const TableInfo& t : infos) {
            if (!imported.count(t.name) || t.foreignKeys.empty() || !db.hasCollection(t.name))
                continue;
            for (const ForeignKeyInfo& fk : t.foreignKeys) {
                if (!db.hasCollection(fk.refTable)) {
                    ++stats.skippedForeignKeys;
                    continue;
                }
                const std::string refField =
                    !fk.refColumn.empty()
                        ? fk.refColumn
                        : ((opts.keepPrimaryKeyField && !opts.pkFieldName.empty()) ? opts.pkFieldName
                                                                                     : pkColumnOf(fk.refTable));
                if (refField.empty()) {
                    ++stats.skippedForeignKeys;
                    continue;
                }
                // Ziel-Map: refField-Wert -> DocumentId
                Collection target = db.collection(fk.refTable);
                std::map<Value, DocumentId, ValueLess> targetByValue;
                target.forEach(Expression{}, [&](const Document& d) {
                    if (const Value* v = d.findPath(refField))
                        targetByValue.emplace(*v, d.id());
                });
                Collection source = db.collection(t.name);
                const std::string relation = fk.name.empty() ? ("fk_" + fk.column) : fk.name;
                source.forEach(Expression{}, [&](const Document& d) {
                    const Value* v = d.findPath(fk.column);
                    if (!v || v->isNull())
                        return;
                    auto it = targetByValue.find(*v);
                    if (it == targetByValue.end()) {
                        ++stats.skippedForeignKeys;
                        return;
                    }
                    try {
                        refs.link(t.name, d.id(), fk.refTable, it->second, relation);
                        ++stats.importedReferences;
                    } catch (const ReferenceExistsError&) {
                        // identisches Duplikat – kein Fehler
                    }
                });
            }
        }
    }
    return stats;
}

ImportStats Importer::import(Database& db, const std::string& sourceName, const std::string& path,
                             const ImportOptions& opts) {
    auto src = ImportRegistry::instance().create(sourceName, path);
    return import(db, *src, opts);
}

} // namespace memdb
