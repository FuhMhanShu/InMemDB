#pragma once

#include "memdb/aggregate.hpp"
#include "memdb/change_event.hpp"
#include "memdb/cursor.hpp"
#include "memdb/document.hpp"
#include "memdb/exceptions.hpp"
#include "memdb/limits.hpp"
#include "memdb/message_handler.hpp"
#include "memdb/index.hpp"
#include "memdb/query.hpp"
#include "memdb/schema.hpp"

#include "json.hpp"

#include <algorithm>
#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <random>
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

using ValueLess = memdb::ValueLess; // oeffentliche Ordnung (value.hpp), v0.27

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
    std::set<Value, ValueLess> distinct; // nur AggOp::Distinct
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
        if (op == AggOp::Distinct) {
            distinct.insert(*v);
            return;
        }
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
        if (op == AggOp::Distinct) {
            std::vector<Value> out(distinct.begin(), distinct.end());
            return Value(std::move(out));
        }
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
    std::atomic<std::uint64_t> expiredDocs{0};
    std::atomic<std::uint64_t> evalErrors{0};   ///< v0.35: abgefangene Eval-Fehler im Query-Scan (Dokument = Nicht-Treffer)
    std::atomic<std::uint64_t> eventErrors{0};  ///< v0.35: abgefangene Observer-Exceptions (Schreib-OP lief weiter)
    std::atomic<std::uint64_t> limitViolations{0}; ///< v0.39: wegen Collection-Limits abgelehnte Schreibzugriffe
    // v0.44 (M2): auch die "last*"-Werte atomar – candidates() schreibt sie unter
    // shared_lock, parallele Leser duerfen nicht racen (TSan-Report im Stresstest).
    std::atomic<double> lastQueryMs{0.0};
    std::atomic<double> totalQueryMs{0.0};
    std::atomic<bool> lastUsedIndex{false};
};

/// v0.44 (M2): atomares += fuer std::atomic<double> (C++17 hat kein fetch_add fuer double).
inline void atomicAdd(std::atomic<double>& a, double v) {
    double cur = a.load(std::memory_order_relaxed);
    while (!a.compare_exchange_weak(cur, cur + v, std::memory_order_relaxed)) {
    }
}

struct CollectionState {
    std::string name;
    std::unordered_map<DocumentId, Document> docs;
    std::uint64_t nextId = 1;
    std::map<std::string, std::unique_ptr<FieldIndex>> indexes;
    mutable std::shared_mutex mtx;
    mutable std::atomic<std::size_t> lastCandidateCount{0}; // v0.44 (M2): atomic, siehe Metrics
    mutable Metrics metrics;
    std::vector<ChangeEvent>* sink = nullptr;
    std::vector<std::pair<std::uint64_t, ChangeCallback>> observers;
    std::uint64_t nextObserverId = 1;
    std::shared_ptr<const Schema> schema_;

    // --- TTL / Auto-Expiry (FA-25, v0.26), opt-in: defaultmaessig AUS ---
    std::atomic<std::size_t> expiryCount{0}; ///< Docs mit `expiresAt`-Feld (RAM + Spill)
    std::int64_t defaultExpiresIn = 0;       ///< Collection-Default-TTL in s; 0 = aus (Default)
    std::function<std::int64_t()> clockFn = []() { return static_cast<std::int64_t>(std::time(nullptr)); };

