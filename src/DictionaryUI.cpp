#include "DictionaryUI.hpp"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace
{
    using lex::Json;

    // return a string from a json object from a specific field
    std::string fieldText(const Json& j, const char* field)
    {
        return j.is_object() && j.contains(field) && j[field].is_string()
            ? j[field].get<std::string>() : ""; // get tells you to get the string associated with the field
    }

    // edit a json string directly by showing an input text field
    void editText(const char* label, Json& parent, const char* key)
    {
        if (!parent.contains(key) || !parent[key].is_string()) parent[key] = "";
        ImGui::InputText(label, &parent[key].get_ref<std::string&>()); /// get_ref retrieves a reference to a string, vs get, which creates a new string
    }

    // edit miltiple lines
    void editMultiline(const char* label, Json& parent, const char* key, float height = 75.0f)
    {
        if (!parent.contains(key) || !parent[key].is_string()) parent[key] = ""; // if field is empty, return null pointer, otherwise return an empty string
        ImGui::InputTextMultiline(label, &parent[key].get_ref<std::string&>(), ImVec2(-1, height));
    }

    // Null in JSON has a different meaning from a blank string.
    void editOptionalText(const char* label, Json& parent, const char* key)
    {
        std::string text = fieldText(parent, key);
        if (ImGui::InputText(label, &text))
            parent[key] = text.empty() ? Json(nullptr) : Json(text);
    }

    // extracting a nested field
    std::string partOfSpeech(const Json& entry)
    {
        return entry.contains("linguistics")
            ? fieldText(entry["linguistics"], "part_of_speech") : "";
    }

    // retrieve the primary definition of a word
    std::string primaryDefinition(const Json& entry)
    {
        if (!entry.contains("senses") || entry["senses"].empty()) return "";
        for (const auto& s : entry["senses"])
            if (s.value("is_primary", false)) return fieldText(s, "definition"); // check if the field is_primary is true, if field doesn't exist, return false
        return fieldText(entry["senses"][0], "definition");// return first if no entry is marked as primary
    }
} // namespace

// UI constructor
DictionaryUI::DictionaryUI(std::filesystem::path path)
{
    std::string error;
    if (!engine_.load(path, error))
    {
        message_ = "Cannot open dictionary: " + error;
        // A missing/invalid dictionary is fatal for this simple prototype.
        loaded_ = false;
    }
    else
    {
        loaded_ = true;
        message_ = "Loaded " + std::to_string(engine_.size()) + " entries.";
        const auto ids = engine_.search("");
        if (!ids.empty()) select(ids.front());
    }
}

bool DictionaryUI::loaded() const { return loaded_; }

const std::string& DictionaryUI::message() const { return message_; }

// Draw UI, triggered every cylce
void DictionaryUI::draw()
{
    // Preparing main window
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize); // Take up entire screen workspace
    ImGui::Begin("New Language Dictionary", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar); // start new window and control what user can do with the window.  

    // Menu bar
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save dictionary")) saveAll();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Entry"))
        {
            if (ImGui::MenuItem("New word")) request([this] { newEntry("word"); }); // use newWord through lambda expression, makes it safe when there are unsaved changes
            if (ImGui::MenuItem("New affix")) request([this] { newEntry("affix"); });
            if (selected_ && ImGui::MenuItem("Delete selected"))
                ImGui::OpenPopup("Delete entry?");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools"))
        {
            if (ImGui::MenuItem("Validate saved entries")) validate();
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar(); // End menu bar
    }

    // Action buttons
    if (ImGui::Button("+ Word")) request([this] { newEntry("word"); });
    ImGui::SameLine();
    if (ImGui::Button("+ Affix")) request([this] { newEntry("affix"); });
    ImGui::SameLine();
    if (ImGui::Button("Apply entry")) applyDraft();
    ImGui::SameLine();
    if (ImGui::Button("Save JSON")) saveAll();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu entries%s", engine_.size(), engine_.dirty() ? "  * not saved to disk" : ""); // final entry is  aternery expression. if engine contains changes not saved to disk, give warning

    if (ImGui::BeginTable("main_layout", 2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(-1, -55)))
    {
        // set up columns, one is the search field, the other the editor 
        ImGui::TableSetupColumn("Search", ImGuiTableColumnFlags_WidthFixed, 330.0f);
        ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("browser", ImVec2(0, 0))) renderBrowser();
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        if (ImGui::BeginChild("editor", ImVec2(0, 0))) renderEditor();
        ImGui::EndChild();
        ImGui::EndTable();
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", message_.c_str());

    renderPopups();
    // End main window
    ImGui::End();
}

