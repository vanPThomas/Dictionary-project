#include "DictionaryEngine.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_map>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace lex
{
namespace {

// join strings
std::string join(const std::vector<std::string>& items)
{
    std::ostringstream out;
    for (const auto& item : items) out << "- " << item << '\n';
    return out.str();
}

std::string fold(std::string s)
{
    std::string result;
    for (std::size_t i = 0; i < s.size();)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 128)
        {
            result += static_cast<char>(std::tolower(c));
            ++i;
        }
        else if (i + 1 < s.size())
        {
            const std::string pair = s.substr(i, 2);
            if (pair == "\xC3\x90") result += "\xC3\xB0";      // Ð → ð
            else if (pair == "\xC3\x87") result += "\xC3\xA7"; // Ç → ç
            else if (pair == "\xC3\x93") result += "\xC3\xB3"; // Ó → ó
            else if (pair == "\xC5\x8C") result += "\xC5\x8D"; // Ō → ō
            else result += pair;
            i += 2;
        }
        else
        {
            result += s[i++];
        }
    }
    return result;
}

std::string stringValue(const Json& object, const char* key)
{
    if (!object.is_object() || !object.contains(key) || !object[key].is_string()) return "";
    return object[key].get<std::string>();
}

bool matches(const Json& item, const std::string& q)
{
    auto has = [&](const std::string& text) { return fold(text).find(q) != std::string::npos; };
    if (has(stringValue(item, "lemma"))) return true;
    if (item.contains("alternate_spellings") && item["alternate_spellings"].is_array())
    {
        for (const auto& v : item["alternate_spellings"])
            if (v.is_string() && has(v.get<std::string>())) return true;
    }
    if (item.contains("senses") && item["senses"].is_array())
    {
        for (const auto& sense : item["senses"])
        {
            if (has(stringValue(sense, "definition"))) return true;
            if (sense.contains("translations") && sense["translations"].is_object())
            {
                for (auto it = sense["translations"].begin(); it != sense["translations"].end(); ++it)
                {
                    if (it.value().is_array())
                    {
                        for (const auto& translation : it.value())
                            if (translation.is_string() && has(translation.get<std::string>())) return true;
                    }
                }
            }
        }
    }
    if (item.contains("morphology") && item["morphology"].is_object() &&
        has(stringValue(item["morphology"], "literal_gloss"))) return true;
    return false;
}
} // namespace

