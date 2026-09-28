# undoStudio

## Overview

undoStudio is the integrated development environment (IDE) of the undoRT ecosystem.

It provides a unified environment for developing, configuring, debugging and deploying industrial automation applications.

Unlike traditional industrial automation environments, undoStudio is designed as a lightweight and extensible platform based on independent applications called **undoApps**.

Each undoApp extends undoStudio with specific functionality, allowing users to install only the required tools and enabling third-party developers to create new extensions.

The goal is to create an open, modular and scalable automation development environment.

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
