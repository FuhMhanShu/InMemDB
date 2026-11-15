#include "memdb/database.hpp"
#include "memdb/exceptions.hpp"
#include "memdb/references.hpp"

#include "collection_state.hpp"
#include "crypto.hpp"
#include "json.hpp"

#include <algorithm>
#include <array>
#include <cstring>
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

DatabaseMetrics Database::metrics() const {
    DatabaseMetrics out;
    for (const auto& name : collectionNames()) {
        CollectionMetrics m = collection(name).metrics();
        out.collectionCount++;
        out.totalDocs += m.docs;
        out.sum.inserts += m.inserts;
        out.sum.updates += m.updates;
        out.sum.removes += m.removes;
        out.sum.queries += m.queries;
        out.sum.indexQueries += m.indexQueries;
        out.sum.scannedDocs += m.scannedDocs;
        out.sum.changeEvents += m.changeEvents;
        out.sum.spillReads += m.spillReads;
        out.sum.spillWrites += m.spillWrites;
        out.sum.txCommits += m.txCommits;
        out.sum.txRollbacks += m.txRollbacks;
        out.sum.totalQueryMs += m.totalQueryMs;
    }
    return out;
}

CollectionMetrics Database::metrics(const std::string& collectionName) const {
    return collection(collectionName).metrics();
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

MergeStats Database::mergeCollections(const std::string& target, std::vector<std::string> sources,
                                      MergeOptions opts, ReferenceIndex* refs) {
    if (sources.empty())
        throw Error("mergeCollections: keine Quell-Collections angegeben");
    for (const auto& s : sources)
        if (s == target)
            throw Error("mergeCollections: Ziel-Collection darf nicht gleichzeitig Quelle sein: " + target);
    std::sort(sources.begin(), sources.end());
    sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    {
        std::shared_lock lock(impl_->mtx);
        for (const auto& s : sources)
            if (impl_->collections.find(s) == impl_->collections.end())
                throw CollectionNotFoundError("mergeCollections: Quell-Collection nicht gefunden: " + s);
        if (refs && opts.dropSources)
            for (const auto& s : sources)
                if (s == refs->storageCollection())
                    throw Error("mergeCollections: Referenz-Speicher '" + s +
                                "' darf nicht als Quelle geloescht werden");
    }
    if (!hasCollection(target))
        createCollection(target);

    // States sammeln und in sortierter Namensreihenfolge exklusiv locken (deadlock-frei;
    // Transaktionen locken nur eine Collection und koennen daher nicht zirkulaer warten).
    std::vector<std::pair<std::string, std::shared_ptr<detail::CollectionState>>> states;
    {
        std::shared_lock lock(impl_->mtx);
        states.emplace_back(target, impl_->collections.at(target));
        for (const auto& s : sources)
            states.emplace_back(s, impl_->collections.at(s));
    }
    std::sort(states.begin(), states.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::unique_lock<std::shared_mutex>> locks;
    locks.reserve(states.size());
    for (auto& [n, st] : states)
        locks.emplace_back(st->mtx);
    for (auto& [n, st] : states)
        ++st->txLocks; // Eviction waehrend Merge unterdruecken (Rollback bleibt in RAM)

    detail::CollectionState* tgt = nullptr;
    for (auto& [n, st] : states)
        if (n == target)
            tgt = st.get();
    auto stateOf = [&](const std::string& n) -> detail::CollectionState* {
        for (auto& [sn, st] : states)
            if (sn == n)
                return st.get();
        return nullptr;
    };

    MergeStats stats;
    std::vector<ChangeEvent> events;
    std::vector<DocumentId> inserted;
    std::map<std::string, std::map<std::string, std::string>> keyMap; // quelle -> (altKey -> neuKey)
    const std::uint64_t savedNextId = tgt->nextId;
    try {
        detail::EventSinkGuard guard(tgt, &events);
        for (const auto& src : sources) {
            detail::CollectionState* s = stateOf(src);
            for (auto& [oldId, doc] : s->doSnapshotAll()) { // inkl. Spill (v0.18)
                if (!opts.sourceField.empty())
                    doc.set(opts.sourceField, Value(src));
                // doInsert: neue ID, Schema-/Unique-Validierung, Index, Event, Metrik
                DocumentId nid = tgt->doInsert(std::move(doc));
                inserted.push_back(nid);
                ++stats.merged;
                ++stats.perSource[src];
                keyMap[src][src + "#" + toString(oldId)] = target + "#" + toString(nid);
            }
            keyMap[src][src + "#"] = target + "#"; // Referenz auf ganze Collection
        }
        if (opts.mergeIndexes) {
            for (const auto& src : sources) {
                detail::CollectionState* s = stateOf(src);
                for (const auto& [path, idx] : s->indexes) {
                    if (tgt->indexes.count(path))
                        continue;
                    auto ni = std::make_unique<detail::FieldIndex>();
                    ni->path = path;
                    ni->unique = idx->unique;
                    for (const auto& [id, doc] : tgt->docs) {
                        if (const Value* v = doc.findPath(path)) {
                            if (ni->unique && ni->entries.count(*v))
                                throw UniqueConstraintError("mergeCollections: Unique-Index '" + path +
                                                             "' durch Index-Uebernahme verletzt");
                            ni->entries[*v].insert(id);
                        }
                    }
                    tgt->indexes.emplace(path, std::move(ni));
                    stats.adoptedIndexes.push_back(path);
                }
            }
        }
    } catch (...) {
        // Atomarer Rollback: alles bisher Eingefuegte entfernen, nextId zuruecksetzen
        for (auto it = inserted.rbegin(); it != inserted.rend(); ++it) {
            auto dit = tgt->docs.find(*it);
            if (dit != tgt->docs.end()) {
                tgt->indexRemove(*it, dit->second);
                tgt->docs.erase(dit);
            }
        }
        tgt->nextId = savedNextId;
        for (const auto& p : stats.adoptedIndexes)
            tgt->indexes.erase(p);
        for (auto& [n, st] : states)
            --st->txLocks;
        throw;
    }
    for (auto& [n, st] : states)
        --st->txLocks;
    locks.clear(); // Locks freigeben, bevor Observer-Callbacks feuern

    if (!events.empty()) {
        std::vector<ChangeCallback> cbs;
        {
            std::shared_lock lock(tgt->mtx);
            for (const auto& [id, cb] : tgt->observers)
                cbs.push_back(cb);
        }
        for (const auto& e : events)
            for (const auto& cb : cbs)
                cb(e);
    }

    if (opts.dropSources)
        for (const auto& s : sources)
            if (dropCollection(s))
                stats.droppedSources.push_back(s);

    if (refs && opts.dropSources) {
        std::map<std::string, std::string> all;
        for (const auto& [src, m] : keyMap)
            all.insert(m.begin(), m.end());
        stats.rewrittenReferences = refs->rewriteKeys(all);
    }

    tgt->maybeEvict();
    return stats;
}

MergeStats Database::mergeAllCollections(const std::string& target, MergeOptions opts,
                                         ReferenceIndex* refs) {
    std::vector<std::string> sources;
    for (const auto& n : collectionNames()) {
        if (n == target)
            continue;
        if (refs && n == refs->storageCollection())
            continue; // Referenz-Speicher weder mergen noch loeschen
        sources.push_back(n);
    }
    if (sources.empty())
        throw Error("mergeAllCollections: keine Quell-Collections vorhanden (nur '" + target + "')");
    return mergeCollections(target, std::move(sources), opts, refs);
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
        out += "]";
        if (state->schema_)
            out += ",\"schema\":" + detail::writeJsonSchema(*state->schema_);
        out += ",\"documents\":" + Collection(state).toJson() + "}";
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
        Value documents;
        std::optional<Schema> schema;
    };
    std::vector<Item> items;
    for (const auto& [name, collValue] : collections->asDocument()->fields()) {
        Item item;
        item.name = name;
        if (collValue.isArray()) {
            item.documents = collValue;
        } else if (collValue.isDocument()) {
            Document& cd = *collValue.asDocument();
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
            Value* docs = cd.findMutable("documents");
            if (!docs || !docs->isArray())
                throw JsonError("Collection '" + name + "': 'documents' fehlt oder ist kein Array");
            if (const Value* s = cd.find("schema"))
                item.schema = detail::parseJsonSchema(*s);
            item.documents = std::move(*docs);
        } else {
            throw JsonError("Collection '" + name + "' muss Array oder Objekt sein");
        }
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
        if (!existed[i]) {
            for (const auto& [field, unique] : items[i].indexes)
                cols[i].createIndex(field, unique);
            if (items[i].schema)
                cols[i].setSchema(std::move(*items[i].schema));
        }
        cols[i].loadJsonParsed(std::move(items[i].documents));
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

namespace {

constexpr std::size_t kMagicLen = 8;
constexpr std::size_t kSaltLen = 16;
constexpr std::size_t kNonceLen = 12;
constexpr std::size_t kTagLen = 32;
constexpr std::size_t kHeaderLen = kMagicLen + kSaltLen + 4 + kNonceLen;
constexpr std::uint32_t kPbkdf2Iterations = 100000;
const char kMagic[kMagicLen] = {'I', 'N', 'M', 'E', 'M', 'D', 'B', 0x01};

void putU32Le(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 24));
}

std::uint32_t getU32Le(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}

} // namespace

void Database::saveToFileEncrypted(const std::string& path, const std::string& password) const {
    if (password.empty())
        throw CryptoError("leeres Passwort nicht erlaubt");
    const std::string json = toJson();

    auto salt = detail::crypto::randomBytes(kSaltLen);
    auto nonce = detail::crypto::randomBytes(kNonceLen);
    auto keys = detail::crypto::pbkdf2HmacSha256(password, salt.data(), salt.size(), kPbkdf2Iterations, 64);
    const std::uint8_t* encKey = keys.data();
    const std::uint8_t* macKey = keys.data() + 32;

    std::vector<std::uint8_t> ciphertext(json.size());
    detail::crypto::chacha20Xor(ciphertext.data(), reinterpret_cast<const std::uint8_t*>(json.data()),
                                json.size(), encKey, 1, nonce.data());

    std::vector<std::uint8_t> file;
    file.reserve(kHeaderLen + ciphertext.size() + kTagLen);
    file.insert(file.end(), kMagic, kMagic + kMagicLen);
    file.insert(file.end(), salt.begin(), salt.end());
    putU32Le(file, kPbkdf2Iterations);
    file.insert(file.end(), nonce.begin(), nonce.end());
    file.insert(file.end(), ciphertext.begin(), ciphertext.end());

    auto tag = detail::crypto::hmacSha256(macKey, 32, file.data(), file.size());
    file.insert(file.end(), tag.begin(), tag.end());

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw Error("Datei nicht beschreibbar: " + path);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!out)
        throw Error("Fehler beim Schreiben: " + path);
}

