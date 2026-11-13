#pragma once

#include "memdb/aggregate.hpp"
#include "memdb/change_event.hpp"
#include "memdb/cursor.hpp"
#include "memdb/document.hpp"
#include "memdb/exceptions.hpp"
#include "memdb/index.hpp"
#include "memdb/query.hpp"
#include "memdb/schema.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <vector>

namespace memdb::detail {

struct ValueLess {
    static int rank(const Value& v) {
        switch (v.type()) {
            case Value::Type::Null: return 0;
            case Value::Type::Bool: return 1;
            case Value::Type::Int:
            case Value::Type::Double: return 2;
            case Value::Type::String: return 3;
            case Value::Type::Binary: return 4;
            case Value::Type::Document: return 5;
            case Value::Type::Array: return 6;
        }
        return 7;
    }
    bool operator()(const Value& a, const Value& b) const {
        int ra = rank(a);
        int rb = rank(b);
        if (ra != rb)
            return ra < rb;
        if (ra == 2 || ra == 3) {
            auto c = compare(a, b);
            return c && *c < 0;
        }
        if (ra == 1)
            return a.asBool() < b.asBool();
        if (ra == 0)
            return false;
        return toString(a) < toString(b);
    }
};

struct FieldIndex {
    std::string path;
    bool unique = false;
    std::map<Value, std::set<DocumentId>, ValueLess> entries;
};

struct Accumulator {
    AggOp op;
    std::size_t count = 0;
    bool have = false;
    Value best;
    std::int64_t sumInt = 0;
    double sumD = 0.0;
    bool allInt = true;
    std::size_t numCount = 0;

    explicit Accumulator(AggOp o) : op(o) {}

    void addDoc() { ++count; }

    void addValue(const Value* v) {
        if (op == AggOp::Count)
            return;
        if (!v || v->isNull())
            return;
        if (op == AggOp::Sum || op == AggOp::Avg) {
            if (!v->isNumber())
                return;
            ++numCount;
            sumD += v->asDouble();
            if (v->isInt()) {
                auto x = v->asInt();
                if (x > 0 && sumInt > std::numeric_limits<std::int64_t>::max() - x)
                    allInt = false;
                else if (x < 0 && sumInt < std::numeric_limits<std::int64_t>::min() - x)
                    allInt = false;
                else
                    sumInt += x;
            } else {
                allInt = false;
            }
        } else {
            if (!v->isNumber() && !v->isString())
                return;
            if (!have) {
                best = *v;
                have = true;
            } else {
                auto c = compare(*v, best);
                if (c && ((op == AggOp::Min && *c < 0) || (op == AggOp::Max && *c > 0)))
                    best = *v;
            }
        }
    }

    std::optional<Value> result() const {
        if (op == AggOp::Count)
            return Value(static_cast<std::int64_t>(count));
        if (op == AggOp::Sum || op == AggOp::Avg) {
            if (numCount == 0)
                return std::nullopt;
            if (op == AggOp::Sum)
                return allInt ? std::optional<Value>(Value(sumInt)) : std::optional<Value>(Value(sumD));
            return Value(sumD / static_cast<double>(numCount));
        }
        return have ? std::optional<Value>(best) : std::nullopt;
    }
};

struct CollectionState {
    std::string name;
    std::map<DocumentId, Document> docs;
    std::uint64_t nextId = 1;
    std::map<std::string, std::unique_ptr<FieldIndex>> indexes;
    mutable std::shared_mutex mtx;
    mutable std::size_t lastCandidateCount = 0;
    std::vector<ChangeEvent>* sink = nullptr;
    std::vector<std::pair<std::uint64_t, ChangeCallback>> observers;
    std::uint64_t nextObserverId = 1;
    std::shared_ptr<const Schema> schema_;

    void validateDoc(const Document& doc) const {
        if (schema_)
            validateDocumentAgainstSchema(doc, *schema_);
    }

    void emit(ChangeType type, DocumentId id, const Document& doc) {
        if (sink)
            sink->push_back(ChangeEvent{type, name, id, doc});
    }

    void indexRemove(DocumentId id, const Document& doc) {
        for (auto& [path, idx] : indexes) {
            if (const Value* v = doc.findPath(path)) {
                auto it = idx->entries.find(*v);
                if (it != idx->entries.end()) {
                    it->second.erase(id);
                    if (it->second.empty())
                        idx->entries.erase(it);
                }
            }
        }
    }

    void indexAdd(DocumentId id, const Document& doc) {
        for (auto& [path, idx] : indexes)
            if (const Value* v = doc.findPath(path))
                idx->entries[*v].insert(id);
    }

    void validateUnique(DocumentId id, const Document& doc) const {
        for (const auto& [path, idx] : indexes) {
            if (!idx->unique)
                continue;
            if (const Value* v = doc.findPath(path)) {
                auto it = idx->entries.find(*v);
                if (it != idx->entries.end() && !it->second.empty() && *it->second.begin() != id)
                    throw UniqueConstraintError("Unique-Index verletzt fuer Feld '" + path + "'");
            }
        }
    }