// Compare entry you've edited with the version stored in teh dictionary
bool DictionaryUI::draftChanged() const
{
    const auto* stored = engine_.find(selected_); // retrieve the stored entry
    return stored && draft_ != *stored;
}

// select an entry for editing
void DictionaryUI::select(std::uint64_t id)
{
    const auto* entry = engine_.find(id); // find entry in the engine using id
    if (!entry) { message_ = "Unknown entry #" + std::to_string(id); return; }
    selected_ = id;
    draft_ = *entry;
    rawJson_ = draft_.dump(2);
    rawEdited_ = false;
}

// Request execution of an action.
//
// If the editor has unapplied changes, store the action
// for later and request confirmation from the user.
// Otherwise, execute the action immediately.
//
// std::function<void()> can hold any compatible callable,
// including capturing lambdas. It takes no arguments and
// returns nothing.
void DictionaryUI::request(std::function<void()> action)
{
    // Check both the structured draft and raw JSON editor.
    if (draftChanged() || rawEdited_)
    {
        // Move the callable into our persistent member variable.
        // It can be executed later from the confirmation popup.
        pendingAction_ = std::move(action);

        // Request the unapplied-edits confirmation popup.
        ImGui::OpenPopup("Unapplied edits");
    }
    else
    {
        // No pending edits: execute the callable immediately.
        action();
    }
}


// Create a new dictionary entry of the requested type.
//
// The engine creates the entry and assigns a permanent ID.
// The UI then selects the new entry for editing.
void DictionaryUI::newEntry(const std::string& type)
{
    // Output variables filled by engine_.add().
    std::uint64_t id = 0;
    std::string error;

    // Ask the engine to create the entry.
    // Returns false if creation fails.
    if (!engine_.add(type, id, error))
    {
        message_ = error;
        return;
    }

    // Select the newly created entry.
    // This also prepares draft_ and rawJson_.
    select(id);

    // Inform the user that the new entry has not yet
    // been written to the JSON file on disk.
    message_ = "Created entry #" + std::to_string(id)
             + " (not saved to disk yet).";
}


// // import json dictionary
// bool DictionaryUI::importRawJson() {
//     try
//     {
//         Json parsed = Json::parse(rawJson_);
//         if (!parsed.is_object()) { message_ = "Entry JSON must be an object."; return false; }
//         std::string error;
//         if (!engine_.validateReplacement(selected_, parsed, error)) {
//             message_ = "Cannot import invalid entry:\n" + error;
//             return false;
//         }
//         draft_ = std::move(parsed);
//         rawEdited_ = false;
//         return true;
//     }
//     catch (const std::exception& ex)
//     {
//         message_ = std::string("Invalid entry JSON: ") + ex.what();
//         return false;
//     }
// }


