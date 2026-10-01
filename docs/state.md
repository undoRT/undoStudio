# State between runs

The IDE writes four files next to the layout. They are yours, they are not tracked,
and deleting any of them puts that part of the setup back to its default.

| File | What it holds |
|---|---|
| `undoStudio_layout.ini` | the dock layout — which panels are where, and which are visible. A first run adopts the one under `resources/`, which **is** tracked, because it is the layout a fresh install opens with |
| `undoStudio.ini` | the window size, the recent projects, the recent files, and the one limit they share |
| `undoApp.Editor.ini` | where the Variables and Body sections are divided |
| `undoApp.Terminal.ini` | the size the terminal is drawn at |

Everything else — `imgui.ini`, the per-undoApp preferences — is Dear ImGui's own
window and menu state.

## The state files are the IDE's, not the project's

They sit in the **working directory**, which is why an installed copy has to be
started from the directory holding `resources/` and `plugins/`. They are ignored by
git and safe to delete: nothing in a project reads them.

## `[recent] max` is one number for two lists

`undoStudio.ini` holds both lists in one `[recent]` section, and `max` is read by
both:

~~~ini
[recent]
max = 10
recent_project_0 = /home/user/projects/main
recent_project_1 = /home/user/projects/line2
recent_file_0 = /home/user/projects/main/undoCore/undoFB.st
~~~

The limit in the popup is the answer to "how much of this do I want", and there is
nothing useful to configure about that separately for two lists of the same kind.
Changing it through one list trims the other on the next load.

## Why the working directory

Fonts, icons, themes, the shipped layout and the undoApps are all opened by relative
path — `resources/…` and `plugins/`. They resolve against the working directory, not
against the executable, which is why the binary is written next to `resources/` and
why a package that moves it away needs a launcher that changes directory first.