Database Database::loadFromFileEncrypted(const std::string& path, const std::string& password) {
    if (password.empty())
        throw CryptoError("leeres Passwort nicht erlaubt");
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw Error("Datei nicht lesbar: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string raw = ss.str();
    if (raw.size() < kHeaderLen + kTagLen)
        throw CryptoError("verschlüsselte Datei zu klein/beschädigt: " + path);
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(raw.data());
    if (std::memcmp(bytes, kMagic, kMagicLen) != 0)
        throw CryptoError("keine InMemDB-Verschlüsselungsdatei (Magic falsch): " + path);

    const std::uint8_t* salt = bytes + kMagicLen;
    std::uint32_t iterations = getU32Le(bytes + kMagicLen + kSaltLen);
    if (iterations == 0 || iterations > 10000000)
        throw CryptoError("ungültige PBKDF2-Iterationszahl in Datei");
    const std::uint8_t* nonce = bytes + kMagicLen + kSaltLen + 4;
    const std::uint8_t* ciphertext = bytes + kHeaderLen;
    std::size_t ctLen = raw.size() - kHeaderLen - kTagLen;
    const std::uint8_t* tag = bytes + raw.size() - kTagLen;

    auto keys = detail::crypto::pbkdf2HmacSha256(password, salt, kSaltLen, iterations, 64);
    const std::uint8_t* encKey = keys.data();
    const std::uint8_t* macKey = keys.data() + 32;

    auto expected = detail::crypto::hmacSha256(macKey, 32, bytes, raw.size() - kTagLen);
    if (!detail::crypto::constantTimeEqual(expected.data(), tag, kTagLen))
        throw CryptoError("falsches Passwort oder Datei beschädigt (HMAC-Prüfung fehlgeschlagen): " + path);

    std::string json(ctLen, '\0');
    detail::crypto::chacha20Xor(reinterpret_cast<std::uint8_t*>(json.data()), ciphertext, ctLen, encKey, 1,
                                nonce);
    return fromJson(json, "");
}

} // namespace memdb
