#include "memdb/collection.hpp"
#include "memdb/exceptions.hpp"

#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>

namespace memdb {

namespace {

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

} // namespace

struct Collection::Impl {
    std::string name;
    std::map<DocumentId, Document> docs;
    std::uint64_t nextId = 1;
    std::map<std::string, std::unique_ptr<FieldIndex>> indexes;
    mutable std::shared_mutex mtx;
    std::size_t lastCandidateCount = 0;

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
                case 2: { // <
                    for (auto it = m.begin(); it != m.lower_bound(hint->value); ++it)
                        ids.insert(ids.end(), it->second.begin(), it->second.end());
                    break;
                }
                case 3: { // <=
                    for (auto it = m.begin(); it != m.upper_bound(hint->value); ++it)
                        ids.insert(ids.end(), it->second.begin(), it->second.end());
                    break;
                }
                case 4: { // >
                    for (auto it = m.upper_bound(hint->value); it != m.end(); ++it)
                        ids.insert(ids.end(), it->second.begin(), it->second.end());
                    break;
                }
                case 5: { // >=
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
};

Collection::Collection(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

Collection Collection::create(std::string name) {
    auto impl = std::make_shared<Impl>();
    impl->name = std::move(name);
    return Collection(std::move(impl));
}

std::string Collection::name() const { return impl_->name; }

DocumentId Collection::insert(Document doc) {
    std::unique_lock lock(impl_->mtx);
    DocumentId id(impl_->nextId++);
    doc.setId(id);
    impl_->validateUnique(id, doc);
    impl_->docs.emplace(id, doc);
    impl_->indexAdd(id, doc);
    return id;
}

DocumentId Collection::insert(std::initializer_list<std::pair<const std::string, Value>> fields) {
    return insert(Document(fields));
}

std::vector<DocumentId> Collection::insertBulk(std::vector<Document> docs) {
    std::vector<DocumentId> ids;
    ids.reserve(docs.size());
    std::unique_lock lock(impl_->mtx);
    for (auto& doc : docs) {
        DocumentId id(impl_->nextId++);
        doc.setId(id);
        impl_->validateUnique(id, doc);
        impl_->docs.emplace(id, doc);
        impl_->indexAdd(id, doc);
        ids.push_back(id);
    }
    return ids;
}

std::optional<Document> Collection::findById(DocumentId id) const {
    std::shared_lock lock(impl_->mtx);
    auto it = impl_->docs.find(id);
    if (it == impl_->docs.end())
        return std::nullopt;
    return it->second;
}

Cursor Collection::find(const Expression& q) const {
    std::shared_lock lock(impl_->mtx);
    auto ids = impl_->candidates(q);
    impl_->lastCandidateCount = ids.size();
    std::vector<Document> result;
    for (auto id : ids) {
        auto it = impl_->docs.find(id);
        if (it != impl_->docs.end() && q.evaluate(it->second))
            result.push_back(it->second);
    }
    return Cursor(std::move(result));
}

std::optional<Document> Collection::findOne(const Expression& q) const {
    std::shared_lock lock(impl_->mtx);
    auto ids = impl_->candidates(q);
    impl_->lastCandidateCount = ids.size();
    for (auto id : ids) {
        auto it = impl_->docs.find(id);
        if (it != impl_->docs.end() && q.evaluate(it->second))
            return it->second;
    }
    return std::nullopt;
}

std::size_t Collection::count(const Expression& q) const {
    std::shared_lock lock(impl_->mtx);
    auto ids = impl_->candidates(q);
    impl_->lastCandidateCount = ids.size();
    std::size_t n = 0;
    for (auto id : ids) {
        auto it = impl_->docs.find(id);
        if (it != impl_->docs.end() && q.evaluate(it->second))
            ++n;
    }
    return n;
}

std::size_t Collection::update(const Expression& q, const std::function<void(Document&)>& mutator) {
    std::unique_lock lock(impl_->mtx);
    auto ids = impl_->candidates(q);
    std::vector<std::pair<DocumentId, Document>> changes;
    for (auto id : ids) {
        auto it = impl_->docs.find(id);
        if (it == impl_->docs.end() || !q.evaluate(it->second))
            continue;
        Document updated = it->second;
        mutator(updated);
        updated.setId(id);
        impl_->validateUnique(id, updated);
        changes.emplace_back(id, std::move(updated));
    }
    for (auto& [id, updated] : changes) {
        Document& old = impl_->docs[id];
        impl_->indexRemove(id, old);
        old = updated;
        impl_->indexAdd(id, updated);
    }
    return changes.size();
}

bool Collection::updateById(DocumentId id, const std::function<void(Document&)>& mutator) {
    std::unique_lock lock(impl_->mtx);
    auto it = impl_->docs.find(id);
    if (it == impl_->docs.end())
        return false;
    Document updated = it->second;
    mutator(updated);
    updated.setId(id);
    impl_->validateUnique(id, updated);
    impl_->indexRemove(id, it->second);
    it->second = std::move(updated);
    impl_->indexAdd(id, it->second);
    return true;
}

bool Collection::replace(DocumentId id, Document doc) {
    std::unique_lock lock(impl_->mtx);
    auto it = impl_->docs.find(id);
    if (it == impl_->docs.end())
        return false;
    doc.setId(id);
    impl_->validateUnique(id, doc);
    impl_->indexRemove(id, it->second);
    it->second = std::move(doc);
    impl_->indexAdd(id, it->second);
    return true;
}

std::size_t Collection::remove(const Expression& q) {
    std::unique_lock lock(impl_->mtx);
    auto ids = impl_->candidates(q);
    std::vector<DocumentId> toRemove;
    for (auto id : ids) {
        auto it = impl_->docs.find(id);
        if (it != impl_->docs.end() && q.evaluate(it->second))
            toRemove.push_back(id);
    }
    for (auto id : toRemove) {
        impl_->indexRemove(id, impl_->docs[id]);
        impl_->docs.erase(id);
    }
    return toRemove.size();
}

bool Collection::removeById(DocumentId id) {
    std::unique_lock lock(impl_->mtx);
    auto it = impl_->docs.find(id);
    if (it == impl_->docs.end())
        return false;
    impl_->indexRemove(id, it->second);
    impl_->docs.erase(it);
    return true;
}

std::size_t Collection::size() const {
    std::shared_lock lock(impl_->mtx);
    return impl_->docs.size();
}

Index Collection::createIndex(const std::string& fieldPath, bool unique) {
    std::unique_lock lock(impl_->mtx);
    if (impl_->indexes.find(fieldPath) != impl_->indexes.end())
        throw IndexExistsError("Index existiert bereits fuer Feld: " + fieldPath);
    auto idx = std::make_unique<FieldIndex>();
    idx->path = fieldPath;
    idx->unique = unique;
    for (const auto& [id, doc] : impl_->docs) {
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
    impl_->indexes.emplace(fieldPath, std::move(idx));
    return result;
}

bool Collection::dropIndex(const std::string& fieldPath) {
    std::unique_lock lock(impl_->mtx);
    return impl_->indexes.erase(fieldPath) > 0;
}

bool Collection::hasIndex(const std::string& fieldPath) const {
    std::shared_lock lock(impl_->mtx);
    return impl_->indexes.find(fieldPath) != impl_->indexes.end();
}

std::vector<Index> Collection::indexes() const {
    std::shared_lock lock(impl_->mtx);
    std::vector<Index> result;
    result.reserve(impl_->indexes.size());
    for (const auto& [path, idx] : impl_->indexes)
        result.emplace_back(idx->path, idx->unique);
    return result;
}

std::size_t Collection::lastQueryCandidateCount() const {
    std::shared_lock lock(impl_->mtx);
    return impl_->lastCandidateCount;
}

} // namespace memdb
