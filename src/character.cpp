#include "memdb/character.hpp"

#include "json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace memdb {
namespace {

bool isComplexType(Value::Type t) {
    return t == Value::Type::Document || t == Value::Type::Array || t == Value::Type::Binary;
}

std::string num(double d) {
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof(buf), d);
    return std::string(buf, r.ptr);
}

} // namespace

std::string toString(DataCategory cat) {
    switch (cat) {
        case DataCategory::Numeric: return "numeric";
        case DataCategory::Mixed: return "mixed";
        case DataCategory::Complex: return "complex";
    }
    return "?";
}

CollectionCharacter assessCharacter(const Collection& c, const DiscoveryOptions& opts) {
    CollectionCharacter cc;
    cc.collection = c.name();
    cc.docCount = c.size();
    const std::size_t n = cc.docCount;
    cc.sampleSize = (opts.sampleSize > 0 && opts.sampleSize < n) ? opts.sampleSize : n;

    struct Acc {
        FieldCharacter fc;
    };
    std::map<std::string, Acc> acc;

    auto eachDoc = [&](const std::function<void(const Document&)>& cb) {
        if (opts.sampleSize > 0 && opts.sampleSize < n) {
            for (auto id : c.sample(opts.sampleSize, opts.seed))
                if (auto d = c.findById(id))
                    cb(*d);
        } else {
            c.forEach(Expression{}, cb);
        }
    };
    eachDoc([&](const Document& d) {
        d.forEachField([&](const std::string& k, const Value& v) {
            Acc& a = acc[k];
            a.fc.name = k;
            ++a.fc.present;
            if (v.isNull()) {
                ++a.fc.nulls;
                return;
            }
            ++a.fc.typeCounts[v.type()];
            if (isComplexType(v.type()))
                a.fc.complexValues = true;
        });
    });

    cc.fields.reserve(acc.size());
    for (auto& kv : acc) {
        FieldCharacter f = std::move(kv.second.fc);
        std::size_t nonNull = 0;
        for (const auto& tc : f.typeCounts)
            nonNull += tc.second;
        if (nonNull == 0) {
            f.character = "empty";
        } else {
            Value::Type dom = Value::Type::Null;
            std::size_t domCnt = 0;
            for (const auto& tc : f.typeCounts) {
                if (tc.second > domCnt) {
                    domCnt = tc.second;
                    dom = tc.first;
                }
            }
            f.dominantType = dom;
            f.typePurity = static_cast<double>(domCnt) / static_cast<double>(nonNull);
            if (isComplexType(dom))
                f.character = "complex";
            else if (f.typeCounts.size() > 1 && f.typePurity < 0.6)
                f.character = "mixed";
            else if (dom == Value::Type::Int || dom == Value::Type::Double)
                f.character = "numeric";
            else if (dom == Value::Type::String)
                f.character = "string";
            else if (dom == Value::Type::Bool)
                f.character = "boolean";
            else
                f.character = "complex";
        }
        cc.fields.push_back(std::move(f));
    }
    if (cc.fields.size() > opts.maxFieldsPerCollection)
        cc.fields.resize(opts.maxFieldsPerCollection);

    cc.fieldCount = cc.fields.size();
    std::size_t driftFields = 0;
    for (const auto& f : cc.fields) {
        if (f.character == "empty")
            continue;
        ++cc.effectiveFields;
        if (f.character == "numeric")
            ++cc.numericFields;
        else if (f.character == "string")
            ++cc.stringFields;
        else if (f.character == "boolean")
            ++cc.boolFields;
        else if (f.character == "complex")
            ++cc.complexFields;
        else if (f.character == "mixed")
            ++cc.mixedFields;
        if (f.typeCounts.size() > 1)
            ++driftFields;
    }
    const std::size_t eff = cc.effectiveFields;
    if (eff > 0) {
        cc.numericFieldRatio = static_cast<double>(cc.numericFields) / static_cast<double>(eff);
        cc.stringFieldRatio = static_cast<double>(cc.stringFields) / static_cast<double>(eff);
        cc.complexFieldRatio =
            static_cast<double>(cc.complexFields + cc.mixedFields) / static_cast<double>(eff);
        cc.driftFieldRatio = static_cast<double>(driftFields) / static_cast<double>(eff);
    }

    if (eff == 0) {
        cc.category = DataCategory::Mixed;
        cc.confidence = 0.0;
        cc.rationale = "keine Felder mit Nicht-Null-Werten";
    } else if (cc.complexFieldRatio >= 0.25 || cc.driftFieldRatio >= 0.4) {
        cc.category = DataCategory::Complex;
        cc.confidence = std::max(cc.complexFieldRatio, cc.driftFieldRatio);
        cc.rationale = std::to_string(cc.complexFields + cc.mixedFields) + "/" + std::to_string(eff) +
                       " Felder komplex/gemischt, " + std::to_string(driftFields) + "/" +
                       std::to_string(eff) + " mit Typ-Drift";
    } else if (cc.numericFieldRatio >= 0.8) {
        cc.category = DataCategory::Numeric;
        cc.confidence = cc.numericFieldRatio;
        cc.rationale = std::to_string(cc.numericFields) + "/" + std::to_string(eff) +
                        " Felder numerisch";
    } else {
        cc.category = DataCategory::Mixed;
        const double nonNum = 1.0 - cc.numericFieldRatio;
        cc.confidence = std::min(1.0, 2.0 * std::min(cc.numericFieldRatio, nonNum));
        cc.rationale = "Mischung: " + std::to_string(cc.numericFields) + " numerisch, " +
                       std::to_string(cc.stringFields) + " Text, " +
                       std::to_string(cc.complexFields + cc.mixedFields) + " komplex/gemischt von " +
                       std::to_string(eff) + " Feldern";
    }
    return cc;
}

