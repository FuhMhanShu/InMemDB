// Tests v0.44 (M2): Nebenlaeufigkeits-Stresstest auf Collection-Ebene (FA-16).
// Parallele Reader (find/count/findById/aggregate/forEach) gegen Writer (insert/update/remove,
// teils in Transaktionen) + Observer – unter TSan (CI-Job "TSan") darf es keine Data-Races geben,
// das Ergebnis muss konsistent sein. Läuft auch normal im ctest.
#include <memdb/memdb.hpp>

#include <atomic>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;
#define CHECK(cond)                                                                              \
    do {                                                                                         \
        ++g_checks;                                                                              \
        if (!(cond)) {                                                                           \
            ++g_failures;                                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ")\n";        \
        }                                                                                        \
    } while (0)

using namespace memdb;

} // namespace

int main() {
    constexpr int kWriters = 4;
    constexpr int kReaders = 4;
    constexpr int kOpsPerWriter = 250;

    Database db("stress");
    auto c = db.createCollection("c");
    c.createIndex("k");        // Bereichs-Index
    c.createIndex("u", true);   // Unique-Index (Wettbewerb um Duplikate -> UniqueConstraintError erwartet)

    std::atomic<long> gInserted{0}, gRemoved{0}, gUniqueViolations{0}, gEvents{0};
    std::atomic<bool> stop{false};
    std::atomic<long> gReadQueries{0};

    auto h = c.onChange([&](const ChangeEvent&) { ++gEvents; });

    // Writer: eigene Schluessel-Raeume, teils Transaktionen (commit/rollback)
    auto writer = [&](int w) {
        for (int i = 0; i < kOpsPerWriter; ++i) {
            const int key = w * 100000 + i;
            switch (i % 5) {
                case 0: case 1: { // plain insert
                    c.insert({{"k", key}, {"u", key}, {"v", i}});
                    ++gInserted;
                    break;
                }
                case 2: { // insert in Transaktion + commit
                    auto tx = c.beginTransaction();
                    tx.insert({{"k", key}, {"u", key}, {"v", i}});
                    tx.commit();
                    ++gInserted;
                    break;
                }
                case 3: { // update eigenes Doc (falls vorhanden)
                    auto doc = c.findOne(Field("k") == key - 1);
                    if (doc)
                        c.updateById(doc->id(), [](Document& d) { d.set("v", 999); });
                    break;
                }
                default: { // remove eigenes Doc (falls vorhanden); Duplikat-u -> UniqueConstraintError zaehlen
                    auto doc = c.findOne(Field("k") == key - 2);
                    if (doc) {
                        c.removeById(doc->id());
                        ++gRemoved;
                    }
                    try {
                        c.insert({{"k", key}, {"u", -1}, {"v", -1}}); // u=-1 kollidiert -> Violation
                        ++gInserted;
                    } catch (const UniqueConstraintError&) {
                        ++gUniqueViolations;
                    }
                    break;
                }
            }
        }
    };

    // Reader: mischt Lesezugriffe (begrenzte Iterationen + yield, damit Writer nicht verhungern)
    auto reader = [&](int) {
        for (int it = 0; it < 400 && !stop.load(std::memory_order_relaxed); ++it) {
            ++gReadQueries;
            c.count(Field("k") >= 0);
            c.count("k >= 0 && v >= 0");
            std::size_t n = 0;
            for (const auto& d : c.find(Field("k") < 1000000))
                ++n;
            c.aggregate(AggOp::Sum, "v");
            auto one = c.findOne(Field("k") == 12345);
            (void)one;
            c.size();
            std::this_thread::yield();
        }
    };

    std::vector<std::thread> threads;
    for (int r = 0; r < kReaders; ++r)
        threads.emplace_back(reader, r);
    std::vector<std::thread> writers;
    for (int w = 0; w < kWriters; ++w)
        writers.emplace_back(writer, w);
    for (auto& t : writers)
        t.join();
    stop = true;
    for (auto& t : threads)
        t.join();

    // Konsistenz: size == inserted - removed; Metriken/Observer haben gearbeitet
    CHECK(c.size() == static_cast<std::size_t>(gInserted.load() - gRemoved.load()));
    CHECK(gReadQueries.load() > 0);
    CHECK(c.metrics().queries > 0);
    CHECK(gEvents.load() > 0);
    CHECK(c.metrics().changeEvents == static_cast<std::uint64_t>(gEvents.load()));
    CHECK(gUniqueViolations.load() > 0); // Unique-Index wurde unter Nebenlaeufigkeit wirklich geprueft

    // Nach dem Sturm: Index-Abfrage liefert konsistentes Ergebnis
    const std::size_t viaIndex = c.count(Field("k") >= 0);
    std::size_t viaScan = 0;
    for (const auto& d : c.find(Expression{}))
        ++viaScan;
    CHECK(viaIndex == viaScan);
    CHECK(viaIndex == c.size());

    c.offChange(h);
    std::cout << g_checks << " Checks, " << g_failures << " Fehler\n";
    return g_failures == 0 ? 0 : 1;
}
