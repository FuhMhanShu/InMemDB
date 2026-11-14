// CLI: externe Datei (CSV, SQLite, ...) per ImportRegistry in eine InMemDB-Database importieren.
// Aufruf: inmemdb_import <datei> [--source NAME] [--out <datei>] [--password <pw>]
//          [--no-schema] [--no-indexes] [--no-pk-field] [--batch N]
//          [--delimiter C] [--no-header] [--infer-types] [--list-sources]
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <memdb/import.hpp>
#include <memdb/import_csv.hpp>
#include <memdb/memdb.hpp>

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--list-sources") {
        std::cout << "registrierte Import-Quellen:\n";
        for (const auto& n : memdb::ImportRegistry::instance().sourceNames())
            std::cout << "  " << n << "\n";
        return 0;
    }
    if (argc < 2) {
        std::cerr << "Aufruf: inmemdb_import <datei> [--source NAME] [--out <datei>] [--password <pw>] "
                     "[--no-schema] [--no-indexes] [--no-pk-field] [--batch N] "
                     "[--delimiter C] [--no-header] [--infer-types] [--list-sources]\n";
        return 2;
    }
    std::string file = argv[1];
    std::string sourceName;
    memdb::ImportOptions opts;
    std::string out, password;
    memdb::CsvImportSource::Options csvOpts;
    bool csvOptsTouched = false;
    bool listSources = false;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "fehlender Wert fuer " << a << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--source") sourceName = next();
        else if (a == "--out") out = next();
        else if (a == "--password") password = next();
        else if (a == "--no-schema") opts.generateSchema = false;
        else if (a == "--no-indexes") opts.createIndexes = false;
        else if (a == "--no-pk-field") opts.keepPrimaryKeyField = false;
        else if (a == "--batch") opts.batchSize = std::stoull(next());
        else if (a == "--list-sources") listSources = true;
        else if (a == "--delimiter") { std::string d = next(); if (!d.empty()) csvOpts.delimiter = d.front(); csvOptsTouched = true; }
        else if (a == "--no-header") { csvOpts.hasHeader = false; csvOptsTouched = true; }
        else if (a == "--infer-types") { csvOpts.inferTypes = true; csvOptsTouched = true; }
        else { std::cerr << "unbekannte Option: " << a << "\n"; return 2; }
    }

    try {
        auto& reg = memdb::ImportRegistry::instance();
        if (listSources) {
            std::cout << "registrierte Import-Quellen:\n";
            for (const auto& n : reg.sourceNames())
                std::cout << "  " << n << "\n";
            return 0;
        }

        std::string name = sourceName.empty() ? reg.sourceForFile(file) : sourceName;
        if (sourceName.empty())
            std::cout << "Quelle per Endung gewaehlt: " << name << " (" << file << ")\n";
        std::unique_ptr<memdb::ImportSource> src;
        if (name == "csv" && csvOptsTouched)
            src = std::make_unique<memdb::CsvImportSource>(file, csvOpts);
        else
            src = reg.create(name, file);

        memdb::Database db(file);
        auto stats = memdb::Importer::import(db, *src, opts);
        std::cout << "Import abgeschlossen: " << stats.totalRows << " Zeilen in "
                  << stats.rowsPerTable.size() << " Collections\n";
        for (const auto& [name, n] : stats.rowsPerTable)
            std::cout << "  " << name << ": " << n << " Zeilen"
                      << (db.collection(name).hasSchema() ? " (mit Schema)" : "") << "\n";
        for (const auto& s : stats.skippedTables)
            std::cout << "  uebersprungen (existiert): " << s << "\n";
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
