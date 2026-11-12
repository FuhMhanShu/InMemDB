#include "memdb/references.hpp"

#include "memdb/exceptions.hpp"
#include "memdb/query.hpp"

#include <cstdint>
#include <functional>
#include <set>

namespace memdb {

namespace {

std::string makeKey(const std::string& collection, const std::optional<DocumentId>& id) {
    std::string key = collection + "#";
    if (id)
        key += toString(*id);
    return key;
}

std::optional<DocumentId> idFromValue(const Value& v) {
    if (v.isNull())
        return std::nullopt;
    return DocumentId(static_cast<std::uint64_t>(v.asInt()));
}

Value idToValue(const std::optional<DocumentId>& id) {
    if (!id)
        return Value(nullptr);
    return Value(static_cast<std::int64_t>(id->value()));
}

Reference toReference(const Document& d) {
    Reference r;
    r.fromCollection = d.at("from_coll").asString();
    r.fromId = idFromValue(d.at("from_id"));
    r.toCollection = d.at("to_coll").asString();
    r.toId = idFromValue(d.at("to_id"));
    r.relation = d.at("relation").asString();
    return r;
}

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> segments;
    std::string current;
    for (char c : path) {
        if (c == '/') {
            if (!current.empty())
                segments.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty())
        segments.push_back(current);
    return segments;
}

std::string joinPath(const std::vector<std::string>& segments) {
    std::string out;
    for (const auto& s : segments) {
        if (!out.empty())
            out += '/';
        out += s;
    }
    return out;
}

bool endpointExists(Database& db, const std::string& collection, const std::optional<DocumentId>& id) {
    if (!db.hasCollection(collection))
        return false;
    if (id && !db.collection(collection).findById(*id).has_value())
        return false;
    return true;
}

} // namespace

ReferenceIndex::ReferenceIndex(Database& db, std::string storageCollection)
    : db_(&db),
      storage_(std::move(storageCollection)),
      coll_(db.hasCollection(storage_) ? db.collection(storage_) : db.createCollection(storage_)) {
    if (!coll_.hasIndex("from_key"))
        coll_.createIndex("from_key");
    if (!coll_.hasIndex("to_key"))
        coll_.createIndex("to_key");
}

std::string ReferenceIndex::resolve(const std::string& fromCollection, const std::string& relPath) const {
    if (relPath.empty())
        throw ReferenceError("Relativer Pfad darf nicht leer sein");
    std::vector<std::string> base;
    if (relPath.front() == '/') {
        base = splitPath(relPath.substr(1));
    } else {
        const bool relative = relPath.find('/') != std::string::npos || relPath == "." || relPath == "..";
        if (!relative)
            return relPath;
        base = splitPath(fromCollection);
        if (!base.empty())
            base.pop_back();
        for (const auto& seg : splitPath(relPath)) {
            if (seg == ".")
                continue;
            if (seg == "..") {
                if (!base.empty())
                    base.pop_back();
                continue;
            }
            base.push_back(seg);
        }
    }
    std::string resolved = joinPath(base);
    if (resolved.empty())
        throw ReferenceError("Relativer Pfad loest sich zu leer auf: " + relPath);
    return resolved;
}

Reference ReferenceIndex::link(const std::string& fromCollection, std::optional<DocumentId> fromId,
                               const std::string& toCollection, std::optional<DocumentId> toId,
                               std::string relation) {
    const std::string to = resolve(fromCollection, toCollection);
    if (!db_->hasCollection(fromCollection))
        throw CollectionNotFoundError("Quell-Collection nicht gefunden: " + fromCollection);
    if (fromId && !db_->collection(fromCollection).findById(*fromId).has_value())
        throw DocumentNotFoundError("Quell-Dokument nicht gefunden: " + fromCollection + "#" +
                                    toString(*fromId));
    if (!db_->hasCollection(to))
        throw CollectionNotFoundError("Ziel-Collection nicht gefunden: " + to);
    if (toId && !db_->collection(to).findById(*toId).has_value())
        throw DocumentNotFoundError("Ziel-Dokument nicht gefunden: " + to + "#" + toString(*toId));

    const std::string fromKey = makeKey(fromCollection, fromId);
    const std::string toKey = makeKey(to, toId);
    const Expression dup = (Field("from_key") == fromKey) && (Field("to_key") == toKey) &&
                           (Field("relation") == relation);
    if (coll_.count(dup) > 0)
        throw ReferenceExistsError("Referenz existiert bereits: " + fromKey + " -> " + toKey +
                                   " (" + relation + ")");

    coll_.insert({{"from_key", fromKey},
                  {"from_coll", fromCollection},
                  {"from_id", idToValue(fromId)},
                  {"to_key", toKey},
                  {"to_coll", to},
                  {"to_id", idToValue(toId)},
                  {"relation", relation}});
    return Reference{fromCollection, fromId, to, toId, std::move(relation)};
}

bool ReferenceIndex::unlink(const std::string& fromCollection, std::optional<DocumentId> fromId,
                            const std::string& toCollection, std::optional<DocumentId> toId,
                            const std::string& relation) {
    const std::string to = resolve(fromCollection, toCollection);
    const Expression q = (Field("from_key") == makeKey(fromCollection, fromId)) &&
                         (Field("to_key") == makeKey(to, toId)) && (Field("relation") == relation);
    return coll_.remove(q) > 0;
}

std::vector<Reference> ReferenceIndex::references(const std::string& collection, RefDirection dir,
                                                  std::optional<DocumentId> id) const {
    const std::string key = makeKey(collection, id);
    Expression q;
    switch (dir) {
        case RefDirection::Outgoing:
            q = Field("from_key") == key;
            break;
        case RefDirection::Incoming:
            q = Field("to_key") == key;
            break;
        case RefDirection::Both:
            q = (Field("from_key") == key) || (Field("to_key") == key);
            break;
    }
    std::vector<Reference> out;
    for (const Document& d : coll_.find(q))
        out.push_back(toReference(d));
    return out;
}

std::vector<Reference> ReferenceIndex::dangling() const {
    std::vector<Reference> out;
    for (const Document& d : coll_.find(Expression{})) {
        Reference r = toReference(d);
        if (!endpointExists(*db_, r.fromCollection, r.fromId) || !endpointExists(*db_, r.toCollection, r.toId))
            out.push_back(std::move(r));
    }
    return out;
}

std::size_t ReferenceIndex::pruneDangling() {
    std::vector<DocumentId> toRemove;
    for (const Document& d : coll_.find(Expression{})) {
        Reference r = toReference(d);
        if (!endpointExists(*db_, r.fromCollection, r.fromId) || !endpointExists(*db_, r.toCollection, r.toId))
            toRemove.push_back(d.id());
    }
    std::size_t n = 0;
    for (DocumentId id : toRemove)
        if (coll_.removeById(id))
            ++n;
    return n;
}

std::vector<Reference> ReferenceIndex::traverse(const std::string& fromCollection,
                                                 std::optional<DocumentId> fromId, RefDirection dir,
                                                 std::size_t maxDepth) const {
    std::vector<Reference> out;
    std::set<std::string> visited;
    std::set<std::string> seenEdges;
    std::function<void(const std::string&, const std::optional<DocumentId>&, std::size_t)> dfs =
        [&](const std::string& coll, const std::optional<DocumentId>& id, std::size_t depth) {
            if (depth >= maxDepth)
                return;
            const std::string key = makeKey(coll, id);
            if (!visited.insert(key).second)
                return;
            for (const Reference& r : references(coll, dir, id)) {
                const std::string edgeKey = makeKey(r.fromCollection, r.fromId) + "|" +
                                            makeKey(r.toCollection, r.toId) + "|" + r.relation;
                if (seenEdges.insert(edgeKey).second)
                    out.push_back(r);
                if (dir == RefDirection::Outgoing) {
                    dfs(r.toCollection, r.toId, depth + 1);
                } else if (dir == RefDirection::Incoming) {
                    dfs(r.fromCollection, r.fromId, depth + 1);
                } else {
                    const bool fromIsSelf = makeKey(r.fromCollection, r.fromId) == key;
                    if (fromIsSelf)
                        dfs(r.toCollection, r.toId, depth + 1);
                    else
                        dfs(r.fromCollection, r.fromId, depth + 1);
                }
            }
        };
    dfs(fromCollection, fromId, 0);
    return out;
}

bool ReferenceIndex::hasCycle(const std::string& fromCollection, std::optional<DocumentId> fromId,
                              RefDirection dir) const {
    std::set<std::string> gray;
    std::set<std::string> black;
    std::function<bool(const std::string&, const std::optional<DocumentId>&, const std::string&)> dfs =
        [&](const std::string& coll, const std::optional<DocumentId>& id, const std::string& parentKey) -> bool {
            const std::string key = makeKey(coll, id);
            if (black.count(key))
                return false;
            if (gray.count(key))
                return true;
            gray.insert(key);
            for (const Reference& r : references(coll, dir, id)) {
                if (dir == RefDirection::Outgoing) {
                    if (dfs(r.toCollection, r.toId, key))
                        return true;
                } else if (dir == RefDirection::Incoming) {
                    if (dfs(r.fromCollection, r.fromId, key))
                        return true;
                } else {
                    const bool fromIsSelf = makeKey(r.fromCollection, r.fromId) == key;
                    const std::string& nc = fromIsSelf ? r.toCollection : r.fromCollection;
                    const std::optional<DocumentId>& nid = fromIsSelf ? r.toId : r.fromId;
                    if (makeKey(nc, nid) == parentKey)
                        continue;
                    if (dfs(nc, nid, key))
                        return true;
                }
            }
            gray.erase(key);
            black.insert(key);
            return false;
        };
    return dfs(fromCollection, fromId, "");
}

} // namespace memdb
