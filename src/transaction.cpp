#include "memdb/transaction.hpp"
#include "memdb/exceptions.hpp"

#include "collection_state.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace memdb {

struct Transaction::Impl {
    std::shared_ptr<detail::CollectionState> state;
    std::unique_lock<std::shared_mutex> lock;
    std::map<DocumentId, Document> backupDocs;
    std::uint64_t backupNextId = 0;
    std::map<std::string, std::unique_ptr<detail::FieldIndex>> backupIndexes;
    bool active = true;

    explicit Impl(std::shared_ptr<detail::CollectionState> s) : state(std::move(s)), lock(state->mtx) {
        backupDocs = state->docs;
        backupNextId = state->nextId;
        for (const auto& [path, idx] : state->indexes)
            backupIndexes.emplace(path, std::make_unique<detail::FieldIndex>(*idx));
    }

    void checkActive() const {
        if (!active)
            throw Error("Transaktion ist nicht mehr aktiv");
    }

    void restore() {
        state->docs = backupDocs;
        state->nextId = backupNextId;
        state->indexes.clear();
        for (const auto& [path, idx] : backupIndexes)
            state->indexes.emplace(path, std::make_unique<detail::FieldIndex>(*idx));
    }
};

Transaction::Transaction(std::shared_ptr<detail::CollectionState> state)
    : impl_(std::make_unique<Impl>(std::move(state))) {}
Transaction::Transaction(Transaction&&) noexcept = default;
Transaction& Transaction::operator=(Transaction&&) noexcept = default;
Transaction::~Transaction() {
    if (impl_ && impl_->active)
        impl_->restore();
}

DocumentId Transaction::insert(Document doc) {
    impl_->checkActive();
    return impl_->state->doInsert(std::move(doc));
}

DocumentId Transaction::insert(std::initializer_list<std::pair<const std::string, Value>> fields) {
    return insert(Document(fields));
}

std::size_t Transaction::update(const Expression& q, const std::function<void(Document&)>& mutator) {
    impl_->checkActive();
    return impl_->state->doUpdate(q, mutator);
}

bool Transaction::updateById(DocumentId id, const std::function<void(Document&)>& mutator) {
    impl_->checkActive();
    return impl_->state->doUpdateById(id, mutator);
}

bool Transaction::replace(DocumentId id, Document doc) {
    impl_->checkActive();
    return impl_->state->doReplace(id, std::move(doc));
}

DocumentId Transaction::upsertById(DocumentId id, Document doc) {
    impl_->checkActive();
    return impl_->state->doUpsertById(id, std::move(doc));
}

DocumentId Transaction::upsert(const Expression& q, Document doc) {
    impl_->checkActive();
    return impl_->state->doUpsert(q, std::move(doc));
}

std::size_t Transaction::remove(const Expression& q) {
    impl_->checkActive();
    return impl_->state->doRemove(q);
}

bool Transaction::removeById(DocumentId id) {
    impl_->checkActive();
    return impl_->state->doRemoveById(id);
}

Cursor Transaction::find(const Expression& q) const {
    impl_->checkActive();
    return impl_->state->doFind(q);
}

std::optional<Document> Transaction::findById(DocumentId id) const {
    impl_->checkActive();
    return impl_->state->doFindById(id);
}

std::size_t Transaction::count(const Expression& q) const {
    impl_->checkActive();
    return impl_->state->doCount(q);
}

std::size_t Transaction::size() const {
    impl_->checkActive();
    return impl_->state->doSize();
}

void Transaction::commit() {
    impl_->checkActive();
    impl_->active = false;
    impl_->lock.unlock();
}

void Transaction::rollback() {
    if (impl_->active) {
        impl_->restore();
        impl_->active = false;
        impl_->lock.unlock();
    }
}

bool Transaction::isActive() const { return impl_->active; }

} // namespace memdb
