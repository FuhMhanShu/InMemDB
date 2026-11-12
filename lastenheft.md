# Lastenheft

## In-Memory Non-Relational Database System (C++, objektorientiert)

| Feld | Wert |
|---|---|
| Projekttitel | MemDoc (Arbeitsname) – In-Memory Document Store |
| Version | 0.1 (Entwurf) |
| Status | In Abstimmung |
| Sprache | Deutsch |
| Technologie | C++ (mindestens C++17, C++20 wünschenswert), CMake |
| Bereitstellungsform | Eingebettete Bibliothek (header-only oder statische Lib), kein Server, kein Netzwerk |

---

## 1. Zielbestimmung

### 1.1 Produktzweck
MemDoc ist ein **in-memory, non-relationales (dokumentenorientiertes) Datenbanksystem**, das als C++-Bibliothek in Anwendungen eingebettet wird. Kernmerkmal: **Dokumente besitzen keine feste Struktur – Felder sind jederzeit und pro Dokument beliebig erweiterbar, änderbar und löschbar** (schema-less / schemalos zur Laufzeit).

### 1.2 Nutzen
- Schnelle prototypische und produktive Datenhaltung ohne Schema-Definition oder Migrationen.
- Volle Geschwindigkeit des Arbeitsspeichers (keine Festplatten-I/O im Normalbetrieb).
- Natürliche Abbildung hierarchischer, verschachtelter Daten (Objekte, Listen) ohne JOINs.
- Typsichere, moderne C++-API trotz dynamischer Felder (Laufzeit-Typprüfung).

### 1.3 Abgrenzung (Nicht-Ziele)
- Kein relationales Modell, **kein SQL**, keine Tabellen/Joins im klassischen Sinn.
- Keine Persistenz als Primärziel (optionaler Snapshot/Export als Kann-Anforderung).
- Kein Netzwerkzugriff, kein Client/Server, keine verteilte Replikation, kein Multi-User-Betrieb über Prozesse hinweg.

---

## 2. Produktumfeld / Systemkontext

- **Einsatz**: als Library in C++-Anwendungen (Desktop, Server-seitige Dienste, eingebettete Systeme, Tests).
- **Plattformen**: Linux, Windows, macOS; Compiler: GCC, Clang, MSVC.
- **Randbedingungen**: Single-Process; Daten liegen ausschließlich im RAM und gehen beim Prozessende verloren (außer Snapshot).
- **Keine externen Pflichtabhängigkeiten** im Kern; optionale Abhängigkeiten nur für JSON-Serialisierung und Tests.

---

## 3. Datenmodell

| Konzept | Beschreibung |
|---|---|
| **Database** | Oberste Einheit; enthält benannte Collections. |
| **Collection** | Benannte Menge von Dokumenten (analog zu einer Tabelle, aber ohne Schema). |
| **Document** | Eine Entität als Menge von **Feld → Wert**-Paaren. Jedes Dokument hat eine automatisch vergebene, eindeutige **ID**. |
| **Feld (Field)** | Schlüssel (String) + Wert. Felder sind **pro Dokument individuell**: Hinzufügen, Ändern des Typs, Umbenennen und Entfernen jederzeit möglich, auch nachträglich für bestehende Dokumente. |
| **Wert (Value)** | Dynamischer Typ pro Feld: `null`, `bool`, `int64`, `double`, `string`, `binary`, verschachteltes **Document**, **Array** (Liste von Values). Der Typ eines Feldes kann sich von Dokument zu Dokument und über die Zeit unterscheiden. |
| **Verschachtelung** | Dokumente und Arrays können beliebig tief verschachtelt sein; Zugriff über Punktnotation (z. B. `adresse.stadt`). |
| **Schema** | Es gibt **kein verbindliches Schema**. Optional (Kann): pro Collection ein Validierungsschema, das neue/aktualisierte Dokumente prüft. |

---

## 4. Funktionale Anforderungen

Prioritäten: **M** = Muss, **S** = Soll, **K** = Kann.

