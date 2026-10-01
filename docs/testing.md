# Testing

Three suites, all headless: **no GPU and no display**. They create an ImGui context
with a font atlas and drive real frames.

~~~bash
./src/core/tests/run_tests.sh                      # 12 tests
./undoApps/undoApp.Editor/tests/run_tests.sh       # 26 tests
./undoApps/undoApp.Terminal/tests/run_tests.sh     # 2 tests

# a subset, by name
./undoApps/undoApp.Editor/tests/run_tests.sh completion
./src/core/tests/run_tests.sh recents
~~~

The Editor and Terminal suites need the project built first: they link against
`libundoStudioCore.so`, `libimgui.a` and `libimplot.a`.

## What each one covers

| Suite | |
|---|---|
| core | the state files, the recent projects and files lists, the open-file request between the core and an undoApp, the popup dismissal, and the shipped layout |
| Editor | the ST document, the completion lists and their placement, the semantic tokens, the diagnostics end to end, the tab bar, the dirty state, renames, the output filters, and the JSON routing |
| Terminal | the shell under a pty, and its tab bar |

The Editor suite links each test against one archive of the sources the tests share
and reaches into the editor's internals with a `#define private public`. Two things
worth knowing before adding a test:

- **a new plugin source file has to be added to `PLUGIN_SRC`** in
  `undoApps/undoApp.Editor/tests/Makefile`, or every test that needs it fails to link
  — and the failure looks like a missing symbol rather than a missing entry in a list
- a test that drives ImGui must not be listed in `PURE`, the set that skips the ImGui
  link line, or it fails at link time pointing at the archive rather than at the list

## Driving the GUI without a window

A panel body draws contents only, because `ImGuiManager` owns the window. So a test
that draws one has to `Begin` the panel itself:

- `tests/st_editor_frame.hpp` is that `Begin` for the ST document
- the Output, Workspace and JSON panels are `Begin`/`End`-ed by hand

A body called bare trips ImGui's own "Calling End() too many times" assert, which
points at the assert rather than at the `Begin` that is missing.

## The four things that are not obvious

- **a widget is found in the draw list, not by name.** ImGui keeps no rect per item
  across frames, and no API asks for the rectangle of an item it is not drawing.
  The draw list has to be read *during* the frame that filled it: after `EndFrame`
  the window is gone. And a widget found by its fill colour is not found while it is
  hovered, so the mouse is parked away before measuring
- **a click is three events and three frames.** Position, press and release, each its
  own frame, with the panel redrawn in every one of them: ImGui hit-tests each item as
  it is submitted, so a frame that queues a mouse event and draws nothing has nothing
  to hit. A checkbox toggles on the release
- **`ContentSize` is one frame behind.** A window publishes the size its content had
  during the *previous* frame. Read straight after changing a filter, the two
  directions measure equal, which reads as "the filter does nothing"
- **typing needs a click first.** The editor raises "the text changed" only for input
  it handled *while being drawn*, so a test cannot type with `SetText` — a load uses
  that too. It has to click into the editor and then feed `io.AddInputCharacter`, and
  the click is also the only thing that focuses the editor's child window

## What the tests are for

Each one has found a real bug: a fuzzy match stealing the member completion, 1841
cells in 1920 painted the wrong colour, a key written above its own section header,
a recent-files row that rendered in the right order in the right place with the
right name on it and could not be picked because its ID belonged to another row.