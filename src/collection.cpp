#include "memdb/collection.hpp"
#include "memdb/exceptions.hpp"

#include "collection_state.hpp"

#include <mutex>
#include <shared_mutex>

namespace memdb {

Collection::Collection(std::shared_ptr<detail::CollectionState> impl) : impl_(std::move(impl)) {}

Collection Collection::create(std::string name) {
    auto state = std::make_shared<detail::CollectionState>();
    state->name = std::move(name);
    return Collection(std::move(state));
}

std::string Collection::name() const { return impl_->name; }

DocumentId Collection::insert(Document doc) {
    std::unique_lock lock(impl_->mtx);
    return impl_->doInsert(std::move(doc));
}

DocumentId Collection::insert(std::initializer_list<std::pair<const std::string, Value>> fields) {
    return insert(Document(fields));
}

std::vector<DocumentId> Collection::insertBulk(std::vector<Document> docs) {
    std::unique_lock lock(impl_->mtx);
    return impl_->doInsertBulk(std::move(docs));
}

std::optional<Document> Collection::findById(DocumentId id) const {
    std::shared_lock lock(impl_->mtx);
    return impl_->doFindById(id);
}

Cursor Collection::find(const Expression& q) const {
    std::shared_lock lock(impl_->mtx);
    return impl_->doFind(q);
}

std::optional<Document> Collection::findOne(const Expression& q) const {
    std::shared_lock lock(impl_->mtx);
    return impl_->doFindOne(q);
}

std::size_t Collection::count(const Expression& q) const {
    std::shared_lock lock(impl_->mtx);
    return impl_->doCount(q);
}

std::size_t Collection::update(const Expression& q, const std::function<void(Document&)>& mutator) {
    std::unique_lock lock(impl_->mtx);
    return impl_->doUpdate(q, mutator);
}

bool Collection::updateById(DocumentId id, const std::function<void(Document&)>& mutator) {
    std::unique_lock lock(impl_->mtx);
    return impl_->doUpdateById(id, mutator);
}

bool Collection::replace(DocumentId id, Document doc) {
    std::unique_lock lock(impl_->mtx);
    return impl_->doReplace(id, std::move(doc));
}

std::size_t Collection::remove(const Expression& q) {
    std::unique_lock lock(impl_->mtx);
    return impl_->doRemove(q);
}

bool Collection::removeById(DocumentId id) {
    std::unique_lock lock(impl_->mtx);
    return impl_->doRemoveById(id);
}

std::size_t Collection::size() const {
    std::shared_lock lock(impl_->mtx);
    return impl_->doSize();
}

Index Collection::createIndex(const std::string& fieldPath, bool unique) {
    std::unique_lock lock(impl_->mtx);
    return impl_->doCreateIndex(fieldPath, unique);
}

bool Collection::dropIndex(const std::string& fieldPath) {
    std::unique_lock lock(impl_->mtx);
    return impl_->doDropIndex(fieldPath);
}

bool Collection::hasIndex(const std::string& fieldPath) const {
    std::shared_lock lock(impl_->mtx);
    return impl_->doHasIndex(fieldPath);
}

std::vector<Index> Collection::indexes() const {
    std::shared_lock lock(impl_->mtx);
    return impl_->doIndexes();
}

std::size_t Collection::lastQueryCandidateCount() const {
    std::shared_lock lock(impl_->mtx);
    return impl_->lastCandidateCount;
}

Transaction Collection::beginTransaction() { return Transaction(impl_); }

} // namespace memdb
