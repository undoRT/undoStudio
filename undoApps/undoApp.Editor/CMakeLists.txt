# ============================================
# undoApp.Editor - Unified Editor Plugin
# ============================================
# This is a shared library, discovered and dlopen()'d at runtime by
# undoStudio::core::PluginManager from the output "plugins/" directory.
# A single public openFile(path) routes to the right backend:
#   .st   -> Structured Text editor
#   .json -> JSON viewer
#   .h/hpp/hxx .c/cpp/cxx -> Cpp viewer
#   else  -> plain-text editor (read / edit / save)

add_library(undoApp.Editor SHARED
    src/undoAppEditor.cpp
    src/undoAppST.cpp
    src/undoAppSTSemantic.cpp
    src/undoAppSTSnippet.cpp
    src/undoAppJSON.cpp
    src/undoAppText.cpp
    src/undoAppCpp.cpp 
)

# ------------------------------------------------------------------
# Include directories
# ------------------------------------------------------------------

set(NLOHMANN_DIR ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json)

target_include_directories(undoApp.Editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include
    ${NLOHMANN_DIR}/include          # nlohmann/json.hpp is header-only
    ${CMAKE_SOURCE_DIR}/third_party/imgui
)

# ------------------------------------------------------------------
# ImGuiColorTextEdit - source file
# ------------------------------------------------------------------
# ImGuiColorTextEdit is a single-file widget that doesn't have a CMakeLists.txt
# so we include it directly.

set(IMGUI_COLOR_TEXT_EDIT_DIR ${CMAKE_SOURCE_DIR}/third_party/ImGuiColorTextEdit)
target_sources(undoApp.Editor PRIVATE
    ${IMGUI_COLOR_TEXT_EDIT_DIR}/TextEditor.cpp
)
target_include_directories(undoApp.Editor PRIVATE
    ${IMGUI_COLOR_TEXT_EDIT_DIR}
)

# ------------------------------------------------------------------
# st2cpp - Structured Text to C++ transpiler
# ------------------------------------------------------------------
# Include all necessary source files from the st2cpp library.
# The CLI main.cpp is excluded to avoid duplicate main() symbols.

set(ST2CPP_DIR ${CMAKE_SOURCE_DIR}/third_party/st2cpp)

# Collect all .cpp files from the source subdirectories
file(GLOB_RECURSE ST2CPP_SOURCES
    ${ST2CPP_DIR}/src/lexer/*.cpp
    ${ST2CPP_DIR}/src/parser/*.cpp
    ${ST2CPP_DIR}/src/codegen/*.cpp
    ${ST2CPP_DIR}/src/ast/*.cpp
    ${ST2CPP_DIR}/src/*.cpp
)

# Remove the CLI main.cpp file (it contains an entry point we don't want)
list(FILTER ST2CPP_SOURCES EXCLUDE REGEX ".*/src/cli/.*\\.cpp$")

# Ensure essential files are included even if GLOB missed them
list(APPEND ST2CPP_SOURCES
    ${ST2CPP_DIR}/src/lexer/Lexer.cpp
    ${ST2CPP_DIR}/src/parser/Parser.cpp
    ${ST2CPP_DIR}/src/parser/ParserStmt.cpp
)

target_sources(undoApp.Editor PRIVATE ${ST2CPP_SOURCES})

target_include_directories(undoApp.Editor PRIVATE
    ${ST2CPP_DIR}/include
    ${ST2CPP_DIR}/include/ast
    ${ST2CPP_DIR}/include/lexer
    ${ST2CPP_DIR}/include/parser
    ${ST2CPP_DIR}/include/codegen
)

# ------------------------------------------------------------------
# tinyfiledialogs (optional) - for native file dialogs
# ------------------------------------------------------------------

if(USE_TINYFILEDIALOGS)
    target_sources(undoApp.Editor PRIVATE ${TINYFILEDIALOGS_SOURCES})
    target_include_directories(undoApp.Editor PRIVATE ${TINYFILEDIALOGS_INCLUDE_DIR})
    target_compile_definitions(undoApp.Editor PRIVATE USE_TINYFILEDIALOGS)
endif()

# ------------------------------------------------------------------
# Link dependencies
# ------------------------------------------------------------------

target_link_libraries(undoApp.Editor PRIVATE
    undoStudioCore
    imgui
    implot
)

# System libraries that st2cpp may need
find_package(Threads REQUIRED)
target_link_libraries(undoApp.Editor PRIVATE
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# ------------------------------------------------------------------
# Target properties
# ------------------------------------------------------------------

set_target_properties(undoApp.Editor PROPERTIES
    PREFIX ""                              # Don't prepend "lib" to the output filename
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/plugins
    POSITION_INDEPENDENT_CODE ON            # Required for shared libraries
)

# ------------------------------------------------------------------
# Compile definitions
# ------------------------------------------------------------------

target_compile_definitions(undoApp.Editor PRIVATE
    ST2CPP_HEADER_ONLY=1   # Enable header-only mode in st2cpp (avoids missing undoCore)
)