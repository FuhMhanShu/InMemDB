#pragma once

#include "memdb/aggregate.hpp"
#include "memdb/change_event.hpp"
#include "memdb/cursor.hpp"
#include "memdb/document.hpp"
#include "memdb/exceptions.hpp"
#include "memdb/index.hpp"
#include "memdb/query.hpp"
#include "memdb/schema.hpp"

#include "json.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <vector>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace memdb::detail {

// Datei physikalisch auf 'bytes' kuerzen (Rollback entfernt so Tx-Spill-Appends)
inline void truncateFileTo(const std::string& path, std::uint64_t bytes) {
#ifdef _WIN32
    int fd = _open(path.c_str(), _O_RDWR | _O_BINARY);
    if (fd >= 0) {
        _chsize_s(fd, static_cast<__int64>(bytes));
        _close(fd);
    }
#else
    int fd = ::open(path.c_str(), O_RDWR);
    if (fd >= 0) {
        ::ftruncate(fd, static_cast<off_t>(bytes));
        ::close(fd);
    }
#endif
}

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

struct Accumulator {
    AggOp op;
    std::size_t count = 0;
    bool have = false;
    Value best;
    std::int64_t sumInt = 0;
    double sumD = 0.0;
    bool allInt = true;
    std::size_t numCount = 0;

    explicit Accumulator(AggOp o) : op(o) {}

    void addDoc() { ++count; }

    void addValue(const Value* v) {
        if (op == AggOp::Count)
            return;
        if (!v || v->isNull())
            return;
        if (op == AggOp::Sum || op == AggOp::Avg) {
            if (!v->isNumber())
                return;
            ++numCount;
            sumD += v->asDouble();
            if (v->isInt()) {
                auto x = v->asInt();
                if (x > 0 && sumInt > std::numeric_limits<std::int64_t>::max() - x)
                    allInt = false;
                else if (x < 0 && sumInt < std::numeric_limits<std::int64_t>::min() - x)
                    allInt = false;
                else
                    sumInt += x;
            } else {
                allInt = false;
            }
        } else {
            if (!v->isNumber() && !v->isString())
                return;
            if (!have) {
                best = *v;
                have = true;
            } else {
                auto c = compare(*v, best);
                if (c && ((op == AggOp::Min && *c < 0) || (op == AggOp::Max && *c > 0)))
                    best = *v;
            }
        }
    }

    std::optional<Value> result() const {
        if (op == AggOp::Count)
            return Value(static_cast<std::int64_t>(count));
        if (op == AggOp::Sum || op == AggOp::Avg) {
            if (numCount == 0)
                return std::nullopt;
            if (op == AggOp::Sum)
                return allInt ? std::optional<Value>(Value(sumInt)) : std::optional<Value>(Value(sumD));
            return Value(sumD / static_cast<double>(numCount));
        }
        return have ? std::optional<Value>(best) : std::nullopt;
    }
};

// Metrik-/Diagnose-Zaehler (v0.22): alle Zugriffe erfolgen unter mtx, die atomics
// machen die Zaehler zusaetzlich robust gegen kuenftige lock-freie Pfade.
struct Metrics {
    std::atomic<std::uint64_t> inserts{0};
    std::atomic<std::uint64_t> updates{0};
    std::atomic<std::uint64_t> removes{0};
    std::atomic<std::uint64_t> queries{0};
    std::atomic<std::uint64_t> indexQueries{0};
    std::atomic<std::uint64_t> scannedDocs{0};
    std::atomic<std::uint64_t> changeEvents{0};
    std::atomic<std::uint64_t> spillReads{0};
    std::atomic<std::uint64_t> spillWrites{0};
    std::atomic<std::uint64_t> txCommits{0};
    std::atomic<std::uint64_t> txRollbacks{0};
    double lastQueryMs = 0.0;
    double totalQueryMs = 0.0;
    bool lastUsedIndex = false;
};

