# undoStudio

## Overview

undoStudio is the integrated development environment (IDE) of the undoRT ecosystem.

It provides a unified environment for developing, configuring, debugging and deploying industrial automation applications.

Unlike traditional industrial automation environments, undoStudio is designed as a lightweight and extensible platform based on independent applications called **undoApps**.

Each undoApp extends undoStudio with specific functionality, allowing users to install only the required tools and enabling third-party developers to create new extensions.

The goal is to create an open, modular and scalable automation development environment.

## Requirements

| | |
|---|---|
| OS | Linux (developed and tested on Ubuntu 24.04) |
| Compiler | C++17, GCC 13 or Clang 15 and later |
| CMake | 3.15 or later |
| GPU | OpenGL 3.x, through GLFW |

### System packages

Debian and Ubuntu:

~~~bash
sudo apt install build-essential cmake git

# OpenGL and GLU
sudo apt install libgl1-mesa-dev libglu1-mesa-dev freeglut3-dev

# X11, which is what GLFW builds against here
sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev

# glm, header-only, and a hard requirement: CMake stops if it is missing
sudo apt install libglm-dev
~~~

Everything else is a submodule: GLFW, Dear ImGui, implot, ImGuiColorTextEdit,
st2cpp, stb, tinyfiledialogs and nlohmann_json are built from source, so there is
no library to install for them.

## Build

~~~bash
git clone --recursive https://github.com/undoRT/undoStudio.git
cd undoStudio
~~~

If the repository was cloned without `--recursive`, or a submodule was added
later:

~~~bash
git submodule update --init --recursive
~~~

Then configure and build:

~~~bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
~~~

Object files and CMake state stay in `build/`, but everything that is looked up
at run time is written next to `resources/`, because that is how the IDE finds
it:

| | |
|---|---|
| executable | `undoStudio` |
| core library | `libundoStudioCore.so` |
| undoApps | `plugins/*.so` |
| fonts, icons, themes, layout | `resources/` |

An in-source build (`cmake -B .`) works as well and differs only in that the
intermediates land in the root too.

### Build options

| Option | Default | Effect |
|---|---|---|
| `BUILD_EDITOR_APP` | `ON` | Build the Editor undoApp, the ST/JSON/text/C++ environment |
| `BUILD_DEMO_APP` | `OFF` | Build the Demo undoApp |
| `USE_SYSTEM_GLFW` | `OFF` | Use the system GLFW instead of the bundled one |
| `ENABLE_IMGUI_DOCKING` | `ON` | Build Dear ImGui with docking support |
| `USE_TINYFILEDIALOGS` | `OFF` | Use tinyfiledialogs for native file dialogs |

## Run

Run it from the repository root:

~~~bash
./build/undoStudio
~~~

The root matters: fonts, icons and themes are opened by relative path, so from
anywhere else the IDE comes up without them. For the same reason an installed
copy has to be started from the directory that holds `resources/` and
`plugins/`, which is where `cmake --install` writes them, `share/undoStudio`.

## The Structured Text transpiler

The Editor hands the project to **st2cpp** when Compile is pressed. st2cpp is
built as a submodule for the editor's own analysis, but the command-line binary
is built separately:

~~~bash
cmake -S third_party/st2cpp -B build/st2cpp -DCMAKE_BUILD_TYPE=Release
cmake --build build/st2cpp -j"$(nproc)"
~~~

That leaves the binary at `build/st2cpp/st2cpp`, which is where the Editor looks
for it. If st2cpp is on the `PATH` instead, that is tried first, and the Editor
prints the path it used in the Output panel. Nothing breaks while it is missing:
Compile reports that it cannot find the transpiler and everything else works.

## Tests

Both suites run headless, so no GPU and no display are needed.

The core: the state files, and the recent projects list:

~~~bash
./src/core/tests/run_tests.sh

# or a subset, by name
./src/core/tests/run_tests.sh recents
~~~

It needs the project to have been built, since one of the two links against
`libundoStudioCore.so`.

The Editor, which builds each test on its own and drives real ImGui frames
without a window:

~~~bash
./undoApps/undoApp.Editor/tests/run_tests.sh

# or a subset, by name
./undoApps/undoApp.Editor/tests/run_tests.sh completion
~~~

The Editor's suite needs `libimgui.a`, `libimplot.a` and
`libundoStudioCore.so` in the build tree, so build the project before running it.

## What is remembered between runs

The IDE writes its own state next to the layout, and each file has one owner:

| File | What it holds |
|---|---|
| `undoStudio_layout.ini` | the dock layout, which a first run takes from `resources/` |
| `undoStudio.ini` | the window size, and the projects opened recently |
| `undoApp.Editor.ini` | where the Variables and Body sections are divided |
| `undoApp.Terminal.ini` | the size the terminal is drawn at |

All four are yours to edit and are not tracked. Delete any of them and that part
of the setup goes back to its default.

The recent projects are under **File > Open Recent**, or `Ctrl+R`. A project is
not reopened on its own: the list is one click away and starting the IDE with
somebody's project already on disk is a surprise. `Ctrl+R` is left alone while a
code editor or the terminal has the keyboard, where the same two keys mean
something else.

## Repository layout

~~~bash
undoStudio/
|-- CMakeLists.txt            top-level build, install and CPack
|-- include/undoStudio/       public headers of the core
|   |-- core/                 application, plugins, projects
|   |-- services/             window service interface
|   `-- ui/                   ImGui framework
|-- src/                      the same, implemented
|-- resources/                fonts, icons, themes
|-- undoApps/
|   |-- undoApp.Demo/         the smallest possible undoApp
|   `-- undoApp.Editor/       ST, JSON, text and C++ editing
|       `-- tests/            headless tests
|-- third_party/              submodules
`-- build/                    out-of-source build tree
~~~

