// Tests: Gemischte Metadaten-Analyse (FA-33, Kategorie 2): dominanter Typ je Feld,
// Zahlen-Kennzahlen, Text-Klassen (kategorisch/Kennung/Freitext), Format-Hinweise
// (Datum/Hex/zahlartig), Boolean-Anteile, Typ-Drift, Missing-Quote, paarweise
// Assoziationen (Pearson, eta^2, Cramers V), Optionen/Guards, Reports.
#include <memdb/memdb.hpp>
#include <memdb/mixed_analysis.hpp>

#include <cmath>
#include <iostream>
#include <string>
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

const MixedFieldAnalysis* findField(const std::vector<MixedFieldAnalysis>& v,
                                    const std::string& name) {
    for (const auto& f : v)
        if (f.field == name)
            return &f;
    return nullptr;
}

const MixedPairAssociation* findAssoc(const std::vector<MixedPairAssociation>& v,
                                      const std::string& x,
                                      const std::string& y,
                                      const std::string& measure) {
    for (const auto& a : v)
        if (a.fieldX == x && a.fieldY == y && a.measure == measure)
            return &a;
    return nullptr;
}

bool near(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    // 1) Zahlenfeld in Mixed-Collection: Kennzahlen + Missing-Quote
    {
        Database db("m1");
        auto c = db.createCollection("personen");
        for (int i = 1; i <= 10; ++i)
            c.insert({{"alter", i}, {"name", "n" + std::to_string(i)}});
        c.insert({{"name", "ohne"}});
        MixedCollectionAnalysis res = analyzeMixed(c);
        CHECK(res.collection == "personen");
        CHECK(res.docCount == 11 && res.sampleSize == 11);
        CHECK(res.fieldCount == 2);
        const auto* alter = findField(res.fields, "alter");
        CHECK(alter != nullptr);
        CHECK(alter->kind == MixedKind::Number);
        CHECK(alter->count == 10 && alter->distinct == 10);
        CHECK(near(alter->min, 1.0) && near(alter->max, 10.0));
        CHECK(near(alter->mean, 5.5) && near(alter->median, 5.5));
        CHECK(near(alter->stddev, std::sqrt(8.25), 1e-12));
        CHECK(near(alter->integralRatio, 1.0));
        CHECK(alter->discrete);
        CHECK(alter->missing == 1);
        CHECK(near(alter->missingRatio, 1.0 / 11.0));
        CHECK(near(alter->dominantShare, 1.0));
        CHECK(alter->note.find("diskret") != std::string::npos);
        CHECK(alter->note.find("fehlt in 9 %") != std::string::npos);
        const auto* name = findField(res.fields, "name");
        CHECK(name != nullptr && name->kind == MixedKind::Text);
    }

    // 2) Kategorisches Textfeld
    {
        Database db("m2");
        auto c = db.createCollection("laender");
        const char* laender[] = {"DE", "AT", "CH"};
        for (int i = 0; i < 12; ++i)
            c.insert({{"land", laender[i % 3]}, {"wert", i}});
        MixedCollectionAnalysis res = analyzeMixed(c);
        const auto* land = findField(res.fields, "land");
        CHECK(land != nullptr);
        CHECK(land->kind == MixedKind::Text);
        CHECK(land->count == 12 && land->distinct == 3);
        CHECK(near(land->uniqueness, 0.25));
        CHECK(land->textClass == TextClass::Categorical);
        CHECK(near(land->emptyShare, 0.0));
        CHECK(land->lenMin == 2 && land->lenMax == 2 && near(land->lenMean, 2.0));
        CHECK(!land->dateLike && !land->hexLike && !land->numericLike);
        CHECK(land->note.find("kategorisch (3 Auspraegungen)") != std::string::npos);
    }

    // 3) Kennung: eindeutige Strings
    {
        Database db("m3");
        auto c = db.createCollection("refs");
        for (int i = 1; i <= 12; ++i)
            c.insert({{"ref", "ID-" + std::to_string(i)}, {"x", i}});
        MixedCollectionAnalysis res = analyzeMixed(c);
        const auto* ref = findField(res.fields, "ref");
        CHECK(ref != nullptr);
        CHECK(ref->kind == MixedKind::Text);
        CHECK(ref->count == 12 && ref->distinct == 12);
        CHECK(near(ref->uniqueness, 1.0));
        CHECK(ref->textClass == TextClass::Identifier); // Kennung schlägt Kategorisch
        CHECK(ref->lenMin == 4 && ref->lenMax == 5);
        CHECK(ref->note.find("Kennung (100 % einzigartig)") != std::string::npos);
    }

    // 4) Freitext: lang, Kardinalität über Schwelle, Wiederholungen
    {
        Database db("m4");
        auto c = db.createCollection("texte");
        const std::string base = "Freitext-Datensatz mit der laufenden Nummer ";
        for (int i = 1; i <= 20; ++i)
            c.insert({{"text", base + std::to_string((i - 1) % 14 + 1)}});
        MixedCollectionAnalysis res = analyzeMixed(c);
        const auto* text = findField(res.fields, "text");
        CHECK(text != nullptr);
        CHECK(text->kind == MixedKind::Text);
        CHECK(text->count == 20 && text->distinct == 14);
        CHECK(near(text->uniqueness, 0.7));
        CHECK(text->textClass == TextClass::FreeText);
        CHECK(text->lenMin == 45 && text->lenMax == 46);
        CHECK(near(text->lenMean, 45.25));
        CHECK(text->note.find("Freitext") != std::string::npos);
    }

    // 5) Format-Hinweise: Datum-, Hex-, zahlartige Strings, leere Strings
    {
        Database db("m5");
        auto daten = db.createCollection("daten");
        const char* ds[] = {"2024-03-01", "2024-03-02", "2025-11-17", "2026-01-05"};
        for (int i = 0; i < 8; ++i)
            daten.insert({{"tag", ds[i % 4]}});
        MixedCollectionAnalysis r1 = analyzeMixed(daten);
        const auto* tag = findField(r1.fields, "tag");
        CHECK(tag != nullptr);
        CHECK(tag->dateLike);
        CHECK(!tag->hexLike && !tag->numericLike);
        CHECK(tag->textClass == TextClass::Categorical);
        CHECK(tag->note.find("ISO-Datum-artig") != std::string::npos);

        auto hashe = db.createCollection("hashe");
        const char* hs[] = {"a1b2c3d4", "deadbeef", "cafe1234", "0f0f0f0f"};
        for (int i = 0; i < 8; ++i)
            hashe.insert({{"hash", hs[i % 4]}});
        MixedCollectionAnalysis r2 = analyzeMixed(hashe);
        const auto* hash = findField(r2.fields, "hash");
        CHECK(hash != nullptr);
        CHECK(hash->hexLike);
        CHECK(!hash->dateLike && !hash->numericLike);
        CHECK(hash->textClass == TextClass::Categorical);

        auto betraege = db.createCollection("betraege");
        const char* bs[] = {"12.5", "7", "-3.25", "0.5"};
        for (int i = 0; i < 8; ++i)
            betraege.insert({{"betrag", bs[i % 4]}});
        MixedCollectionAnalysis r3 = analyzeMixed(betraege);
        const auto* betrag = findField(r3.fields, "betrag");
        CHECK(betrag != nullptr);
        CHECK(betrag->numericLike);
        CHECK(!betrag->hexLike && !betrag->dateLike);
        CHECK(betrag->textClass == TextClass::Categorical);

        auto notizen = db.createCollection("notizen");
        for (int i = 0; i < 8; ++i)
            notizen.insert({{"notiz", i < 6 ? "x" : ""}});
        MixedCollectionAnalysis r4 = analyzeMixed(notizen);
        const auto* notiz = findField(r4.fields, "notiz");
        CHECK(notiz != nullptr);
        CHECK(near(notiz->emptyShare, 0.25));
        CHECK(!notiz->dateLike && !notiz->hexLike && !notiz->numericLike);
        CHECK(notiz->distinct == 2); // "x" und "" sind zwei Werte
        CHECK(notiz->note.find("kategorisch (2 Auspraegungen)") != std::string::npos);
        CHECK(notiz->note.find("leere Strings (25 %)") != std::string::npos);
    }

    // 6) Boolean-Feld
    {
        Database db("m6");
        auto c = db.createCollection("flags");
        for (int i = 0; i < 12; ++i)
            c.insert({{"aktiv", i < 8}, {"x", i}});
        MixedCollectionAnalysis res = analyzeMixed(c);
        const auto* aktiv = findField(res.fields, "aktiv");
        CHECK(aktiv != nullptr);
        CHECK(aktiv->kind == MixedKind::Boolean);
        CHECK(aktiv->count == 12 && aktiv->distinct == 2);
        CHECK(near(aktiv->trueShare, 8.0 / 12.0));
        CHECK(aktiv->note.find("Boolean (67 % true)") != std::string::npos);
    }

    // 7) Typ-Drift: dominanter Typ zählt, Abweichungen senken dominantShare
    {
        Database db("m7");
        auto c = db.createCollection("drift");
        for (int i = 0; i < 12; ++i) {
            if (i < 9)
                c.insert({{"x", 10.0 + i}, {"y", i}});
            else
                c.insert({{"x", "drift"}, {"y", i}});
        }
        for (int i = 0; i < 10; ++i)
            c.insert({{"t", i < 5 ? Value(i) : Value("s" + std::to_string(i))}});
        MixedCollectionAnalysis res = analyzeMixed(c);
        const auto* x = findField(res.fields, "x");
        CHECK(x != nullptr);
        CHECK(x->kind == MixedKind::Number);
        CHECK(x->count == 9);
        CHECK(near(x->dominantShare, 0.75));
        CHECK(x->note.find("Typ-Drift (25 % abweichend)") != std::string::npos);
        const auto* t = findField(res.fields, "t");
        CHECK(t != nullptr);
        CHECK(t->kind == MixedKind::Number); // Gleichstand: Zahl vor Text
        CHECK(t->count == 5);
        CHECK(near(t->dominantShare, 0.5));
    }

    // 8) Assoziationen: Pearson (Zahl/Zahl, Boolean/Zahl, Boolean/Boolean),
    //    konstante Felder ausgeschlossen
    {
        Database db("m8");
        auto c = db.createCollection("zahlen");
        for (int i = 1; i <= 12; ++i)
            c.insert({{"x", i}, {"y", 2 * i + 1}, {"b", i >= 7},
                      {"nb", i < 7}, {"konst", 5.0}});
        MixedCollectionAnalysis res = analyzeMixed(c);
        const auto* r1 = findAssoc(res.associations, "x", "y", "pearson");
        CHECK(r1 != nullptr);
        CHECK(r1 != nullptr && near(r1->value, 1.0, 1e-9));
        CHECK(r1 != nullptr && r1->pairs == 12);
        const auto* r2 = findAssoc(res.associations, "b", "x", "pearson");
        CHECK(r2 != nullptr);
        CHECK(r2 != nullptr && near(r2->value, 0.8690481892534817, 1e-9));
        const auto* r3 = findAssoc(res.associations, "b", "y", "pearson");
        CHECK(r3 != nullptr);
        CHECK(r3 != nullptr && near(r3->value, 0.8690481892534817, 1e-9));
        const auto* r4 = findAssoc(res.associations, "b", "nb", "pearson");
        CHECK(r4 != nullptr);
        CHECK(r4 != nullptr && near(r4->value, -1.0, 1e-9));
        bool konstPaar = false;
        for (const auto& a : res.associations)
            if (a.fieldX == "konst" || a.fieldY == "konst")
                konstPaar = true;
        CHECK(!konstPaar); // Varianz 0: konstantes Feld nie in Assoziationen
    }

    // 9) Assoziationen: eta^2 (kategorisch -> Zahl) und Cramers V (kategorisch <-> kategorisch)
    {
        Database db("m9");
        auto c = db.createCollection("eta");
        const char* laender[] = {"DE", "AT", "CH"};
        for (int i = 0; i < 12; ++i)
            c.insert({{"land", laender[i % 3]}, {"rabatt", i % 3 == 2 ? 0.8 : 0.9}});
        MixedCollectionAnalysis res = analyzeMixed(c);
        const auto* e1 = findAssoc(res.associations, "land", "rabatt", "etaSquared");
        CHECK(e1 != nullptr);
        CHECK(e1 != nullptr && near(e1->value, 1.0, 1e-9));
        CHECK(e1 != nullptr && e1->pairs == 12);

        auto c2 = db.createCollection("eta2");
        for (int i = 0; i < 6; ++i)
            c2.insert({{"g", i < 3 ? "A" : "B"}, {"wert", i < 3 ? 10 + i : 20 + i}});
        MixedCollectionAnalysis res2 = analyzeMixed(c2);
        const auto* e2 = findAssoc(res2.associations, "g", "wert", "etaSquared");
        CHECK(e2 != nullptr);
        CHECK(e2 != nullptr && near(e2->value, 253.5 / 257.5, 1e-9));

        auto c3 = db.createCollection("cv");
        const char* kats[] = {"A", "B", "C"};
        const char* codes[] = {"a", "b", "c"};
        for (int i = 0; i < 12; ++i)
            c3.insert({{"kat", kats[i % 3]}, {"code", codes[i % 3]}});
        MixedCollectionAnalysis res3 = analyzeMixed(c3);
        const auto* v1 = findAssoc(res3.associations, "code", "kat", "cramersV");
        CHECK(v1 != nullptr);
        CHECK(v1 != nullptr && near(v1->value, 1.0, 1e-9));

        auto c4 = db.createCollection("cv2");
        for (int i = 0; i < 12; ++i) {
            const bool a = i < 6;
            const bool y = (i >= 3 && i < 6); // a-x 3, a-y 3, b-x 6, b-y 0
            const bool x = !(a && y);
            c4.insert({{"r", a ? "a" : "b"}, {"c", x ? "x" : "y"}});
        }
        MixedCollectionAnalysis res4 = analyzeMixed(c4);
        const auto* v2 = findAssoc(res4.associations, "c", "r", "cramersV");
        CHECK(v2 != nullptr);
        CHECK(v2 != nullptr && near(v2->value, 0.5773502691896257, 1e-9));
    }

    // 10) Guards und Optionen: minValuesPerField, Cap, withAssociations, minPairs, Sampling
    {
        Database db("m10");
        auto c = db.createCollection("guards");
        for (int i = 0; i < 5; ++i) {
            if (i < 4)
                c.insert({{"gross", i}, {"klein", i}, {"dritt", i}});
            else
                c.insert({{"gross", i}, {"dritt", i}}); // klein fehlt im 5. Doc
        }
        MixedAnalysisOptions opts;
        auto fields = analyzeMixedFields(c, opts);
        CHECK(findField(fields, "gross") != nullptr);
        CHECK(findField(fields, "dritt") != nullptr);
        CHECK(findField(fields, "klein") == nullptr); // 4 Werte < minValuesPerField 5
        const auto* gross = findField(fields, "gross");
        CHECK(gross != nullptr && gross->missing == 0);

        MixedAnalysisOptions capOpts;
        capOpts.maxFieldsPerCollection = 2;
        auto capped = analyzeMixedFields(c, capOpts);
        CHECK(capped.size() == 2); // Cap in alphabetischer Erstsicht-Reihenfolge (std::map):
                                   // dritt, gross – klein als drittes Feld vom Cap ausgeschlossen
        CHECK(findField(capped, "gross") != nullptr);
        CHECK(findField(capped, "klein") == nullptr); // zudem 4 Werte < minValuesPerField 5
        CHECK(findField(capped, "dritt") != nullptr);

        MixedAnalysisOptions off;
        off.withAssociations = false;
        MixedCollectionAnalysis r = analyzeMixed(c, off);
        CHECK(r.associations.empty());
        CHECK(!r.fields.empty());

        auto c2 = db.createCollection("pairs");
        for (int i = 0; i < 5; ++i)
            c2.insert({{"p", i}});
        for (int i = 5; i < 10; ++i)
            c2.insert({{"q", i}});
        c2.insert({{"p", 100}, {"q", 100}});
        c2.insert({{"p", 101}, {"q", 101}});
        MixedCollectionAnalysis rp = analyzeMixed(c2);
        CHECK(rp.associations.empty()); // nur 2 gemeinsame Dokumente < minPairs 3

        auto c3 = db.createCollection("sampled");
        for (int i = 0; i < 10; ++i)
            c3.insert({{"v", i}});
        MixedAnalysisOptions s;
        s.sampleSize = 5;
        MixedCollectionAnalysis rs = analyzeMixed(c3, s);
        CHECK(rs.docCount == 10 && rs.sampleSize == 5);
        const auto* v = findField(rs.fields, "v");
        CHECK(v != nullptr && v->count == 5);
        MixedCollectionAnalysis rs2 = analyzeMixed(c3, s);
        const auto* v2 = findField(rs2.fields, "v");
        CHECK(v2 != nullptr && v2->mean == v->mean); // reproduktives Sampling (gleicher Seed)
    }

    // 11) Überladungen: vector<Collection>, Database (__references wird ignoriert), leer
    {
        Database db("m11");
        auto c1 = db.createCollection("a1");
        for (int i = 0; i < 6; ++i)
            c1.insert({{"x", i}, {"s", i % 2 == 0 ? "ja" : "nein"}});
        auto c2 = db.createCollection("a2");
        for (int i = 0; i < 6; ++i)
            c2.insert({{"y", i * 1.5}, {"aktiv", i % 3 == 0}});
        std::vector<Collection> colls = {c1, c2};
        auto both = analyzeMixed(colls);
        CHECK(both.size() == 2);
        CHECK(both[0].collection == "a1" && both[1].collection == "a2");
        auto fromDb = analyzeMixed(db);
        CHECK(fromDb.size() == 2);
        auto leer = db.createCollection("leer");
        MixedCollectionAnalysis rl = analyzeMixed(leer);
        CHECK(rl.docCount == 0 && rl.sampleSize == 0);
        CHECK(rl.fields.empty() && rl.associations.empty());
        CHECK(rl.fieldCount == 0 && rl.mixedFieldCount == 0);
    }

    // 12) Reports: JSON und Markdown
    {
        Database db("m12");
        auto c = db.createCollection("rep");
        for (int i = 0; i < 12; ++i)
            c.insert({{"x", i}, {"land", std::string("L") + std::to_string(i % 3)},
                      {"aktiv", i % 2 == 0}});
        MixedCollectionAnalysis res = analyzeMixed(c);
        const std::string js = toJson(res);
        CHECK(js.front() == '{');
        CHECK(js.find("\"kind\":\"number\"") != std::string::npos);
        CHECK(js.find("\"kind\":\"text\"") != std::string::npos);
        CHECK(js.find("\"kind\":\"boolean\"") != std::string::npos);
        CHECK(js.find("\"textClass\":\"categorical\"") != std::string::npos);
        CHECK(js.find("\"measure\":\"pearson\"") != std::string::npos);
        CHECK(js.find("\"note\":") != std::string::npos);
        CHECK(js.find("\"collection\":\"rep\"") != std::string::npos);
        const auto* land = findField(res.fields, "land");
        CHECK(land != nullptr);
        const std::string jsf = toJson(*land);
        CHECK(jsf.find("\"field\":\"land\"") != std::string::npos);
        CHECK(jsf.find("\"lenMin\":") != std::string::npos);
        const std::string md = toMarkdown(res);
        CHECK(md.find("# Gemischte Analyse: rep (FA-33)") != std::string::npos);
        CHECK(md.find("| Feld | Typ | n | distinct | Merkmale |") != std::string::npos);
        CHECK(md.find("Assoziationen") != std::string::npos);
        CHECK(md.find("| Feld X | Feld Y | Mass | Wert | Paare |") != std::string::npos);
        CHECK(md.find("land") != std::string::npos);
    }

    // 13) Haertung: gegenlaeufige Extremwerte (+/-1e308) – die Median-
    //     Interpolation darf nicht nach unendlich ueberlaufen
    {
        Database db("m13");
        auto c = db.createCollection("extrem");
        for (int i = 0; i < 6; ++i)
            c.insert({{"v", i < 3 ? -1e308 : 1e308}});
        MixedCollectionAnalysis res = analyzeMixed(c);
        const auto* f = findField(res.fields, "v");
        CHECK(f != nullptr);
        CHECK(f->kind == MixedKind::Number);
        CHECK(f->count == 6 && f->distinct == 2);
        CHECK(f->min == -1e308 && f->max == 1e308);
        CHECK(f->median == 0.0); // Interpolation zwischen -1e308 und +1e308
        CHECK(std::isfinite(f->mean) && f->mean == 0.0);
        CHECK(std::isfinite(f->median));
        CHECK(near(f->stddev, 0.0)); // Streuung ueberlaeuft in double -> Finite-Guard
        CHECK(std::isfinite(f->stddev));
        CHECK(!f->discrete); // Spannweite inf > discreteMaxRange
        CHECK(f->note.empty()); // integralRatio 1.0: kein Dezimalanteil-Hinweis
        const std::string j = toJson(*f);
        CHECK(j.find(":inf") == std::string::npos);
        CHECK(j.find(":nan") == std::string::npos);
        CHECK(res.associations.empty());
    }

    // 14) Haertung: inf/nan werden nicht in die Statistik aufgenommen
    {
        Database db("m14");
        auto c = db.createCollection("finit");
        for (int i = 1; i <= 5; ++i)
            c.insert({{"v", i}});
        c.insert({{"v", HUGE_VAL}});
        c.insert({{"v", std::nan("")}});
        MixedCollectionAnalysis res = analyzeMixed(c);
        const auto* f = findField(res.fields, "v");
        CHECK(f != nullptr);
        CHECK(res.docCount == 7);
        CHECK(f->count == 5 && f->distinct == 5);
        CHECK(f->missing == 2);
        CHECK(near(f->missingRatio, 2.0 / 7.0, 1e-12));
        CHECK(f->min == 1.0 && f->max == 5.0);
        CHECK(near(f->mean, 3.0) && near(f->median, 3.0));
        CHECK(near(f->stddev, std::sqrt(2.0), 1e-12));
        CHECK(f->integralRatio == 1.0 && f->discrete);
        CHECK(f->note.find("fehlt in 29 %") != std::string::npos);
        const std::string j = toJson(*f);
        CHECK(j.find(":inf") == std::string::npos);
        CHECK(j.find(":nan") == std::string::npos);
    }

    // 15) Haertung: konstante Felder (Zahl/Text/Boolean) – analysiert mit
    //     Stdabw 0, aber nie in Assoziationen
    {
        Database db("m15");
        auto c = db.createCollection("konst");
        for (int i = 0; i < 6; ++i)
            c.insert({{"x", 7}, {"y", i}, {"s", "a"}, {"b", true}});
        MixedCollectionAnalysis res = analyzeMixed(c);
        const auto* x = findField(res.fields, "x");
        CHECK(x != nullptr);
        CHECK(x->distinct == 1 && near(x->stddev, 0.0));
        CHECK(x->note.find("konstantes Feld") != std::string::npos);
        const auto* s = findField(res.fields, "s");
        CHECK(s != nullptr && s->kind == MixedKind::Text);
        CHECK(s->distinct == 1);
        CHECK(s->textClass == TextClass::Categorical); // uniqueness 1/6 < 0.99
        CHECK(s->note.find("konstantes Feld") != std::string::npos);
        const auto* b = findField(res.fields, "b");
        CHECK(b != nullptr && b->kind == MixedKind::Boolean);
        CHECK(near(b->trueShare, 1.0));
        CHECK(b->distinct == 1);
        CHECK(b->note.find("Boolean (100 % true)") != std::string::npos);
        CHECK(b->note.find("konstantes Feld") != std::string::npos);
        CHECK(res.associations.empty()); // Varianz 0 bzw. 1 Gruppe: nie assoziiert
    }

    // 16) Haertung: grosser Offset bei kleiner Streuung – Kennzahlen und
    //     Pearson bleiben genau (zweistufige Zentrierung in long double)
    {
        Database db("m16");
        auto c = db.createCollection("offset");
        for (int i = 0; i < 10; ++i)
            c.insert({{"off", 1e12 + i}, {"v", 2.0 * i}});
        MixedCollectionAnalysis res = analyzeMixed(c);
        const auto* off = findField(res.fields, "off");
        CHECK(off != nullptr);
        CHECK(near(off->mean, 1e12 + 4.5, 1e-6));
        CHECK(near(off->median, 1e12 + 4.5, 1e-6));
        CHECK(near(off->stddev, std::sqrt(8.25), 1e-12));
        const auto* p = findAssoc(res.associations, "off", "v", "pearson");
        CHECK(p != nullptr);
        CHECK(p != nullptr && near(p->value, 1.0, 1e-9));
        CHECK(p != nullptr && p->pairs == 10);
        const std::string j = toJson(res);
        CHECK(j.find(":inf") == std::string::npos);
        CHECK(j.find(":nan") == std::string::npos);
    }

    // 17) Haertung: sehr lange Strings – Laengen-Statistik bleibt korrekt
    {
        Database db("m17");
        auto c = db.createCollection("lang");
        for (int i = 0; i < 5; ++i)
            c.insert({{"t", std::string(100000, static_cast<char>('a' + i))}});
        MixedCollectionAnalysis res = analyzeMixed(c);
        const auto* f = findField(res.fields, "t");
        CHECK(f != nullptr);
        CHECK(f->kind == MixedKind::Text);
        CHECK(f->count == 5 && f->distinct == 5);
        CHECK(f->lenMin == 100000 && f->lenMax == 100000);
        CHECK(near(f->lenMean, 100000.0));
        CHECK(f->textClass == TextClass::Identifier); // uniqueness 1.0
        CHECK(f->note.find("Kennung (100 % einzigartig)") != std::string::npos);
    }

    std::cout << "test_mixed_analysis: " << g_checks << " Checks, " << g_failures
              << " Fehlgeschlagen\n";
    return g_failures == 0 ? 0 : 1;
}
