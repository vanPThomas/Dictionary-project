#pragma once

#include "DictionaryEngine.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

class DictionaryUI
{
public:
    explicit DictionaryUI(std::filesystem::path path);

    bool loaded() const;
    const std::string& message() const;
    void draw();

private:
    lex::DictionaryEngine engine_;
    bool loaded_ = false;
    std::string message_;
    std::string query_;
    std::uint64_t selected_ = 0;
    lex::Json draft_;
    std::string rawJson_;
    bool rawEdited_ = false;
    std::function<void()> pendingAction_;
    std::optional<std::uint64_t> navigation_;
    // Cached search results, avoiding a full dictionary search every frame.
    std::vector<std::uint64_t> searchResults_;

    // Whether the cached results need to be updated.
    bool searchDirty_ = true;

    bool draftChanged() const;
    void select(std::uint64_t id);
    void request(std::function<void()> action);
    void newEntry(const std::string& type);
    bool importRawJson();
    bool applyDraft();
    void saveAll();
    void validate();

    void renderBrowser();
    void renderSenses();
    void linkButton(std::uint64_t id, const std::string& context);
    void renderLinkedEntries();
    void renderQuickEditor();
    void renderAdvancedEditor();
    void renderEditor();
    void renderPopups();
};
