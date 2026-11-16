#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace memdb {

/// @brief Schweregrad einer Message (v0.36, MessageHandler).
enum class MessageLevel {
    Debug = 0,   ///< Detail-Diagnose |
    Info = 1,    ///< normaler Betriebs-Hinweis |
    Warning = 2, ///< etwas wurde übergangen/übersprungen (z. B. Import-Skip) |
    Error = 3    ///< abgefangene Exception / Fehler (Programmfluss lief weiter) |
};

/// @brief Eine Meldung des MessageHandlers (v0.36).
struct Message {
    MessageLevel level = MessageLevel::Info;
    std::string source; ///< Herkunft, z. B. "query-eval", "observer", "load", "import", "regex", "cli"
    std::string text;   ///< Meldungstext (bei Exceptions: `what()`)
};

/// @brief Zentraler Message-Handler (Singleton, thread-safe, v0.36).
///
/// Idee: Die Exception-Policy (v0.35, `docs/de/internals/error-handling.md`) fängt Exceptions dort ab, wo
/// sonst mitten in einer Operation abgebrochen würde – diese Stellen **melden** zusätzlich hier
/// (z. B. `query-eval`, `observer`, `load`, `import`, `regex`, `cli`). Eigene Handler können
/// via `subscribe()` angemeldet werden (Logging, UI, Monitoring, Test-Mitschnitt …).
///
/// ```cpp
/// memdb::MessageHandler::instance().subscribe([](const memdb::Message& m) {
///     std::cerr << "[" << (int)m.level << "] " << m.source << ": " << m.text << "\n";
/// });
/// ```
///
/// Eigenschaften: `publish()` ist thread-safe; Handler laufen außerhalb des Locks; eine
/// throwende Handler-Callback wird selbst abgefangen (`handlerErrors()`) und stört weder die
/// restlichen Handler noch den Aufrufer. Über `setMinLevel()` lässt sich filtern.
/// Ohne angemeldete Handler werden Meldungen nur gezählt (`count(level)`).
class MessageHandler {
public:
    using Handler = std::function<void(const Message&)>;

    static MessageHandler& instance();

    /// Handler anmelden. @return ID für `unsubscribe`.
    std::uint64_t subscribe(Handler h);
    /// Handler abmelden. @return false, wenn die ID unbekannt ist.
    bool unsubscribe(std::uint64_t id);
    std::size_t handlerCount() const;

    /// Mindest-Level für publish (Default: Debug = alles). @see count()
    void setMinLevel(MessageLevel lvl);
    MessageLevel minLevel() const;

    /// Meldung veröffentlichen (thread-safe; Handler-Exceptions werden abgefangen und gezählt).
    void publish(MessageLevel lvl, std::string source, std::string text);
    void debug(std::string source, std::string text) { publish(MessageLevel::Debug, std::move(source), std::move(text)); }
    void info(std::string source, std::string text) { publish(MessageLevel::Info, std::move(source), std::move(text)); }
    void warning(std::string source, std::string text) { publish(MessageLevel::Warning, std::move(source), std::move(text)); }
    void error(std::string source, std::string text) { publish(MessageLevel::Error, std::move(source), std::move(text)); }

    /// Anzahl veröffentlichter Meldungen pro Level (gefiltert, d. h. >= minLevel).
    std::uint64_t count(MessageLevel lvl) const;
    std::uint64_t totalCount() const;
    /// Wie viele Handler-Callbacks selbst geworfen haben (werden abgefangen, s. o.).
    std::uint64_t handlerErrors() const;
    /// Alle Zähler (count/handlerErrors) zurücksetzen.
    void resetCounts();

private:
    MessageHandler() = default;
    MessageHandler(const MessageHandler&) = delete;
    MessageHandler& operator=(const MessageHandler&) = delete;

    mutable std::mutex mtx_;
    std::map<std::uint64_t, Handler> handlers_;
    std::uint64_t nextId_ = 1;
    MessageLevel minLevel_ = MessageLevel::Debug;
    std::array<std::uint64_t, 4> counts_{};
    std::uint64_t handlerErrors_ = 0;
};

} // namespace memdb
