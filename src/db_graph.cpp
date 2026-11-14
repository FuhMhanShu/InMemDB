#include "memdb/db_graph.hpp"

#include "json.hpp"
#include "memdb/document_id.hpp"
#include "memdb/exceptions.hpp"
#include "memdb/references.hpp"
#include "memdb/schema.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace memdb {

namespace {

std::string valueTypeName(Value::Type t) {
    switch (t) {
        case Value::Type::Null: return "null";
        case Value::Type::Bool: return "bool";
        case Value::Type::Int: return "int";
        case Value::Type::Double: return "double";
        case Value::Type::String: return "string";
        case Value::Type::Binary: return "binary";
        case Value::Type::Document: return "document";
        case Value::Type::Array: return "array";
    }
    return "unknown";
}

std::string jsonStr(const std::string& s) { return detail::writeJsonString(s); }

/// Mermaid-Knoten-ID: nur [A-Za-z0-9_] erlaubt
std::string mermaidId(const std::string& name) {
    std::string out;
    for (char c : name)
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front())))
        out = "n_" + out;
    return out;
}

std::string mermaidLabel(const std::string& s) {
    std::string out;
    for (char c : s)
        out += (c == '"') ? '\'' : c;
    return out;
}

std::string refEndpoint(const std::string& coll, const std::optional<DocumentId>& id) {
    if (!id)
        return coll;
    return coll + "#" + toString(*id);
}

std::optional<DocumentId> docIdFromValue(const Value* v) {
    if (!v || v->isNull())
        return std::nullopt;
    return DocumentId(static_cast<std::uint64_t>(v->asInt()));
}

GraphEdge edgeFromRefDoc(const Document& d, bool dangling) {
    GraphEdge e;
    e.from = refEndpoint(d.at("from_coll").asString(), docIdFromValue(d.find("from_id")));
    e.to = refEndpoint(d.at("to_coll").asString(), docIdFromValue(d.find("to_id")));
    if (const Value* r = d.find("relation"))
        e.relation = r->asString();
    e.dangling = dangling;
    return e;
}

void addNodeFields(GraphNode& n, const Collection& c) {
    std::map<std::string, std::string> idx; // Pfad -> "index"/"unique"
    for (const Index& ix : c.indexes())
        idx[ix.field()] = ix.isUnique() ? "unique" : "index";
    if (c.hasSchema()) {
        n.hasSchema = true;
        auto sch = c.schema();
        for (const auto& [path, fs] : sch->fields()) {
            GraphField f;
            f.name = path;
            f.type = valueTypeName(fs.type());
            f.nullable = fs.nullable();
            auto it = idx.find(path);
            if (it != idx.end()) {
                f.index = it->second;
                idx.erase(it);
            }
            n.fields.push_back(std::move(f));
        }
    }
    // Indizes auf Pfaden, die nicht im Schema stehen (schema-los oder verschachtelt)
    for (auto& [p, kind] : idx)
        n.fields.push_back(GraphField{p, "", true, kind});
}

} // namespace

DbGraph DbGraph::fromDatabase(Database& db, const ReferenceIndex* refs) {
    DbGraph g;
    ReferenceIndex autoRefs(db);
    if (!refs)
        refs = &autoRefs;
    const std::string storage = refs->storageCollection();

    for (const auto& name : db.collectionNames()) {
        if (name == storage)
            continue; // interne Speicher-Collection: nur Kanten, kein Knoten
        Collection c = db.collection(name);
        GraphNode n;
        n.collection = name;
        n.documents = c.size();
        addNodeFields(n, c);
        g.nodes.push_back(std::move(n));
    }

    if (db.hasCollection(storage)) {
        Collection rc = db.collection(storage);
        for (const Document& d : rc.find()) {
            std::optional<DocumentId> fromId = docIdFromValue(d.find("from_id"));
            std::optional<DocumentId> toId = docIdFromValue(d.find("to_id"));
            const std::string fromColl = d.at("from_coll").asString();
            const std::string toColl = d.at("to_coll").asString();
            bool dangling = !db.hasCollection(fromColl) || !db.hasCollection(toColl);
            if (!dangling && fromId && !db.collection(fromColl).findById(*fromId).has_value())
                dangling = true;
            if (!dangling && toId && !db.collection(toColl).findById(*toId).has_value())
                dangling = true;
            g.edges.push_back(edgeFromRefDoc(d, dangling));
        }
    }
    return g;
}

