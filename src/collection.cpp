#include "memdb/collection.hpp"
#include "memdb/exceptions.hpp"
#include "memdb/query_dsl.hpp"

#include "collection_state.hpp"
#include "json.hpp"

#include <algorithm>
#include <mutex>
#include <shared_mutex>

namespace memdb {

namespace {

// Mutator aus geparster Set-DSL: alle Ausdruecke zuerst gegen das alte Doc auswerten
// (Snapshot-Semantik), dann per setPath schreiben.
std::function<void(Document&)> makeSetMutator(std::vector<std::pair<std::string, SetExpression>> assigns) {
    return [assigns = std::move(assigns)](Document& d) {
        std::vector<std::pair<std::string, Value>> vals;
        vals.reserve(assigns.size());
        for (const auto& [path, expr] : assigns)
            vals.emplace_back(path, expr(d));
        for (auto& [path, v] : vals)
            d.setPath(path, std::move(v));
    };
}

void fireEvents(const std::shared_ptr<detail::CollectionState>& state, const std::vector<ChangeEvent>& events) {
    if (events.empty())
        return;
    std::vector<ChangeCallback> cbs;
    {
        std::shared_lock lock(state->mtx);
        cbs.reserve(state->observers.size());
        for (const auto& [id, cb] : state->observers)
            cbs.push_back(cb);
    }
    for (const auto& e : events)
        for (const auto& cb : cbs)
            cb(e);
}

} // namespace

// FA-25: TTL-Sweep vor Lesezugriffen (lazy). Billig wenn kein expiresAt existiert.
void sweepExpired(const std::shared_ptr<detail::CollectionState>& state) {
    if (state->expiryCount.load(std::memory_order_relaxed) == 0)
        return;
    std::vector<ChangeEvent> events;
    {
        std::unique_lock lock(state->mtx);
        if (state->txLocks.load() > 0)
            return; // mitten in Tx/Merge nicht sweepen (Snapshot-Isolation)
        detail::EventSinkGuard guard(state.get(), &events);
        state->doExpireNow();
    }
    fireEvents(state, events);
}

Collection::Collection(std::shared_ptr<detail::CollectionState> impl) : impl_(std::move(impl)) {}

Collection Collection::create(std::string name) {
    auto state = std::make_shared<detail::CollectionState>();
    state->name = std::move(name);
    return Collection(std::move(state));
}

std::string Collection::name() const { return impl_->name; }

DocumentId Collection::insert(Document doc) {
    std::vector<ChangeEvent> events;
    DocumentId id;
    {
        std::unique_lock lock(impl_->mtx);
        detail::EventSinkGuard guard(impl_.get(), &events);
        id = impl_->doInsert(std::move(doc));
    }
    fireEvents(impl_, events);
    return id;
}

DocumentId Collection::insert(std::initializer_list<std::pair<const std::string, Value>> fields) {
    return insert(Document(fields));
}

std::vector<DocumentId> Collection::insertBulk(std::vector<Document> docs) {
    std::vector<ChangeEvent> events;
    std::vector<DocumentId> ids;
    {
        std::unique_lock lock(impl_->mtx);
        detail::EventSinkGuard guard(impl_.get(), &events);
        ids = impl_->doInsertBulk(std::move(docs));
    }
    fireEvents(impl_, events);
    return ids;
}

std::optional<Document> Collection::findById(DocumentId id) const {
    sweepExpired(impl_);
    std::shared_lock lock(impl_->mtx);
    return impl_->doFindById(id);
}

Cursor Collection::find(const Expression& q) const {
    sweepExpired(impl_);
    std::shared_lock lock(impl_->mtx);
    return impl_->doFind(q);
}

std::vector<DocumentId> Collection::findIds(const Expression& q) const {
    sweepExpired(impl_);
    std::shared_lock lock(impl_->mtx);
    return impl_->doFindIds(q);
}

std::size_t Collection::forEach(const Expression& q, const std::function<void(const Document&)>& cb) const {
    sweepExpired(impl_);
    std::shared_lock lock(impl_->mtx);
    return impl_->doForEach(q, cb);
}

std::optional<Document> Collection::findOne(const Expression& q) const {
    sweepExpired(impl_);
    std::shared_lock lock(impl_->mtx);
    return impl_->doFindOne(q);
}

std::size_t Collection::count(const Expression& q) const {
    sweepExpired(impl_);
    std::shared_lock lock(impl_->mtx);
    return impl_->doCount(q);
}

Cursor Collection::find(std::string_view dsl) const { return find(parseQuery(dsl)); }

std::vector<DocumentId> Collection::findIds(std::string_view dsl) const {
    return findIds(parseQuery(dsl));
}

std::size_t Collection::forEach(std::string_view dsl,
                                const std::function<void(const Document&)>& cb) const {
    return forEach(parseQuery(dsl), cb);
}

std::optional<Document> Collection::findOne(std::string_view dsl) const {
    return findOne(parseQuery(dsl));
}

std::size_t Collection::count(std::string_view dsl) const { return count(parseQuery(dsl)); }

std::size_t Collection::update(std::string_view dsl,
                               const std::function<void(Document&)>& mutator) {
    return update(parseQuery(dsl), mutator);
}

std::size_t Collection::update(std::string_view dsl, std::string_view setDsl) {
    return update(parseQuery(dsl), makeSetMutator(parseUpdateSet(setDsl)));
}

std::size_t Collection::remove(std::string_view dsl) { return remove(parseQuery(dsl)); }

Cursor Collection::find(std::string_view dsl, Database& db) const {
    return find(parseQuery(dsl, &db, name()));
}

std::vector<DocumentId> Collection::findIds(std::string_view dsl, Database& db) const {
    return findIds(parseQuery(dsl, &db, name()));
}

std::size_t Collection::forEach(std::string_view dsl, Database& db,
                                const std::function<void(const Document&)>& cb) const {
    return forEach(parseQuery(dsl, &db, name()), cb);
}

std::optional<Document> Collection::findOne(std::string_view dsl, Database& db) const {
    return findOne(parseQuery(dsl, &db, name()));
}

std::size_t Collection::count(std::string_view dsl, Database& db) const {
    return count(parseQuery(dsl, &db, name()));
}

std::size_t Collection::update(std::string_view dsl, Database& db,
                               const std::function<void(Document&)>& mutator) {
    return update(parseQuery(dsl, &db, name()), mutator);
}

std::size_t Collection::update(std::string_view dsl, std::string_view setDsl, Database& db) {
    return update(parseQuery(dsl, &db, name()), makeSetMutator(parseUpdateSet(setDsl)));
}

std::size_t Collection::remove(std::string_view dsl, Database& db) {
    return remove(parseQuery(dsl, &db, name()));
}

std::size_t Collection::update(const Expression& q, const std::function<void(Document&)>& mutator) {
    std::vector<ChangeEvent> events;
    std::size_t n;
    {
        std::unique_lock lock(impl_->mtx);
        detail::EventSinkGuard guard(impl_.get(), &events);
        n = impl_->doUpdate(q, mutator);
    }
    fireEvents(impl_, events);
    return n;
}

bool Collection::updateById(DocumentId id, const std::function<void(Document&)>& mutator) {
    std::vector<ChangeEvent> events;
    bool ok;
    {
        std::unique_lock lock(impl_->mtx);
        detail::EventSinkGuard guard(impl_.get(), &events);
        ok = impl_->doUpdateById(id, mutator);
    }
    fireEvents(impl_, events);
    return ok;
}

bool Collection::replace(DocumentId id, Document doc) {
    std::vector<ChangeEvent> events;
    bool ok;
    {
        std::unique_lock lock(impl_->mtx);
        detail::EventSinkGuard guard(impl_.get(), &events);
        ok = impl_->doReplace(id, std::move(doc));
    }
    fireEvents(impl_, events);
    return ok;
}

DocumentId Collection::upsertById(DocumentId id, Document doc) {
    std::vector<ChangeEvent> events;
    DocumentId result;
    {
        std::unique_lock lock(impl_->mtx);
        detail::EventSinkGuard guard(impl_.get(), &events);
        result = impl_->doUpsertById(id, std::move(doc));
    }
    fireEvents(impl_, events);
    return result;
}

DocumentId Collection::upsert(const Expression& q, Document doc) {
    std::vector<ChangeEvent> events;
    DocumentId result;
    {
        std::unique_lock lock(impl_->mtx);
        detail::EventSinkGuard guard(impl_.get(), &events);
        result = impl_->doUpsert(q, std::move(doc));
    }
    fireEvents(impl_, events);
    return result;
}

std::size_t Collection::remove(const Expression& q) {
    std::vector<ChangeEvent> events;
    std::size_t n;
    {
        std::unique_lock lock(impl_->mtx);
        detail::EventSinkGuard guard(impl_.get(), &events);
        n = impl_->doRemove(q);
    }
    fireEvents(impl_, events);
    return n;
}

bool Collection::removeById(DocumentId id) {
    std::vector<ChangeEvent> events;
    bool ok;
    {
        std::unique_lock lock(impl_->mtx);
        detail::EventSinkGuard guard(impl_.get(), &events);
        ok = impl_->doRemoveById(id);
    }
    fireEvents(impl_, events);
    return ok;
}

void Collection::setOverflow(const std::string& spillFile, std::size_t maxInMemoryDocs) {
    if (impl_->txLocks.load() > 0) // vor dem Lock pruefen (sonst Deadlock im eigenen Tx-Kontext)
        throw Error("setOverflow: waehrend aktiver Transaktion nicht erlaubt");
    std::unique_lock lock(impl_->mtx);
    impl_->enableOverflow(spillFile, maxInMemoryDocs);
}

void Collection::disableOverflow() {
    if (impl_->txLocks.load() > 0)
        throw Error("disableOverflow: waehrend aktiver Transaktion nicht erlaubt");
    std::unique_lock lock(impl_->mtx);
    impl_->disableOverflow();
}

OverflowInfo Collection::overflowInfo() const {
    std::shared_lock lock(impl_->mtx);
    OverflowInfo info;
    info.enabled = impl_->overflowEnabled;
    info.spillFile = impl_->spillPath;
    info.maxInMemory = impl_->maxInMemory;
    info.inMemory = impl_->docs.size();
    info.spilled = impl_->spillIndex.size();
    info.spillBytes = impl_->spillBytes;
    return info;
}

std::uint64_t Collection::compactSpill() {
    std::unique_lock lock(impl_->mtx);
    return impl_->compactSpill();
}

std::size_t Collection::size() const {
    sweepExpired(impl_);
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

CollectionMetrics Collection::metrics() const {
    std::shared_lock lock(impl_->mtx);
    CollectionMetrics m;
    const auto& s = impl_->metrics;
    m.inserts = s.inserts.load();
    m.updates = s.updates.load();
    m.removes = s.removes.load();
    m.queries = s.queries.load();
    m.indexQueries = s.indexQueries.load();
    m.scannedDocs = s.scannedDocs.load();
    m.changeEvents = s.changeEvents.load();
    m.spillReads = s.spillReads.load();
    m.spillWrites = s.spillWrites.load();
    m.txCommits = s.txCommits.load();
    m.txRollbacks = s.txRollbacks.load();
    m.expiredDocs = s.expiredDocs.load();
    m.docs = impl_->doSize();
    m.indexCount = impl_->indexes.size();
    m.lastCandidateCount = impl_->lastCandidateCount;
    m.lastQueryUsedIndex = s.lastUsedIndex;
    m.lastQueryMs = s.lastQueryMs;
    m.totalQueryMs = s.totalQueryMs;
    return m;
}

void Collection::resetMetrics() {
    std::unique_lock lock(impl_->mtx);
    auto& s = impl_->metrics;
    s.inserts = 0;
    s.updates = 0;
    s.removes = 0;
    s.queries = 0;
    s.indexQueries = 0;
    s.scannedDocs = 0;
    s.changeEvents = 0;
    s.spillReads = 0;
    s.spillWrites = 0;
    s.txCommits = 0;
    s.txRollbacks = 0;
    s.expiredDocs = 0;
    s.lastQueryMs = 0.0;
    s.totalQueryMs = 0.0;
    s.lastUsedIndex = false;
    impl_->lastCandidateCount = 0;
}

Transaction Collection::beginTransaction() { return Transaction(impl_); }

std::string Collection::toJson() const {
    sweepExpired(impl_);
    std::shared_lock lock(impl_->mtx);
    std::string out = "[";
    bool first = true;
    // deterministische Reihenfolge (ID aufsteigend), unabhaengig vom internen Hash-Map-Layout
    std::vector<DocumentId> ids;
    ids.reserve(impl_->docs.size());
    for (const auto& [id, doc] : impl_->docs)
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (DocumentId id : ids) {
        if (!first)
            out += ",";
        first = false;
        out += detail::writeJsonDocument(impl_->docs[id]);
    }
    out += "]";
    return out;
}

void Collection::loadJson(std::string_view json) {
    Value root = detail::parseJson(json);
    if (!root.isArray())
        throw JsonError("Collection-JSON muss ein Array von Objekten sein");
    loadJsonParsed(std::move(root));
}

void Collection::loadJsonParsed(Value arrayValue) {
    std::vector<ChangeEvent> events;
    {
        std::unique_lock lock(impl_->mtx);
        detail::EventSinkGuard guard(impl_.get(), &events);
        impl_->doLoadBulk(arrayValue);
    }
    fireEvents(impl_, events);
}

void Collection::clear() {
    std::vector<ChangeEvent> events;
    {
        std::unique_lock lock(impl_->mtx);
        detail::EventSinkGuard guard(impl_.get(), &events);
        impl_->doClear();
    }
    fireEvents(impl_, events);
}

std::optional<Value> Collection::aggregate(AggOp op, const std::string& fieldPath, const Expression& q) const {
    sweepExpired(impl_);
    std::shared_lock lock(impl_->mtx);
    return impl_->doAggregate(op, fieldPath, q);
}

std::vector<GroupAggregate> Collection::groupBy(const std::string& keyPath, const Expression& q, AggOp op,
                                                 const std::string& aggPath) const {
    sweepExpired(impl_);
    std::shared_lock lock(impl_->mtx);
    return impl_->doGroupBy(keyPath, q, op, aggPath);
}

bool Collection::expiresIn(DocumentId id, std::int64_t seconds) {
    std::vector<ChangeEvent> events;
    bool ok = false;
    {
        std::unique_lock lock(impl_->mtx);
        detail::EventSinkGuard guard(impl_.get(), &events);
        const std::int64_t ts = impl_->clockFn() + seconds;
        ok = impl_->doUpdateById(id, [&](Document& d) { d.set("expiresAt", Value(ts)); });
    }
    fireEvents(impl_, events);
    return ok;
}

bool Collection::expiresAt(DocumentId id, std::int64_t unixSeconds) {
    std::vector<ChangeEvent> events;
    bool ok = false;
    {
        std::unique_lock lock(impl_->mtx);
        detail::EventSinkGuard guard(impl_.get(), &events);
        ok = impl_->doUpdateById(id, [&](Document& d) { d.set("expiresAt", Value(unixSeconds)); });
    }
    fireEvents(impl_, events);
    return ok;
}

std::optional<std::int64_t> Collection::expiresAtOf(DocumentId id) const {
    sweepExpired(impl_);
    std::shared_lock lock(impl_->mtx);
    auto doc = impl_->doFindById(id);
    if (!doc || !detail::CollectionState::hasExpiryField(*doc))
        return std::nullopt;
    return detail::CollectionState::expiryOf(*doc);
}

void Collection::setDefaultExpiresIn(std::int64_t seconds) {
    std::unique_lock lock(impl_->mtx);
    impl_->defaultExpiresIn = seconds;
}

std::int64_t Collection::defaultExpiresIn() const {
    std::shared_lock lock(impl_->mtx);
    return impl_->defaultExpiresIn;
}

std::size_t Collection::expireNow() {
    std::vector<ChangeEvent> events;
    std::size_t n = 0;
    {
        std::unique_lock lock(impl_->mtx);
        detail::EventSinkGuard guard(impl_.get(), &events);
        n = impl_->doExpireNow();
    }
    fireEvents(impl_, events);
    return n;
}

void Collection::setClockForTesting(std::function<std::int64_t()> fn) {
    std::unique_lock lock(impl_->mtx);
    impl_->clockFn = std::move(fn);
}

std::vector<Value> Collection::distinctValues(const std::string& fieldPath, const Expression& q) const {
    sweepExpired(impl_);
    std::shared_lock lock(impl_->mtx);
    return impl_->doDistinctValues(fieldPath, q);
}

std::vector<Value> Collection::distinctValues(const std::string& fieldPath, std::string_view dsl) const {
    return distinctValues(fieldPath, parseQuery(dsl));
}

std::vector<DocumentId> Collection::sample(std::size_t maxN, std::uint64_t seed) const {
    sweepExpired(impl_);
    std::shared_lock lock(impl_->mtx);
    return impl_->doSample(maxN, seed);
}

std::uint64_t Collection::onChange(ChangeCallback cb) {
    std::unique_lock lock(impl_->mtx);
    std::uint64_t handle = impl_->nextObserverId++;
    impl_->observers.emplace_back(handle, std::move(cb));
    return handle;
}

bool Collection::offChange(std::uint64_t handle) {
    std::unique_lock lock(impl_->mtx);
    for (auto it = impl_->observers.begin(); it != impl_->observers.end(); ++it)
        if (it->first == handle) {
            impl_->observers.erase(it);
            return true;
        }
    return false;
}

void Collection::setSchema(Schema schema) {
    std::unique_lock lock(impl_->mtx);
    impl_->doSetSchema(std::move(schema));
}

std::optional<Schema> Collection::schema() const {
    std::shared_lock lock(impl_->mtx);
    return impl_->doSchema();
}

bool Collection::hasSchema() const {
    std::shared_lock lock(impl_->mtx);
    return impl_->doHasSchema();
}

void Collection::clearSchema() {
    std::unique_lock lock(impl_->mtx);
    impl_->doClearSchema();
}

} // namespace memdb
