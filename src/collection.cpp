#include "memdb/collection.hpp"
#include "memdb/exceptions.hpp"

#include <map>
#include <mutex>
#include <shared_mutex>

namespace memdb {

struct Collection::Impl {
    std::string name;
    std::map<DocumentId, Document> docs;
    std::uint64_t nextId = 1;
    mutable std::shared_mutex mtx;
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
    impl_->docs.emplace(id, std::move(doc));
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
        impl_->docs.emplace(id, std::move(doc));
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
    std::vector<Document> result;
    for (const auto& [id, doc] : impl_->docs)
        if (q.evaluate(doc))
            result.push_back(doc);
    return Cursor(std::move(result));
}

std::optional<Document> Collection::findOne(const Expression& q) const {
    std::shared_lock lock(impl_->mtx);
    for (const auto& [id, doc] : impl_->docs)
        if (q.evaluate(doc))
            return doc;
    return std::nullopt;
}

std::size_t Collection::count(const Expression& q) const {
    std::shared_lock lock(impl_->mtx);
    std::size_t n = 0;
    for (const auto& [id, doc] : impl_->docs)
        if (q.evaluate(doc))
            ++n;
    return n;
}

std::size_t Collection::update(const Expression& q, const std::function<void(Document&)>& mutator) {
    std::unique_lock lock(impl_->mtx);
    std::size_t n = 0;
    for (auto& [id, doc] : impl_->docs)
        if (q.evaluate(doc)) {
            mutator(doc);
            ++n;
        }
    return n;
}

bool Collection::updateById(DocumentId id, const std::function<void(Document&)>& mutator) {
    std::unique_lock lock(impl_->mtx);
    auto it = impl_->docs.find(id);
    if (it == impl_->docs.end())
        return false;
    mutator(it->second);
    return true;
}

bool Collection::replace(DocumentId id, Document doc) {
    std::unique_lock lock(impl_->mtx);
    auto it = impl_->docs.find(id);
    if (it == impl_->docs.end())
        return false;
    doc.setId(id);
    it->second = std::move(doc);
    return true;
}

std::size_t Collection::remove(const Expression& q) {
    std::unique_lock lock(impl_->mtx);
    std::size_t n = 0;
    for (auto it = impl_->docs.begin(); it != impl_->docs.end();) {
        if (q.evaluate(it->second)) {
            it = impl_->docs.erase(it);
            ++n;
        } else {
            ++it;
        }
    }
    return n;
}

bool Collection::removeById(DocumentId id) {
    std::unique_lock lock(impl_->mtx);
    return impl_->docs.erase(id) > 0;
}

std::size_t Collection::size() const {
    std::shared_lock lock(impl_->mtx);
    return impl_->docs.size();
}

} // namespace memdb
