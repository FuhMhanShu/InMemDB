#include "memdb/transaction.hpp"
#include "memdb/exceptions.hpp"
#include "memdb/query_dsl.hpp"

#include "collection_state.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace memdb {

struct Transaction::Impl {
    std::shared_ptr<detail::CollectionState> state;
    std::unique_lock<std::shared_mutex> lock;
    // Copy-on-Write-Undo (v0.19): statt Voll-Backup (O(n) bei Tx-Start) nur angefasste
    // Dokumente ueber CollectionState::noteUndo protokollieren -> O(1) Start/Commit,
    // O(angefasste Docs + Reindex) nur beim Rollback.
    detail::CollectionState::TxUndo undo;
    std::uint64_t backupNextId = 0;
    std::map<std::string, bool> backupIndexPaths; // Index-Struktur (path -> unique), meist wenige Eintraege
    std::map<DocumentId, std::uint64_t> backupSpillIndex; // Hybrid-Modus (v0.18)
    std::uint64_t backupSpillBytes = 0;
    std::vector<ChangeEvent> events;
    bool active = true;

    explicit Impl(std::shared_ptr<detail::CollectionState> s) : state(std::move(s)), lock(state->mtx) {
        state->undo = &undo;
        ++state->txLocks; // waehrend der Transaktion kein Evict ins Spill
        backupNextId = state->nextId;
        backupSpillIndex = state->spillIndex;
        backupSpillBytes = state->spillBytes;
        for (const auto& [path, idx] : state->indexes)
            backupIndexPaths.emplace(path, idx->unique);
    }

    ~Impl() { releaseState(); }

    // Undo-Hook abmelden + Evict wieder erlauben (idempotent)
    void releaseState() {
        if (state && state->undo == &undo) {
            state->undo = nullptr;
            --state->txLocks;
            state->maybeEvict(); // nach Tx-Ende evtl. ueberfuellig geworden
        }
    }

    void checkActive() const {
        if (!active)
            throw Error("Transaktion ist nicht mehr aktiv");
    }

