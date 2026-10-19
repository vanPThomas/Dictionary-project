#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_stdlib.h"
#include "DictionaryEngine.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace
{
using lex::Json;

std::string fieldText(const Json& j, const char* field)
{
    return j.is_object() && j.contains(field) && j[field].is_string()
        ? j[field].get<std::string>() : "";
}

void editText(const char* label, Json& parent, const char* key)
{
    if (!parent.contains(key) || !parent[key].is_string()) parent[key] = "";
    ImGui::InputText(label, &parent[key].get_ref<std::string&>());
}

void editMultiline(const char* label, Json& parent, const char* key, float height = 75.0f)
{
    if (!parent.contains(key) || !parent[key].is_string()) parent[key] = "";
    ImGui::InputTextMultiline(label, &parent[key].get_ref<std::string&>(), ImVec2(-1, height));
}

// Null in JSON has a different meaning from a blank string.
void editOptionalText(const char* label, Json& parent, const char* key)
{
    std::string text = fieldText(parent, key);
    if (ImGui::InputText(label, &text))
        parent[key] = text.empty() ? Json(nullptr) : Json(text);
}

std::string partOfSpeech(const Json& entry)
{
    return entry.contains("linguistics")
        ? fieldText(entry["linguistics"], "part_of_speech") : "";
}

std::string primaryDefinition(const Json& entry)
{
    if (!entry.contains("senses") || entry["senses"].empty()) return "";
    for (const auto& s : entry["senses"])
        if (s.value("is_primary", false)) return fieldText(s, "definition");
    return fieldText(entry["senses"][0], "definition");
}

class DictionaryUI
{
public:
    explicit DictionaryUI(std::filesystem::path path)
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

    bool loaded() const { return loaded_; }
    const std::string& message() const { return message_; }

    void draw()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("New Language Dictionary", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar);

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save dictionary")) saveAll();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Entry"))
            {
                if (ImGui::MenuItem("New word")) request([this] { newEntry("word"); });
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
            ImGui::EndMenuBar();
        }

        if (ImGui::Button("+ Word")) request([this] { newEntry("word"); });
        ImGui::SameLine();
        if (ImGui::Button("+ Affix")) request([this] { newEntry("affix"); });
        ImGui::SameLine();
        if (ImGui::Button("Apply entry")) applyDraft();
        ImGui::SameLine();
        if (ImGui::Button("Save JSON")) saveAll();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu entries%s", engine_.size(), engine_.dirty() ? "  * not saved to disk" : "");

        if (ImGui::BeginTable("main_layout", 2,
                ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(-1, -55)))
        {
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
        ImGui::End();
    }