std::vector<CollectionCharacter> assessCharacter(const std::vector<Collection>& colls,
                                                const DiscoveryOptions& opts) {
    std::vector<CollectionCharacter> out;
    for (const auto& c : colls) {
        if (c.name() == "__references")
            continue;
        out.push_back(assessCharacter(c, opts));
    }
    return out;
}

std::vector<CollectionCharacter> assessCharacter(Database& db, const DiscoveryOptions& opts) {
    std::vector<Collection> colls;
    for (const auto& name : db.collectionNames())
        colls.push_back(db.collection(name));
    return assessCharacter(colls, opts);
}

std::string toJson(const CollectionCharacter& cc) {
    std::string out = "{\"collection\":" + detail::writeJsonString(cc.collection) +
                      ",\"docCount\":" + std::to_string(cc.docCount) +
                      ",\"sampleSize\":" + std::to_string(cc.sampleSize) +
                      ",\"fieldCount\":" + std::to_string(cc.fieldCount) +
                      ",\"effectiveFields\":" + std::to_string(cc.effectiveFields) +
                      ",\"category\":\"" + toString(cc.category) + "\"" +
                      ",\"confidence\":" + num(cc.confidence) +
                      ",\"numericFieldRatio\":" + num(cc.numericFieldRatio) +
                      ",\"stringFieldRatio\":" + num(cc.stringFieldRatio) +
                      ",\"complexFieldRatio\":" + num(cc.complexFieldRatio) +
                      ",\"driftFieldRatio\":" + num(cc.driftFieldRatio) +
                      ",\"rationale\":" + detail::writeJsonString(cc.rationale) + ",\"fields\":[";
    bool first = true;
    for (const auto& f : cc.fields) {
        if (!first)
            out += ',';
        first = false;
        out += "{\"name\":" + detail::writeJsonString(f.name) +
               ",\"present\":" + std::to_string(f.present) +
               ",\"nulls\":" + std::to_string(f.nulls) +
               ",\"dominantType\":" + std::to_string(static_cast<int>(f.dominantType)) +
               ",\"typePurity\":" + num(f.typePurity) +
               ",\"complexValues\":" + (f.complexValues ? "true" : "false") +
               ",\"character\":" + detail::writeJsonString(f.character) + ",\"types\":{";
        bool ft = true;
        for (const auto& tc : f.typeCounts) {
            if (!ft)
                out += ',';
            ft = false;
            out += std::to_string(static_cast<int>(tc.first)) + ":" + std::to_string(tc.second);
        }
        out += "}}";
    }
    out += "]}";
    return out;
}

std::string toJson(const std::vector<CollectionCharacter>& ccs) {
    std::string out = "[";
    bool first = true;
    for (const auto& cc : ccs) {
        if (!first)
            out += ',';
        first = false;
        out += toJson(cc);
    }
    out += "]";
    return out;
}

std::string toMarkdown(const std::vector<CollectionCharacter>& ccs) {
    std::string out = "# Datencharakter (FA-29)\n\n";
    out += "| Collection | Kategorie | Konfidenz | Felder (effektiv) | numerisch | Text | komplex/gemischt | Typ-Drift | Begründung |\n";
    out += "|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& cc : ccs) {
        out += "| " + cc.collection + " | " + toString(cc.category) + " | " + num(cc.confidence) +
               " | " + std::to_string(cc.effectiveFields) + " | " + std::to_string(cc.numericFields) +
               " | " + std::to_string(cc.stringFields) + " | " +
               std::to_string(cc.complexFields + cc.mixedFields) + " | " + num(cc.driftFieldRatio) +
               " | " + cc.rationale + " |\n";
    }
    out += "\nKategorien: `numeric` = Kategorie 1 (fast ausschließlich numerisch), `mixed` = Kategorie 2 (gemischt), `complex` = Kategorie 3 (komplexe Mischformen).\n";
    return out;
}

} // namespace memdb