    std::vector<DocumentId> candidates(const Expression& q) const {
        std::vector<DocumentId> ids;
        auto hint = q.indexHint();
        FieldIndex* idx = nullptr;
        if (hint) {
            auto it = indexes.find(hint->path);
            if (it != indexes.end())
                idx = it->second.get();
        }
        if (idx && hint->op == 0) {
            auto it = idx->entries.find(hint->value);
            if (it != idx->entries.end())
                ids.assign(it->second.begin(), it->second.end());
        } else if (idx && (hint->value.isNumber() || hint->value.isString())) {
            const auto& m = idx->entries;
            switch (hint->op) {
                case 2: {
                    for (auto it = m.begin(); it != m.lower_bound(hint->value); ++it)
                        ids.insert(ids.end(), it->second.begin(), it->second.end());
                    break;
                }
                case 3: {
                    for (auto it = m.begin(); it != m.upper_bound(hint->value); ++it)
                        ids.insert(ids.end(), it->second.begin(), it->second.end());
                    break;
                }
                case 4: {
                    for (auto it = m.upper_bound(hint->value); it != m.end(); ++it)
                        ids.insert(ids.end(), it->second.begin(), it->second.end());
                    break;
                }
                case 5: {
                    for (auto it = m.lower_bound(hint->value); it != m.end(); ++it)
                        ids.insert(ids.end(), it->second.begin(), it->second.end());
                    break;
                }
                default:
                    break;
            }
        } else {
            for (const auto& [id, doc] : docs)
                ids.push_back(id);
        }
        return ids;
    }

    DocumentId doInsert(Document doc) {
        DocumentId id(nextId++);
        doc.setId(id);
        validateDoc(doc);
        validateUnique(id, doc);
        auto [it, ok] = docs.emplace(id, doc);
        indexAdd(id, it->second);
        emit(ChangeType::Insert, id, it->second);
        return id;
    }

    std::vector<DocumentId> doInsertBulk(std::vector<Document> docs_) {
        std::vector<DocumentId> ids;
        ids.reserve(docs_.size());
        for (auto& doc : docs_)
            ids.push_back(doInsert(std::move(doc)));
        return ids;
    }

    std::optional<Document> doFindById(DocumentId id) const {
        auto it = docs.find(id);
        if (it == docs.end())
            return std::nullopt;
        return it->second;
    }

    Cursor doFind(const Expression& q) const {
        auto ids = candidates(q);
        lastCandidateCount = ids.size();
        std::vector<Document> result;
        for (auto id : ids) {
            auto it = docs.find(id);
            if (it != docs.end() && q.evaluate(it->second))
                result.push_back(it->second);
        }
        return Cursor(std::move(result));
    }

    std::optional<Document> doFindOne(const Expression& q) const {
        auto ids = candidates(q);
        lastCandidateCount = ids.size();
        for (auto id : ids) {
            auto it = docs.find(id);
            if (it != docs.end() && q.evaluate(it->second))
                return it->second;
        }
        return std::nullopt;
    }

    std::size_t doCount(const Expression& q) const {
        auto ids = candidates(q);
        lastCandidateCount = ids.size();
        std::size_t n = 0;
        for (auto id : ids) {
            auto it = docs.find(id);
            if (it != docs.end() && q.evaluate(it->second))
                ++n;
        }
        return n;
    }

    std::size_t doUpdate(const Expression& q, const std::function<void(Document&)>& mutator) {
        auto ids = candidates(q);
        std::vector<std::pair<DocumentId, Document>> changes;
        for (auto id : ids) {
            auto it = docs.find(id);
            if (it == docs.end() || !q.evaluate(it->second))
                continue;
            Document updated = it->second;
            mutator(updated);
            updated.setId(id);
            validateDoc(updated);
            validateUnique(id, updated);
            changes.emplace_back(id, std::move(updated));
        }
        for (auto& [id, updated] : changes) {
            Document& old = docs[id];
            indexRemove(id, old);
            old = updated;
            indexAdd(id, updated);
            emit(ChangeType::Update, id, old);
        }
        return changes.size();
    }

    bool doUpdateById(DocumentId id, const std::function<void(Document&)>& mutator) {
        auto it = docs.find(id);
        if (it == docs.end())
            return false;
        Document updated = it->second;
        mutator(updated);
        updated.setId(id);
        validateDoc(updated);
        validateUnique(id, updated);
        indexRemove(id, it->second);
        it->second = std::move(updated);
        indexAdd(id, it->second);
        emit(ChangeType::Update, id, it->second);
        return true;
    }

    bool doReplace(DocumentId id, Document doc) {
        auto it = docs.find(id);
        if (it == docs.end())
            return false;
        doc.setId(id);
        validateDoc(doc);
        validateUnique(id, doc);
        indexRemove(id, it->second);
        it->second = std::move(doc);
        indexAdd(id, it->second);
        emit(ChangeType::Replace, id, it->second);
        return true;
    }

