# CMakeLists.txt - Build configuration for undoStudio
# Updated for latest GLFW syntax with ImGui docking support

cmake_minimum_required(VERSION 3.15)
project(undoStudio VERSION 0.1.2)

# C++ standard settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(BUILD_DEMO_APP "Build the demo undoApp plugin" OFF)
option(BUILD_EDITOR_APP "Build the multi-format Editor plugin (ST, JSON, C++)" ON)
option(BUILD_TERMINAL_APP "Build the Terminal plugin (embedded shell)" ON)
option(USE_SYSTEM_GLFW "Use system GLFW instead of bundled" OFF)
option(ENABLE_IMGUI_DOCKING "Enable ImGui docking support" ON)
option(USE_TINYFILEDIALOGS "Use tinyfiledialogs for native file dialogs" OFF)

# ============================================
# Output layout
# ============================================
# Fonts, icons and themes are opened by relative path, and the undoApps are
# looked up in plugins/, so what is needed at run time has to end up beside
# resources/ rather than buried in the build tree. Intermediates still go to
# build/, which is what a -B build configure is for.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

# ============================================
# Include directories
# ============================================
include_directories(include)

# Find glm (header-only)
find_path(GLM_INCLUDE_DIRS glm/glm.hpp PATHS /usr/include /usr/local/include)
if(NOT GLM_INCLUDE_DIRS)
    message(FATAL_ERROR "glm not found")
endif()

# ============================================
# GLFW Library
# ============================================
if(USE_SYSTEM_GLFW)
    find_package(glfw3 REQUIRED)
else()
    # Configure GLFW - Use X11 only, disable Wayland
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    # Use the new variable names for GLFW 3.4+
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "Disable Wayland support" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "Enable X11 support" FORCE)

    add_subdirectory(third_party/glfw)
endif()

# ============================================
# ImGui Library
# ============================================
add_library(imgui STATIC)

# Core ImGui files
set(IMGUI_CORE
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
)

# ImGui backends
set(IMGUI_BACKENDS
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)

target_sources(imgui PRIVATE ${IMGUI_CORE} ${IMGUI_BACKENDS})
target_include_directories(imgui PUBLIC
    third_party/imgui
    third_party/imgui/backends
    third_party/stb
    ${GLM_INCLUDE_DIRS}
)

# ImGui compile definitions
target_compile_definitions(imgui PUBLIC
    IMGUI_DEFINE_MATH_OPERATORS
)

