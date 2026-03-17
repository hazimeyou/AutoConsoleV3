# AGENTS.md

## Project
AutoConsole v3

## Purpose
AutoConsole v3 is a generic execution platform for console applications.
It must not be designed as a Minecraft-only tool.
Minecraft Java/Bedrock, ffmpeg, Python scripts, SSH-like CLI tools, and other console apps are all valid targets.

The architecture should be similar to:
- Core = loader/runtime
- Standard Plugins = platform API
- External Plugins = extensions
- UI = separate client

---

## Core Design Rules

### Core must stay generic
Do not put app-specific logic into Core.
Do not put Minecraft-specific, ffmpeg-specific, SSH-specific, or workflow-specific behavior into Core.

Core is responsible only for:
- process launch
- process stop
- stdin write
- stdout/stderr read
- session state management
- event generation and dispatch
- plugin host
- UI connection

### Core must stay thin
Do not grow Core into a macro engine or workflow engine.
Complex automation belongs to plugins.

### Session and Profile must remain separate
- Profile = static definition of target app execution
- Session = runtime instance created from a Profile

Do not mix them.

### Event-driven architecture is required
Plugins and UI must react through Events.
Avoid tight coupling between components.

---

## Plugin Rules

### Plugins are the main extension unit
Most automation logic must live in plugins.
If a feature can live in a plugin, do not put it in Core.

### Macro engine is NOT a required Core feature
Do not implement a built-in complex macro language in v3.0.0.
If macro-like behavior is needed, implement it as a plugin later.

### Plugins must use PluginContext
Plugins must not directly manipulate Core internals.
All interaction with Core must go through PluginContext.

### Plugin types
The design should support:
- Standard Plugins
- External Plugins
- Macro Plugin later if needed

---

## v3.0.0 Scope Rules

### Include in v3.0.0
- generic console process execution
- stdin/stdout/stderr handling
- session model
- event system
- plugin host
- plugin context
- standard plugins
- CLI UI
- profile loading

### Explicitly exclude from v3.0.0
- GUI UI
- Web UI
- ConPTY / pseudo console support
- advanced macro DSL
- advanced conditional workflow language
- SSH-specialized support
- plugin marketplace/store
- sandbox/security isolation
- cloud/distributed features

---

## Platform Rules

### OS target
v3.0.0 targets Windows only.

### Process I/O model
Use pipe-based process control for v3.0.0.
Do not add ConPTY in this version.

### Encoding
Prefer UTF-8-oriented design, but keep room for profile-based encoding settings.

---

## Project Structure Rules

The solution must remain split at least into:
- AutoConsole.Abstractions
- AutoConsole.Core
- AutoConsole.StandardPlugins
- AutoConsole.Cli

Recommended dependency direction:
- Abstractions depends on nothing
- Core depends on Abstractions
- StandardPlugins depends on Abstractions and minimal Core surface if necessary
- Cli depends on Abstractions and Core

Do not create circular dependencies.

---

## Abstractions Rules

Shared contracts and models must go into Abstractions:
- Event models
- Session state
- Profile model
- Plugin metadata
- Job model
- shared DTOs/interfaces

Do not put implementation logic in Abstractions.

---

## Event Rules

Minimum built-in event types for v3.0.0:
- process_started
- process_exited
- stdout_line
- stderr_line
- timer
- manual_trigger

stdout/stderr should be handled as line-based events.

Core generates events but should not interpret app-specific meaning.

Plugins may emit custom events.
Custom events must not require Core to understand domain-specific semantics.

---

## Plugin Execution Rules

### Core must not be blocked by plugins
Heavy plugin work must not freeze Core.

### Prefer sequential plugin processing per plugin
For v3.0.0, plugin execution should favor predictable sequential handling.

### Long-running work should become Jobs
Long operations such as backup, archive, update, or ffmpeg workflow should be handled as jobs.

Job completion/failure should be surfaced through events.

---

## Standard Plugin Rules

Standard Plugins are the platform API layer and should stay generic.

Expected v3.0.0 standard plugin coverage:
- IO: send_input, wait_output
- Process: start_process, stop_process
- Time: delay, timer
- File: copy_file, copy_directory, archive_zip
- Utility: log
- Event: emit_event
- Control: call_plugin

Do not add domain-specific standard plugins in the initial version.

---

## UI Rules

UI must remain a client of Core, not the owner of runtime state.
Core is the source of truth.

CLI UI is required in v3.0.0.
GUI and Web are postponed.

UI communication conceptually uses:
- Command
- Query
- Event Stream

Do not embed business logic into UI.

---

## Profile Rules

Profiles define how to run a target application.
Profiles should include:
- id
- name
- command
- args (optional)
- working directory (optional)
- environment settings (optional)
- plugin bindings/config

Profiles must not contain complex automation logic.

---

## Implementation Priorities

Preferred implementation order:
1. Process + Session
2. stdout/stderr line events
3. Event system
4. Plugin host
5. Plugin context
6. Standard plugins
7. CLI UI

Follow this order unless there is a strong technical reason not to.

---

## Safety for Future Changes

When making implementation decisions:
- favor extensibility over short-term convenience
- avoid app-specific hacks in Core
- avoid hidden coupling
- preserve the Profile / Session / Event / Plugin / UI boundaries

If uncertain, choose the option that keeps Core smaller and pushes logic outward into plugins.