DbGraph DbGraph::fromSnapshotFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw Error("Snapshot nicht lesbar: " + path);
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Value root = detail::parseJson(data);
    if (!root.isDocument())
        throw JsonError("Snapshot: Wurzel muss ein Objekt sein");
    const Value* colls = root.asDocument()->find("collections");
    if (!colls || !colls->isDocument())
        throw JsonError("Snapshot: Feld 'collections' fehlt oder ist kein Objekt");

    DbGraph g;
    const std::string storage = "__references";
    std::set<std::string> collNames;
    for (const auto& [name, cv] : colls->asDocument()->fields()) {
        collNames.insert(name);
        if (name == storage)
            continue;
        GraphNode n;
        n.collection = name;
        std::vector<Value> docs;
        std::map<std::string, std::string> idx;
        if (cv.isArray()) {
            docs = cv.asArray();
        } else if (cv.isDocument()) {
            const Document& cd = *cv.asDocument();
            if (const Value* ix = cd.find("indexes"))
                for (const Value& e : ix->asArray())
                    if (e.isDocument()) {
                        const Document& ed = *e.asDocument();
                        const Value* f = ed.find("field");
                        const Value* u = ed.find("unique");
                        if (f && f->isString())
                            idx[f->asString()] = (u && u->isBool() && u->asBool()) ? "unique" : "index";
                    }
            if (const Value* s = cd.find("schema")) {
                n.hasSchema = true;
                Schema sch = detail::parseJsonSchema(*s);
                for (const auto& [p, fs] : sch.fields()) {
                    GraphField f;
                    f.name = p;
                    f.type = valueTypeName(fs.type());
                    f.nullable = fs.nullable();
                    auto it = idx.find(p);
                    if (it != idx.end()) {
                        f.index = it->second;
                        idx.erase(it);
                    }
                    n.fields.push_back(std::move(f));
                }
            }
            const Value* d = cd.find("documents");
            if (!d || !d->isArray())
                throw JsonError("Snapshot '" + name + "': 'documents' fehlt oder ist kein Array");
            docs = d->asArray();
        } else {
            throw JsonError("Snapshot '" + name + "': Collection muss Array oder Objekt sein");
        }
        n.documents = docs.size();
        for (auto& [p, kind] : idx)
            n.fields.push_back(GraphField{p, "", true, kind});
        g.nodes.push_back(std::move(n));
    }

    if (collNames.count(storage)) {
        const Value* rc = colls->asDocument()->find(storage);
        std::vector<Value> docs;
        if (rc->isArray())
            docs = rc->asArray();
        else if (rc->isDocument())
            docs = rc->asDocument()->find("documents")->asArray();
        for (const Value& v : docs) {
            if (!v.isDocument())
                continue;
            Document d;
            v.asDocument()->forEachField([&](const std::string& k, const Value& val) { d.set(k, val); });
            const std::string toColl = d.at("to_coll").asString();
            bool dangling = collNames.count(toColl) == 0; // Dok-id im Snapshot nicht Prüfbar
            g.edges.push_back(edgeFromRefDoc(d, dangling));
        }
    }
    return g;
}

