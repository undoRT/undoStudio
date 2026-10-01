# Changelog

## [0.1.1] - 2026-10-01

### Added
- The editor has tabs. `OpenDocuments` holds the set of open files, which one is
  on screen, and which of them are permanent, and it is the only thing that
  answers those questions: the backends behind it know about one file each, and
  none of them knows that a second `.st` is open somewhere. A click in the tree
  opens a file and keeps it, the tab bar scrolls sideways rather than squeezing,
  each tab carries a dot while it has changes that are not on disk and a close
  button, and the panel is sized from what it holds rather than by a zero that
  ImGui reads as "all of it" (a bar of that kind grew to the height of the whole
  ST Editor panel and pushed the editor it sat above out of sight).
- A file that is open and not on screen is held whole, per backend:
  `STDocument` for the ST panes, the text and its path for the others. A tab
  that comes back shows what was in it, at the cursor and the pane division it
  was left at. The AST, the semantic analysis and the source map are not kept:
  they are derived, and a snapshot of them is stale the moment a character is
  typed.
- The files that were opened are remembered between runs, under **File > Open
  Recent Files** or `Ctrl+P`. The list is persistent, newest first, and it is fed
  by every route into the editor rather than by the menu: a file is remembered
  however it was reached. It shares one length with the recent projects list
  (`[recent] max`), because one remembered-things-too-many is enough, and a
  rename or a move rewrites the entry in place rather than leaving an entry for a
  file that is no longer there. A file deleted outside the IDE stays in the list
  and is shown as missing, so that an entry disappearing is never a mistake.
- What the IDE is handed is opened: a file or a project on the command line, and
  files dropped onto the window. `OpenTarget` decides which of the two a path is
  and leaves a request, because the undoApp that can act on it is a plugin and the
  core cannot call into one. The desktop entry gained `%F` and the JSON and
  plain-text MIME types, so a `.st` file can be opened from the file manager.
- The ST Output panel can be filtered. The generated ST was dumped into the same
  list as the diagnostics, so for any file of size the two errors worth reading
  arrived somewhere under hundreds of lines of it. `undoAppSTOutput.hpp` holds the
  decisions, apart from the panel that draws them, because what is counted is what
  is hidden: a panel that says "0 errors" while hiding them is worse than one that
  says nothing. The switches persist per workspace, and a search field filters
  what is left.
- The terminal has one tab per shell, each with its own close button. Every
  session is pumped rather than only the one on screen: a shell left in a
  background tab is a process that is still running, and its output was sitting
  in the kernel buffer until the user came back and then arrived all at once.
- Three suites, all headless and all runnable on a machine with no display:
  11 core tests, 23 editor tests and 1 terminal test. Each one has found a real
  bug; the ones that found one are named below.

### Changed
- A single click in the tree keeps its file in a tab. It used to open a preview
  that the next click replaced, so the file being read was gone from the bar the
  moment another was opened, and holding on to one meant double clicking, which
  is a rule nobody can see — and ImGui only counts a double click when both
  clicks land within a few pixels of each other. The preview slot survives in the
  model for a caller that means "I am looking through these" and asks for it
  explicitly, and browsing past a preview that holds edits pins it.
- A rename or a move is followed. Four things file an open document under its
  path: the tab (which is its ImGui id), the key its stashed text is held under,
  the path the editor is told to save to, and the entry in the recent files list.
  A rename that moved three of the four was not a stale label but a second file:
  `Ctrl+S` wrote a copy where the original used to be, and the file the stash held
  came back with the old path inside it. A folder carries every file inside it,
  so renaming one moves their tabs too, and deleting a file or a folder closes
  the tabs that pointed at it rather than leaving rows that offer to open
  something that is gone.
- The unsaved mark is read from the editors once a frame and nowhere else. It
  used to be set when a tab was left, which answered a different question with
  the same answer: "keep this tab" and "this has changes" are not the same thing,
  so every tab that had been looked at twice carried an asterisk that nothing took
  off, because no save path cleared it either. `Ctrl+S` now reaches `.st`, `.cpp`
  and text files from any panel, and clears the mark only after the file opened
  for writing.