    void restore() {
        if (undo.cleared)
            state->docs = undo.clearedDocs;
        for (auto& [id, old] : undo.docs) {
            if (old)
                state->docs[id] = *old;
            else
                state->docs.erase(id);
        }
        // Index-Struktur wiederherstellen und Eintraege komplett neu aufbauen
        state->indexes.clear();
        for (const auto& [path, uniq] : backupIndexPaths) {
            auto idx = std::make_unique<detail::FieldIndex>();
            idx->path = path;
            idx->unique = uniq;
            state->indexes.emplace(path, std::move(idx));
        }
        for (const auto& [id, doc] : state->docs)
            state->indexAdd(id, doc);
        state->nextId = backupNextId;
        state->spillIndex = backupSpillIndex;
        state->truncateSpill(backupSpillBytes); // Spill-Datei auf Pre-Tx-Stand kuerzen (setzt spillBytes)
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
    detail::EventSinkGuard guard(impl_->state.get(), &impl_->events);
    return impl_->state->doInsert(std::move(doc));
}

DocumentId Transaction::insert(std::initializer_list<std::pair<const std::string, Value>> fields) {
    return insert(Document(fields));
}

std::size_t Transaction::update(const Expression& q, const std::function<void(Document&)>& mutator) {
    impl_->checkActive();
    detail::EventSinkGuard guard(impl_->state.get(), &impl_->events);
    return impl_->state->doUpdate(q, mutator);
}

bool Transaction::updateById(DocumentId id, const std::function<void(Document&)>& mutator) {
    impl_->checkActive();
    detail::EventSinkGuard guard(impl_->state.get(), &impl_->events);
    return impl_->state->doUpdateById(id, mutator);
}

bool Transaction::replace(DocumentId id, Document doc) {
    impl_->checkActive();
    detail::EventSinkGuard guard(impl_->state.get(), &impl_->events);
    return impl_->state->doReplace(id, std::move(doc));
}

DocumentId Transaction::upsertById(DocumentId id, Document doc) {
    impl_->checkActive();
    detail::EventSinkGuard guard(impl_->state.get(), &impl_->events);
    return impl_->state->doUpsertById(id, std::move(doc));
}

DocumentId Transaction::upsert(const Expression& q, Document doc) {
    impl_->checkActive();
    detail::EventSinkGuard guard(impl_->state.get(), &impl_->events);
    return impl_->state->doUpsert(q, std::move(doc));
}

std::size_t Transaction::remove(const Expression& q) {
    impl_->checkActive();
    detail::EventSinkGuard guard(impl_->state.get(), &impl_->events);
    return impl_->state->doRemove(q);
}

bool Transaction::removeById(DocumentId id) {
    impl_->checkActive();
    detail::EventSinkGuard guard(impl_->state.get(), &impl_->events);
    return impl_->state->doRemoveById(id);
}

Cursor Transaction::find(const Expression& q) const {
    impl_->checkActive();
    return impl_->state->doFind(q);
}

std::vector<DocumentId> Transaction::findIds(const Expression& q) const {
    impl_->checkActive();
    return impl_->state->doFindIds(q);
}

std::size_t Transaction::forEach(const Expression& q, const std::function<void(const Document&)>& cb) const {
    impl_->checkActive();
    return impl_->state->doForEach(q, cb);
}

std::optional<Document> Transaction::findById(DocumentId id) const {
    impl_->checkActive();
    return impl_->state->doFindById(id);
}

std::size_t Transaction::count(const Expression& q) const {
    impl_->checkActive();
    return impl_->state->doCount(q);
}

std::size_t Transaction::update(std::string_view dsl,
                                const std::function<void(Document&)>& mutator) {
    return update(parseQuery(dsl), mutator);
}

std::size_t Transaction::remove(std::string_view dsl) { return remove(parseQuery(dsl)); }

Cursor Transaction::find(std::string_view dsl) const { return find(parseQuery(dsl)); }

std::vector<DocumentId> Transaction::findIds(std::string_view dsl) const {
    return findIds(parseQuery(dsl));
}

std::size_t Transaction::forEach(std::string_view dsl,
                                 const std::function<void(const Document&)>& cb) const {
    return forEach(parseQuery(dsl), cb);
}

std::size_t Transaction::count(std::string_view dsl) const { return count(parseQuery(dsl)); }

std::optional<Document> Transaction::findOne(std::string_view dsl) const {
    auto cur = find(parseQuery(dsl));
    if (cur.empty())
        return std::nullopt;
    return cur.front();
}

std::size_t Transaction::size() const {
    impl_->checkActive();
    return impl_->state->doSize();
}

std::optional<Value> Transaction::aggregate(AggOp op, const std::string& fieldPath, const Expression& q) const {
    impl_->checkActive();
    return impl_->state->doAggregate(op, fieldPath, q);
}

std::vector<GroupAggregate> Transaction::groupBy(const std::string& keyPath, const Expression& q, AggOp op,
                                                  const std::string& aggPath) const {
    impl_->checkActive();
    return impl_->state->doGroupBy(keyPath, q, op, aggPath);
}

void Transaction::commit() {
    impl_->checkActive();
    if (impl_->undo.cleared)
        impl_->state->truncateSpill(0); // Clear/LoadBulk in Tx: Spill-Datei endgueltig leeren
    impl_->active = false;
    std::vector<ChangeEvent> events = std::move(impl_->events);
    impl_->events.clear();
    impl_->releaseState(); // Undo-Hook abmelden + txLocks-- (noch unter exklusivem Lock)
    impl_->lock.unlock();
    if (!events.empty()) {
        std::vector<ChangeCallback> cbs;
        {
            std::shared_lock lock(impl_->state->mtx);
            cbs.reserve(impl_->state->observers.size());
            for (const auto& [id, cb] : impl_->state->observers)
                cbs.push_back(cb);
        }
        for (const auto& e : events)
            for (const auto& cb : cbs)
                cb(e);
    }
}

void Transaction::rollback() {
    if (impl_->active) {
        impl_->restore();
        impl_->events.clear();
        impl_->active = false;
        impl_->releaseState();
        impl_->lock.unlock();
    }
}

bool Transaction::isActive() const { return impl_->active; }

} // namespace memdb
