# undoApps

undoStudio implements **no domain functionality**. It owns the application
lifecycle, the window, the ImGui framework and the project on disk, and that is
all. Everything else is an undoApp: an independent module, built as a shared object
and loaded from `plugins/`.

The split is the architecture, not a convenience: an undoApp that has to be linked
into the executable cannot be shipped on its own.

## What an undoApp is

| | |
|---|---|
| **Its own lifecycle** | loaded at startup, and it can be unloaded again |
| **Its own panels** | it registers them by name and draws them |
| **Core services** | window management, logging, settings, project access, the file system |
| **Requests, not calls** | the core cannot call into an undoApp; it leaves a request and the undoApp picks it up |

That last one is the rule the recent-files list and the add-METHOD dialog both live
by. A pick in the core's popup leaves a request for the undoApp that owns the tree,
because the tree is what has to be rebuilt afterwards.

## Writing one

A plugin is a shared object exporting **two C symbols**. `createUndoApp` is expected
to have fully initialised the app — registering its panels and all — before it
returns, and `destroyUndoApp` to have removed them again; the pointers are opaque to
the loader, which `dlopen`s and `dlsym`s them and nothing else:

~~~cpp
extern "C" {

void* createUndoApp()
{
   auto& app = MyApp::getInstance();
   app.initialize();   // registers the panels
   return &app;
}

void destroyUndoApp(void* app)
{
   static_cast<MyApp*>(app)->shutdown();   // removes them
}

} // extern "C"
~~~

`undoApps/undoApp.Demo` is that in its smallest form, and
`undoApps/undoApp.Editor` is the same two functions around a real app.

Registering a panel **by a name another undoApp already registered** replaces that
one's callback rather than adding a second panel, so the load order decides which
body is drawn. Panels are listed under **View** and can be shown or hidden.

Three rules a backend has to keep:

- **a backend does not open files and does not decide which one it is.** Every route
  goes through `EditorApp::openFile`, because the tab bar is what answers "what is
  open"
- **a panel owns its window.** `ImGuiManager::render` has already begun it, so a
  `Begin` in a render function opens a second window
- **a popup belongs to the window it is opened in.** `OpenPopup` in one window and
  `BeginPopupModal` in another makes the popup silently never appear, which is why
  the add-METHOD dialog travels as a flag from one to the other

## The ones that exist

| undoApp | |
|---|---|
| **undoApp.Editor** | Structured Text, JSON, text and C++ editing, the Workspace tree and the projects |
| **undoApp.Terminal** | the embedded shell |
| **undoApp.Demo** | the smallest possible undoApp, there to show the shape of one. `BUILD_DEMO_APP` is off by default |

## The ones that are meant to

Each is described by what it is for rather than what it does, because none of them
exists yet.

| undoApp | For |
|---|---|
| **undoApp.PLC** | PLC configuration, tasks, variables, deployment, against undoPLC |
| **undoApp.BUS** | EtherCAT and fieldbus configuration, PDO mapping, network diagnostics |
| **undoApp.Diagnostics** | PLC status, task execution, cycle time, jitter, CPU, runtime errors |
| **undoApp.OPCUA** | OPC UA servers and clients, namespaces, node publishing |
| **undoApp.Motion** | axes, motion profiles, drives, kinematics |
| **undoApp.Import** | migrating TwinCAT, CODESYS and other IEC 61131 projects |
| **undoApp.Cpp** | native C++ components against the undoCore APIs — folded into undoApp.Editor today |

## The ecosystem around it

~~~bash
undoRT
|
|-- undoCore     the core undoStudio builds the project against
|-- undoPLC      the PLC runtime
|-- undoBUS      industrial communication
|-- st2cpp       the Structured Text transpiler
|-- undoOS       the operating system layer
`-- undoStudio   this: the IDE, plus undoApps/
~~~

`st2cpp` is the one that exists as a binary today, and **Compile** shells out to it:
[output.md](output.md). The rest are what the IDE is meant to sit in front of.

## Design goals

**Modularity** — features are independent undoApps. **Extensibility** — a third party
can extend it. **Separation** — runtime, compiler and visualisation stay
independent. **No lock-in** — open architecture. **Scalability** — the same
environment has to hold a small PLC application and a line of industrial machines.