- "Open as Text" on a `.json` gives the file a tab of its own. It used to close
  the editors and load the text behind their backs, so the file had no name in
  the bar, nothing in the list to ask for it again, and one file could be open in
  two backends at once. It is one file, so it is one row, and the row is the text
  one.
- The ST editor panel lost its `File: <name>` row and its `(unsaved changes)`
  text: the tab bar drawn a few lines above already says both. The tooltip switch
  moved onto the button row, where a per-editor setting belongs. The POU type
  stayed, because it is the only place that writes down whether the file declares
  a `PROGRAM`, a `FUNCTION_BLOCK` or a `FUNCTION`, and a `FUNCTION`'s return type
  hangs off it.
- The recent projects list is loaded the first time it is asked for. Loading it
  only happened on the calls that open or forget a project, so a run in which
  nobody opened anything showed an empty list while the entries sat in the state
  file, which reads as a lost history.
- The editor suite compiles the sources the tests share once into an archive
  instead of once per test. It used to be the whole cost of the suite: each of
  the tests recompiled the plugin and the st2cpp sources around it, in sequence.
- The shipped layout no longer carries the default Debug window, so a first run
  does not open a panel with nothing in it.

### Fixed
- A row of a recent-files list could not be picked when another row had the same
  file name in a different folder. `PushID` covered the button and stopped before
  the `Selectable`, so both rows were drawn and submitted under the label alone.
  Both popups had it, and it looked right: the rows rendered, in order, with the
  right name on them, and the first pick worked.
- The recent-files popup was measured on the frame it opened, which is the size
  asked for rather than the size of its contents: a list of two rows reported
  itself as one row high. ImGui only learns how tall a window's content is once it
  has laid it out, so a popup is drawn for a few frames before it is read.
- Escape did nothing after asking for a workspace folder. The fallback dialog
  called `OpenPopup` with no `Begin` anywhere to match it, so the entry stayed on
  ImGui's open stack for the rest of the session and swallowed every Escape
  pressed afterwards.
- A file list row is not found by its fill colour while the cursor is on it:
  ImGui draws a button under the cursor in the hovered colour, and the row that
  had just been clicked moved out from under the pointer anyway. The tests park
  the mouse before measuring, which is also the only way to say why.
- Switching tabs marked the file being left as edited, and nothing ever took the
  mark off, because `stashActiveDocument` was asked to do two things at once.
  Reading it once a frame from the editors is the fix, and a save that failed
  leaves the mark alone, because it is the last thing standing between an edit
  and losing it.
- Closing a project left the previous project's files in the tab bar. The bar is
  what a user looks at to see what is open, so it was claiming files that the
  editors were not holding and that could not be opened.
- The recent projects and recent files lists could be emptied by a limit of zero
  read back from a hand-edited state file, with nothing to say why. The limit is
  clamped on the way out as well as on the way in, and the two lists are trimmed
  to it on the next load.
- The second and later terminal tabs were folded into the first. Every tab is
  titled "Terminal" until the shell renames it, and ImGui derives a tab's id from
  its label alone, so the shells ran and could not be seen or closed. Selecting a
  tab that was appended behind ImGui's back needed `ImGuiTabItemFlags_SetSelected`
  on the item: `ImGuiTabBarFlags_AutoSelectNewTabs` only selects when the bar has
  nothing selected, which is why the `+` button left the old tab on screen.

### Known issues
- The GUI has never been seen running. This was developed on a machine with no X
  display, so everything visual is verified by compiling it, by a headless ImGui
  test that drives real frames, or by asking. Anything that is a question of
  appearance should be treated as unverified until somebody has looked at it.
- "Open as Text" is only offered on `.json`, which is the only backend that
  cannot show its file as text.
- A terminal tab cannot be reordered by dragging. ImGui would move the tab and
  the panel would keep drawing the shell it thinks is there, so it is not offered
  rather than offered and wrong.
- Opening a text file and saving it without editing it adds a newline at the end.
  The editor normalises the text it holds, and the save writes what it holds.
- The editor suite is the slow one: it compiles the plugin and st2cpp before it
  runs anything. The archive took the per-test cost out of it; what is left is
  the cost of building the thing once.
