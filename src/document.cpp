#include "memdb/document.hpp"
#include "memdb/exceptions.hpp"

#include <ctime>
#include <sstream>

namespace memdb {

struct Document::Impl {
    DocumentId id;
    // transparenter Komparator: find(string_view) ohne std::string-Allokation (Hotpath)
    std::map<std::string, Value, std::less<>> fields;
};

namespace {

Value deepCopyValue(const Value& v) {
    if (v.isDocument()) {
        auto d = v.asDocument();
        return Value(d ? std::make_shared<Document>(*d) : nullptr);
    }
    if (v.isArray()) {
        std::vector<Value> copy;
        copy.reserve(v.asArray().size());
        for (const auto& item : v.asArray())
            copy.push_back(deepCopyValue(item));
        return Value(std::move(copy));
    }
    return v;
}

} // namespace

Document::Document() : impl_(std::make_unique<Impl>()) {}

Document::Document(std::initializer_list<std::pair<const std::string, Value>> fields)
    : impl_(std::make_unique<Impl>()) {
    for (const auto& [key, value] : fields)
        impl_->fields.emplace(key, value);
}

Document::Document(const Document& other) : impl_(std::make_unique<Impl>()) {
    impl_->id = other.impl_->id;
    for (const auto& [key, value] : other.impl_->fields)
        impl_->fields.emplace(key, deepCopyValue(value));
}

Document::Document(Document&&) noexcept = default;
Document& Document::operator=(const Document& other) {
    if (this != &other) {
        auto fresh = std::make_unique<Impl>();
        fresh->id = other.impl_->id;
        for (const auto& [key, value] : other.impl_->fields)
            fresh->fields.emplace(key, deepCopyValue(value));
        impl_ = std::move(fresh);
    }
    return *this;
}
Document& Document::operator=(Document&&) noexcept = default;
Document::~Document() = default;

DocumentId Document::id() const { return impl_->id; }
void Document::setId(DocumentId id) { impl_->id = id; }

std::size_t Document::fieldCount() const { return impl_->fields.size(); }

void Document::forEachField(const std::function<void(const std::string&, const Value&)>& f) const {
    for (const auto& [key, value] : impl_->fields)
        f(key, value);
}

bool Document::contains(std::string_view key) const {
    return impl_->fields.find(key) != impl_->fields.end();
}

const Value* Document::find(std::string_view key) const {
    auto it = impl_->fields.find(key);
    return it == impl_->fields.end() ? nullptr : &it->second;
}

Value* Document::findMutable(std::string_view key) {
    auto it = impl_->fields.find(key);
    return it == impl_->fields.end() ? nullptr : &it->second;
}

Value& Document::operator[](const std::string& key) { return impl_->fields[key]; }

const Value& Document::at(std::string_view key) const {
    auto it = impl_->fields.find(key);
    if (it == impl_->fields.end())
        throw Error("Feld nicht gefunden: " + std::string(key));
    return it->second;
}

void Document::set(std::string key, Value value) { impl_->fields[std::move(key)] = std::move(value); }

bool Document::remove(std::string_view key) {
    auto it = impl_->fields.find(key);
    if (it == impl_->fields.end())
        return false;
    impl_->fields.erase(it);
    return true;
}

bool Document::rename(std::string_view from, std::string_view to) {
    auto it = impl_->fields.find(from);
    if (it == impl_->fields.end())
        return false;
    if (impl_->fields.find(to) != impl_->fields.end())
        return false;
    auto node = impl_->fields.extract(it);
    node.key() = std::string(to);
    impl_->fields.insert(std::move(node));
    return true;
}

Document& Document::expiresIn(std::int64_t seconds) {
    return expiresAt(static_cast<std::int64_t>(std::time(nullptr)) + seconds);
}

Document& Document::expiresAt(std::int64_t unixSeconds) {
    set("expiresAt", Value(unixSeconds));
    return *this;
}

void Document::setPath(std::string_view dotPath, Value value) {
    Document* doc = this;
    std::size_t start = 0;
    while (true) {
        auto dot = dotPath.find('.', start);
        auto segment = dotPath.substr(start, dot == std::string_view::npos ? dot : dot - start);
        if (dot == std::string_view::npos) {
            doc->set(std::string(segment), std::move(value));
            return;
        }
        Value& slot = (*doc)[std::string(segment)]; // legt null an, falls fehlt
        if (!slot.isDocument() || !slot.asDocument())
            slot = Value(std::make_shared<Document>());
        doc = slot.asDocument().get();
        start = dot + 1;
    }
}

const Value* Document::findPath(std::string_view dotPath) const {
    const Value* current = nullptr;
    const Document* doc = this;
    std::size_t start = 0;
    while (true) {
        auto dot = dotPath.find('.', start);
        auto segment = dotPath.substr(start, dot == std::string_view::npos ? dot : dot - start);
        if (doc) {
            current = doc->find(segment);
            if (!current)
                return nullptr;
        }
        if (dot == std::string_view::npos)
            break;
        if (!current->isDocument())
            return nullptr;
        doc = current->asDocument().get();
        start = dot + 1;
    }
    return current;
}

std::map<std::string, Value> Document::fields() const {
    return std::map<std::string, Value>(impl_->fields.begin(), impl_->fields.end());
}

bool Document::operator==(const Document& other) const { return impl_->fields == other.impl_->fields; }

} // namespace memdb
