// CLI: Datenbank-Graph (FA-22) aus einem JSON-Snapshot erzeugen.
// Aufruf: inmemdb_graph <snapshot.json> [--format mermaid|json|md|all] [--out <datei>]
// Default: Mermaid auf stdout.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include <memdb/db_graph.hpp>
#include <memdb/memdb.hpp>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Aufruf: inmemdb_graph <snapshot.json> [--format mermaid|json|md|all] "
                     "[--out <datei>]\n";
        return 2;
    }
    std::string file = argv[1];
    std::string format = "mermaid";
    std::string out;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "fehlender Wert fuer " << a << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--format") format = next();
        else if (a == "--out") out = next();
        else { std::cerr << "unbekannte Option: " << a << "\n"; return 2; }
    }

    try {
        memdb::DbGraph g = memdb::DbGraph::fromSnapshotFile(file);
        std::cout << "# " << g.summary() << "\n\n";
        auto writeOut = [&](const std::string& content, const std::string& suffix) {
            if (out.empty()) {
                std::cout << content << "\n";
            } else {
                std::string p = (format == "all") ? out + suffix : out;
                std::ofstream o(p, std::ios::binary);
                o << content << "\n";
                std::cout << "geschrieben: " << p << "\n";
            }
        };
        if (format == "mermaid" || format == "all")
            writeOut(g.toMermaid(), ".mmd");
        if (format == "json" || format == "all")
            writeOut(g.toJson(), ".json");
        if (format == "md" || format == "markdown" || format == "all")
            writeOut(g.toMarkdown(), ".md");
        if (format != "mermaid" && format != "json" && format != "md" && format != "markdown" &&
            format != "all") {
            std::cerr << "unbekanntes Format: " << format << " (mermaid|json|md|all)\n";
            return 2;
        }
        return 0;
    } catch (const memdb::Error& e) {
        std::cerr << "Fehler: " << e.what() << "\n";
        return 1;
    }
}
