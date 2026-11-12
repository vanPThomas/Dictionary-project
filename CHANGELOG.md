# Changelog

All notable changes to the New Language Dictionary project will be documented in this file.

This changelog follows the [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format.

## [1.0.0] - 2026-11-12

### Added

- Initial stable release of the New Language Dictionary application.
- Desktop graphical interface using Dear ImGui, GLFW, and OpenGL.
- JSON-based dictionary engine with persistent file storage.
- Support for word and affix entries.
- Automatic assignment of permanent numerical entry IDs.
- Search functionality covering dictionary entries, definitions, and translations.
- Two-panel interface with dictionary browser and entry editor.
- Quick Edit interface for lemmas, grammatical information, meanings, and notes.
- Advanced JSON editor for modifying complete dictionary entries.
- Support for multiple senses, translations, morphological information, cultural notes, etymology, examples, and relationships between entries.
- Dictionary validation, including entry integrity and cross-reference checking.
- Confirmation dialogs to protect unapplied editor changes.
- Ability to create, modify, and delete dictionary entries.
- Explicit Apply and Save operations to distinguish in-memory changes from changes written to disk.

### Performance

- Optimized dictionary search and alphabetical sorting.
- Added search-result caching to avoid redundant searches during rendering.
- Implemented `ImGuiListClipper` to efficiently display large result lists.

### Technical

- C++20 application with CMake build configuration.
- Separation of dictionary data management (`DictionaryEngine`) and graphical interface (`DictionaryUI`).
- Modular source structure for future expansion.

---

