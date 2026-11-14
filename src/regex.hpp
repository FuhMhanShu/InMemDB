#pragma once

#include <string_view>

namespace memdb::detail {

/// @brief Mini-RegEx mit Full-Match-Semantik (intern, für `matches` in Queries, v0.20 erweitert).
///
/// Unterstützt: Literale, `.`, Quantoren `*` `+` `?` `{n}` `{n,}` `{n,m}` (auch lazy `*?` etc.,
/// ohne semantische Wirkung bei Full-Match), Klassen `[...]` / `[^...]` inkl. Bereiche `a-z`
/// und Kurzschriften `\d \D \s \S \w \W`, Fluchten `\x` (`\n \r \t \f \v \a \0` + Literal),
/// Anker `^` / `$`, Alternativen `|`, Gruppen `(...)` und `(?:...)`.
/// Implementierung als Thompson-NFA mit Visited-Set (kein exponentielles Backtracking,
/// keine Endlosschleife bei verschachtelten Schleifen wie `(a*)*`). Wirft
/// `std::invalid_argument` mit Position bei ungültigem Pattern.
bool regexMatch(std::string_view pattern, std::string_view text);

} // namespace memdb::detail
