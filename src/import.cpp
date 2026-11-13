#include "memdb/import.hpp"

#include "memdb/exceptions.hpp"

namespace memdb {

std::optional<Value::Type> sourceTypeToValueType(SourceType t) {
    switch (t) {
        case SourceType::Null: return Value::Type::Null;
        case SourceType::Integer: return Value::Type::Int;
        case SourceType::Real: return Value::Type::Double;
        case SourceType::Text: return Value::Type::String;
        case SourceType::Blob: return Value::Type::Binary;
        case SourceType::Unknown: return std::nullopt;
    }
    return std::nullopt;
}

ImportStats Importer::import(Database& db, const ImportSource& src, const ImportOptions& opts) {
    ImportStats stats;
    for (const TableInfo& t : src.tables()) {
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
    return stats;
}

} // namespace memdb
