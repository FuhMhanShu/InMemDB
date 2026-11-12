#include "memdb/database.hpp"
#include "memdb/exceptions.hpp"

#include <map>
#include <mutex>
#include <shared_mutex>

namespace memdb {

struct Database::Impl {
    std::string name;
    std::map<std::string, std::shared_ptr<Collection::Impl>> collections;
    mutable std::shared_mutex mtx;
};

Database::Database(std::string name) : impl_(std::make_unique<Impl>()) { impl_->name = std::move(name); }
Database::~Database() = default;
Database::Database(Database&&) noexcept = default;
Database& Database::operator=(Database&&) noexcept = default;

const std::string& Database::name() const { return impl_->name; }

Collection Database::createCollection(const std::string& name) {
    std::unique_lock lock(impl_->mtx);
    if (impl_->collections.find(name) != impl_->collections.end())
        throw CollectionExistsError("Collection existiert bereits: " + name);
    Collection col = Collection::create(name);
    impl_->collections.emplace(name, col.impl_);
    return col;
}

Collection Database::collection(const std::string& name) const {
    std::shared_lock lock(impl_->mtx);
    auto it = impl_->collections.find(name);
    if (it == impl_->collections.end())
        throw CollectionNotFoundError("Collection nicht gefunden: " + name);
    return Collection(it->second);
}

bool Database::hasCollection(const std::string& name) const {
    std::shared_lock lock(impl_->mtx);
    return impl_->collections.find(name) != impl_->collections.end();
}

std::vector<std::string> Database::collectionNames() const {
    std::shared_lock lock(impl_->mtx);
    std::vector<std::string> names;
    names.reserve(impl_->collections.size());
    for (const auto& [name, impl] : impl_->collections)
        names.push_back(name);
    return names;
}

bool Database::dropCollection(const std::string& name) {
    std::unique_lock lock(impl_->mtx);
    return impl_->collections.erase(name) > 0;
}

void Database::clear() {
    std::unique_lock lock(impl_->mtx);
    impl_->collections.clear();
}

} // namespace memdb