    /// true, wenn das Dokument ein gueltiges `expiresAt`-Feld (Int, Unix-Sekunden) traegt.
    static bool hasExpiryField(const Document& d) {
        const Value* v = d.find("expiresAt");
        return v && v->isInt();
    }
    /// Ablaufzeitpunkt (Unix-Sekunden); nur aufrufen, wenn hasExpiryField true.
    static std::int64_t expiryOf(const Document& d) { return d.at("expiresAt").asInt(); }
    /// expiryCount bei Ersetzen/Einfuegen anpassen.
    void adjustExpiry(const Document* oldDoc, const Document* newDoc) {
        const bool o = oldDoc && hasExpiryField(*oldDoc);
        const bool n = newDoc && hasExpiryField(*newDoc);
        if (o || n) {
            if (auto* u = undo.load())
                u->touchedExpiry = true;
            if (n && !o)
                ++expiryCount;
            else if (o && !n)
                --expiryCount;
        }
    }
    /// Collection-Default-TTL auf neues Doc anwenden (nur wenn Feld im Schema erlaubt).
    void applyDefaultExpiry(Document& d) {
        if (defaultExpiresIn > 0 && !hasExpiryField(d) && (!schema_ || schema_->fields().count("expiresAt") > 0))
            d.set("expiresAt", Value(clockFn() + defaultExpiresIn));
    }
    /// Alle abgelaufenen Dokumente entfernen (Sweep). Aufrufer haelt unique_lock;
    /// mitten in Transaktion/Merge (txLocks > 0) wird nichts entfernt (Snapshot-Isolation).
    std::size_t doExpireNow() {
        if (txLocks.load() > 0)
            return 0;
        const std::int64_t now = clockFn();
        std::vector<DocumentId> expired;
        for (const auto& [id, d] : docs)
            if (hasExpiryField(d) && expiryOf(d) <= now)
                expired.push_back(id);
        for (const auto& [id, off] : spillIndex)
            if (auto d = loadSpilled(id))
                if (hasExpiryField(*d) && expiryOf(*d) <= now)
                    expired.push_back(id);
        for (auto id : expired)
            if (doRemoveById(id))
                ++metrics.expiredDocs;
        if (!expired.empty())
            maybeEvict();
        return expired.size();
    }

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
    std::unordered_map<DocumentId, std::uint64_t> spillIndex;
    std::uint64_t spillBytes = 0;
    std::atomic<std::size_t> txLocks{0}; // >0 waehrend Transaktion: kein Evict

    // Copy-on-Write-Undo fuer Transaktionen (v0.19): statt Voll-Backup nur angefasste Docs
    struct TxUndo {
        std::unordered_map<DocumentId, std::optional<Document>> docs; // alter Zustand (nullopt = gab es nicht)
        bool cleared = false;
        std::unordered_map<DocumentId, Document> clearedDocs;         // nur bei Clear/Load waehrend Tx
        bool touchedExpiry = false;                          // FA-25: Rollback muss expiryCount neu zaehlen
    };
    std::atomic<TxUndo*> undo{nullptr}; // v0.44 (M2): atomic – wird unter shared_lock gelesen (Sweep) und unter unique_lock geschrieben (Tx)

