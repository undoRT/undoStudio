# ============================================
# undoApp.Demo - built as a plugin, not linked into undoStudio
# ============================================
# This is a shared library, discovered and dlopen()'d at runtime by
# undoStudio::core::PluginManager from the output "plugins/" directory.
# It must NOT appear in target_link_libraries(undoStudio ...): linking it
# statically would defeat the point of a dynamically-loaded plugin.

add_library(undoApp.Demo SHARED
    DemoApp.cpp
)

target_include_directories(undoApp.Demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Link against undoStudioCore (for ImGuiManager::getInstance(), etc.) and
# against imgui/implot directly, so the plugin can call ImGui/ImPlot
# functions itself.
target_link_libraries(undoApp.Demo PRIVATE
    undoStudioCore
    imgui
    implot
)

set_target_properties(undoApp.Demo PROPERTIES
    BUILD_RPATH "$ORIGIN/.."
    INSTALL_RPATH "$ORIGIN/.."
    PREFIX ""                                             # keep the plugin filename as "undoApp.Demo.so", no "lib" prefix
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/plugins
    POSITION_INDEPENDENT_CODE ON
)