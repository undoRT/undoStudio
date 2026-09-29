# ============================================
# undoApp.Terminal - built as a plugin, not linked into undoStudio
# ============================================
# An embedded terminal: a real shell on a real pseudo-terminal, with libvterm
# interpreting what it writes.
#
# As with every undoApp this is a shared library that PluginManager dlopen()'s
# from plugins/. It must not appear in undoStudio's link libraries, or loading it
# dynamically would be pointless.
#
# libvterm is C and the plugin is C++, so its sources are compiled as C and linked
# into the plugin. Vendoring it as a submodule rather than a system library keeps
# the terminal the same version everywhere.

set(LIBVTERM_DIR ${CMAKE_SOURCE_DIR}/third_party/libvterm)

add_library(undoApp.Terminal SHARED
    src/undoAppTerminal.cpp
    src/TerminalSession.cpp
    src/TerminalView.cpp
    # libvterm, compiled as C
    ${LIBVTERM_DIR}/src/encoding.c
    ${LIBVTERM_DIR}/src/keyboard.c
    ${LIBVTERM_DIR}/src/mouse.c
    ${LIBVTERM_DIR}/src/parser.c
    ${LIBVTERM_DIR}/src/pen.c
    ${LIBVTERM_DIR}/src/screen.c
    ${LIBVTERM_DIR}/src/state.c
    ${LIBVTERM_DIR}/src/unicode.c
    ${LIBVTERM_DIR}/src/vterm.c
)

target_include_directories(undoApp.Terminal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/imgui
    ${LIBVTERM_DIR}/include
    ${LIBVTERM_DIR}/src
)

target_link_libraries(undoApp.Terminal PRIVATE
    undoStudioCore
    imgui
    implot
)

# The pseudo-terminal calls live in libutil on older glibc and in libc on newer
# ones; asking for it is harmless either way.
find_library(UTIL_LIBRARY util)
if(UTIL_LIBRARY)
    target_link_libraries(undoApp.Terminal PRIVATE ${UTIL_LIBRARY})
endif()

set_target_properties(undoApp.Terminal PROPERTIES
    PREFIX ""                                             # "undoApp.Terminal.so", no "lib" prefix
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/plugins
    POSITION_INDEPENDENT_CODE ON
)