/*
    Import manually edited JSON text into the current draft.

    The function:
    1. Parses rawJson_ into a JSON value.
    2. Checks that the result is an object.
    3. Validates it against the dictionary schema.
    4. Moves the validated data into draft_.
    5. Resets the raw-editor modification flag.

    Returns true on success, false on failure.

    Does NOT commit the entry to DictionaryEngine
    or save anything to disk.
*/
bool DictionaryUI::importRawJson()
{
    // JSON parsing and other operations may throw exceptions.
    try
    {
        // Convert raw JSON text into a structured JSON value.
        // Throws an exception if the JSON syntax is invalid.
        Json parsed = Json::parse(rawJson_);

        // Dictionary entries must be JSON objects.
        if (!parsed.is_object())
        {
            message_ = "Entry JSON must be an object.";
            return false;
        }

        std::string error;

        // Validate the proposed entry using DictionaryEngine.
        // Checks IDs, required fields, references, etc.
        // Does not actually modify the engine.
        if (!engine_.validateReplacement(selected_, parsed, error))
        {
            message_ = "Cannot import invalid entry:\n" + error;
            return false;
        }

        // Validation succeeded.
        // Move the parsed JSON into the editable draft.
        // Avoids unnecessarily copying the entire JSON object.
        draft_ = std::move(parsed);

        // The raw text has now been imported into the draft.
        rawEdited_ = false;

        // Import successful.
        return true;
    }
    catch (const std::exception& ex)
    {
        // Handle JSON parsing errors and other standard exceptions.
        // ex.what() provides a human-readable error description.
        message_ = std::string("Invalid entry JSON: ") + ex.what();

        // Leave the existing draft unchanged if importing fails.
        return false;
    }
}

/*
    Commit the current editor draft to DictionaryEngine.

    If raw JSON has been edited, import and validate it first.
    Only replace the stored entry if the draft has changed.

    Returns true if successful or nothing needs applying.
    Returns false if importing or replacing fails.

    Does NOT save changes to disk.
*/
bool DictionaryUI::applyDraft()
{
    // No selected entry means there's nothing to apply.
    // This isn't an error, so return success.
    if (!selected_)
        return true;

    // If the Advanced JSON editor has pending changes,
    // parse and validate them before continuing.
    if (rawEdited_ && !importRawJson())
        return false;

    // If the draft matches the stored entry,
    // there's nothing to commit.
    if (!draftChanged())
        return true;

    // Output variable for engine validation errors.
    std::string error;

    // Replace the stored entry with the edited draft.
    // The engine validates the replacement before accepting it.
    if (!engine_.replace(selected_, draft_, error))
    {
        message_ = "Cannot apply entry:\n" + error;
        return false;
    }

    // Synchronize the Advanced JSON editor's text
    // with the newly committed draft.
    rawJson_ = draft_.dump(2);

    // Inform the user that the changes are applied
    // in memory but haven't been saved to disk.
    message_ = "Applied entry #"
             + std::to_string(selected_)
             + ". Save JSON to write it to disk.";

    return true;
}

// save dictionary
void DictionaryUI::saveAll()
{
    // applay the draft, otherwise return
    if (!applyDraft()) return;
    std::string error;
    // save teh data, return teh save location, otherwise throw error
    if (engine_.save(error)) message_ = "Saved to " + engine_.path().string();
    else message_ = "Save failed:\n" + error;
}

// validate commited entries
void DictionaryUI::validate()
{
    // validate engine, return vector of problems
    auto issues = engine_.validate();
    // if issues is empty, good! everything is good to go
    message_ = issues.empty() ? "All committed entries passed validation." : "Validation failed:\n";
    // loop over potential problems in issues
    for (const auto& issue : issues) message_ += "- " + issue + "\n";
    // check if there is anything left to apply and give warning of there is something
    if (draftChanged() || rawEdited_)
        message_ += "(Current editor draft is not validated until you press Apply entry.)";
}

// render the browser part of the program, the left side
void DictionaryUI::renderBrowser()
{
    ImGui::TextUnformatted("Find a word");
    ImGui::SetNextItemWidth(-1);

    // Search only when query changes. (if triggers when something is typed into search bar)
    if (ImGui::InputTextWithHint("##search", "Lemma, definition, translation...", &query_))
    {
        searchDirty_ = true;
    }

    // Refresh cached results if necessary. search dirty becme true when something was typed in search bar. so search!
    if (searchDirty_)
    {
        searchResults_ = engine_.search(query_);
        searchDirty_ = false;
    }

    // number of results
    ImGui::TextDisabled("%zu results", searchResults_.size());
    ImGui::Separator();

    // Only process entries currently visible in the browser.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(searchResults_.size()));

    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            const std::uint64_t id = searchResults_[i];

            const Json* e = engine_.find(id);
            if (!e)
                continue;

            const std::string lemma = fieldText(*e, "lemma");

            std::string category = partOfSpeech(*e);
            if (category.empty())
                category = fieldText(*e, "entry_type");

            const std::string label =
                lemma + "   (" + category + ")##entry"
                + std::to_string(id);

            if (ImGui::Selectable(label.c_str(), selected_ == id))
            {
                request([this, id] { select(id); });
            }

            ImGui::TextDisabled(
                "#%llu  %s",
                static_cast<unsigned long long>(id),
                primaryDefinition(*e).c_str()
            );

            ImGui::Separator();
        }
    }

    clipper.End();
}