    DocumentId doUpsertById(DocumentId id, Document doc) {
        auto it = docs.find(id);
        if (it != docs.end()) {
            doc.setId(id);
            validateDoc(doc);
            validateUnique(id, doc);
            indexRemove(id, it->second);
            it->second = std::move(doc);
            indexAdd(id, it->second);
            emit(ChangeType::Update, id, it->second);
            return id;
        }
        return doInsert(std::move(doc));
    }

    DocumentId doUpsert(const Expression& q, Document doc) {
        auto ids = candidates(q);
        for (auto id : ids) {
            auto it = docs.find(id);
            if (it != docs.end() && q.evaluate(it->second))
                return doUpsertById(id, std::move(doc));
        }
        return doInsert(std::move(doc));
    }

    std::size_t doRemove(const Expression& q) {
        auto ids = candidates(q);
        std::vector<DocumentId> toRemove;
        for (auto id : ids) {
            auto it = docs.find(id);
            if (it != docs.end() && q.evaluate(it->second))
                toRemove.push_back(id);
        }
        for (auto id : toRemove) {
            indexRemove(id, docs[id]);
            emit(ChangeType::Remove, id, docs[id]);
            docs.erase(id);
        }
        return toRemove.size();
    }

    bool doRemoveById(DocumentId id) {
        auto it = docs.find(id);
        if (it == docs.end())
            return false;
        indexRemove(id, it->second);
        emit(ChangeType::Remove, id, it->second);
        docs.erase(it);
        return true;
    }

    std::size_t doSize() const { return docs.size(); }

    std::optional<Value> doAggregate(AggOp op, const std::string& fieldPath, const Expression& q) const {
        Accumulator acc(op);
        for (auto id : candidates(q)) {
            auto it = docs.find(id);
            if (it == docs.end() || !q.evaluate(it->second))
                continue;
            acc.addDoc();
            acc.addValue(it->second.findPath(fieldPath));
        }
        return acc.result();
    }

    std::vector<GroupAggregate> doGroupBy(const std::string& keyPath, const Expression& q, AggOp op,
                                           const std::string& aggPath) const {
        std::map<Value, Accumulator, ValueLess> groups;
        for (auto id : candidates(q)) {
            auto it = docs.find(id);
            if (it == docs.end() || !q.evaluate(it->second))
                continue;
            const Value* k = it->second.findPath(keyPath);
            Value key = k ? *k : Value(nullptr);
            auto& acc = groups.try_emplace(key, op).first->second;
            acc.addDoc();
            acc.addValue(it->second.findPath(aggPath));
        }
        std::vector<GroupAggregate> out;
        out.reserve(groups.size());
        for (auto& [k, acc] : groups)
            out.push_back(GroupAggregate{k, acc.count, op == AggOp::Count ? std::nullopt : acc.result()});
        return out;
    }

    void doClear() {
        docs.clear();
        nextId = 1;
        for (auto& [path, idx] : indexes)
            idx->entries.clear();
        emit(ChangeType::Clear, DocumentId(0), Document{});
    }

    void doSetSchema(Schema schema) {
        for (const auto& [id, doc] : docs)
            validateDocumentAgainstSchema(doc, schema);
        schema_ = std::make_shared<const Schema>(std::move(schema));
    }

    std::optional<Schema> doSchema() const {
        if (!schema_)
            return std::nullopt;
        return *schema_;
    }

    bool doHasSchema() const { return schema_ != nullptr; }

    void doClearSchema() { schema_.reset(); }

    Index doCreateIndex(const std::string& fieldPath, bool unique) {
        if (indexes.find(fieldPath) != indexes.end())
            throw IndexExistsError("Index existiert bereits fuer Feld: " + fieldPath);
        auto idx = std::make_unique<FieldIndex>();
        idx->path = fieldPath;
        idx->unique = unique;
        for (const auto& [id, doc] : docs) {
            if (const Value* v = doc.findPath(fieldPath)) {
                if (unique) {
                    auto it = idx->entries.find(*v);
                    if (it != idx->entries.end() && !it->second.empty())
                        throw UniqueConstraintError("Unique-Index nicht moeglich, Duplikat in Feld '" + fieldPath + "'");
                }
                idx->entries[*v].insert(id);
            }
        }
        Index result(fieldPath, unique);
        indexes.emplace(fieldPath, std::move(idx));
        return result;
    }

    bool doDropIndex(const std::string& fieldPath) { return indexes.erase(fieldPath) > 0; }

    bool doHasIndex(const std::string& fieldPath) const {
        return indexes.find(fieldPath) != indexes.end();
    }

    std::vector<Index> doIndexes() const {
        std::vector<Index> result;
        result.reserve(indexes.size());
        for (const auto& [path, idx] : indexes)
            result.emplace_back(idx->path, idx->unique);
        return result;
    }
};

struct EventSinkGuard {
    explicit EventSinkGuard(CollectionState* s, std::vector<ChangeEvent>* e) : state(s) { state->sink = e; }
    ~EventSinkGuard() { state->sink = nullptr; }
    EventSinkGuard(const EventSinkGuard&) = delete;
    EventSinkGuard& operator=(const EventSinkGuard&) = delete;
    CollectionState* state;
};

} // namespace memdb::detail
