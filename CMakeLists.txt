
cmake_minimum_required(VERSION 3.16)

project(Dictionary-project VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------
# Dependencies
# -------------------------------------------------

include(FetchContent)

# JSON
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)

# Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(json glfw imgui)

find_package(OpenGL REQUIRED)

# -------------------------------------------------
# Dictionary Engine
# -------------------------------------------------

add_library(dictionary_engine
    src/DictionaryEngine.cpp
)

target_include_directories(dictionary_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dictionary_engine PUBLIC
    nlohmann_json::nlohmann_json
)

# -------------------------------------------------
# Dear ImGui Library
# -------------------------------------------------

add_library(imgui_runtime STATIC

    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp

    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp

    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)

target_include_directories(imgui_runtime PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
)

target_link_libraries(imgui_runtime PUBLIC
    glfw
    OpenGL::GL
)

# -------------------------------------------------
# Dictionary Application
# -------------------------------------------------

add_executable(dictionary-project
    src/main.cpp
    src/DictionaryUI.cpp
)

target_include_directories(dictionary-project PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dictionary-project PRIVATE
    dictionary_engine
    imgui_runtime
)

target_compile_definitions(dictionary-project PRIVATE
    PROJECT_VERSION="${PROJECT_VERSION}"
)

# -------------------------------------------------
# Output
# -------------------------------------------------

set_target_properties(dictionary-project PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
