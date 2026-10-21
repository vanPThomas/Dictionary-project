# Dictionary Project

A small dictionary application I'm developing for my constructed language.

The goal is to replace my old Word-based dictionary with something easier to search, edit, and expand. Eventually, I also want to integrate the language's grammar, phonetics, and other linguistic features.

The project is still in early development.

## Features

- Search and browse dictionary entries.
- Add, edit, and delete words.
- Support for multiple meanings per word.
- Grammatical information, etymology, and cultural notes.
- Cross-references between entries.
- JSON-based storage.

## Built With

- C++20
- Dear ImGui
- GLFW / OpenGL
- nlohmann/json
- CMake

## Building

The project uses CMake. Dependencies are automatically downloaded during the initial configuration.

```bash
mkdir build
cd build
cmake ..

```

To run the application:

```bash
./bin/dictionary-project data/dictionary.json
```

## Plans

There's still plenty I want to add, including a proper grammar system, verb conjugation, phonetic information, and improvements to the interface.

For now, I'm focusing on getting the dictionary itself working the way I want it.

## Status

Work in progress. This is a personal project, mainly developed for my own use and as an opportunity to experiment with C++ and Dear ImGui. But if anyone can use it, you are more than welcome to do so.