std::vector<std::string> DictionaryEngine::validateDocument(const Json& doc)
{
    std::vector<std::string> errors;
    if (!doc.is_object()) return {"Root must be a JSON object."};
    if (!doc.contains("schema_version") || !doc["schema_version"].is_number_integer() ||
        doc["schema_version"].get<int>() != 1)
        errors.emplace_back("Only schema_version 1 is supported.");
    if (!doc.contains("entries") || !doc["entries"].is_array())
        return {"Root must have an entries array."};
    if (!doc.contains("next_entry_id") || !doc["next_entry_id"].is_number_unsigned())
        errors.emplace_back("next_entry_id must be an unsigned integer.");

    std::unordered_map<std::uint64_t, const Json*> ids;
    std::unordered_map<std::uint64_t, std::set<std::uint64_t>> senseIds;
    std::uint64_t maximum = 0;
    for (const auto& entry : doc["entries"]) {
        if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_number_unsigned())
        {
            errors.emplace_back("Every entry must be an object with an unsigned integer id.");
            continue;
        }
        const auto id = entry["id"].get<std::uint64_t>();
        maximum = std::max(maximum, id);
        if (id == 0) errors.emplace_back("Entry id 0 is reserved.");
        if (!ids.emplace(id, &entry).second) errors.emplace_back("Duplicate entry id: " + std::to_string(id));
        if (stringValue(entry, "lemma").empty())
            errors.emplace_back("Empty lemma for entry " + std::to_string(id));
        const std::string type = stringValue(entry, "entry_type");
        if (type != "word" && type != "affix")
            errors.emplace_back("Entry " + std::to_string(id) + ": entry_type must be word or affix.");
        // Validate the fields used directly by the Quick Editor. This matters for
        // advanced JSON edits: a syntactically valid object must not crash the UI.
        const std::string prefix = "Entry " + std::to_string(id) + ": ";
        if (!entry.contains("linguistics") || !entry["linguistics"].is_object() ||
            !entry["linguistics"].contains("grammatical_notes") ||
            !entry["linguistics"]["grammatical_notes"].is_string())
            errors.emplace_back(prefix + "linguistics.grammatical_notes must be a string.");
        if (!entry.contains("culture") || !entry["culture"].is_object() ||
            !entry["culture"].contains("cultural_notes") ||
            !entry["culture"]["cultural_notes"].is_string())
            errors.emplace_back(prefix + "culture.cultural_notes must be a string.");
        if (!entry.contains("history") || !entry["history"].is_object())
            errors.emplace_back(prefix + "history must be an object.");
        if (!entry.contains("morphology") || !entry["morphology"].is_object() ||
            !entry["morphology"].contains("components") ||
            !entry["morphology"]["components"].is_array())
            errors.emplace_back(prefix + "morphology.components must be an array.");
        if (!entry.contains("relations") || !entry["relations"].is_array())
            errors.emplace_back(prefix + "relations must be an array.");
        if (!entry.contains("examples") || !entry["examples"].is_array())
            errors.emplace_back(prefix + "examples must be an array.");
        if (!entry.contains("senses") || !entry["senses"].is_array()) {
            errors.emplace_back("Entry " + std::to_string(id) + " requires a senses array.");
            continue;
        }
        int primaryCount = 0;
        for (const auto& s : entry["senses"])
        {
            if (!s.is_object() || !s.contains("id") || !s["id"].is_number_unsigned()) {
                errors.emplace_back("Entry " + std::to_string(id) + " has a sense without integer id.");
                continue;
            }
            const auto sid = s["id"].get<std::uint64_t>();
            if (sid == 0 || !senseIds[id].insert(sid).second)
                errors.emplace_back("Entry " + std::to_string(id) + " has duplicate/invalid sense id.");
            if (stringValue(s, "definition").empty())
                errors.emplace_back("Entry " + std::to_string(id) + " has an empty definition.");
            if (!s.contains("is_primary") || !s["is_primary"].is_boolean())
                errors.emplace_back("Entry " + std::to_string(id) + " sense has non-boolean is_primary.");
            else if (s["is_primary"].get<bool>()) ++primaryCount;
            if (!s.contains("translations") || !s["translations"].is_object() ||
                !s["translations"].contains("en") || !s["translations"]["en"].is_array()) {
                errors.emplace_back("Entry " + std::to_string(id) + " sense needs translations.en array.");
            } else {
                for (const auto& gloss : s["translations"]["en"])
                    if (!gloss.is_string())
                        errors.emplace_back("Entry " + std::to_string(id) + " English gloss must be a string.");
            }
            if (!s.contains("usage_notes") || !s["usage_notes"].is_string())
                errors.emplace_back("Entry " + std::to_string(id) + " sense usage_notes must be a string.");
        }
        if (primaryCount > 1)
            errors.emplace_back("Entry " + std::to_string(id) + " has multiple primary senses.");
    }
    if (doc.contains("next_entry_id") && doc["next_entry_id"].is_number_unsigned() &&
        doc["next_entry_id"].get<std::uint64_t>() <= maximum)
        errors.emplace_back("next_entry_id must be greater than every existing id.");

    // Check references only after all IDs/senses have been collected.
    for (const auto& entry : doc["entries"])
    {
        if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_number_unsigned()) continue;
        const auto id = entry["id"].get<std::uint64_t>();
        auto checkRef = [&](const Json& ref, const std::string& where)
        {
            if (ref.is_null()) return; // unresolved compound components are allowed
            if (!ref.is_number_unsigned() || ids.find(ref.get<std::uint64_t>()) == ids.end())
                errors.emplace_back("Entry " + std::to_string(id) + " has broken " + where + " reference.");
        };
        if (entry.contains("morphology") && entry["morphology"].is_object())
        {
            const auto& morph = entry["morphology"];
            if (morph.contains("components") && morph["components"].is_array())
                for (const auto& comp : morph["components"]) {
                    if (!comp.is_object() || !comp.contains("entry_id") ||
                        !comp.contains("surface_form") || !comp["surface_form"].is_string())
                        errors.emplace_back("Entry " + std::to_string(id) + " has a malformed component.");
                    else checkRef(comp["entry_id"], "component");
                }
        }
        if (entry.contains("relations") && entry["relations"].is_array())
        {
            for (const auto& rel : entry["relations"])
            {
                if (!rel.is_object() || !rel.contains("target_id")) continue;
                checkRef(rel["target_id"], "relation");
                if (rel["target_id"].is_number_unsigned() && rel.contains("target_sense_id") &&
                    rel["target_sense_id"].is_number_unsigned())
                    {
                    const auto target = rel["target_id"].get<std::uint64_t>();
                    const auto sid = rel["target_sense_id"].get<std::uint64_t>();
                    if (senseIds[target].count(sid) == 0)
                        errors.emplace_back("Entry " + std::to_string(id) + " references missing target sense.");
                }
            }
        }
        if (entry.contains("examples") && entry["examples"].is_array())
        {
            for (const auto& example : entry["examples"])
            {
                if (example.is_object() && example.contains("sense_id") &&
                    example["sense_id"].is_number_unsigned() &&
                    senseIds[id].count(example["sense_id"].get<std::uint64_t>()) == 0)
                    errors.emplace_back("Entry " + std::to_string(id) + " example references missing sense.");
            }
        }
    }
    return errors;
}