# ImGui is linked into undoStudioCore (a shared library) AND potentially
# directly into plugins, so it must be position-independent regardless of
# platform default.
set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Platform-specific settings for ImGui
if(WIN32)
    target_compile_definitions(imgui PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Link dependencies
target_link_libraries(imgui PUBLIC glfw)

# ============================================
# ImPlot Library
# ============================================
add_library(implot STATIC)

set(IMPLOT_SOURCES
    third_party/implot/implot.cpp
    third_party/implot/implot_items.cpp
)

target_sources(implot PRIVATE ${IMPLOT_SOURCES})
target_include_directories(implot PUBLIC third_party/implot)
target_link_libraries(implot PUBLIC imgui)
set_target_properties(implot PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ============================================
# tinyfiledialogs (optional, for native file dialogs)
# ============================================
if(USE_TINYFILEDIALOGS)
    add_definitions(-DUSE_TINYFILEDIALOGS)
    
    # Try to find tinyfiledialogs in third_party
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/tinyfiledialogs/tinyfiledialogs.c")
        set(TINYFILEDIALOGS_SOURCES
            ${CMAKE_SOURCE_DIR}/third_party/tinyfiledialogs/tinyfiledialogs.c
        )
        set(TINYFILEDIALOGS_INCLUDE_DIR
            ${CMAKE_SOURCE_DIR}/third_party/tinyfiledialogs
        )
        message(STATUS "tinyfiledialogs found in third_party")
    else()
        message(WARNING "tinyfiledialogs not found. Install it in third_party/tinyfiledialogs/")
        message(WARNING "Download from: https://sourceforge.net/projects/tinyfiledialogs/")
        set(USE_TINYFILEDIALOGS OFF)
    endif()
endif()

# ============================================
# undoStudioCore - shared library
# ============================================
add_library(undoStudioCore SHARED
    src/core/Application.cpp
    src/core/PluginManager.cpp
    src/core/ProjectManager.cpp
    src/core/OpenTarget.cpp
    src/core/RecentFiles.cpp
    src/core/Settings.cpp
    src/services/WindowServiceGLFW.cpp
    src/ui/ImGuiManager.cpp
)

target_include_directories(undoStudioCore PUBLIC include
    ${GLM_INCLUDE_DIRS}
    third_party/stb
    third_party/glfw/include
    third_party/nlohmann_json/single_include   # nlohmann/json.hpp is header-only
)

target_link_libraries(undoStudioCore PUBLIC
    imgui
    implot
)

target_link_libraries(undoStudioCore PRIVATE
    ${CMAKE_DL_LIBS}
)

# Platform-specific linking for undoStudioCore
if(WIN32)
    target_link_libraries(undoStudioCore PUBLIC opengl32)
    target_compile_definitions(undoStudioCore PRIVATE WIN32_LEAN_AND_MEAN)
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(X11 REQUIRED x11)
    target_link_libraries(undoStudioCore PUBLIC ${X11_LIBRARIES})

    if(OPENGL_FOUND)
        target_link_libraries(undoStudioCore PUBLIC ${OPENGL_LIBRARIES})
    else()
        find_library(GL_LIBRARY GL)
        if(GL_LIBRARY)
            target_link_libraries(undoStudioCore PUBLIC ${GL_LIBRARY})
        endif()
    endif()

    target_link_libraries(undoStudioCore PUBLIC
        Xrandr
        Xinerama
        Xcursor
        Xi
    )
endif()

if(MSVC)
    target_compile_options(undoStudioCore PRIVATE /W4)
else()
    target_compile_options(undoStudioCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ============================================
# Main Application (executable)
# ============================================
add_executable(undoStudio
    src/main.cpp
)

target_link_libraries(undoStudio PRIVATE
    undoStudioCore
)

if(MSVC)
    target_compile_options(undoStudio PRIVATE /W4)
else()
    target_compile_options(undoStudio PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ============================================
# Plugins (undoApps)
# ============================================
if(BUILD_DEMO_APP)
    add_subdirectory(undoApps/undoApp.Demo)
endif()

if(BUILD_EDITOR_APP)
    add_subdirectory(undoApps/undoApp.Editor)
endif()

if(BUILD_TERMINAL_APP)
    add_subdirectory(undoApps/undoApp.Terminal)
endif()

# ============================================
# Relocatable linking
# ============================================
# CMake bakes the absolute build tree into RUNPATH by default, which finds
# libundoStudioCore.so in place and nowhere else. $ORIGIN finds it wherever
# the tree is unpacked, which is what a release archive needs. The undoApps
# sit one level down, in plugins/, so they reach the core through $ORIGIN/..
set_target_properties(undoStudio PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)
set_target_properties(undoStudioCore PROPERTIES
    INSTALL_RPATH "$ORIGIN"
)

# ============================================
# Installation
# ============================================
install(TARGETS undoStudio undoStudioCore
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

if(BUILD_DEMO_APP)
    install(TARGETS undoApp.Demo
        LIBRARY DESTINATION share/undoStudio/plugins
    )
endif()

if(BUILD_EDITOR_APP)
    install(TARGETS undoApp.Editor
        LIBRARY DESTINATION share/undoStudio/plugins
    )
endif()

if(BUILD_TERMINAL_APP)
    install(TARGETS undoApp.Terminal
        LIBRARY DESTINATION share/undoStudio/plugins
    )
endif()

install(DIRECTORY resources/
    DESTINATION share/undoStudio/resources
)

install(FILES resources/undoStudio.desktop
    DESTINATION share/applications
)

# ============================================
# Testing
# ============================================
enable_testing()

add_test(NAME test_application
    COMMAND undoStudio --test
)

# ============================================
# Output summary
# ============================================
message(STATUS "")
message(STATUS "=== undoStudio Build Configuration ===")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Build Demo App (plugin): ${BUILD_DEMO_APP}")
message(STATUS "Build Editor App (plugin): ${BUILD_EDITOR_APP}")
message(STATUS "Build Terminal App (plugin): ${BUILD_TERMINAL_APP}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "OpenGL Found: ${OPENGL_FOUND}")
message(STATUS "GLFW Build X11: ${GLFW_BUILD_X11}")
message(STATUS "GLFW Build Wayland: ${GLFW_BUILD_WAYLAND}")
message(STATUS "ImGui Docking: ${ENABLE_IMGUI_DOCKING}")
message(STATUS "tinyfiledialogs: ${USE_TINYFILEDIALOGS}")
message(STATUS "======================================")
message(STATUS "")