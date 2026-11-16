#include "memdb/import_sqlite.hpp"

#ifdef INMEMDB_HAS_SQLITE

#include "memdb/exceptions.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace memdb {

namespace {

std::string quoteIdent(const std::string& name) {
    std::string out = "\"";
    for (char c : name)
        out += (c == '"') ? "\"\"" : std::string(1, c);
    out += "\"";
    return out;
}

SourceType sqliteAffinityToSourceType(const std::string& decl) {
    std::string up;
    for (char c : decl)
        up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (up.find("INT") != std::string::npos || up.find("BOOL") != std::string::npos)
        return SourceType::Integer;
    if (up.find("CHAR") != std::string::npos || up.find("CLOB") != std::string::npos ||
        up.find("TEXT") != std::string::npos)
        return SourceType::Text;
    if (up.find("BLOB") != std::string::npos)
        return SourceType::Blob;
    if (up.find("REAL") != std::string::npos || up.find("FLOA") != std::string::npos ||
        up.find("DOUB") != std::string::npos)
        return SourceType::Real;
    return SourceType::Unknown;
}

Value columnToValue(sqlite3_stmt* st, int i) {
    switch (sqlite3_column_type(st, i)) {
        case SQLITE_INTEGER:
            return Value(static_cast<std::int64_t>(sqlite3_column_int64(st, i)));
        case SQLITE_FLOAT:
            return Value(sqlite3_column_double(st, i));
        case SQLITE_TEXT: {
            const unsigned char* p = sqlite3_column_text(st, i);
            int n = sqlite3_column_bytes(st, i);
            return Value(std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(n)));
        }
        case SQLITE_BLOB: {
            const void* p = sqlite3_column_blob(st, i);
            int n = sqlite3_column_bytes(st, i);
            std::vector<std::uint8_t> bytes;
            if (p && n > 0) {
                const auto* b = static_cast<const std::uint8_t*>(p);
                bytes.assign(b, b + n);
            }
            return Value(std::move(bytes));
        }
        default:
            return Value(nullptr);
    }
}

} // namespace

struct SqliteImportSource::Impl {
    sqlite3* db = nullptr;

    explicit Impl(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            std::string msg = db ? sqlite3_errmsg(db) : "unbekannter Fehler";
            if (db)
                sqlite3_close(db);
            db = nullptr;
            throw Error("SQLite-Datei nicht öffenbar: " + path + " (" + msg + ")");
        }
    }
    ~Impl() {
        if (db)
            sqlite3_close(db);
    }

    std::vector<std::string> execStrings(const std::string& sql) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
            throw Error(std::string("SQLite: ") + sqlite3_errmsg(db) + " bei: " + sql);
        std::vector<std::string> out;
        while (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* p = sqlite3_column_text(st, 0);
            out.emplace_back(p ? reinterpret_cast<const char*>(p) : "");
        }
        sqlite3_finalize(st);
        return out;
    }
};

SqliteImportSource::SqliteImportSource(const std::string& path) : impl_(std::make_unique<Impl>(path)) {}
SqliteImportSource::~SqliteImportSource() = default;

std::vector<TableInfo> SqliteImportSource::tables() const {
    std::vector<TableInfo> out;
    for (const auto& name : impl_->execStrings(
             "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name")) {
        TableInfo t;
        t.name = name;
        sqlite3_stmt* st = nullptr;
        std::string sql = "PRAGMA table_info(" + quoteIdent(name) + ")";
        if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
            throw Error(std::string("SQLite: ") + sqlite3_errmsg(impl_->db));
        while (sqlite3_step(st) == SQLITE_ROW) {
            ColumnInfo col;
            const unsigned char* cn = sqlite3_column_text(st, 1);
            const unsigned char* ct = sqlite3_column_text(st, 2);
            col.name = cn ? reinterpret_cast<const char*>(cn) : "";
            col.type = sqliteAffinityToSourceType(ct ? reinterpret_cast<const char*>(ct) : "");
            col.notNull = sqlite3_column_int(st, 3) != 0;
            col.primaryKey = sqlite3_column_int(st, 5) != 0;
            t.columns.push_back(std::move(col));
        }
        sqlite3_finalize(st);

        // PRAGMA index_list: Spalten seq(0), name(1), unique(2), origin(3), partial(4)
        sqlite3_stmt* ist = nullptr;
        std::string isql = "PRAGMA index_list(" + quoteIdent(name) + ")";
        if (sqlite3_prepare_v2(impl_->db, isql.c_str(), -1, &ist, nullptr) != SQLITE_OK)
            throw Error(std::string("SQLite: ") + sqlite3_errmsg(impl_->db));
        while (sqlite3_step(ist) == SQLITE_ROW) {
            const unsigned char* in = sqlite3_column_text(ist, 1);
            bool unique = sqlite3_column_int(ist, 2) != 0;
            if (!in)
                continue;
            std::string idxName = reinterpret_cast<const char*>(in);
            sqlite3_stmt* cst = nullptr;
            std::string csql = "PRAGMA index_info(" + quoteIdent(idxName) + ")";
            if (sqlite3_prepare_v2(impl_->db, csql.c_str(), -1, &cst, nullptr) == SQLITE_OK) {
                // nur einfache (einspaltige) Indizes uebernehmen
                if (sqlite3_step(cst) == SQLITE_ROW) {
                    const unsigned char* col = sqlite3_column_text(cst, 2);
                    if (col && sqlite3_step(cst) != SQLITE_ROW)
                        t.indexes.push_back(IndexInfo{reinterpret_cast<const char*>(col), unique});
                }
                sqlite3_finalize(cst);
            }
        }
        sqlite3_finalize(ist);

        // v0.28: PRAGMA foreign_key_list – Spalten: id(0) seq(1) table(2) from(3) to(4) ...
        sqlite3_stmt* fst = nullptr;
        std::string fsql = "PRAGMA foreign_key_list(" + quoteIdent(name) + ")";
        if (sqlite3_prepare_v2(impl_->db, fsql.c_str(), -1, &fst, nullptr) == SQLITE_OK) {
            while (sqlite3_step(fst) == SQLITE_ROW) {
                ForeignKeyInfo fk;
                const unsigned char* tbl = sqlite3_column_text(fst, 2);
                const unsigned char* from = sqlite3_column_text(fst, 3);
                const unsigned char* to = sqlite3_column_text(fst, 4);
                fk.refTable = tbl ? reinterpret_cast<const char*>(tbl) : "";
                fk.column = from ? reinterpret_cast<const char*>(from) : "";
                fk.refColumn = to ? reinterpret_cast<const char*>(to) : ""; // NULL = PK der Zieltabelle
                t.foreignKeys.push_back(std::move(fk));
            }
            sqlite3_finalize(fst);
        }
        out.push_back(std::move(t));
    }
    return out;
}

void SqliteImportSource::readRows(const std::string& table,
                                  const std::function<void(const std::vector<Value>&)>& rowCb) const {
    sqlite3_stmt* st = nullptr;
    std::string sql = "SELECT * FROM " + quoteIdent(table);
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
        throw Error(std::string("SQLite: ") + sqlite3_errmsg(impl_->db) + " bei: " + sql);
    int ncol = sqlite3_column_count(st);
    while (sqlite3_step(st) == SQLITE_ROW) {
        std::vector<Value> row;
        row.reserve(static_cast<std::size_t>(ncol));
        for (int i = 0; i < ncol; ++i)
            row.push_back(columnToValue(st, i));
        rowCb(row);
    }
    sqlite3_finalize(st);
}

} // namespace memdb

#endif // INMEMDB_HAS_SQLITE
