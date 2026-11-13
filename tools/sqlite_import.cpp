// CLI: SQLite-Datei in eine InMemDB-Database importieren.
// Build nur mit -DINMEMDB_WITH_SQLITE=ON.
// Aufruf: sqlite_import <sqlite-datei> [--out <datei>] [--password <pw>] [--no-schema] [--no-indexes] [--batch N]
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#ifdef INMEMDB_HAS_SQLITE

#include <memdb/import.hpp>
#include <memdb/import_sqlite.hpp>
#include <memdb/memdb.hpp>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Aufruf: sqlite_import <sqlite-datei> [--out <datei>] [--password <pw>] "
                     "[--no-schema] [--no-indexes] [--batch N] [--no-pk-field]\n";
        return 2;
    }
    std::string file = argv[1];
    memdb::ImportOptions opts;
    std::string out, password;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "fehlender Wert für " << a << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--out") out = next();
        else if (a == "--password") password = next();
        else if (a == "--no-schema") opts.generateSchema = false;
        else if (a == "--no-indexes") opts.createIndexes = false;
        else if (a == "--no-pk-field") opts.keepPrimaryKeyField = false;
        else if (a == "--batch") opts.batchSize = std::stoull(next());
        else { std::cerr << "unbekannte Option: " << a << "\n"; return 2; }
    }

    try {
        memdb::SqliteImportSource src(file);
        memdb::Database db(file);
        auto stats = memdb::Importer::import(db, src, opts);
        std::cout << "Import abgeschlossen: " << stats.totalRows << " Zeilen in "
                  << stats.rowsPerTable.size() << " Collections\n";
        for (const auto& [name, n] : stats.rowsPerTable)
            std::cout << "  " << name << ": " << n << " Zeilen"
                      << (db.collection(name).hasSchema() ? " (mit Schema)" : "") << "\n";
        for (const auto& s : stats.skippedTables)
            std::cout << "  übersprungen (existiert): " << s << "\n";
        for (const auto& s : stats.skippedSchemas)
            std::cout << "  ohne Schema (Typ-Mismatch): " << s << "\n";
        if (!out.empty()) {
            if (!password.empty())
                db.saveToFileEncrypted(out, password);
            else
                db.saveToFile(out);
            std::cout << "Snapshot geschrieben: " << out << "\n";
        }
        return 0;
    } catch (const memdb::Error& e) {
        std::cerr << "Fehler: " << e.what() << "\n";
        return 1;
    }
}

#else

int main() {
    std::cerr << "Dieses Tool wurde ohne -DINMEMDB_WITH_SQLITE=ON gebaut.\n";
    return 2;
}

#endif
