#pragma once

#include "database.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace memdb {

class ReferenceIndex;

/// @brief Ein Feld einer Collection im Datenbank-Graph (FA-22).
struct GraphField {
    std::string name;        ///< Feldpfad (Punktnotation)
    std::string type;        ///< Typname wie in `docs/de/guides/type-mapping.md` ("string","int","double","bool","binary","document","array","null"); "" = unbekannt (Index ohne Schema)
    bool nullable = true;
    std::string index;       ///< "" | "index" | "unique"
};

/// @brief Collection als Knoten des Datenbank-Graphs.
struct GraphNode {
    std::string collection;
    std::size_t documents = 0;
    bool hasSchema = false;
    std::vector<GraphField> fields; ///< nur wenn hasSchema (plus Index-only-Felder)
};

/// @brief Referenz als Kante des Datenbank-Graphs (FA-18).
struct GraphEdge {
    std::string from;   ///< "orders#1" (Dokument) oder "orders" (Collection-Level)
    std::string to;     ///< "users#2" oder "users"
    std::string relation;
    bool dangling = false; ///< Ziel existiert nicht (mehr)
};

/// @brief Abhängigkeitsfreie Visualisierung einer Database als Graph (FA-22):
/// Collections (inkl. optionalem FA-20-Schema und FA-10-Indizes) als Knoten,
/// FA-18-Referenzen als Kanten. Ausgabe als JSON / Mermaid / Markdown –
/// bewusst textbasiert und LLM-freundlich (stabile Namen, explizite Typen).
///
/// ```cpp
/// DbGraph g = DbGraph::fromDatabase(db);
/// std::string mmd = g.toMermaid();   // rendert direkt als Diagramm
/// std::string js  = g.toJson();      // strukturiert für LLM/Tooling
/// DbGraph g2 = DbGraph::fromSnapshotFile("shop.inmemdb.json"); // aus FA-14-Snapshot
/// ```
class DbGraph {
public:
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;

    /// @brief Graph aus einer laufenden Database. Die Speicher-Collection der
    /// Referenzen (Default `__references`) wird nicht als Knoten ausgegeben,
    /// sondern nur für Kanten verwendet.
    /// @param refs wenn nullptr, wird ein `ReferenceIndex` mit Default-Storage verwendet.
    static DbGraph fromDatabase(Database& db, const ReferenceIndex* refs = nullptr);

    /// @brief Graph aus einem FA-14-JSON-Snapshot (`saveToFile`/`toJson`).
    /// Dokument-IDs sind im Snapshot nicht enthalten; `dangling` wird daher nur
    /// über die Ziel-Collection geprüft (fehlende Collection => dangling).
    static DbGraph fromSnapshotFile(const std::string& path);

    /// @brief Strukturiertes JSON (Sammlung + Referenzen), für LLM/Tooling.
    std::string toJson() const;
    /// @brief Mermaid-Diagramm (`graph LR`), rendert direkt in GitHub/VS Code/LLM.
    std::string toMermaid() const;
    /// @brief Markdown-Tabellen (Collections + Referenzen), für Doku.
    std::string toMarkdown() const;
    /// @brief Einzeilige Text-Zusammenfassung, z. B. "2 Collections, 5 Dokumente, 2 Referenzen (1 dangling)".
    std::string summary() const;
};

} // namespace memdb