    void noteUndo(DocumentId id) {
        if (auto* u = undo.load(); u && u->docs.count(id) == 0) {
            auto it = docs.find(id);
            u->docs[id] = it != docs.end() ? std::optional<Document>(it->second) : std::nullopt;
        }
    }
    void noteUndoClear() {
        if (auto* u = undo.load(); u && !u->cleared) {
            u->cleared = true;
            // Bereits per noteUndo erfasste IDs ueberlagern wir beim Restore separat
            for (const auto& [id, doc] : docs) {
                if (hasExpiryField(doc))
                    u->touchedExpiry = true;
                if (u->docs.count(id) == 0)
                    u->clearedDocs.emplace(id, doc);
            }
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

    /// Zeiger auf das Doc OHNE Kopie, wenn es im RAM liegt; Spill-Docs landen in `tmp`
    /// (Lebensdauer beim Aufrufer). nullptr = Doc existiert nicht. Read-Hotpath (count/findIds/
    /// aggregate/...): spart die Dokument-Kopie pro Kandidat.
    const Document* docForEval(DocumentId id, Document& tmp) const {
        auto it = docs.find(id);
        if (it != docs.end())
            return &it->second;
        if (!spillIndex.count(id))
            return nullptr;
        tmp = *loadSpilled(id);
        return &tmp;
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
        const std::size_t excess = docs.size() > maxInMemory ? docs.size() - maxInMemory : 0;
        if (excess == 0)
            return;
        // niedrigste IDs zuerst (FIFO) – docs ist eine Hash-Map, daher explizit sortieren
        std::vector<DocumentId> ids;
        ids.reserve(docs.size());
        for (const auto& [id, d] : docs)
            ids.push_back(id);
        std::partial_sort(ids.begin(), ids.begin() + static_cast<std::ptrdiff_t>(excess), ids.end());
        for (std::size_t i = 0; i < excess; ++i) {
            auto it = docs.find(ids[i]);
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
            std::unordered_map<DocumentId, std::uint64_t> newIndex;
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
            atomicAdd(metrics.totalQueryMs, ms);
            lastCandidateCount = ids.size();
        }
        return ids;
    }

    // v0.39: optionale Collection-Limits (FA-28), Default 0 = unbegrenzt
    CollectionLimits limits;

    void checkCountLimit(std::size_t adding) const {
        if (limits.maxDocuments > 0 && doSize() + adding > limits.maxDocuments) {
            ++metrics.limitViolations;
            throw LimitError("Collection '" + name + "': Dokument-Limit erreicht (max " +
                             std::to_string(limits.maxDocuments) + ", +" + std::to_string(adding) + " angefragt)");
        }
    }
    void checkDocLimits(const Document& d) const {
        if (limits.maxFieldsPerDocument > 0 && d.fieldCount() > limits.maxFieldsPerDocument) {
            ++metrics.limitViolations;
            throw LimitError("Collection '" + name + "': Feld-Limit pro Dokument ueberschritten (max " +
                             std::to_string(limits.maxFieldsPerDocument) + ", Doc hat " +
                             std::to_string(d.fieldCount()) + ")");
        }
        if (limits.maxDocumentBytes > 0) {
            std::string js = detail::writeJsonDocument(d);
            if (js.size() > limits.maxDocumentBytes) {
                ++metrics.limitViolations;
                throw LimitError("Collection '" + name + "': Dokument zu gross (max " +
                                 std::to_string(limits.maxDocumentBytes) + " Bytes, Doc hat " +
                                 std::to_string(js.size()) + ")");
            }
        }
    }

    DocumentId doInsert(Document doc) {
        checkCountLimit(1);
        checkDocLimits(doc);
        DocumentId id(nextId++);
        doc.setId(id);
        applyDefaultExpiry(doc); // FA-25: Collection-Default-TTL (opt-in)
        validateDoc(doc);
        validateUnique(id, doc);
        noteUndo(id);
        auto [it, ok] = docs.emplace(id, doc);
        indexAdd(id, it->second);
        adjustExpiry(nullptr, &it->second);
        emit(ChangeType::Insert, id, it->second);
        ++metrics.inserts;
        maybeEvict();
        return id;
    }

    std::vector<DocumentId> doInsertBulk(std::vector<Document> docs_) {
        checkCountLimit(docs_.size());          // v0.39: vor jeder Mutation
        for (const auto& d : docs_)
            checkDocLimits(d);                   // v0.39: Bulk atomar gegen Limit-Verstoss
        std::vector<DocumentId> ids;
        ids.reserve(docs_.size());
        docs.reserve(docs.size() + docs_.size());
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

    // v0.35: Eval-Fehler pro Dokument (z. B. throwende eigene DSL-Funktion via Registry,
    // Regex-Match-Budget) duerfen einen Scan/Update nicht abbrechen -> Dokument zaehlt als
    // Nicht-Treffer, der Fehler wird in metrics.evalErrors gezaehlt. Aufruf an expliziten
    // Einzelstellen (Expression::evaluate) bleibt dagegen throwend.
    bool safeEval(const Expression& q, const Document& d) const {
        try {
            return q.evaluate(d);
        } catch (const std::exception& e) {
            ++metrics.evalErrors;
            MessageHandler::instance().error("query-eval", e.what()); // v0.36: melden an MessageHandler
            return false;
        } catch (...) {
            ++metrics.evalErrors;
            MessageHandler::instance().error("query-eval", "unbekannter Fehler (keine std::exception)");
            return false;
        }
    }

    Cursor doFind(const Expression& q) const {
        auto ids = candidates(q);
        lastCandidateCount = ids.size();
        std::vector<Document> result;
        for (auto id : ids) {
            if (!hasDoc(id))
                continue;
            Document d = loadDoc(id);
            if (safeEval(q, d))
                result.push_back(std::move(d));
        }
        return Cursor(std::move(result));
    }

    std::optional<Document> doFindOne(const Expression& q) const {
        auto ids = candidates(q);
        lastCandidateCount = ids.size();
        Document tmp;
        for (auto id : ids) {
            const Document* d = docForEval(id, tmp);
            if (d && safeEval(q, *d))
                return *d;
        }
        return std::nullopt;
    }

    std::size_t doCount(const Expression& q) const {
        auto ids = candidates(q);
        lastCandidateCount = ids.size();
        std::size_t n = 0;
        Document tmp;
        for (auto id : ids) {
            const Document* d = docForEval(id, tmp);
            if (d && safeEval(q, *d))
                ++n;
        }
        return n;
    }

    std::vector<DocumentId> doFindIds(const Expression& q) const {
        auto ids = candidates(q);
        lastCandidateCount = ids.size();
        std::vector<DocumentId> out;
        out.reserve(ids.size());
        Document tmp;
        for (auto id : ids) {
            const Document* d = docForEval(id, tmp);
            if (d && safeEval(q, *d))
                out.push_back(id);
        }
        return out;
    }

    std::size_t doForEach(const Expression& q, const std::function<void(const Document&)>& cb) const {
        auto ids = candidates(q);
        lastCandidateCount = ids.size();
        std::size_t n = 0;
        Document tmp;
        for (auto id : ids) {
            const Document* d = docForEval(id, tmp);
            if (d && safeEval(q, *d)) {
                cb(*d);
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
            if (!safeEval(q, updated))
                continue;
            mutator(updated);
            updated.setId(id);
            checkDocLimits(updated); // v0.39
            validateDoc(updated);
            validateUnique(id, updated);
            changes.emplace_back(id, std::move(updated));
        }
        for (auto& [id, updated] : changes) {
            auto it = docs.find(id);
            if (it != docs.end()) {
                noteUndo(id);
                adjustExpiry(&it->second, &updated);
                indexRemove(id, it->second);
                it->second = std::move(updated);
                indexAdd(id, it->second);
                emit(ChangeType::Update, id, it->second);
            } else { // ausgelagert: neue Version ans Spill anhaengen
                Document old = loadDoc(id);
                adjustExpiry(&old, &updated);
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
            Document old = *loadSpilled(id);
            Document updated = old;
            mutator(updated);
            updated.setId(id);
            checkDocLimits(updated); // v0.39
            validateDoc(updated);
            adjustExpiry(&old, &updated);
            spillAppend(id, updated); // neue Version ans Spill
            emit(ChangeType::Update, id, updated);
            ++metrics.updates;
            return true;
        }
        Document updated = it->second;
        mutator(updated);
        updated.setId(id);
        checkDocLimits(updated); // v0.39
        validateDoc(updated);
        validateUnique(id, updated);
        adjustExpiry(&it->second, &updated);
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
            Document old = *loadSpilled(id);
            doc.setId(id);
            checkDocLimits(doc); // v0.39
            validateDoc(doc);
            adjustExpiry(&old, &doc);
            spillAppend(id, doc);
            emit(ChangeType::Replace, id, doc);
            ++metrics.updates;
            return true;
        }
        doc.setId(id);
        checkDocLimits(doc); // v0.39
        validateDoc(doc);
        validateUnique(id, doc);
        adjustExpiry(&it->second, &doc);
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
            checkDocLimits(doc); // v0.39
            validateDoc(doc);
            validateUnique(id, doc);
            adjustExpiry(&it->second, &doc);
            indexRemove(id, it->second);
            it->second = std::move(doc);
            indexAdd(id, it->second);
            emit(ChangeType::Update, id, it->second);
            ++metrics.updates;
            return id;
        }
        if (spillIndex.count(id)) { // ausgelagert: im Spill aktualisieren
            Document old = *loadSpilled(id);
            doc.setId(id);
            checkDocLimits(doc); // v0.39
            validateDoc(doc);
            adjustExpiry(&old, &doc);
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
            if (it != docs.end() && safeEval(q, it->second))
                return doUpsertById(id, std::move(doc));
        }
        return doInsert(std::move(doc));
    }

    std::size_t doRemove(const Expression& q) {
        auto ids = candidates(q, false);
        std::vector<DocumentId> toRemove;
        Document tmp;
        for (auto id : ids) {
            const Document* d = docForEval(id, tmp);
            if (d && safeEval(q, *d))
                toRemove.push_back(id);
        }
        for (auto id : toRemove) {
            auto it = docs.find(id);
            if (it != docs.end()) {
                noteUndo(id);
                adjustExpiry(&it->second, nullptr);
                indexRemove(id, it->second);
                emit(ChangeType::Remove, id, it->second);
                docs.erase(it);
            } else if (spillIndex.count(id)) {
                Document d = *loadSpilled(id);
                adjustExpiry(&d, nullptr);
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
            adjustExpiry(&it->second, nullptr);
            indexRemove(id, it->second);
            emit(ChangeType::Remove, id, it->second);
            docs.erase(it);
            ++metrics.removes;
            return true;
        }
        if (spillIndex.count(id)) {
            Document d = *loadSpilled(id);
            adjustExpiry(&d, nullptr);
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
        Document tmp;
        for (auto id : candidates(q)) {
            const Document* d = docForEval(id, tmp);
            if (!d || !safeEval(q, *d))
                continue;
            acc.addDoc();
            acc.addValue(d->findPath(fieldPath));
        }
        return acc.result();
    }

    std::vector<GroupAggregate> doGroupBy(const std::string& keyPath, const Expression& q, AggOp op,
                                           const std::string& aggPath) const {
        std::map<Value, Accumulator, ValueLess> groups;
        Document tmp;
        for (auto id : candidates(q)) {
            const Document* d = docForEval(id, tmp);
            if (!d || !safeEval(q, *d))
                continue;
            const Value* k = d->findPath(keyPath);
            Value key = k ? *k : Value(nullptr);
            auto& acc = groups.try_emplace(key, op).first->second;
            acc.addDoc();
            acc.addValue(d->findPath(aggPath));
        }
        std::vector<GroupAggregate> out;
        out.reserve(groups.size());
        for (auto& [k, acc] : groups)
            out.push_back(GroupAggregate{k, acc.count, op == AggOp::Count ? std::nullopt : acc.result()});
        return out;
    }

    // Foundation v0.27: distinct Werte eines Feldes. Index-aware: bei match-all-Query
    // und Index auf dem Feld direkt aus den Index-Entries (O(#distinct) statt O(n)).
    std::vector<Value> doDistinctValues(const std::string& fieldPath, const Expression& q) const {
        std::set<Value, ValueLess> uniq;
        if (q.isMatchAll()) {
            auto it = indexes.find(fieldPath);
            if (it != indexes.end()) {
                auto t0 = std::chrono::steady_clock::now();
                std::size_t n = 0;
                for (const auto& [v, ids] : it->second->entries) {
                    uniq.insert(v);
                    n += ids.size();
                }
                double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                ++metrics.queries;
                metrics.scannedDocs += n;
                ++metrics.indexQueries;
                metrics.lastUsedIndex = true;
                metrics.lastQueryMs = ms;
                atomicAdd(metrics.totalQueryMs, ms);
                lastCandidateCount = n;
                return std::vector<Value>(uniq.begin(), uniq.end());
            }
        }
        Document tmp;
        for (auto id : candidates(q)) {
            const Document* d = docForEval(id, tmp);
            if (!d || !safeEval(q, *d))
                continue;
            if (const Value* v = d->findPath(fieldPath))
                uniq.insert(*v);
        }
        return std::vector<Value>(uniq.begin(), uniq.end());
    }

    // Foundation v0.27: reproduzierbares Zufalls-Sample von IDs (Reservoir-Sampling,
    // Algorithm R). Gleicher seed -> gleiches Sample. maxN >= size() -> alle IDs.
    std::vector<DocumentId> doSample(std::size_t maxN, std::uint64_t seed) const {
        auto t0 = std::chrono::steady_clock::now();
        std::vector<DocumentId> all;
        all.reserve(docs.size() + spillIndex.size());
        for (const auto& [id, d] : docs)
            all.push_back(id);
        for (const auto& [id, off] : spillIndex)
            all.push_back(id);
        std::vector<DocumentId> res;
        if (maxN == 0)
            res = {};
        else if (all.size() <= maxN)
            res = std::move(all);
        else {
            std::mt19937_64 rng(seed);
            res.assign(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(maxN));
            for (std::size_t i = maxN; i < all.size(); ++i) {
                std::uniform_int_distribution<std::size_t> dist(0, i);
                std::size_t j = dist(rng);
                if (j < maxN)
                    res[j] = all[i];
            }
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        ++metrics.queries;
        metrics.scannedDocs += all.size();
        metrics.lastUsedIndex = false;
        metrics.lastQueryMs = ms;
        atomicAdd(metrics.totalQueryMs, ms);
        lastCandidateCount = res.size();
        return res;
    }

    void doClear() {
        noteUndoClear();
        docs.clear();
        nextId = 1;
        expiryCount = 0;
        for (auto& [path, idx] : indexes)
            idx->entries.clear();
        if (overflowEnabled) {
            spillIndex.clear();
            if (undo.load() == nullptr) {
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
        // v0.35: erst vollstaendig validieren, dann mutieren -> bei kaputtem Element bleibt
        // die Collection unveraendert (kein geleerter/halbgeladener Zustand), der Aufrufer
        // kann den Fehler behandeln und den Flow fortsetzen.
        for (const auto& item : items)
            if (!item.isDocument()) {
                std::string msg = "Collection-JSON: Element ist kein Objekt";
                MessageHandler::instance().error("load", msg); // v0.36: melden + weiterhin werfen
                throw JsonError(std::move(msg));
            }
        // v0.39: Limits vor dem Leeren prüfen (neuer Inhalt ersetzt den alten)
        if (limits.maxDocuments > 0 && items.size() > limits.maxDocuments) {
            ++metrics.limitViolations;
            throw LimitError("Collection '" + name + "': loadJson wuerde Dokument-Limit ueberschreiten (max " +
                             std::to_string(limits.maxDocuments) + ", " + std::to_string(items.size()) + " Elemente)");
        }
        for (const auto& item : items)
            checkDocLimits(*item.asDocument());
        noteUndoClear();
        docs.clear();
        docs.reserve(items.size());
        nextId = 1;
        expiryCount = 0;
        for (auto& [path, idx] : indexes)
            idx->entries.clear();
        if (overflowEnabled) {
            spillIndex.clear();
            if (undo.load() == nullptr) {
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
            // isDocument() wurde oben bereits fuer alle Elemente geprueft
            assert(item.isDocument());
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
            adjustExpiry(nullptr, &docs.at(id));
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
        if (limits.maxIndexes > 0 && indexes.size() >= limits.maxIndexes) { // v0.39
            ++metrics.limitViolations;
            throw LimitError("Collection '" + name + "': Index-Limit erreicht (max " +
                             std::to_string(limits.maxIndexes) + ")");
        }
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