| ID | Anforderung | Prio |
|---|---|---|
| FA-01 | Database instanziieren, benennen, schließen; alle Daten werden beim Schließen verworfen. | M |
| FA-02 | Collections anlegen, auflisten, umbenennen, löschen (inkl. aller Dokumente). | M |
| FA-03 | Dokument in Collection **einfügen**; automatische, eindeutige ID-Vergabe; Rückgabe der ID. | M |
| FA-04 | Dokument **nach ID lesen** (`findById`); optional: Version/Timestamp des Dokuments. | M |
| FA-05 | **Abfragen (Query)** über Collections: Vergleichsoperatoren (`=`, `!=`, `<`, `<=`, `>`, `>=`), logische Verknüpfung (`AND`, `OR`, `NOT`), Textoperatoren (z. B. `contains`, Präfix), Array-Operatoren (enthält Element), Zugriff auf verschachtelte Felder via Punktnotation. | M |
| FA-06 | **Felder dynamisch verwalten**: Feld setzen/überschreiben (auch mit neuem Typ), Feld löschen, Feld umbenennen – einzeln oder im Batch, für neue und bestehende Dokumente. | M |
| FA-07 | Dokument **aktualisieren** (feldweise, partiell) und **ersetzen** (komplett); **Upsert** (insert-or-update nach ID oder Query). | M |
| FA-08 | Dokument(e) **löschen**: nach ID, nach Query; Rückgabe der Anzahl betroffener Dokumente. | M |
| FA-09 | **Bulk-Operationen**: mehrere Dokumente in einem Aufruf einfügen/löschen/aktualisieren. | S |
| FA-10 | **Indizes** pro Collection und Feld: anlegen, auflisten, löschen; beschleunigte Gleichheits-/Bereichsabfragen. | S |
| FA-11 | **Iteration** über alle Dokumente einer Collection (Iterator/Cursor), inkl. Gefilterter mit Query. | M |
| FA-12 | **Transaktionen** auf Collection-Ebene: mehrere Änderungen atomar (alles oder nichts), mit Rollback. | S |
| FA-13 | **Aggregationen**: `count`, `min`, `max`, `sum`, `avg` über Felder; optional `groupBy`. | K |
| FA-14 | **Snapshot/Export und Import** der gesamten Database oder einzelner Collections in ein serialisiertes Format (JSON oder binär), um In-Memory-Daten zu sichern/wiederherzustellen. | K |
| FA-15 | **Beobachtung**: Callback/Observer, der bei insert/update/delete einer Collection benachrichtigt wird. | K |
| FA-16 | **Thread-Safety**: parallele Lesezugriffe ohne gegenseitigen Ausschluss; Schreibzugriffe serialisiert (RW-Lock); optional konsistente Snapshot-Isolation innerhalb einer Transaktion. | S |
| FA-17 | **Objektorientierte C++-API** (siehe Abschnitt 6): Klassen `Database`, `Collection`, `Document`, `Value`, `Query`, `Index`; RAII, Smart Pointer, `std::optional`, Exceptions statt Fehlercodes. | M |

---

## 5. Nicht-funktionale Anforderungen

| ID | Anforderung | Prio |
|---|---|---|
| NFA-01 | **Performance**: `findById` und indexierte Lookups in O(1)–O(log n); ungefilterte Scans O(n) (akzeptabel für In-Memory). Typische Einzeloperationen im Mikrosekundenbereich. | M |
| NFA-02 | **Speicher**: ausschließlich RAM; Zielordnung: Millionen Dokumente mit typischer Größe (einige KB) auf handelsüblicher Hardware. | S |
| NFA-03 | **Erweiterbarkeit**: neue Werttypen durch Ableitung von einer `Value`-Basisklasse registrierbar; neue Query-Operatoren erweiterbar; keine Änderung am Kern für Erweiterungen nötig (Open/Closed). | M |
| NFA-04 | **API-Ergonomie**: intuitive, moderne C++-Syntax (z. B. Initializer-Listen für Dokumente, `std::string_view`, `std::optional`); sprechende Methodennamen. | M |
| NFA-05 | **Zuverlässigkeit/Speicher**: keine Lecks (RAII, Smart Pointer), starke Exception-Garantie bei fehlgeschlagenen Updates; ungültige Zugriffe werfen typisierte Exceptions. | M |
| NFA-06 | **Portabilität & Build**: plattformunabhängiger Code, Build über CMake; Kern ohne externe Pflichtabhängigkeiten (header-only oder statische Lib). | M |
| NFA-07 | **Testbarkeit**: Unit-Tests für alle Kernpfade (Empfehlung: Catch2 oder GoogleTest als Dev-Dependency); Abdeckung der CRUD-, Query- und Nebenläufigkeitsfälle. | S |
| NFA-08 | **Dokumentation**: Doxygen-kommentierte öffentliche API, README mit Quickstart, mindestens ein lauffähiges Beispiel. | S |
| NFA-09 | **Lizenz**: projektverträgliche Open-Source-Lizenz (z. B. MIT). | K |

---

## 6. Schnittstellen

### 6.1 C++-API (Kern)
Namespace `memdb`. Wesentliche Klassen:

| Klasse | Verantwortung |
|---|---|
| `memdb::Database` | Einstiegspunkt; verwaltet Collections; Transaktions-Kontext. |
| `memdb::Collection` | CRUD für Dokumente; Query-Ausführung; Indexverwaltung; Transaktionen. |
| `memdb::Document` | Feld-Container (Key → `Value`); dynamische Felder; ID; verschachtelte Dokumente. |
| `memdb::Value` | Varianten-Typ (`std::variant`-basiert) für alle Feldtypen inkl. Erweiterbarkeit. |
| `memdb::Query` / `Expression` | Typsichere, baumartige Abfrageausdrücke (Operatoren erweiterbar). |
| `memdb::Cursor` / `Iterator` | Sequentielle/ gefilterte Iteration über Dokumente. |
| `memdb::Index` | Sekundärindex pro Feld. |
| `memdb::Transaction` | Atomare Ausführung mehrerer Änderungen. |
| `memdb::DocumentId` | Typsicherer Dokument-Identifikator. |

### 6.2 Serialisierung (Kann)
- Export/Import als JSON (über optionale Abhängigkeit, z. B. nlohmann/json) oder kompaktes binäres Format.
- Optionaler Validierungs-Hook pro Collection (Schema-Check bei insert/update).

### 6.3 Explizit keine Schnittstellen
- Kein SQL-Parser, kein Netzwerkprotokoll, keine CLI (Out of Scope).

---

## 7. Qualitätsanforderungen

- Automatisierte Unit- und Integrationstests in CI (GitHub Actions o. Ä.) für alle unterstützten Plattformen/Compiler.
- Code-Reviews vor Merge; statische Analyse (z. B. clang-tidy) empfohlen.
- Doxygen-Dokumentation der öffentlichen API; README mit Build- und Nutzungsanleitung.
- Beispiele: CRUD-Grundlagen, dynamische Felder, verschachtelte Queries, Transaktion, Snapshot.

---

## 8. Randbedingungen

- Sprache: C++17 minimum, C++20-Features wo sinnvoll.
- Build-System: CMake ≥ 3.20.
- Keine Pflichtabhängigkeiten im Kern; Dev-Dependencen (Test-Framework, JSON) nur für Tests/optionale Features.
- Entwicklung auf Linux; Portierung auf Windows/macOS vorgesehen.

---

## 9. Risiken

| Risiko | Auswirkung | Gegenmaßnahme |
|---|---|---|
| Datenverlust bei Prozessende (In-Memory) | Hoch für produktive Daten | Snapshot/Export (FA-14) dokumentieren und früh umsetzen |
| Unbegrenzter RAM-Verbrauch bei großen Datenmengen | Absturz bei Erschöpfung | Speicherüberwachung, dokumentierte Kapazitätsgrenzen; später Out-of-Core als Ausblick |
| Typsicherheit bei dynamischen Feldern | Laufzeitfehler in der Anwendung | Strikte Laufzeit-Typprüfung in `Value`, typisierte Exceptions, optional Validierungsschema |
| Komplexität der Nebenläufigkeit | Dateninkonsistenz, Deadlocks | Klare RW-Locking-Strategie, kurze kritische Abschnitte, Tests unter `TSan` |
| API-Drift bei Erweiterbarkeit | Breaking Changes | Stabile Basisklassen, Erweiterungen über Ableitung/Registrierung |

---

## 10. Ausblick (nicht in v1)

- Persistente Speicherung (Append-only-Log, Memory-Mapped Files).
- Eingebettete Referenzen zwischen Dokumenten (statt Kopie) mit optionalem "Join" bei Abfrage.
- Out-of-Core-/Hybrid-Modus für Daten größer als RAM.
- Netzwerk-Schnittstelle / Server-Modus.

---

## 11. Glossar

| Begriff | Bedeutung |
|---|---|
| Dokument | Eine Entität als Menge von Feld-Wert-Paaren mit eindeutiger ID. |
| Collection | Benannte, schema-lose Menge von Dokumenten. |
| Feld | Schlüssel-Wert-Paar innerhalb eines Dokuments; jederzeit erweiterbar. |
| Value | Dynamisch typisierter Wert eines Feldes. |
| Query | Abfrageausdruck zur Selektion von Dokumenten. |
| Index | Datenstruktur zur Beschleunigung von Abfragen auf einem Feld. |
| Snapshot | Serialisierter Zustand der Database zum Sichern/Wiederherstellen. |
| In-Memory | Datenhaltung ausschließlich im Arbeitsspeicher. |
| Schema-less | Kein vorab definiertes Struktur-Schema; Struktur ergibt sich aus den Daten. |
