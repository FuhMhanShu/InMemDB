#include "memdb/collection.hpp"
#include "memdb/exceptions.hpp"

#include "collection_state.hpp"
#include "json.hpp"

#include <mutex>
#include <shared_mutex>

namespace memdb {

namespace {

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
    std::shared_lock lock(impl_->mtx);
    return impl_->doFindById(id);
}

Cursor Collection::find(const Expression& q) const {
    std::shared_lock lock(impl_->mtx);
    return impl_->doFind(q);
}

std::vector<DocumentId> Collection::findIds(const Expression& q) const {
    std::shared_lock lock(impl_->mtx);
    return impl_->doFindIds(q);
}

std::size_t Collection::forEach(const Expression& q, const std::function<void(const Document&)>& cb) const {
    std::shared_lock lock(impl_->mtx);
    return impl_->doForEach(q, cb);
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

std::string Collection::toJson() const {
    std::shared_lock lock(impl_->mtx);
    std::string out = "[";
    bool first = true;
    for (const auto& [id, doc] : impl_->docs) {
        if (!first)
            out += ",";
        first = false;
        out += detail::writeJsonDocument(doc);
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
    std::shared_lock lock(impl_->mtx);
    return impl_->doAggregate(op, fieldPath, q);
}

std::vector<GroupAggregate> Collection::groupBy(const std::string& keyPath, const Expression& q, AggOp op,
                                                 const std::string& aggPath) const {
    std::shared_lock lock(impl_->mtx);
    return impl_->doGroupBy(keyPath, q, op, aggPath);
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