// load dictionary
bool DictionaryEngine::load(const std::filesystem::path& path, std::string& error)
{
    try
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) { error = "Cannot open " + path.string(); return false; }
        Json candidate = Json::parse(file);
        auto issues = validateDocument(candidate);
        if (!issues.empty()) { error = join(issues); return false; }
        document_ = std::move(candidate);
        path_ = path;
        dirty_ = false;
        error.clear();
        return true;
    } catch (const std::exception& ex) { error = ex.what(); return false; }
}

std::vector<std::string> DictionaryEngine::validate() const
{
    return validateDocument(document_);
}

const Json* DictionaryEngine::find(std::uint64_t id) const
{
    if (!document_.is_object() || !document_.contains("entries")) return nullptr;
    for (const auto& entry : document_["entries"])
        if (entry["id"].get<std::uint64_t>() == id) return &entry;
    return nullptr;
}

std::vector<std::uint64_t> DictionaryEngine::search(const std::string& text) const
{
    std::vector<std::uint64_t> ids;
    const std::string q = fold(text);
    if (!document_.is_object()) return ids;
    for (const auto& entry : document_["entries"])
        if (q.empty() || matches(entry, q)) ids.push_back(entry["id"].get<std::uint64_t>());
    std::stable_sort(ids.begin(), ids.end(), [this](auto lhs, auto rhs) {
        return fold(stringValue(*find(lhs), "lemma")) < fold(stringValue(*find(rhs), "lemma"));
    });
    return ids;
}

Json DictionaryEngine::newEntry(std::uint64_t id, const std::string& type)
{
    Json entry = Json::object();
    entry["id"] = id;
    entry["lemma"] = "new_word";
    entry["entry_type"] = type;
    entry["status"] = "draft";
    entry["alternate_spellings"] = Json::array();
    entry["pronunciation"] = {{"ipa", nullptr}, {"phonetic_notes", ""}, {"variants", Json::array()}};
    entry["linguistics"] = {
        {"part_of_speech", type == "affix" ? Json(nullptr) : Json("noun")},
        {"gender", nullptr}, {"grammatical_notes", ""},
        {"inflection", {{"class_id", nullptr}, {"stem", nullptr}, {"irregular_forms", Json::object()}}}
    };
    Json sense = {{"id", std::uint64_t{1}}, {"definition", "New definition"},
        {"translations", {{"en", Json::array()}, {"nl", Json::array()}}},
        {"register", Json::array({"standard"})}, {"usage_labels", Json::array()},
        {"connotation", "neutral"}, {"usage_notes", ""},
        {"is_primary", true}, {"historical_periods", Json::array()}};
    entry["senses"] = Json::array({sense});
    entry["morphology"] = {{"formation_type", "simple"}, {"components", Json::array()},
        {"literal_gloss", nullptr}, {"formation_notes", ""}, {"productive", nullptr}};
    entry["affix"] = type == "affix" ? Json{{"position", "suffix"}, {"function", ""},
        {"productive", true}, {"applies_to", Json::array()}, {"grammatical_notes", ""}} : Json(nullptr);
    entry["script"] = {{"script_override", nullptr}, {"script_notes", ""}};
    entry["culture"] = {{"symbolism", Json::array()}, {"cultural_notes", ""},
        {"social_context", Json::array()}, {"literary_traditions", Json::array()}, {"taboo_notes", ""}};
    entry["relations"] = Json::array();
    entry["history"] = {{"etymology", nullptr}, {"first_attested", nullptr}, {"attestations", Json::array()}};
    entry["examples"] = Json::array();
    entry["notes"] = "";
    entry["created_at"] = nullptr;
    entry["updated_at"] = nullptr;
    return entry;
}