void DictionaryUI::renderSenses()
{
    if (!ImGui::CollapsingHeader("Meanings / senses", ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto& senses = draft_["senses"];
    for (std::size_t i = 0; i < senses.size(); ++i)
    {
        auto& sense = senses[i];
        const auto sid = sense["id"].get<std::uint64_t>();
        ImGui::PushID(static_cast<int>(sid));
        const std::string title = "Sense #" + std::to_string(sid);
        bool deleteSense = false;
        if (ImGui::TreeNode(title.c_str()))
        {
            editMultiline("Definition", sense, "definition", 58.0f);
            bool isPrimary = sense.value("is_primary", false);
            if (ImGui::Checkbox("Primary sense", &isPrimary))
            {
                if (isPrimary) for (auto& other : senses) other["is_primary"] = false;
                sense["is_primary"] = isPrimary;
            }
            auto& en = sense["translations"]["en"];
            ImGui::TextUnformatted("English equivalents");
            for (std::size_t j = 0; j < en.size(); ++j)
            {
                ImGui::PushID(static_cast<int>(j));
                ImGui::SetNextItemWidth(-65);
                ImGui::InputText("##gloss", &en[j].get_ref<std::string&>());
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) { en.erase(en.begin() + j); --j; }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+ English gloss")) en.push_back("");
            editMultiline("Usage notes", sense, "usage_notes", 52.0f);
            if (ImGui::Button("Remove this sense")) deleteSense = true;
            ImGui::TreePop();
        }
        ImGui::PopID();
        if (deleteSense) { senses.erase(senses.begin() + i); --i; }
    }
    if (ImGui::Button("+ Add sense"))
    {
        std::uint64_t next = 1;
        for (const auto& sense : senses)
            next = std::max(next, sense["id"].get<std::uint64_t>() + 1);
        Json s = lex::DictionaryEngine::newEntry(1, "word")["senses"][0];
        s["id"] = next;
        s["is_primary"] = false;
        senses.push_back(std::move(s));
    }
}

void DictionaryUI::linkButton(std::uint64_t id, const std::string& context)
{
    const Json* target = engine_.find(id);
    const std::string name = target ? fieldText(*target, "lemma") : "MISSING";
    const std::string label = context + ": " + name + " (#" + std::to_string(id) + ")";
    if (ImGui::SmallButton(label.c_str())) navigation_ = id;
}

void DictionaryUI::renderLinkedEntries()
{
    if (!ImGui::CollapsingHeader("Components / relations")) return;
    const Json& morph = draft_["morphology"];
    for (const auto& c : morph["components"])
    {
        if (c["entry_id"].is_number_unsigned())
            linkButton(c["entry_id"].get<std::uint64_t>(), "Component");
        else ImGui::TextDisabled("Unresolved component: %s", fieldText(c, "surface_form").c_str());
    }
    for (const auto& r : draft_["relations"])
    {
        if (r.contains("target_id") && r["target_id"].is_number_unsigned())
            linkButton(r["target_id"].get<std::uint64_t>(), fieldText(r, "type"));
    }
    ImGui::TextDisabled("Edit links and compound roles in Advanced JSON.");
}

void DictionaryUI::renderQuickEditor() {
    ImGui::TextDisabled("ID #%llu (permanent)", static_cast<unsigned long long>(selected_));
    editText("Lemma", draft_, "lemma");
    editText("Status", draft_, "status");
    ImGui::TextDisabled("Type: %s", fieldText(draft_, "entry_type").c_str());
    auto& ling = draft_["linguistics"];
    editOptionalText("Part of speech", ling, "part_of_speech");
    editOptionalText("Gender", ling, "gender");
    editMultiline("Grammatical notes", ling, "grammatical_notes", 54.0f);
    renderSenses();
    renderLinkedEntries();
    if (ImGui::CollapsingHeader("Culture"))
        editMultiline("Cultural notes", draft_["culture"], "cultural_notes");
    if (ImGui::CollapsingHeader("History"))
        editOptionalText("Etymology", draft_["history"], "etymology");
    if (ImGui::CollapsingHeader("Examples"))
    {
        for (const auto& ex : draft_["examples"])
        {
            ImGui::BulletText("%s", fieldText(ex, "text").c_str());
            if (ex.contains("translations"))
                ImGui::TextDisabled("%s", fieldText(ex["translations"], "en").c_str());
        }
        ImGui::TextDisabled("Use Advanced JSON to add/edit examples.");
    }
    editMultiline("Internal notes", draft_, "notes");
}

void DictionaryUI::renderAdvancedEditor()
{
    ImGui::TextWrapped("Full entry JSON. It includes phonetics, morphology, cultural "
                       "symbolism, history, example sentences, and references.");
    if (ImGui::Button("Refresh text from form"))
    {
        rawJson_ = draft_.dump(2);
        rawEdited_ = false;
        message_ = "Refreshed JSON text. Previous unapplied JSON text was discarded.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Import JSON into form"))
    {
        if (importRawJson()) message_ = "JSON parsed into the editor draft. Apply entry to validate it.";
    }
    if (rawEdited_)
        ImGui::TextWrapped("JSON text has pending edits; Apply entry or Save JSON will parse them first.");
    const float height = std::max(220.0f, ImGui::GetContentRegionAvail().y - 15.0f);
    if (ImGui::InputTextMultiline("##rawJson", &rawJson_, ImVec2(-1, height),
                                 ImGuiInputTextFlags_AllowTabInput)) rawEdited_ = true;
}

void DictionaryUI::renderEditor()
{
    if (!selected_) { ImGui::TextUnformatted("Select or create an entry."); return; }
    const Json* saved = engine_.find(selected_);
    if (!saved) return;
    ImGui::Text("%s", fieldText(draft_, "lemma").c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("#%llu%s", static_cast<unsigned long long>(selected_),
                        (draftChanged() || rawEdited_) ? "  (unapplied edits)" : "");
    ImGui::Separator();
    if (ImGui::BeginTabBar("editor_tabs"))
    {
        if (ImGui::BeginTabItem("Quick edit"))
        {
            renderQuickEditor();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Advanced JSON"))
        {
            renderAdvancedEditor();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (navigation_)
    {
        const auto target = *navigation_;
        navigation_.reset();
        request([this, target] { select(target); });
    }
}

void DictionaryUI::renderPopups() {
    if (ImGui::BeginPopupModal("Unapplied edits", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("Entry #%llu has changes not yet applied to the dictionary.",
                           static_cast<unsigned long long>(selected_));
        if (ImGui::Button("Apply and continue"))
        {
            if (applyDraft())
            {
                auto action = std::move(pendingAction_);
                pendingAction_ = {};
                ImGui::CloseCurrentPopup();
                if (action) action();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard and continue"))
        {
            auto action = std::move(pendingAction_);
            pendingAction_ = {};
            ImGui::CloseCurrentPopup();
            if (action) action();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { pendingAction_ = {}; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Delete entry?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Permanently remove entry #%llu from the in-memory dictionary?",
                    static_cast<unsigned long long>(selected_));
        ImGui::TextWrapped("Removal is not saved until Save JSON. Referenced entries cannot be removed.");
        if (ImGui::Button("Delete")) {
            std::string error;
            if (engine_.remove(selected_, error))
            {
                const auto ids = engine_.search(query_);
                selected_ = 0;
                if (!ids.empty()) select(ids.front());
                message_ = "Removed entry. Save JSON to write the change.";
            } else message_ = error;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##deletion")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