struct CollectionState {
    std::string name;
    std::map<DocumentId, Document> docs;
    std::uint64_t nextId = 1;
    std::map<std::string, std::unique_ptr<FieldIndex>> indexes;
    mutable std::shared_mutex mtx;
    mutable std::size_t lastCandidateCount = 0;
    mutable Metrics metrics;
    std::vector<ChangeEvent>* sink = nullptr;
    std::vector<std::pair<std::uint64_t, ChangeCallback>> observers;
    std::uint64_t nextObserverId = 1;
    std::shared_ptr<const Schema> schema_;

    void validateDoc(const Document& doc) const {
        if (schema_)
            validateDocumentAgainstSchema(doc, *schema_);
    }

    void emit(ChangeType type, DocumentId id, const Document& doc) {
        if (sink) {
            sink->push_back(ChangeEvent{type, name, id, doc});
            ++metrics.changeEvents;
        }
    }

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

    // --- Hybrid / Out-of-Core (v0.18), opt-in ---
    // Spill-Record: [u64 id][u32 len][JSON bytes]; spillIndex: id -> Offset.
    bool overflowEnabled = false;
    std::string spillPath;
    std::size_t maxInMemory = 0;
    std::map<DocumentId, std::uint64_t> spillIndex;
    std::uint64_t spillBytes = 0;
    std::atomic<std::size_t> txLocks{0}; // >0 waehrend Transaktion: kein Evict

    // Copy-on-Write-Undo fuer Transaktionen (v0.19): statt Voll-Backup nur angefasste Docs
    struct TxUndo {
        std::map<DocumentId, std::optional<Document>> docs; // alter Zustand (nullopt = gab es nicht)
        bool cleared = false;
        std::map<DocumentId, Document> clearedDocs;         // nur bei Clear/Load waehrend Tx
    };
    TxUndo* undo = nullptr;

    void noteUndo(DocumentId id) {
        if (undo && undo->docs.count(id) == 0) {
            auto it = docs.find(id);
            undo->docs[id] = it != docs.end() ? std::optional<Document>(it->second) : std::nullopt;
        }
    }
    void noteUndoClear() {
        if (undo && !undo->cleared) {
            undo->cleared = true;
            // Bereits per noteUndo erfasste IDs ueberlagern wir beim Restore separat
            for (const auto& [id, doc] : docs)
                if (undo->docs.count(id) == 0)
                    undo->clearedDocs.emplace(id, doc);
        }
    }
    std::fstream spillFile;

    bool hasDoc(DocumentId id) const { return docs.count(id) != 0 || spillIndex.count(id) != 0; }

    std::optional<Document> loadSpilled(DocumentId id) const {
        auto it = spillIndex.find(id);
        if (it == spillIndex.end() || !spillFile.is_open())
            return std::nullopt;
        auto& f = const_cast<std::fstream&>(spillFile);
        f.seekg(static_cast<std::streamoff>(it->second));
        std::uint64_t rid = 0;
        std::uint32_t len = 0;
        f.read(reinterpret_cast<char*>(&rid), 8);
        f.read(reinterpret_cast<char*>(&len), 4);
        if (!f || rid != id.value() || len > 64u * 1024 * 1024)
            return std::nullopt;
        std::string buf(len, '\0');
        f.read(buf.data(), static_cast<std::streamsize>(len));
        if (!f)
            return std::nullopt;
        Value v = detail::parseJson(buf);
        if (!v.isDocument())
            return std::nullopt;
        Document d;
        v.asDocument()->forEachField([&](const std::string& k, const Value& val) { d.set(k, val); });
        d.setId(id);
        ++metrics.spillReads;
        return d;
    }

    Document loadDoc(DocumentId id) const {
        auto it = docs.find(id);
        if (it != docs.end())
            return it->second;
        return *loadSpilled(id);
    }

    void spillAppend(DocumentId id, const Document& doc) {
        std::string json = detail::writeJsonDocument(doc);
        std::uint64_t off = spillBytes;
        std::uint64_t rid = id.value();
        std::uint32_t len = static_cast<std::uint32_t>(json.size());
        spillFile.seekp(static_cast<std::streamoff>(spillBytes)); // == Dateiende (Invariante)
        spillFile.write(reinterpret_cast<const char*>(&rid), 8);
        spillFile.write(reinterpret_cast<const char*>(&len), 4);
        spillFile.write(json.data(), static_cast<std::streamsize>(json.size()));
        spillFile.flush();
        spillBytes += 12 + len;
        spillIndex[id] = off;
        ++metrics.spillWrites;
    }

