// CLI: inmemdb_discover – Relationship-Discovery (v0.29)
// Aufruf: inmemdb_discover <datei1> [datei2 ...] [--password pw] [--min-support X]
//          [--min-confidence X] [--min-correlation X] [--sample N] [--seed N]
//          [--mermaid] [--orphans] [--outliers] [--path A B] [--diff] [--suggest-merges]
//          [--report <prefix>] [--confusion] [--describe [de|en]] [--apply] [--out <datei>]
// Mehrere Dateien = database-übergreifende Analyse (Report über alle Collections).
// --describe [de|en]: Klartext-Beschreibung der Collection-Zusammenhänge (v0.45, Default de)
// --apply: FK-Befunde (confidence >= --min-confidence) pro Database als FA-18-Referenzen
//          anlegen und die jeweilige Datei zurückschreiben (verschlüsselt bei --password).
#include <memdb/memdb.hpp>
#include <memdb/message_handler.hpp>
#include <memdb/references.hpp>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static int run(int argc, char** argv) {
    // v0.36: CLI-Meldungen laufen ueber den MessageHandler (stderr ab Warning)
    memdb::MessageHandler::instance().subscribe([](const memdb::Message& m) {
        if (static_cast<int>(m.level) >= static_cast<int>(memdb::MessageLevel::Warning))
            std::cerr << "Fehler: " << m.text << "\n";
    });
    if (argc < 2) {
        std::cerr << "Aufruf: inmemdb_discover <datei1> [datei2 ...] [--password pw] [--min-support X] "
                     "[--min-confidence X] [--min-correlation X] [--min-similarity X] [--sample N] [--seed N] "
                     "[--mermaid] [--orphans] [--outliers] [--path A B] [--diff] [--suggest-merges] [--report <prefix>] "
                     "[--confusion] [--describe [de|en]] [--apply] [--out <datei>]\n"
                     "  Dateien: .inmemdb.json / .json-Snapshot (plain) bzw. verschlüsselt mit --password\n"
                     "  --report <prefix>: Gesamtanalyse (v0.33) -> <prefix>.report.md / .relations.json / .graph.mmd + Summary\n"
                     "  --confusion: Confusion-Matrix der FK-Befunde vs. __references (v0.42, nur 1 Datei mit Referenzen)\n"
                     "  --describe [de|en]: Klartext-Beschreibung der Zusammenhänge (v0.45, Default de)\n";
        return 2;
    }
    std::vector<std::string> files;
    std::string password, out;
    memdb::DiscoveryOptions opts;
    bool mermaid = false, apply = false, orphans = false, diffMode = false, suggestMerges = false, outliers = false, confusion = false;
    bool describe = false; // v0.45: Klartext-Beschreibung
    memdb::NarrativeLanguage narrLang = memdb::NarrativeLanguage::German;
    std::string reportPrefix;
    std::string pathFrom, pathTo;
    double applyMinConf = 0.9;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "fehlender Wert fuer " << a << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--password") password = next();
        else if (a == "--out") out = next();
        else if (a == "--min-support") opts.minSupport = std::stod(next());
        else if (a == "--min-confidence") { opts.minConfidence = std::stod(next()); applyMinConf = opts.minConfidence; }
        else if (a == "--min-correlation") opts.minCorrelation = std::stod(next());
        else if (a == "--min-similarity") opts.minSimilarity = std::stod(next());
        else if (a == "--sample") opts.sampleSize = static_cast<std::size_t>(std::stoull(next()));
        else if (a == "--seed") opts.seed = std::stoull(next());
        else if (a == "--mermaid") mermaid = true;
        else if (a == "--orphans") orphans = true;
        else if (a == "--outliers") outliers = true; // v0.40
        else if (a == "--confusion") confusion = true; // v0.42
        else if (a == "--describe") { // v0.45: Klartext, optional Sprachcode de|en
            describe = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                std::string l = argv[++i];
                for (auto& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (l == "en" || l == "english") narrLang = memdb::NarrativeLanguage::English;
                else if (l == "de" || l == "german" || l == "deutsch") narrLang = memdb::NarrativeLanguage::German;
                else { std::cerr << "unbekannte Sprache fuer --describe: " << l << " (de|en)\n"; return 2; }
            }
        }
        else if (a == "--diff") diffMode = true;
        else if (a == "--suggest-merges") suggestMerges = true;
        else if (a == "--report") reportPrefix = next();
        else if (a == "--path") { pathFrom = next(); pathTo = next(); }
        else if (a == "--apply") apply = true;
        else if (!a.empty() && a[0] == '-') { std::cerr << "unbekannte Option: " << a << "\n"; return 2; }
        else files.push_back(a);
    }
    if (files.empty()) {
        std::cerr << "keine Eingabedatei\n";
        return 2;
    }

    std::vector<memdb::Database> dbs;
    std::vector<memdb::Collection> allColls;
    for (const auto& f : files) {
        memdb::Database db = password.empty() ? memdb::Database::loadFromFile(f)
                                              : memdb::Database::loadFromFileEncrypted(f, password);
        for (const auto& n : db.collectionNames()) {
            // Referenz-Speicher nicht als Daten-Collection analysieren
            if (n == "__references")
                continue;
            allColls.push_back(db.collection(n));
        }
        dbs.push_back(std::move(db));
    }

    auto rels = memdb::discoverRelations(allColls, opts);
    std::cout << memdb::toJson(rels) << "\n";

    if (describe) { // v0.45: Klartext-Beschreibung der Collection-Zusammenhänge (de/en)
        auto rep = dbs.size() == 1 ? memdb::analyze(dbs[0], opts) : memdb::analyze(allColls, opts);
        memdb::NarrativeOptions nopts;
        nopts.language = narrLang;
        std::cout << memdb::toNarrative(rep, nopts);
    }

    if (!reportPrefix.empty()) {
        // v0.42: bei genau einer Datei analyze(Database&) -> Confusion-Matrix gegen __references im Report
        auto rep = dbs.size() == 1 ? memdb::analyze(dbs[0], opts) : memdb::analyze(allColls, opts);
        memdb::printSummary(rep, std::cout);
        std::ofstream(reportPrefix + ".report.md", std::ios::binary) << memdb::toMarkdown(rep);
        std::ofstream(reportPrefix + ".relations.json", std::ios::binary) << memdb::toJson(rep);
        std::ofstream(reportPrefix + ".graph.mmd", std::ios::binary) << memdb::toMermaid(rep);
        std::cerr << "Report geschrieben: " << reportPrefix << ".report.md / .relations.json / .graph.mmd\n";
    }
    if (mermaid)
        std::cout << memdb::toMermaid(rels) << "\n";
    if (!out.empty()) {
        std::ofstream o(out, std::ios::binary);
        o << memdb::toJson(rels);
    }
    std::cerr << rels.size() << " Relationen gefunden\n";

    if (orphans) {
        auto reps = memdb::findOrphansForRelations(allColls, rels, opts);
        std::string oj = "{\"orphans\":[";
        bool f2 = true;
        for (const auto& r : reps) {
            if (!f2) oj += ',';
            f2 = false;
            oj += "{\"from\":\"" + r.fromCollection + "." + r.field + "\",\"to\":\"" + r.toCollection + "." + r.toField +
                  "\",\"scanned\":" + std::to_string(r.scanned) + ",\"orphans\":" + std::to_string(r.orphans) + "}";
        }
        oj += "]}";
        std::cout << oj << "\n";
    }
    if (outliers) { // v0.40: Ausreißer pro numerischem Feld (z-Score + IQR)
        auto outs = memdb::discoverOutliers(allColls, opts);
        std::string oj = "{\"outliers\":[";
        bool fo = true;
        for (const auto& f : outs) {
            if (!fo) oj += ',';
            fo = false;
            oj += "{\"from\":\"" + f.collection + "." + f.field + "\",\"count\":" + std::to_string(f.count) +
                  ",\"outliers\":" + std::to_string(f.outliers) + ",\"rate\":" + std::to_string(f.outlierRate) +
                  ",\"method\":\"" + f.method + "\"}";
        }
        oj += "]}";
        std::cout << oj << "\n";
    }
    if (confusion) { // v0.42: Confusion-Matrix der FK-Befunde vs. deklarierte __references
        if (dbs.size() != 1 || !dbs[0].hasCollection("__references")) {
            std::cerr << "--confusion braucht genau eine Datei mit __references-Collection\n";
            return 2;
        }
        std::cout << memdb::toJson(memdb::evaluateConfusion(rels, dbs[0], opts)) << "\n";
    }
    if (diffMode && dbs.size() >= 2) {
        auto dd = memdb::diffDatabases(dbs[0], dbs[1], opts);
        std::cout << memdb::toJson(dd) << "\n";
    }
    if (suggestMerges) {
        auto sug = memdb::suggestMerges(allColls, opts);
        std::string sj = "{\"mergeSuggestions\":[";
        bool f3 = true;
        for (const auto& m : sug) {
            if (!f3) sj += ',';
            f3 = false;
            sj += "{\"a\":\"" + m.collectionA + "\",\"b\":\"" + m.collectionB + "\",\"schemaSimilarity\":" +
                  std::to_string(m.schemaSimilarity) + ",\"valueOverlap\":" + std::to_string(m.valueOverlap) +
                  ",\"commonField\":\"" + m.commonField + "\"}";
        }
        sj += "]}";
        std::cout << sj << "\n";
    }
    if (!pathFrom.empty() && !pathTo.empty()) {
        auto paths = memdb::findPaths(rels, pathFrom, pathTo, 4);
        std::cout << "{\"paths\":[" << paths.size() << "]}" << "\n";
        for (const auto& p : paths) {
            std::cout << "  ";
            for (const auto& r : p)
                std::cout << r.fromCollection << "." << r.field << " -> " << r.toCollection << "." << r.toField << "  ";
            std::cout << "\n";
        }
    }

    if (apply) {
        std::size_t total = 0;
        for (std::size_t i = 0; i < dbs.size(); ++i) {
            memdb::ReferenceIndex refs(dbs[i]);
            // pro DB nur die Befunde anwenden, deren Collections in dieser DB liegen
            std::vector<memdb::DiscoveredRelation> local;
            for (const auto& r : rels)
                if (r.kind == memdb::RelationKind::ForeignKey && dbs[i].hasCollection(r.fromCollection) &&
                    dbs[i].hasCollection(r.toCollection))
                    local.push_back(r);
            total += memdb::applyDiscovered(dbs[i], local, refs, applyMinConf);
            if (password.empty())
                dbs[i].saveToFile(files[i]);
            else
                dbs[i].saveToFileEncrypted(files[i], password);
        }
        std::cerr << "apply: " << total << " Referenzen angelegt und Dateien zurückgeschrieben\n";
    }
    return 0;
}

int main(int argc, char** argv) {
    // v0.35: CLI immer sauber beenden – auch bei Nicht-memdb-Exceptions (z. B. std::stod auf
    // ungueltiger Option, std::bad_alloc): Fehlermeldung + Exit-Code statt terminate/crash.
    try {
        return run(argc, argv);
    } catch (const memdb::Error& e) {
        memdb::MessageHandler::instance().error("cli", e.what()); // v0.36: ueber MessageHandler (stderr-Handler)
        return 1;
    } catch (const std::exception& e) {
        memdb::MessageHandler::instance().error("cli", e.what());
        return 1;
    }
}
