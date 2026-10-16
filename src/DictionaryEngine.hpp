#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace lex {
using Json = nlohmann::ordered_json; // Maintains hand-friendly key order on save.

class DictionaryEngine {
public:
    bool load(const std::filesystem::path& path, std::string& error);
    bool save(std::string& error);

    const Json* find(std::uint64_t id) const;
    std::vector<std::uint64_t> search(const std::string& text) const;
    std::vector<std::string> validate() const;
    bool validateReplacement(std::uint64_t id, const Json& replacement, std::string& error) const;

    bool add(const std::string& type, std::uint64_t& newId, std::string& error);
    bool replace(std::uint64_t id, const Json& replacement, std::string& error);
    bool remove(std::uint64_t id, std::string& error);

    static Json newEntry(std::uint64_t id, const std::string& type);

    std::size_t size() const { return document_.at("entries").size(); }
    bool dirty() const { return dirty_; }
    const std::filesystem::path& path() const { return path_; }

private:
    Json document_;
    std::filesystem::path path_;
    bool dirty_ = false;

    static std::vector<std::string> validateDocument(const Json& document);
};
} // namespace lex
