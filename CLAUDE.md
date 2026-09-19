Windows Native Desktop Environment

Project Overview & Architecture Brief

You are helping design and eventually implement a native Windows Desktop Environment (DE) written primarily in modern C++ with D3D12, with Lua as the customization/configuration/extension layer.

This is a long-term systems project. Do not jump immediately into implementation. First understand the architecture, constraints, and design philosophy described below. When proposing architecture, prioritize simplicity, performance, maintainability, native Windows compatibility, and a small number of powerful abstractions.

1. Core Concept

The project is a custom Windows Desktop Environment, not a replacement Windows shell and not a new operating system.

Windows underlying infrastructure remains intact.

In particular:

* Shell32 remains the Windows shell API/infrastructure.
* Win32 remains the application compatibility layer.
* Existing Windows applications should continue to work.
* Existing Windows concepts such as HWNDs, WndProc, D3D12, monitors, virtual desktops, etc. remain relevant.
* The project provides alternative implementations of the Desktop Environment components that normally provide the user-facing Windows experience.
* Certain components have *already* been reimplemented for you, such as the system tray (which is usually undocumented). You can see so [here](https://github.com/Balcony4Windows/Railing).

The important distinction is:

We are replacing the Desktop Environment implementation, not replacing Shell32 itself.

The resulting system should feel like a deliberately designed Windows distribution rather than an entirely foreign operating system.

2. What We Are Trying to Replace

The distribution should avoid shipping unnecessary consumer/application ¿slop¿ by default.

The goal is a minimal, coherent base environment containing only what is necessary for the operating system and DE.

Instead of relying on Microsoft¿s existing Explorer/Start/taskbar implementation, we want our own DE components.

Conceptually:

Windows
NT Kernel / Drivers
Win32
Shell32
D3D12
Our Desktop Environment
    Desktop
    Taskbar
    Start Menu
    Search
    Notifications
    Virtual Desktop UI
    Widgets
    Other system UI

Again, these are DE components, not a new shell infrastructure.

3. Process Architecture

We want to carefully determine the correct process architecture.

The default design direction is:

One primary DE process hosting the core DE components.

For example:

Balcony.exe
Desktop
Taskbar
Start Menu
Search
Notification Center
Virtual Desktop UI
Widget Manager
System UI

This is intentional.

The components should be able to share:

* D3D12 device/resources
* textures
* descriptor heaps
* font resources
* rendering infrastructure
* event infrastructure
* system state
* configuration
* cached data
* input state

However, this should NOT become an unmaintainable monolith.

The architecture should internally separate these components into well-defined modules/interfaces.

Third-party or untrusted extensions may eventually need process isolation, but do not assume that every DE component needs to be its own process.

A major architectural goal is:

Shared resources without sacrificing modularity.

4. D3D12-First Rendering

The DE should be designed around a native D3D12 renderer.

Do not build the UI around:

* WebView
* Chromium
* Electron
* HTML
* CSS
* JavaScript rendering
* a browser-based desktop

We previously experimented with a configuration system that effectively became a homegrown CSS implementation. That is specifically something we want to avoid.

The renderer should be designed as a high-performance native rendering system.

Areas that need architectural consideration include:

* D3D12 device management
* command queues
* command allocators
* command lists
* descriptor heaps
* resource lifetime
* texture management
* font rendering
* batching
* GPU synchronization
* frame pacing
* double/triple buffering where appropriate
* multi-monitor rendering
* animation
* GPU/CPU synchronization avoidance
* minimizing allocations
* minimizing state changes
* avoiding unnecessary copies
* efficient invalidation/redraw behavior

Performance is a first-class requirement.

The DE should be capable of remaining extremely lightweight even while rendering animations, widgets, taskbar UI, notifications, and other system UI.

Do not optimize prematurely, but design the architecture so that optimization is possible without rewriting the entire UI system.

5. Primitive UI Model

One of the most important architectural principles is that we do NOT want a giant declarative configuration language that recreates CSS.

The DE should instead expose a very small set of fundamental UI primitives.

For example:

enum class WidgetType
{
    TEXT,
    IMAGE,
    ANIMATION,
    SPACER,
    CONTAINER
};

The exact set is intentionally undecided and should be evaluated carefully.

Each primitive should expose only operations relevant to its fundamental purpose.

For example:

Text.setText(...)
Text.setFont(...)
Text.setAlignment(...)
Image.setTexture(...)
Image.setOpacity(...)
Image.setDimensions(...)
Spacer.setDimensions(...)

The philosophy is:

Build complex UI by composing simple primitives.

Do NOT create specialized primitives merely because a particular system feature exists.

For example, we do not necessarily want:

ClockWidget
CPUWidget
GPUWidget
RAMWidget
NotificationWidget
WeatherWidget

Instead, the system should expose information/events, and generic UI primitives consume that information.

For example:

System.Time
Text

or:

System.CPUUsage
Text / Animation / Image

or:

System.Notification
Container
    Image
    Text

This separation is fundamental.

6. Data and Event Model

The DE should expose a central data/event system.

There should be a conceptual distinction between:

Persistent/current state

Examples:

System.Time
System.CPUUsage
System.GPUUsage
System.MemoryUsage
System.Battery
System.NetworkState
System.ActiveWindow
System.ActiveMonitor
System.ActiveVirtualDesktop
Applications
RunningWindows

and:

Events

Examples:

WindowCreated
WindowDestroyed
WindowFocused
WindowMoved
VirtualDesktopCreated
VirtualDesktopDestroyed
VirtualDesktopChanged
MonitorConnected
MonitorDisconnected
NotificationReceived
ApplicationInstalled
ApplicationRemoved

The exact API is open for design.

The important principle is:

The DE exposes information and events; UI components decide how to represent them.

This allows the same system information to drive completely different interfaces.

For example:

clock = TextWidget()
clock:setText(System.Time)

Conceptually, that should be possible without the DE having a special ¿clock widget.¿

7. Lua as the Configuration and Customization Layer

Lua should be a major part of the architecture.

We want Lua to interoperate directly with the C++ DE runtime.

Lua should not merely be a scripting language for miscellaneous automation.

It should be the primary configuration/composition/customization layer for the DE.

The goal is something conceptually like:

clock = TextWidget()
clock:setText(System.Time)
taskbar:add(clock)

The C++ runtime provides the actual implementation of:

* rendering
* input
* windows
* monitors
* virtual desktops
* system state
* primitives
* event streams
* resource management

Lua provides:

* composition
* customization
* behavior
* bindings
* layout
* user configuration
* optional extensions

This creates a strong separation:

C++

Rendering
Windows
Input
System integration
D3D12
Performance-critical code
Primitive widgets
System/event infrastructure
      Lua
        Desktop composition
        Taskbar composition
        Start Menu
        Widgets
        Bindings
        User customization
        Behavioral customization

Lua must not become an excuse to move performance-critical rendering or system operations into interpreted code.

8. Extensive Lua Customization

Customization should be unusually powerful.

Users should eventually be able to modify things such as:

* taskbar position
* taskbar contents
* taskbar layout
* Start Menu composition
* widget placement
* desktop widgets
* system information displays
* clock presentation
* notifications
* search behavior
* keyboard shortcuts
* mouse interactions
* virtual desktop behavior
* window-management behavior
* monitor-specific configuration
* animations
* themes
* system UI behavior

However, avoid inventing a second CSS-like language.

Lua should compose native objects directly.

The desired mental model is:

Lua is programming the DE, not describing HTML.

We should investigate whether a small native layout API is sufficient instead of implementing a massive generic layout language.

9. Configuration vs State vs Code

The architecture should explicitly distinguish:

DE implementation

Compiled C++.

User customization

Lua.

Persistent user/system state

Potentially SQLite and/or carefully selected native files.

Application/package metadata

Maintained by the package manager.

Do not blur these together.

For example, a user¿s taskbar arrangement should not require recompiling anything.

But it also shouldn¿t require generating a gigantic YAML/XML/JSON representation of every UI element.

Lua should be capable of expressing the composition.

10. Persistence

We need a secure and reliable persistence architecture.

SQLite is one candidate and should be seriously evaluated.

Potential persisted information includes:

* installed packages
* package metadata
* application associations
* pinned applications
* taskbar state
* Start Menu state
* widget configuration
* monitor-specific configuration
* virtual desktop state
* user preferences
* recent applications
* search indexes/metadata
* DE state

The system should consider:

* atomic updates
* corruption resistance
* transactions
* versioning/migrations
* backups/recovery
* permissions
* integrity
* concurrent access
* secure storage of sensitive information
* separation of trusted system state from user-controlled configuration

Do not assume SQLite is automatically correct for every category of data.

Determine which data belongs in:

* SQLite
* Lua configuration
* ordinary files
* Windows-provided storage mechanisms
* package-manager databases

The persistence architecture should be deliberately designed rather than chosen solely for convenience.

11. Virtual Desktops

Virtual desktops should be treated as first-class DE concepts.

The system should provide APIs for:

* enumerating desktops
* creating desktops
* destroying desktops
* switching desktops
* moving windows between desktops
* observing desktop changes
* configuring desktop-specific UI
* associating widgets/components with desktops where appropriate

The DE event system should expose virtual-desktop events independently of traditional Win32 WndProc messages.

For example:

VirtualDesktopCreated
VirtualDesktopDestroyed
VirtualDesktopChanged
WindowMovedToVirtualDesktop

The exact Windows API mechanisms should be investigated during implementation.

Do not invent behavior that Windows does not actually support.

12. Multi-Monitor Support

Monitor recognition and configuration should be first-class.

The DE should understand:

* monitor identity
* connection/disconnection
* resolution
* refresh rate
* DPI/scaling
* orientation
* primary monitor
* monitor arrangement
* per-monitor configuration
* monitor-specific widgets
* monitor-specific taskbars
* monitor-specific wallpapers
* monitor-specific layouts

The system should react dynamically when monitors are:

* connected
* disconnected
* rearranged
* resized
* reconfigured

Avoid assuming a fixed monitor topology.

13. Taskbar

The taskbar should not be a monolithic special-purpose object.

It should be primarily a composition of primitives and data sources.

For example:

Taskbar
Start
Pinned applications
Running applications
Spacer
Virtual desktop indicator
System information
Clock

But the exact composition should be controlled through Lua.

The taskbar should consume system state such as:

RunningWindows
PinnedApplications
ActiveWindow
ActiveVirtualDesktop
System.Time
System.NetworkState
System.Battery
Notifications

This should allow a user to construct substantially different taskbars without changing C++.

14. Start Menu and Search

The Start Menu should be its own DE component, but should share the same primitive/runtime infrastructure.

Search should be treated as a proper subsystem rather than simply filtering visible Start Menu entries.

The architecture should leave room for:

* application search
* file search
* settings search
* command search
* indexed search
* recent items
* fuzzy matching
* extensible search providers

Search providers may eventually be plugins that publish results into a common search API.

15. Widget Movement and Composition

The user should be able to move DE components such as:

* widgets
* taskbar elements
* system indicators
* desktop elements

without requiring changes to the underlying C++ implementation.

The exact interaction model should be designed.

Avoid making the layout engine unnecessarily complicated.

The goal is not to reproduce CSS.

The goal is to provide a small number of compositional primitives that are powerful enough to construct the DE.

16. Package Manager

The operating environment should include its own package manager.

The package manager is more than an installer.

It should maintain a coherent model of installed software and dependencies.

Important goals include:

* package installation
* package removal
* dependency resolution
* version management
* updates
* rollback/recovery
* integrity verification
* application discovery
* application metadata
* file ownership
* dependency tracking
* garbage collection
* repair
* deduplication

One particularly important design goal is avoiding unnecessary duplication of common DLLs and runtime components.

For example:

Application A 
                Shared Library X
Application B 

rather than:

Application A
private copy of Library X
Application B
private copy of Library X

This must be designed around Windows actual DLL/ABI/versioning behavior. Do not assume that every DLL can safely be globally deduplicated.

The package manager should also track installed applications independently of the Registry where practical.

The Registry still exists for Windows compatibility, but it should not necessarily be the authoritative package database.

Investigate:

* content-addressed storage
* immutable package versions
* shared dependencies
* reference counting
* package manifests
* DLL compatibility
* side-by-side dependencies
* runtime dependencies
* application registration
* file associations
* protocol handlers
* services
* shell extensions

17. Security

Security should be designed into the architecture.

Consider:

* Lua sandboxing
* extension permissions
* package signatures
* package integrity
* trusted vs untrusted plugins
* DLL loading
* code signing
* user/system separation
* configuration tampering
* SQLite/database integrity
* privilege boundaries
* third-party widget isolation
* update authenticity

Do not design an extension system that requires arbitrary third-party code to run with unrestricted DE privileges by default.

18. Project/Solution Organization

We need a clean Visual Studio/CMake solution structure.

This is one of the areas that should be resolved early.

The repository should clearly separate:

Core runtime
Rendering
Windows integration
DE components
Lua bindings
Plugins
Persistence
Package management
Tests
Tools
Assets

The final directory structure should make architectural boundaries obvious.

Potentially:

/
src/
   core/
   renderer/
   windows/
   ui/
   lua/
   persistence/
   packages/
   components/

plugins/
scripts/
assets/
tests/
tools/
third_party/
docs/
CMakeLists.txt

This is only a starting point, not a prescribed final structure.

We need to determine the best organization based on actual dependency boundaries.

The project should avoid circular dependencies.

19. Engineering Priorities

Prioritize in roughly this order:

1. Architectural correctness
2. Windows compatibility
3. Rendering architecture
4. Performance
5. Extensibility
6. Security
7. Maintainability
8. User customization
9. Convenience

Do not sacrifice the architecture merely to get a flashy prototype running quickly.

At the same time, avoid speculative abstractions.

Every abstraction should justify its existence.

20. Design Philosophy

Several principles are non-negotiable.

Native first

Prefer native C++ and D3D12 over browser technologies.

Small primitives

Prefer a handful of powerful primitives over hundreds of specialized widgets.

Data over specialization

Expose system information and events rather than inventing specialized widgets for every piece of information.

Composition over configuration languages

Use Lua to compose and customize native components rather than inventing YAML/XML/CSS-like languages.

Shared resources

Keep the core DE in a process where sharing GPU/system resources makes sense.

Modular internals

One process should not mean one giant monolithic codebase.

Performance-conscious

The DE should be lightweight and predictable.

Windows-compatible

Use Windows APIs where appropriate instead of unnecessarily recreating existing OS functionality.

Secure extensibility

Third-party customization should not automatically receive unrestricted system privileges.

Opinionated defaults

The default DE should have a coherent design and behavior.

Customization exists because users want it, not because the base design is intentionally generic.

21. Important Architectural Questions to Resolve

Before significant implementation, investigate and document answers to questions such as:

1. What exactly belongs in Balcony.exe?
2. Which components should be separate modules?
3. When should an extension be out-of-process?
4. How should the D3D12 renderer be structured?
5. What is the minimum useful primitive widget set?
6. What should the layout model look like without becoming CSS?
7. How should Lua bindings expose native objects safely?
8. What should the Lua lifecycle look like?
9. How should Lua scripts react to state changes?
10. How should state streams differ from events?
11. What should the DE event bus look like?
12. How should HWNDs map into the DE¿s internal model?
13. How should virtual desktops integrate with the internal event system?
14. How should monitor identity/configuration work?
15. How should taskbar composition work?
16. How should Start Menu composition work?
17. How should search providers work?
18. What belongs in SQLite?
19. What belongs in Lua configuration?
20. What belongs in ordinary files?
21. How should configuration migrations work?
22. How should package dependencies work?
23. How can DLL deduplication be done safely?
24. How should application registration work without making the Registry the authoritative package database?
25. How should third-party plugins be isolated?
26. How should the DE recover if a plugin crashes?
27. How should updates/rollback work?
28. How should the project be structured as a CMake/Visual Studio solution?
29. What APIs need stable ABI guarantees?
30. Which parts need to remain entirely internal?

These questions should be answered through careful architectural reasoning and, where appropriate, investigation of actual Windows APIs and constraints.

22. Development Approach

Do not attempt to implement the entire DE immediately.

The project should be developed in architectural layers.

A likely progression is:

Windows process integration

D3D12 renderer

Window/compositor model

Primitive UI system

Input/event system

Lua bindings

Configuration/persistence

Desktop

Taskbar

Start/Search

Virtual desktops

Multi-monitor behavior

Notifications/widgets

Package manager

Extension ecosystem

The exact order can change after architectural investigation.

At each stage, preserve the fundamental goal:

A small native runtime with powerful composition rather than a huge framework full of special cases.

23. What We Are NOT Building

Do not accidentally turn this project into any of the following:

* a Linux desktop environment
* a replacement OS kernel
* a browser-based desktop
* an Electron application
* a CSS framework
* a YAML-driven UI framework
* a generic game engine
* a giant widget framework
* a replacement for Win32
* a replacement for Shell32
* a collection of unrelated shell processes
* a clone of Explorer¿s UI

We are building:

A native, high-performance Windows Desktop Environment whose core runtime is C++/D3D12 and whose customization/composition layer is Lua.

24. Expected Role During Architecture

Before writing substantial code, analyze this specification and propose a coherent architecture.

Do not simply accept every idea above literally if Windows imposes constraints that make a particular design impossible or counterproductive.

Where something is uncertain, identify the uncertainty and investigate it.

Where there are multiple viable designs, compare them based on:

* performance
* complexity
* Windows compatibility
* security
* extensibility
* maintainability
* resource sharing
* ABI stability

Do not solve architectural questions by adding another abstraction layer unless that abstraction is genuinely necessary.

The ultimate goal is to produce a DE that feels native, extremely fast, minimal, coherent, and deeply customizable, while retaining the enormous compatibility advantages of Windows.
