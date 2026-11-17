# Influence Analysis (FA-31) – API Reference

| Field | Value |
|---|---|
| Status | implemented – **no external dependencies** (only InMemDB + C++17) |
| Reference | Requirements FA-31 (optional); deepening of the numeric metadata analysis (FA-30) |
| Module | `include/memdb/influence.hpp` + `src/influence.cpp` |

The module interprets the statistical values of the numeric analysis (FA-30)
one step further and answers the question: **which field dominantly influences
the others?** It combines three kinds of evidence into a role assignment and an
influence score per field. Read-only; sampling and field caps behave as in FA-30.

## Kinds of evidence

1. **Hub over Pearson correlations**: sum of |r| against all partner fields
   (`hubScore`, `hubRatio`) plus the number of strong relationships
   (|r| >= `strongCorrelation`, default 0.8). Fields with many strong
   correlations are dominant driver candidates.
2. **Multiple R² (explained variance)**: linear regression of every field
   against the others (complete cases, Gaussian elimination with partial
   pivoting and a minimal ridge term against exact collinearity). High R²
   (>= `rSquaredHigh`, default 0.9): the field is explained by the others.
   Low R² with a high hub: driver.
3. **Functional dependency**: equal values in field A always map to the same
   value in field B (B = f(A), e.g. `price = f(quantity)`). Evidence comes only
   from groups with a repeated source value; `coverage` is the share of
   consistent groups (>= `dependencyRatio`, default 0.99). The strongest
   structural hint at derivation. If a dependency holds in both directions, the
   direction with more distinct source values wins.

## Roles (FieldRole)

| Role | Meaning | Criterion |
|---|---|---|
| `driver` | determines other fields without being explained itself | `determines > 0` or >= 2 strong correlations with R² < rSquaredHigh |
| `dependent` | explained by the other fields | `determinedBy > 0` or R² >= rSquaredHigh |
| `mediator` | both: dependent and driving further fields | driver and dependent criteria at the same time |
| `independent` | no strong relationships | otherwise |

Influence score (0..1): `0.5 * hubRatio + 0.3 * (determines / partners) + 0.2 * (1 - R²)`.
`topDriver` is the highest-scoring field, provided a dominance signal exists
(functional determination, strong correlation or hubRatio >= 0.8).

## Functions

```cpp
#include <memdb/memdb.hpp>
#include <memdb/influence.hpp>

memdb::InfluenceOptions opts;
//   opts.numeric               – NumericAnalysisOptions of the base scan (FA-30)
//   strongCorrelation (0.8)    – |r| >= X counts as a strong relationship
//   dependencyRatio (0.99)     – share of consistent groups per dependency
//   minDependencyPairs (5)     – minimum documents containing both fields
//   rSquaredHigh (0.9)        – R^2 >= X => field counts as explained
//   maxPredictors (16)         – predictor cap per regression
//   minRegressionRows (8)      – minimum rows (complete cases) for R^2

memdb::InfluenceAnalysis res = memdb::analyzeInfluence(coll, opts);
//   res.topDriver             – field with the highest influence score
//   res.dependencies[i]       – {source, target, coverage, groups, pairs}
//   res.fields[i]             – sorted by influenceScore, descending:
//     .role                   – driver / dependent / mediator / independent
//     .hubScore/.hubRatio     – sum resp. mean of |r| over all partners
//     .strongCorrelations     – count of |r| >= strongCorrelation
//     .rSquared               – explained variance from the other fields
//     .determines/.determinedBy – functional determinations per direction
//     .influenceScore         – combined score (0..1)
//     .rationale              – plain-text reasoning (German)

// Several collections (across databases; __references is ignored)
auto all = memdb::analyzeInfluence(db, opts);

// Reports
std::string json = memdb::toJson(res);      // machine-readable / LLM-friendly
std::string md   = memdb::toMarkdown(res);  // tables per collection
std::string mdAll = memdb::toMarkdown(all); // overview of all collections
```

## Limits

Correlation does not prove causation: the result is a reasoned ranking based on
statistical dominance and structural derivation, not causal truth. With
sampling (opts.numeric.sampleSize) the evidence base shrinks accordingly; R² is
only computed from `minRegressionRows` complete cases upwards.

## Tests

`tests/test_influence.cpp` (103 checks): driver/dependent/independent
separation, non-invertible functional dependency, exact linearity (R² = 1),
mediators in derivation chains, a single field without relationships, sampling,
database/vector overloads, JSON/Markdown reports, role names.