private:
    lex::DictionaryEngine engine_;
    bool loaded_ = false;
    std::string message_;
    std::string query_;
    std::uint64_t selected_ = 0;
    Json draft_;
    std::string rawJson_;
    bool rawEdited_ = false;
    std::function<void()> pendingAction_;
    std::optional<std::uint64_t> navigation_;

    bool draftChanged() const
    {
        const auto* stored = engine_.find(selected_);
        return stored && draft_ != *stored;
    }

    void select(std::uint64_t id)
    {
        const auto* entry = engine_.find(id);
        if (!entry) { message_ = "Unknown entry #" + std::to_string(id); return; }
        selected_ = id;
        draft_ = *entry;
        rawJson_ = draft_.dump(2);
        rawEdited_ = false;
    }

    void request(std::function<void()> action)
    {
        if (draftChanged() || rawEdited_)
        {
            pendingAction_ = std::move(action);
            ImGui::OpenPopup("Unapplied edits");
        }
        else
        {
            action();
        }
    }

    void newEntry(const std::string& type)
    {
        std::uint64_t id = 0;
        std::string error;
        if (!engine_.add(type, id, error)) { message_ = error; return; }
        select(id);
        message_ = "Created entry #" + std::to_string(id) + " (not saved to disk yet).";
    }

    bool importRawJson() {
        try
        {
            Json parsed = Json::parse(rawJson_);
            if (!parsed.is_object()) { message_ = "Entry JSON must be an object."; return false; }
            std::string error;
            if (!engine_.validateReplacement(selected_, parsed, error)) {
                message_ = "Cannot import invalid entry:\n" + error;
                return false;
            }
            draft_ = std::move(parsed);
            rawEdited_ = false;
            return true;
        }
        catch (const std::exception& ex)
        {
            message_ = std::string("Invalid entry JSON: ") + ex.what();
            return false;
        }
    }

    bool applyDraft()
    {
        if (!selected_) return true;
        if (rawEdited_ && !importRawJson()) return false;
        if (!draftChanged()) return true;
        std::string error;
        if (!engine_.replace(selected_, draft_, error))
        {
            message_ = "Cannot apply entry:\n" + error;
            return false;
        }
        rawJson_ = draft_.dump(2);
        message_ = "Applied entry #" + std::to_string(selected_) + ". Save JSON to write it to disk.";
        return true;
    }

    void saveAll() {
        if (!applyDraft()) return;
        std::string error;
        if (engine_.save(error)) message_ = "Saved to " + engine_.path().string();
        else message_ = "Save failed:\n" + error;
    }

    void validate()
    {
        auto issues = engine_.validate();
        message_ = issues.empty() ? "All committed entries passed validation." : "Validation failed:\n";
        for (const auto& issue : issues) message_ += "- " + issue + "\n";
        if (draftChanged() || rawEdited_)
            message_ += "(Current editor draft is not validated until you press Apply entry.)";
    }

    void renderBrowser()
    {
        ImGui::TextUnformatted("Find a word");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##search", "Lemma, definition, translation...", &query_);
        const auto results = engine_.search(query_);
        ImGui::TextDisabled("%zu results", results.size());
        ImGui::Separator();
        for (const auto id : results)
        {
            const Json* e = engine_.find(id);
            if (!e) continue;
            const std::string lemma = fieldText(*e, "lemma");
            std::string category = partOfSpeech(*e);
            if (category.empty()) category = fieldText(*e, "entry_type");
            const std::string label = lemma + "   (" + category + ")##entry" + std::to_string(id);
            if (ImGui::Selectable(label.c_str(), selected_ == id)) {
                request([this, id] { select(id); });
            }
            ImGui::TextDisabled("#%llu  %s", static_cast<unsigned long long>(id),
                                primaryDefinition(*e).c_str());
            ImGui::Separator();
        }
    }

    void renderSenses()
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

    void linkButton(std::uint64_t id, const std::string& context)
    {
        const Json* target = engine_.find(id);
        const std::string name = target ? fieldText(*target, "lemma") : "MISSING";
        const std::string label = context + ": " + name + " (#" + std::to_string(id) + ")";
        if (ImGui::SmallButton(label.c_str())) navigation_ = id;
    }

    void renderLinkedEntries()
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

    void renderQuickEditor() {
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

    void renderAdvancedEditor()
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

    void renderEditor()
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

    void renderPopups() {
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
};

void glfwError(int code, const char* description)
{
    std::cerr << "GLFW error " << code << ": " << description << '\n';
}

void loadUnicodeFont()
{
    // Do not bundle a font: use an installed OS font with Latin-1 glyphs.
    const char* paths[] =
    {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf"
    };
    for (const char* path : paths)
    {
        if (std::filesystem::exists(path))
        {
            ImGui::GetIO().Fonts->AddFontFromFileTTF(path, 17.0f);
            return;
        }
    }
    // Default font may not render every character in the constructed language.
}
} // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path dictionaryPath = argc >= 2 ? argv[1] : "data/dictionary.json";
    glfwSetErrorCallback(glfwError);
    if (!glfwInit()) return 1;

#if defined(__APPLE__)
    const char* glsl = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 820, "New Language Dictionary", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    loadUnicodeFont();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl);

    DictionaryUI ui(dictionaryPath);
    if (!ui.loaded()) {
        std::cerr << ui.message() << '\n';
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ui.draw();

        ImGui::Render();
        int displayWidth = 0, displayHeight = 0;
        glfwGetFramebufferSize(window, &displayWidth, &displayHeight);
        glViewport(0, 0, displayWidth, displayHeight);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}