    // Spill-Datei physikalisch kuerzen und spillBytes setzen (Rollback/Commit nach Clear)
    void truncateSpill(std::uint64_t bytes) {
        if (!overflowEnabled || spillPath.empty())
            return;
        spillFile.flush();
        spillFile.clear();
        truncateFileTo(spillPath, bytes);
        spillBytes = bytes;
        spillFile.clear();
    }

    void maybeEvict() {
        if (!overflowEnabled || txLocks.load() > 0)
            return;
        while (docs.size() > maxInMemory && !docs.empty()) {
            auto it = docs.begin(); // niedrigste ID zuerst (FIFO)
            spillAppend(it->first, it->second);
            indexRemove(it->first, it->second);
            docs.erase(it);
        }
    }

    void enableOverflow(const std::string& path, std::size_t maxInMem) {
        if (txLocks.load() > 0)
            throw Error("setOverflow: waehrend aktiver Transaktion nicht erlaubt");
        spillPath = path;
        maxInMemory = maxInMem;
        overflowEnabled = true;
        spillFile.open(path, std::ios::in | std::ios::out | std::ios::binary);
        if (!spillFile.is_open()) {
            spillFile.clear();
            { std::ofstream c(path, std::ios::binary | std::ios::trunc); }
            spillFile.open(path, std::ios::in | std::ios::out | std::ios::binary);
        }
        if (!spillFile.is_open())
            throw Error("Spill-Datei nicht oeffnen: " + path);
        // Index aus evtl. bestehender Datei aufbauen (Recovery); letzte Version gewinnt
        spillIndex.clear();
        spillFile.seekg(0);
        std::uint64_t off = 0;
        while (true) {
            std::uint64_t rid = 0;
            std::uint32_t len = 0;
            spillFile.read(reinterpret_cast<char*>(&rid), 8);
            if (!spillFile)
                break;
            spillFile.read(reinterpret_cast<char*>(&len), 4);
            if (!spillFile || len > 64u * 1024 * 1024)
                break;
            std::streamoff dataPos = spillFile.tellg();
            spillIndex[DocumentId(rid)] = off;
            spillFile.seekg(dataPos + static_cast<std::streamoff>(len));
            off = static_cast<std::uint64_t>(spillFile.tellg());
            if (spillFile.eof())
                break;
        }
        spillFile.clear();
        spillBytes = off;
        maybeEvict();
    }

    void disableOverflow() {
        if (!overflowEnabled)
            return;
        for (const auto& [id, off] : spillIndex) {
            if (docs.count(id))
                continue;
            if (auto d = loadSpilled(id)) {
                auto [it, ok] = docs.emplace(id, std::move(*d));
                indexAdd(id, it->second);
            }
        }
        spillIndex.clear();
        if (spillFile.is_open())
            spillFile.close();
        std::remove(spillPath.c_str());
        overflowEnabled = false;
        spillPath.clear();
        spillBytes = 0;
        maxInMemory = 0;
    }

    std::uint64_t compactSpill() {
        if (!overflowEnabled)
            return 0;
        std::string tmp = spillPath + ".tmp";
        std::uint64_t oldBytes = spillBytes;
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            std::map<DocumentId, std::uint64_t> newIndex;
            std::uint64_t off = 0;
            for (const auto& [id, oldoff] : spillIndex) {
                if (auto d = loadSpilled(id)) {
                    std::string json = detail::writeJsonDocument(*d);
                    std::uint64_t rid = id.value();
                    std::uint32_t len = static_cast<std::uint32_t>(json.size());
                    out.write(reinterpret_cast<const char*>(&rid), 8);
                    out.write(reinterpret_cast<const char*>(&len), 4);
                    out.write(json.data(), static_cast<std::streamsize>(json.size()));
                    newIndex[id] = off;
                    off += 12 + len;
                }
            }
            out.flush();
        }
        spillIndex.clear();
        if (spillFile.is_open())
            spillFile.close();
        std::remove(spillPath.c_str());
        std::rename(tmp.c_str(), spillPath.c_str());
        spillFile.open(spillPath, std::ios::in | std::ios::out | std::ios::binary);
        // Offsets neu aufbauen
        spillFile.seekg(0);
        std::uint64_t off = 0;
        while (true) {
            std::uint64_t rid = 0;
            std::uint32_t len = 0;
            spillFile.read(reinterpret_cast<char*>(&rid), 8);
            if (!spillFile)
                break;
            spillFile.read(reinterpret_cast<char*>(&len), 4);
            if (!spillFile || len > 64u * 1024 * 1024)
                break;
            std::streamoff dataPos = spillFile.tellg();
            spillIndex[DocumentId(rid)] = off;
            spillFile.seekg(dataPos + static_cast<std::streamoff>(len));
            off = static_cast<std::uint64_t>(spillFile.tellg());
            if (spillFile.eof())
                break;
        }
        spillFile.clear();
        spillBytes = off;
        return oldBytes > spillBytes ? oldBytes - spillBytes : 0;
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