## Architecture Philosophy

Traditional automation IDEs are usually monolithic applications where every feature is tightly integrated into a single environment.

undoStudio follows a different approach:

~~~bash
            undoStudio
                |
    -----------------------------
    |            |              |
 undoApp      undoApp        undoApp
   ST          BUS        Diagnostics

                |
             undoCore
~~~

undoStudio provides:

- Application lifecycle management
- User interface framework
- Docking system
- Project management
- Configuration management
- Build orchestration
- Plugin management
- Common services

Domain-specific functionality is implemented through undoApps.

## undoRT Ecosystem

The complete undoRT ecosystem is composed of independent modules:

~~~bash
undoRT

|
|-- undoCore
|
|-- undoPLC
|
|-- undoBUS
|
|-- st2cpp
|
|-- undoOS
|
-- undoStudio | -- undoApps
~~~

Each component has a well-defined responsibility.

## undoStudio architecture

undoStudio is the central development environment for the undoRT ecosystem.

Responsibilities:

- Project management
- Source code editing
- Build management
- Runtime connection
- Debugging
- Visualization
- Diagnostics
- Deployment
- Extension management

undoStudio itself does not implement domain-specific automation features.

Instead, it provides a common framework where undoApps can operate.

## undoApp Concept

An undoApp is an independent application module that extends undoStudio.

Examples:

- ST programming environment
- C++ development tools
- PLC configuration
- EtherCAT management
- Motion control
- Diagnostics
- Import tools

Each undoApp:

- Has its own lifecycle
- Provides its own user interface
- Can register menus and panels
- Can access undoStudio services
- Can communicate with undoRT components

## undoApp Architecture

A generic undoApp follows this model:

~~~bash
+---------------------------+
| undoStudio |
| |
| +---------------------+ |
| | undoApp | |
| | | |
| | UI Components | |
| | Services | |
| | Configuration | |
| | Runtime Interface | |
| +---------------------+ |
| |
+---------------------------+
~~~

undoStudio exposes common interfaces:

- Window management
- Logging
- Settings
- Project access
- File system
- Runtime communication
- Event system

undoApps use these services without duplicating infrastructure.

## Official undoApps

What the ecosystem is meant to end up with. Two of these exist today:
**undoApp.Editor**, which is the Structured Text, JSON, text and C++ editing
environment, and **undoApp.Demo**, which is the worked example of what an undoApp
looks like. The rest are still to be built, and the headings below describe what
each is for rather than what it does.

### undoApp.ST

Structured Text development environment.

Provides:

- IEC 61131-3 Structured Text editor
- Syntax highlighting
- Code navigation
- Project integration
- Build integration with st2cpp

Workflow:

~~~bash
Structured Text

  |
  v

st2cpp

  |
  v

Generated C++

  |
  v

C++ Compiler

  |
  v

undoPLC Runtime
~~~

### undoApp.Cpp

Native C++ development environment.

Provides:

- C++ editor
- Build configuration
- Library management
- Integration with undoCore APIs

Allows developers to write native real-time components directly.

### undoApp.PLC

PLC engineering environment.

Provides:

- PLC configuration
- Task management
- Variable management
- Runtime deployment

Interfaces directly with undoPLC.

### undoApp.Diagnostics

Runtime diagnostic environment.

Provides visibility into the running system:

- PLC status
- Task execution
- Cycle time
- Jitter
- CPU usage
- Memory usage
- Runtime errors
- Logs

Example:

~~~txt
Runtime

Status:
RUNNING

Cycle:
1 ms

Jitter:
8 us

CPU:
15 %
~~~

### undoApp.Import

Project migration tools.

Responsible for importing existing automation projects.

Examples:

- TwinCAT projects
- CODESYS projects
- Other IEC 61131 environments

The goal is to simplify migration towards the undoRT ecosystem.

### undoApp.BUS

Industrial communication configuration.

Provides:

- EtherCAT configuration
- Fieldbus management
- PDO mapping
- Device configuration
- Network diagnostics

Interfaces with:

~~~txt
undoBUS
~~~

### undoApp.OPCUA

OPC UA configuration and management.

Provides:

- Server configuration
- Namespace management
- Variable publishing
- Client connections

### undoApp.Motion

Motion control environment.

Provides:

- Axis configuration
- Motion profiles
- Drive configuration
- Kinematic systems

## Future undoApps

Possible future extensions:

- **undoApp.Scope**
- **Real-time oscilloscope**
- **undoApp.HMI**
- **Visualization editor**
- **undoApp.Simulation**
- **Offline simulation environment**
- **undoApp.PackageManager**
- **Install and update undoApps**
- **undoApp.Cloud**
- **Remote monitoring and deployment**

## Plugin System

undoStudio is designed around a plugin architecture.

An undoApp can be:

- Included with undoStudio
- Installed separately
- Developed by third parties
- Distributed through a future marketplace

Possible future workflow:

~~~bash
undoStudio

  |

Package Manager

  |

Download undoApp

  |

Install Extension

  |

Available in IDE
~~~

## Design Goals

undoStudio should always maintain these principles:

### Modularity

Features must be implemented as independent undoApps.

### Extensibility

Third-party developers should be able to extend the ecosystem.

### Separation of concerns

Runtime, compiler and visualization tools must remain independent.

### Open architecture

No proprietary lock-in.

### Scalability

The same environment should support:

- small PLC applications
- complex industrial machines
- robotics
- motion systems
- distributed automation systems

## Summary

undoStudio is not only a graphical interface.

It is the development platform of undoRT.

The platform provides the foundation, while undoApps provide specialized automation capabilities.

This architecture allows undoRT to evolve from a PLC runtime into a complete open industrial automation ecosystem.
