#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace memdb::detail {

/// @brief Mini-RegEx mit Full-Match-Semantik (intern, fuer `matches` in Queries).
///
/// Syntax (v0.20): Literale, `.` (jedes Byte inkl. '\n'), Quantoren `*` `+` `?` `{n}` `{n,}`
/// `{n,m}` (lazy `*?` etc. erlaubt, ohne semantische Wirkung bei Full-Match), Klassen
/// `[...]` / `[^...]` inkl. Bereiche `a-z` und Kurzschriften `\d \D \s \S \w \W`,
/// Fluchten `\x` (`\n \r \t \f \v \a \0` + Literal), Anker `^` / `$`, Alternativen `|`,
/// Gruppen `(...)` und `(?:...)`.
///
/// Implementierung: Thompson-NFA mit Visited-Set (kein Backtracking, kein Hang bei
/// verschachtelten Schleifen wie `(a*)*`), byte-basiert (UTF-8 wird nicht als Zeichen
/// behandelt). Full-Match: das gesamte Pattern muss zum gesamten Text passen.
///
/// Nicht unterstuetzt (NFA-Grenze): Backreferences, Lookaround, `\b`/`\B` (Wortgrenzen;
/// `\b` ist Literal 'b').
///
/// Harte Limits gegen pathologische Patterns (v0.34, sonst Stack-Overflow/Hang):
/// Pattern max. 4096 Zeichen, Verschachtelung max. 256, NFA max. 50000 Anweisungen,
/// Match max. 100 Mio. Schritte. Violations werfen `std::invalid_argument` (Pattern) bzw.
/// `std::runtime_error` (Match-Budget). Ungueltige Patterns werfen `std::invalid_argument`
/// mit Position.
///
/// `Regex` kompiliert das Pattern einmalig und ist dann fuer beliebig viele `match()`-Aufrufe
/// wiederverwendbar und thread-sicher (match() arbeitet nur mit lokalem Zustand).
class Regex {
public:
    /// Kompiliert das Pattern. @throws std::invalid_argument bei ungueltigem/zu komplexem Pattern.
    explicit Regex(std::string_view pattern);
    ~Regex();
    Regex(const Regex&) = default;
    Regex& operator=(const Regex&) = default;
    Regex(Regex&&) = default;
    Regex& operator=(Regex&&) = default;

    /// Full-Match gegen `text`. @throws std::runtime_error bei ueberschrittenem Match-Budget.
    bool match(std::string_view text) const;

    const std::string& pattern() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

/// Convenience: kompiliert und matcht in einem Aufruf (z. B. zum Validieren von Patterns).
/// @throws std::invalid_argument bei ungueltigem Pattern, std::runtime_error bei Match-Budget.
bool regexMatch(std::string_view pattern, std::string_view text);

} // namespace memdb::detail