    // metered=false: Schreibpfade (update/remove/upsert) zaehlen nicht als Query
    std::vector<DocumentId> candidates(const Expression& q, bool metered = true) const {
        auto t0 = std::chrono::steady_clock::now();
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
                case 2: {
                    for (auto it = m.begin(); it != m.lower_bound(hint->value); ++it)
                        ids.insert(ids.end(), it->second.begin(), it->second.end());
                    break;
                }
                case 3: {
                    for (auto it = m.begin(); it != m.upper_bound(hint->value); ++it)
                        ids.insert(ids.end(), it->second.begin(), it->second.end());
                    break;
                }
                case 4: {
                    for (auto it = m.upper_bound(hint->value); it != m.end(); ++it)
                        ids.insert(ids.end(), it->second.begin(), it->second.end());
                    break;
                }
                case 5: {
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
        if (overflowEnabled) // ausgelagerte Dokumente beim Scan mitnehmen
            for (const auto& [id, off] : spillIndex)
                ids.push_back(id);
        if (metered) {
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            ++metrics.queries;
            metrics.scannedDocs += ids.size();
            if (idx)
                ++metrics.indexQueries;
            metrics.lastUsedIndex = idx != nullptr;
            metrics.lastQueryMs = ms;
            metrics.totalQueryMs += ms;
            lastCandidateCount = ids.size();
        }
        return ids;
    }

    DocumentId doInsert(Document doc) {
        DocumentId id(nextId++);
        doc.setId(id);
        validateDoc(doc);
        validateUnique(id, doc);
        noteUndo(id);
        auto [it, ok] = docs.emplace(id, doc);
        indexAdd(id, it->second);
        emit(ChangeType::Insert, id, it->second);
        ++metrics.inserts;
        maybeEvict();
        return id;
    }

    std::vector<DocumentId> doInsertBulk(std::vector<Document> docs_) {
        std::vector<DocumentId> ids;
        ids.reserve(docs_.size());
        if (sink)
            sink->reserve(sink->size() + docs_.size()); // Observer-Events ohne Realloc
        for (auto& doc : docs_)
            ids.push_back(doInsert(std::move(doc)));
        maybeEvict();
        return ids;
    }

    std::optional<Document> doFindById(DocumentId id) const {
        auto it = docs.find(id);
        if (it != docs.end())
            return it->second;
        return loadSpilled(id);
    }

    /// Snapshot aller Dokumente inkl. gespillter (Out-of-Core, v0.18) – fuer
    /// Database::mergeCollections (FA-24). Aufrufer haelt das Lock.
    std::vector<std::pair<DocumentId, Document>> doSnapshotAll() const {
        std::vector<std::pair<DocumentId, Document>> out;
        out.reserve(docs.size() + spillIndex.size());
        for (const auto& [id, d] : docs)
            out.emplace_back(id, d);
        for (const auto& [id, off] : spillIndex)
            if (auto d = loadSpilled(id))
                out.emplace_back(id, *d);
        return out;
    }

    Cursor doFind(const Expression& q) const {
        auto ids = candidates(q);
        lastCandidateCount = ids.size();
        std::vector<Document> result;
        for (auto id : ids) {
            if (!hasDoc(id))
                continue;
            Document d = loadDoc(id);
            if (q.evaluate(d))
                result.push_back(std::move(d));
        }
        return Cursor(std::move(result));
    }

    std::optional<Document> doFindOne(const Expression& q) const {
        auto ids = candidates(q);
        lastCandidateCount = ids.size();
        for (auto id : ids) {
            if (!hasDoc(id))
                continue;
            Document d = loadDoc(id);
            if (q.evaluate(d))
                return d;
        }
        return std::nullopt;
    }

    std::size_t doCount(const Expression& q) const {
        auto ids = candidates(q);
        lastCandidateCount = ids.size();
        std::size_t n = 0;
        for (auto id : ids) {
            if (!hasDoc(id))
                continue;
            Document d = loadDoc(id);
            if (q.evaluate(d))
                ++n;
        }
        return n;
    }

    std::vector<DocumentId> doFindIds(const Expression& q) const {
        auto ids = candidates(q);
        lastCandidateCount = ids.size();
        std::vector<DocumentId> out;
        out.reserve(ids.size());
        for (auto id : ids) {
            if (!hasDoc(id))
                continue;
            Document d = loadDoc(id);
            if (q.evaluate(d))
                out.push_back(id);
        }
        return out;
    }

    std::size_t doForEach(const Expression& q, const std::function<void(const Document&)>& cb) const {
        auto ids = candidates(q);
        lastCandidateCount = ids.size();
        std::size_t n = 0;
        for (auto id : ids) {
            if (!hasDoc(id))
                continue;
            Document d = loadDoc(id);
            if (q.evaluate(d)) {
                cb(d);
                ++n;
            }
        }
        return n;
    }

    std::size_t doUpdate(const Expression& q, const std::function<void(Document&)>& mutator) {
        auto ids = candidates(q, false);
        std::vector<std::pair<DocumentId, Document>> changes;
        for (auto id : ids) {
            if (!hasDoc(id))
                continue;
            Document updated = loadDoc(id);
            if (!q.evaluate(updated))
                continue;
            mutator(updated);
            updated.setId(id);
            validateDoc(updated);
            validateUnique(id, updated);
            changes.emplace_back(id, std::move(updated));
        }
        for (auto& [id, updated] : changes) {
            auto it = docs.find(id);
            if (it != docs.end()) {
                noteUndo(id);
                indexRemove(id, it->second);
                it->second = std::move(updated);
                indexAdd(id, it->second);
                emit(ChangeType::Update, id, it->second);
            } else { // ausgelagert: neue Version ans Spill anhaengen
                spillAppend(id, updated);
                emit(ChangeType::Update, id, updated);
            }
        }
        metrics.updates += changes.size();
        return changes.size();
    }

    bool doUpdateById(DocumentId id, const std::function<void(Document&)>& mutator) {
        auto it = docs.find(id);
        noteUndo(id);
        if (it == docs.end()) {
            if (!spillIndex.count(id))
                return false;
            Document updated = *loadSpilled(id);
            mutator(updated);
            updated.setId(id);
            validateDoc(updated);
            spillAppend(id, updated); // neue Version ans Spill
            emit(ChangeType::Update, id, updated);
            ++metrics.updates;
            return true;
        }
        Document updated = it->second;
        mutator(updated);
        updated.setId(id);
        validateDoc(updated);
        validateUnique(id, updated);
        indexRemove(id, it->second);
        it->second = std::move(updated);
        indexAdd(id, it->second);
        emit(ChangeType::Update, id, it->second);
        ++metrics.updates;
        return true;
    }

    bool doReplace(DocumentId id, Document doc) {
        auto it = docs.find(id);
        noteUndo(id);
        if (it == docs.end()) {
            if (!spillIndex.count(id))
                return false;
            doc.setId(id);
            validateDoc(doc);
            spillAppend(id, doc);
            emit(ChangeType::Replace, id, doc);
            ++metrics.updates;
            return true;
        }
        doc.setId(id);
        validateDoc(doc);
        validateUnique(id, doc);
        indexRemove(id, it->second);
        it->second = std::move(doc);
        indexAdd(id, it->second);
        emit(ChangeType::Replace, id, it->second);
        ++metrics.updates;
        return true;
    }

    DocumentId doUpsertById(DocumentId id, Document doc) {
        auto it = docs.find(id);
        noteUndo(id);
        if (it != docs.end()) {
            doc.setId(id);
            validateDoc(doc);
            validateUnique(id, doc);
            indexRemove(id, it->second);
            it->second = std::move(doc);
            indexAdd(id, it->second);
            emit(ChangeType::Update, id, it->second);
            ++metrics.updates;
            return id;
        }
        if (spillIndex.count(id)) { // ausgelagert: im Spill aktualisieren
            doc.setId(id);
            validateDoc(doc);
            spillAppend(id, doc);
            emit(ChangeType::Update, id, doc);
            ++metrics.updates;
            return id;
        }
        return doInsert(std::move(doc));
    }

    DocumentId doUpsert(const Expression& q, Document doc) {
        auto ids = candidates(q, false);
        for (auto id : ids) {
            auto it = docs.find(id);
            if (it != docs.end() && q.evaluate(it->second))
                return doUpsertById(id, std::move(doc));
        }
        return doInsert(std::move(doc));
    }

    std::size_t doRemove(const Expression& q) {
        auto ids = candidates(q, false);
        std::vector<DocumentId> toRemove;
        for (auto id : ids) {
            if (!hasDoc(id))
                continue;
            Document d = loadDoc(id);
            if (q.evaluate(d))
                toRemove.push_back(id);
        }
        for (auto id : toRemove) {
            auto it = docs.find(id);
            if (it != docs.end()) {
                noteUndo(id);
                indexRemove(id, it->second);
                emit(ChangeType::Remove, id, it->second);
                docs.erase(it);
            } else if (spillIndex.count(id)) {
                Document d = *loadSpilled(id);
                spillIndex.erase(id); // Datei behaelt den Record bis compactSpill()
                emit(ChangeType::Remove, id, d);
            }
        }
        metrics.removes += toRemove.size();
        return toRemove.size();
    }

    bool doRemoveById(DocumentId id) {
        auto it = docs.find(id);
        noteUndo(id);
        if (it != docs.end()) {
            indexRemove(id, it->second);
            emit(ChangeType::Remove, id, it->second);
            docs.erase(it);
            ++metrics.removes;
            return true;
        }
        if (spillIndex.count(id)) {
            Document d = *loadSpilled(id);
            spillIndex.erase(id);
            emit(ChangeType::Remove, id, d);
            ++metrics.removes;
            return true;
        }
        return false;
    }

    std::size_t doSize() const { return docs.size() + spillIndex.size(); }

    std::optional<Value> doAggregate(AggOp op, const std::string& fieldPath, const Expression& q) const {
        Accumulator acc(op);
        for (auto id : candidates(q)) {
            if (!hasDoc(id))
                continue;
            Document d = loadDoc(id);
            if (!q.evaluate(d))
                continue;
            acc.addDoc();
            acc.addValue(d.findPath(fieldPath));
        }
        return acc.result();
    }

    std::vector<GroupAggregate> doGroupBy(const std::string& keyPath, const Expression& q, AggOp op,
                                           const std::string& aggPath) const {
        std::map<Value, Accumulator, ValueLess> groups;
        for (auto id : candidates(q)) {
            if (!hasDoc(id))
                continue;
            Document d = loadDoc(id);
            if (!q.evaluate(d))
                continue;
            const Value* k = d.findPath(keyPath);
            Value key = k ? *k : Value(nullptr);
            auto& acc = groups.try_emplace(key, op).first->second;
            acc.addDoc();
            acc.addValue(d.findPath(aggPath));
        }
        std::vector<GroupAggregate> out;
        out.reserve(groups.size());
        for (auto& [k, acc] : groups)
            out.push_back(GroupAggregate{k, acc.count, op == AggOp::Count ? std::nullopt : acc.result()});
        return out;
    }

    void doClear() {
        noteUndoClear();
        docs.clear();
        nextId = 1;
        for (auto& [path, idx] : indexes)
            idx->entries.clear();
        if (overflowEnabled) {
            spillIndex.clear();
            if (undo == nullptr) {
                // waehrend Tx Datei/Bytes stehenlassen: Rollback braucht die Records
                if (spillFile.is_open())
                    spillFile.close();
                { std::ofstream t(spillPath, std::ios::binary | std::ios::trunc); }
                spillFile.open(spillPath, std::ios::in | std::ios::out | std::ios::binary);
                spillBytes = 0;
            }
        }
        emit(ChangeType::Clear, DocumentId(0), Document{});
    }

    // Bulk-Load (loadJson): Dokumente per Move uebernehmen (keine Deep-Copies),
    // nicht-unique Indizes erst am Ende in einem Durchlauf neu aufbauen,
    // unique Indizes bleiben live (Unique-Pruefung greift).
    void doLoadBulk(Value& arrayValue) {
        if (!arrayValue.isArray())
            throw JsonError("Collection-JSON muss ein Array von Objekten sein");
        auto& items = arrayValue.asArray();
        noteUndoClear();
        docs.clear();
        nextId = 1;
        for (auto& [path, idx] : indexes)
            idx->entries.clear();
        if (overflowEnabled) {
            spillIndex.clear();
            if (undo == nullptr) {
                // waehrend Tx Datei/Bytes stehenlassen: Rollback braucht die Records
                if (spillFile.is_open())
                    spillFile.close();
                { std::ofstream t(spillPath, std::ios::binary | std::ios::trunc); }
                spillFile.open(spillPath, std::ios::in | std::ios::out | std::ios::binary);
                spillBytes = 0;
            }
        }
        emit(ChangeType::Clear, DocumentId(0), Document{});
        for (auto& item : items) {
            if (!item.isDocument())
                throw JsonError("Collection-JSON: Element ist kein Objekt");
            Document doc = std::move(*item.asDocument());
            DocumentId id(nextId++);
            doc.setId(id);
            validateDoc(doc);
            validateUnique(id, doc);
            for (auto& [path, idx] : indexes)
                if (idx->unique)
                    if (const Value* v = doc.findPath(path))
                        idx->entries[*v].insert(id);
            docs.emplace(id, std::move(doc));
        }
        for (auto& [path, idx] : indexes) {
            if (idx->unique)
                continue;
            for (const auto& [id, doc] : docs)
                if (const Value* v = doc.findPath(path))
                    idx->entries[*v].insert(id);
        }
        for (const auto& [id, doc] : docs)
            emit(ChangeType::Insert, id, doc);
    }

    void doSetSchema(Schema schema) {
        for (const auto& [id, doc] : docs)
            validateDocumentAgainstSchema(doc, schema);
        schema_ = std::make_shared<const Schema>(std::move(schema));
    }

    std::optional<Schema> doSchema() const {
        if (!schema_)
            return std::nullopt;
        return *schema_;
    }

    bool doHasSchema() const { return schema_ != nullptr; }

    void doClearSchema() { schema_.reset(); }

    Index doCreateIndex(const std::string& fieldPath, bool unique) {
        if (indexes.find(fieldPath) != indexes.end())
            throw IndexExistsError("Index existiert bereits fuer Feld: " + fieldPath);
        auto idx = std::make_unique<FieldIndex>();
        idx->path = fieldPath;
        idx->unique = unique;
        for (const auto& [id, doc] : docs) {
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
        indexes.emplace(fieldPath, std::move(idx));
        return result;
    }

    bool doDropIndex(const std::string& fieldPath) { return indexes.erase(fieldPath) > 0; }

    bool doHasIndex(const std::string& fieldPath) const {
        return indexes.find(fieldPath) != indexes.end();
    }

    std::vector<Index> doIndexes() const {
        std::vector<Index> result;
        result.reserve(indexes.size());
        for (const auto& [path, idx] : indexes)
            result.emplace_back(idx->path, idx->unique);
        return result;
    }
};

struct EventSinkGuard {
    explicit EventSinkGuard(CollectionState* s, std::vector<ChangeEvent>* e) : state(s) { state->sink = e; }
    ~EventSinkGuard() { state->sink = nullptr; }
    EventSinkGuard(const EventSinkGuard&) = delete;
    EventSinkGuard& operator=(const EventSinkGuard&) = delete;
    CollectionState* state;
};

} // namespace memdb::detail