std::string DbGraph::toJson() const {
    std::string out = "{\"collections\":[";
    bool first = true;
    for (const auto& n : nodes) {
        if (!first)
            out += ",";
        first = false;
        out += "{\"name\":" + jsonStr(n.collection) + ",\"documents\":" + std::to_string(n.documents) +
               ",\"schema\":";
        if (!n.hasSchema) {
            out += "null";
        } else {
            out += "{\"fields\":[";
            bool ff = true;
            for (const auto& f : n.fields) {
                if (!ff)
                    out += ",";
                ff = false;
                out += "{\"name\":" + jsonStr(f.name) + ",\"type\":" + jsonStr(f.type) +
                       ",\"nullable\":" + (f.nullable ? "true" : "false");
                if (!f.index.empty())
                    out += ",\"index\":" + jsonStr(f.index);
                out += "}";
            }
            out += "]}";
        }
        out += "}";
    }
    out += "],\"references\":[";
    first = true;
    for (const auto& e : edges) {
        if (!first)
            out += ",";
        first = false;
        out += "{\"from\":" + jsonStr(e.from) + ",\"to\":" + jsonStr(e.to) +
               ",\"relation\":" + jsonStr(e.relation) +
               ",\"dangling\":" + (e.dangling ? "true" : "false") + "}";
    }
    out += "]}";
    return out;
}

std::string DbGraph::toMermaid() const {
    std::string out = "graph LR\n";
    std::set<std::string> declared;
    auto declareNode = [&](const std::string& id, const std::string& label) {
        if (declared.insert(id).second)
            out += "  " + id + "[\"" + mermaidLabel(label) + "\"]\n";
    };
    for (const auto& n : nodes) {
        std::string label = n.collection + " · " + std::to_string(n.documents) + " Dokumente";
        if (n.hasSchema) {
            for (const auto& f : n.fields) {
                label += "<br/>" + f.name + ": " + (f.type.empty() ? "any" : f.type) +
                         (f.nullable ? "?" : "!");
                if (f.index == "unique")
                    label += " [unique]";
                else if (f.index == "index")
                    label += " [index]";
            }
        } else if (n.fields.empty()) {
            label += "<br/>(schema-los)";
        } else {
            for (const auto& f : n.fields)
                label += "<br/>" + f.name + " [" + f.index + "]";
        }
        declareNode(mermaidId(n.collection), label);
    }
    for (const auto& e : edges) {
        std::string fromId = mermaidId(e.from);
        std::string toId = mermaidId(e.to);
        declareNode(fromId, e.from);
        declareNode(toId, e.to);
        std::string rel = e.relation.empty() ? "" : e.relation + (e.dangling ? " (dangling)" : "");
        if (rel.empty())
            out += "  " + fromId + " --> " + toId + "\n";
        else
            out += "  " + fromId + " -->|" + mermaidLabel(rel) + "| " + toId + "\n";
    }
    return out;
}

std::string DbGraph::toMarkdown() const {
    std::string out = "# Datenbank-Graph\n\n" + summary() + "\n\n## Collections\n\n";
    out += "| Collection | Dokumente | Schema | Felder |\n|---|---|---|---|\n";
    for (const auto& n : nodes) {
        std::string fields;
        for (const auto& f : n.fields) {
            if (!fields.empty())
                fields += ", ";
            fields += "`" + f.name + ": " + (f.type.empty() ? "any" : f.type) + (f.nullable ? "?" : "!") +
                      (f.index.empty() ? "" : " [" + f.index + "]") + "`";
        }
        if (fields.empty())
            fields = n.hasSchema ? "—" : "schema-los";
        out += "| " + n.collection + " | " + std::to_string(n.documents) + " | " +
               (n.hasSchema ? "ja" : "nein") + " | " + fields + " |\n";
    }
    out += "\n## Referenzen\n\n| Von | Nach | Relation | Dangling |\n|---|---|---|---|\n";
    for (const auto& e : edges)
        out += "| " + e.from + " | " + e.to + " | " + e.relation + " | " +
               (e.dangling ? "**ja**" : "nein") + " |\n";
    return out;
}

std::string DbGraph::summary() const {
    std::size_t docs = 0;
    for (const auto& n : nodes)
        docs += n.documents;
    std::size_t dang = 0;
    for (const auto& e : edges)
        if (e.dangling)
            ++dang;
    return std::to_string(nodes.size()) + " Collections, " + std::to_string(docs) + " Dokumente, " +
           std::to_string(edges.size()) + " Referenzen" + (dang ? " (" + std::to_string(dang) + " dangling)" : "");
}

} // namespace memdb
