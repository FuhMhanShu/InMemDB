#include "memdb/message_handler.hpp"

#include <utility>
#include <vector>

namespace memdb {

MessageHandler& MessageHandler::instance() {
    static MessageHandler inst;
    return inst;
}

std::uint64_t MessageHandler::subscribe(Handler h) {
    std::lock_guard lock(mtx_);
    std::uint64_t id = nextId_++;
    handlers_.emplace(id, std::move(h));
    return id;
}

bool MessageHandler::unsubscribe(std::uint64_t id) {
    std::lock_guard lock(mtx_);
    return handlers_.erase(id) > 0;
}

std::size_t MessageHandler::handlerCount() const {
    std::lock_guard lock(mtx_);
    return handlers_.size();
}

void MessageHandler::setMinLevel(MessageLevel lvl) {
    std::lock_guard lock(mtx_);
    minLevel_ = lvl;
}

MessageLevel MessageHandler::minLevel() const {
    std::lock_guard lock(mtx_);
    return minLevel_;
}

void MessageHandler::publish(MessageLevel lvl, std::string source, std::string text) {
    std::vector<Handler> cbs;
    {
        std::lock_guard lock(mtx_);
        if (static_cast<int>(lvl) < static_cast<int>(minLevel_))
            return;
        counts_[static_cast<int>(lvl)]++;
        cbs.reserve(handlers_.size());
        for (const auto& [id, h] : handlers_)
            cbs.push_back(h);
    }
    // Handler laufen bewusst ausserhalb des Locks; eine throwende Callback darf weder die
    // restlichen Handler noch den Aufrufer stoeren -> wird abgefangen und gezaehlt.
    Message m{lvl, std::move(source), std::move(text)};
    for (const auto& h : cbs) {
        try {
            h(m);
        } catch (...) {
            std::lock_guard lock(mtx_);
            ++handlerErrors_;
        }
    }
}

std::uint64_t MessageHandler::count(MessageLevel lvl) const {
    std::lock_guard lock(mtx_);
    return counts_[static_cast<int>(lvl)];
}

std::uint64_t MessageHandler::totalCount() const {
    std::lock_guard lock(mtx_);
    std::uint64_t sum = 0;
    for (auto c : counts_)
        sum += c;
    return sum;
}

std::uint64_t MessageHandler::handlerErrors() const {
    std::lock_guard lock(mtx_);
    return handlerErrors_;
}

void MessageHandler::resetCounts() {
    std::lock_guard lock(mtx_);
    counts_.fill(0);
    handlerErrors_ = 0;
}

} // namespace memdb
