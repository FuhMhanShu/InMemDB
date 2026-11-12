#include "memdb/database.hpp"
#include "memdb/exceptions.hpp"

#include "collection_state.hpp"
#include "json.hpp"

#include <fstream>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <sstream>

namespace memdb {

struct Database::Impl {
    std::string name;
    std::map<std::string, std::shared_ptr<detail::CollectionState>> collections;
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

std::string Database::toJson() const {
    std::shared_lock lock(impl_->mtx);
    std::string out = "{\"name\":" + detail::writeJsonString(impl_->name) + ",\"collections\":{";
    bool first = true;
    for (const auto& [name, state] : impl_->collections) {
        if (!first)
            out += ",";
        first = false;
        out += detail::writeJsonString(name) + ":{\"indexes\":[";
        bool firstIdx = true;
        for (const auto& [path, idx] : state->indexes) {
            if (!firstIdx)
                out += ",";
            firstIdx = false;
            out += "{\"field\":" + detail::writeJsonString(idx->path) + ",\"unique\":" +
                   (idx->unique ? "true" : "false") + "}";
        }
        out += "],\"documents\":" + Collection(state).toJson() + "}";
    }
    out += "}}";
    return out;
}

void Database::fromJson(std::string_view json) {
    Value root = detail::parseJson(json);
    if (!root.isDocument())
        throw JsonError("Database-JSON muss ein Objekt sein");
    const Value* collections = root.asDocument()->find("collections");
    if (!collections || !collections->isDocument())
        throw JsonError("Database-JSON: Feld 'collections' fehlt oder ist kein Objekt");

    struct Item {
        std::string name;
        std::vector<std::pair<std::string, bool>> indexes;
        std::string documentsJson;
    };
    std::vector<Item> items;
    for (const auto& [name, collValue] : collections->asDocument()->fields()) {
        Item item;
        item.name = name;
        const Value* docs = nullptr;
        if (collValue.isArray()) {
            docs = &collValue;
        } else if (collValue.isDocument()) {
            const Document& cd = *collValue.asDocument();
            if (const Value* ix = cd.find("indexes")) {
                if (!ix->isArray())
                    throw JsonError("Collection '" + name + "': 'indexes' muss ein Array sein");
                for (const auto& e : ix->asArray()) {
                    if (!e.isDocument())
                        throw JsonError("Collection '" + name + "': Index-Eintrag muss Objekt sein");
                    const Document& ed = *e.asDocument();
                    const Value* f = ed.find("field");
                    const Value* u = ed.find("unique");
                    if (!f || !f->isString())
                        throw JsonError("Collection '" + name + "': Index-Eintrag ohne 'field'");
                    item.indexes.emplace_back(f->asString(), u && u->isBool() && u->asBool());
                }
            }
            docs = cd.find("documents");
            if (!docs || !docs->isArray())
                throw JsonError("Collection '" + name + "': 'documents' fehlt oder ist kein Array");
        } else {
            throw JsonError("Collection '" + name + "' muss Array oder Objekt sein");
        }
        item.documentsJson = detail::writeJson(*docs);
        items.push_back(std::move(item));
    }

    std::vector<Collection> cols;
    std::vector<bool> existed;
    {
        std::unique_lock lock(impl_->mtx);
        for (const auto& item : items) {
            auto it = impl_->collections.find(item.name);
            existed.push_back(it != impl_->collections.end());
            if (it != impl_->collections.end()) {
                cols.push_back(Collection(it->second));
            } else {
                Collection col = Collection::create(item.name);
                impl_->collections.emplace(item.name, col.impl_);
                cols.push_back(std::move(col));
            }
        }
    }
    for (std::size_t i = 0; i < cols.size(); ++i) {
        if (!existed[i])
            for (const auto& [field, unique] : items[i].indexes)
                cols[i].createIndex(field, unique);
        cols[i].loadJson(items[i].documentsJson);
    }
}

Database Database::fromJson(std::string_view json, std::string name) {
    Value root = detail::parseJson(json);
    if (name.empty() && root.isDocument()) {
        if (const Value* n = root.asDocument()->find("name"))
            if (n->isString())
                name = n->asString();
    }
    Database db(std::move(name));
    db.fromJson(json);
    return db;
}

void Database::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw Error("Datei nicht beschreibbar: " + path);
    out << toJson();
    if (!out)
        throw Error("Fehler beim Schreiben: " + path);
}

Database Database::loadFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw Error("Datei nicht lesbar: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return fromJson(ss.str(), "");
}

} // namespace memdb
