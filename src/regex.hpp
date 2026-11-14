#pragma once

#include <string_view>

namespace memdb::detail {

/// @brief Mini-RegEx mit Full-Match-Semantik (intern, für `matches` in Queries).
///
/// Unterstützt: Literale, `.`, Quantoren `*` `+` `?`, Klassen `[...]` / `[^...]`
/// (inkl. Bereiche `a-z`), Fluchten `\x`, Anker `^` / `$`, Alternativen `|`,
/// Gruppen `(...)`. Implementierung als Thompson-NFA (kein exponentielles
/// Backtracking). Wirft `std::invalid_argument` bei ungueltigem Pattern.
bool regexMatch(std::string_view pattern, std::string_view text);

} // namespace memdb::detail