bool DictionaryEngine::add(const std::string& type, std::uint64_t& newId, std::string& error)
{
    if (type != "word" && type != "affix") { error = "Unknown entry type"; return false; }
    try {
        Json candidate = document_;
        const auto id = candidate["next_entry_id"].get<std::uint64_t>();
        if (id == std::numeric_limits<std::uint64_t>::max()) { error = "ID space exhausted"; return false; }
        candidate["entries"].push_back(newEntry(id, type));
        candidate["next_entry_id"] = id + 1;
        auto issues = validateDocument(candidate);
        if (!issues.empty()) { error = join(issues); return false; }
        document_ = std::move(candidate);
        dirty_ = true;
        newId = id;
        error.clear();
        return true;
    } catch (const std::exception& ex) { error = ex.what(); return false; }
}

bool DictionaryEngine::validateReplacement(std::uint64_t id, const Json& replacement, std::string& error) const
{
    if (!replacement.is_object() || !replacement.contains("id") ||
        !replacement["id"].is_number_unsigned() || replacement["id"].get<std::uint64_t>() != id)
        {
        error = "Entry ID is immutable. Edit the other fields instead.";
        return false;
    }
    Json candidate = document_;
    bool found = false;
    for (auto& entry : candidate["entries"])
    {
        if (entry["id"].get<std::uint64_t>() == id)
        {
            entry = replacement;
            found = true;
            break;
        }
    }
    if (!found) { error = "Entry not found"; return false; }
    auto issues = validateDocument(candidate);
    if (!issues.empty()) { error = join(issues); return false; }
    error.clear();
    return true;
}

bool DictionaryEngine::replace(std::uint64_t id, const Json& replacement, std::string& error)
{
    if (!replacement.is_object() || !replacement.contains("id") ||
        !replacement["id"].is_number_unsigned() || replacement["id"].get<std::uint64_t>() != id)
    {
        error = "Entry ID is immutable. Edit the other fields instead.";
        return false;
    }
    if (!validateReplacement(id, replacement, error)) return false;
    Json candidate = document_;
    for (auto& entry : candidate["entries"])
    {
        if (entry["id"].get<std::uint64_t>() == id)
        {
            entry = replacement;
            break;
        }
    }
    document_ = std::move(candidate);
    dirty_ = true;
    error.clear();
    return true;
}

bool DictionaryEngine::remove(std::uint64_t id, std::string& error)
{
    Json candidate = document_;
    bool found = false;
    auto& entries = candidate["entries"];
    for (auto it = entries.begin(); it != entries.end(); ++it)
    {
        if ((*it)["id"].get<std::uint64_t>() == id)
        {
            entries.erase(it);
            found = true;
            break;
        }
    }
    if (!found) { error = "Entry not found"; return false; }
    auto issues = validateDocument(candidate);
    if (!issues.empty()) { error = "Deletion would break references:\n" + join(issues); return false; }
    document_ = std::move(candidate);
    dirty_ = true;
    error.clear();
    return true;
}

bool DictionaryEngine::save(std::string& error)
{
    auto issues = validate();
    if (!issues.empty()) { error = join(issues); return false; }
    if (path_.empty()) { error = "No dictionary path set"; return false; }
    auto temp = path_; temp += ".tmp";
    auto backup = path_; backup += ".bak";
    try
    {
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) { error = "Cannot write temporary file"; return false; }
            out << document_.dump(2) << '\n';
            out.flush();
            if (!out) { error = "Write error"; return false; }
        }
        if (std::filesystem::exists(path_))
            std::filesystem::copy_file(path_, backup, std::filesystem::copy_options::overwrite_existing);
#ifdef _WIN32
        if (!MoveFileExW(temp.wstring().c_str(), path_.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            error = "Failed to replace dictionary file (Windows error " + std::to_string(GetLastError()) + ")";
            return false;
        }
#else
        std::filesystem::rename(temp, path_);
#endif
        dirty_ = false;
        error.clear();
        return true;
    } catch (const std::exception& ex) { error = ex.what(); return false; }
}
} // namespace lex