---
status: implemented
tags: [debugging, ui, tooling, renderer]
---

# Debug UI

## Problem

The current debug overlay is a small text block in the bottom-right corner
showing FPS, draw call counts, and Lua memory. It's toggled with Tab. This
is barely useful for day-to-day development:

- No way to inspect or modify game state (entity positions, physics bodies,
  timers, camera).
- No way to visualize engine internals (allocator usage, command buffer
  fill, audio streams, collision world).
- No live-tweaking of values (gravity, damping, colors, spawn rates) without
  editing Lua and waiting for hot-reload.
- No entity browser or scene graph viewer.
- No way to see what the physics or collision system is doing without
  writing Lua debug code.
- The profiler dumps to `trace.json` for offline viewing — no in-engine
  frame timing graph.

Every serious game engine (Godot, Unity, Unreal, Love2D with debug plugins)
has an interactive debug UI. This document proposes one for our engine.

## Goals

1. A **developer-facing** debug UI rendered as an overlay on top of the game.
   Not exposed to game scripts or players.
2. **Interactive panels** with real-time graphs, value sliders, tree views,
   and toggle switches — not just text.
3. **Minimal integration cost** — the debug UI should use the existing
   OpenGL context and batch renderer, not require a separate rendering
   pipeline.
4. **Compiled out in release** — zero overhead in packaged builds.
5. **Keyboard-driven** — must be fully usable without a mouse (tiling WM
   workflow), though mouse support is nice to have.

## Non-goals

- A user-facing in-game UI system (menus, HUD, dialogs). That's a separate
  feature.
- Editor-grade tools (scene editor, asset browser, visual scripting). This
  is a debug overlay, not an IDE.

## Library evaluation

### Option 1: Dear ImGui

The de facto standard for game/engine debug UIs. C++ with a C API wrapper.

| Aspect | Assessment |
|--------|-----------|
| License | MIT |
| Size | ~35K lines core (`imgui.cpp` + `imgui_draw.cpp` + `imgui_widgets.cpp` + `imgui_tables.cpp`) |
| Rendering | Backend-agnostic: you provide vertex/index buffers, it provides draw lists. Official `imgui_impl_opengl3` + `imgui_impl_sdl3` backends exist. |
| Dependencies | None (self-contained C++) |
| Widgets | Text, buttons, sliders, drag values, color pickers, checkboxes, radio buttons, combo boxes, trees, tabs, tables, plots, menus, tooltips, text input, images |
| Layout | Automatic window layout with docking (optional). Windows can be dragged, resized, collapsed. |
| Keyboard | Full keyboard navigation via `ConfigFlags_NavEnableKeyboard`. Tab between widgets, arrow keys in trees/lists, Enter to activate. |
| Customization | Extensive style system (colors, padding, rounding). Multiple built-in themes. |
| SDL3 support | Official `imgui_impl_sdl3.cpp` backend shipped with ImGui. |
| OpenGL support | Official `imgui_impl_opengl3.cpp` for GL 3.x+. |
| Font support | Built-in font atlas with TTF loading via stb_truetype. Can use our existing debug font. |

**Strengths:** Battle-tested in thousands of game engines and tools.
Enormous widget library. Official SDL3 + OpenGL3 backends mean near-zero
integration work. The community has built extensions for everything
(node editors, text editors, Lua bindings, profiler visualizations).

**Weaknesses:** C++ with templates and STL usage (we compile with
`-fno-exceptions -fno-rtti`, which ImGui supports via
`IMGUI_DISABLE_EXCEPTIONS`). Uses its own allocator by default (overridable
via `ImGui::SetAllocatorFunctions`). ~35K lines is large but comparable to
other vendored libraries (Box2D, Lua).

### Option 2: Nuklear

Single-header C immediate-mode UI library.

| Aspect | Assessment |
|--------|-----------|
| License | Public domain / MIT |
| Size | ~18K lines (single header `nuklear.h`) |
| Rendering | Backend-agnostic: provides vertex/index buffers via `nk_convert`. |
| Dependencies | None |
| Widgets | Buttons, sliders, checkboxes, text input, trees, tabs, color picker, combo, progress bars, property editors |
| Layout | Row-based layout. Windows can be moved/resized. No docking. |
| Keyboard | Basic keyboard support but less polished than ImGui. |
| Font support | TTF loading via stb_truetype (bundled or external). |

**Strengths:** Pure C, single header, public domain. Simpler API surface.
Smaller codebase. No C++ concerns (RTTI, exceptions).

**Weaknesses:** Less mature than ImGui. Fewer widgets (no tables, limited
plotting). No official SDL3 backend — would need to write one. Keyboard
navigation is weaker. Smaller community means fewer examples and extensions.
No built-in docking. The single-header approach means all 18K lines compile
every translation unit that includes it (mitigated by including in only one
.cc file).

### Option 3: Custom immediate-mode UI on existing renderer

Build a minimal IM-UI directly on the engine's `Renderer` class, which
already provides `DrawRect`, `DrawRectOutline`, `DrawString`, `SetScissor`,
and input handling.

**Strengths:** Zero dependencies. Perfect integration with the existing
rendering pipeline. Full control over style and behavior.

**Weaknesses:** Enormous effort to get right. Text input, scrolling,
clipping, focus management, keyboard navigation, tree views — each is
weeks of work. We'd be building a UI framework instead of a debug tool.

### Recommendation: Dear ImGui

ImGui is the clear choice:

1. **Official SDL3 + OpenGL3 backends** — we already use both. Integration
   is ~50 lines of glue code.
2. **Compiles with our flags** — `IMGUI_DISABLE_EXCEPTIONS` is a supported
   config. `-fno-rtti` is fine (ImGui doesn't use RTTI).
3. **Allocator override** — `ImGui::SetAllocatorFunctions(alloc, free, userdata)`
   lets us route ImGui allocations through our allocator system.
4. **Widget coverage** — plots, tables, trees, sliders, color pickers are
   all built-in. We'd have to build each one from scratch with Nuklear or
   custom.
5. **Community** — every debug UI feature we might want has been built by
   someone with ImGui (profiler timelines, entity inspectors, memory
   visualizers, audio waveform viewers).

## Integration

### Rendering pipeline

ImGui renders after the game and after our existing debug overlay, but
before `SDL_GL_SwapWindow`. It manages its own OpenGL state and restores
ours when done.

```
Game::Render():
  engine->renderer.ClearForFrame()
  engine->lua.Draw()              // game content
  // existing debug text overlay
  engine->renderer.FlushFrame()
  engine->batch_renderer.Render() // flush our batched GL commands

  ImGui_ImplOpenGL3_NewFrame()    // NEW
  ImGui_ImplSDL3_NewFrame()       // NEW
  ImGui::NewFrame()               // NEW
  DrawDebugUI()                   // NEW — our debug panels
  ImGui::Render()                 // NEW
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData())  // NEW

  SDL_GL_SwapWindow(sdl.window)
```

ImGui's OpenGL3 backend saves and restores all GL state it touches
(blend mode, scissor, vertex array, shader program, textures). Our batch
renderer's state is not affected.

### Input routing

ImGui needs SDL events to handle mouse clicks, keyboard input, and text
entry. The official `imgui_impl_sdl3.cpp` backend provides
`ImGui_ImplSDL3_ProcessEvent(&event)` which should be called for every
SDL event in `PollEvents()`.

When ImGui wants to capture input (e.g., mouse is over an ImGui window, or
a text field has focus), `ImGui::GetIO().WantCaptureMouse` /
`WantCaptureKeyboard` return true. The game's input system should skip
processing when these are set:

```cpp
void PollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (ImGui::GetIO().WantCaptureKeyboard) continue;
        if (ImGui::GetIO().WantCaptureMouse) continue;
        // ... existing input handling ...
    }
}
```

This means: when you're interacting with a debug slider, the game doesn't
also receive the mouse click. When you're typing in a debug text field, the
game doesn't also receive key events. Tab still toggles the debug UI
because we process it before the ImGui capture check.

### Allocator integration

Route ImGui allocations through the engine allocator:

```cpp
static void* ImGuiAlloc(size_t size, void* userdata) {
    auto* alloc = static_cast<Allocator*>(userdata);
    return alloc->Alloc(size, 16);
}
static void ImGuiFree(void* ptr, void* userdata) {
    auto* alloc = static_cast<Allocator*>(userdata);
    alloc->Dealloc(ptr);
}
// During init:
ImGui::SetAllocatorFunctions(ImGuiAlloc, ImGuiFree, allocator);
```

ImGui's memory usage is modest — typically under 1 MB for complex UIs.

### Compile-time guard

All ImGui code lives behind `#ifdef GAME_DEV_MODE`. In release builds
(`game package` output), ImGui is not compiled, not linked, and the debug
UI render calls are no-ops. Zero overhead.

### Files

```
libraries/imgui/           Vendored ImGui source (core + backends)
src/debug_ui.h             DebugUI class declaration
src/debug_ui.cc            Panel implementations
```

`debug_ui.cc` is the only engine file that includes `imgui.h`. All other
engine code interacts via the `DebugUI` class interface.

## Panels

### 1. Performance (always visible when debug is on)

The existing text overlay, upgraded:

- **Frame time graph** — rolling 300-frame history with min/avg/max lines.
  `ImGui::PlotLines` with the existing `Stats` sample buffer.
- **FPS counter** — large, top-left.
- **Draw call breakdown** — bar chart of flush reasons (texture change,
  transform change, shader change, etc.) from `FrameStats`.
- **Lua memory** — current and peak, with a sparkline.
- **Command buffer fill** — percentage of `kCommandMemory` used, colored
  green/yellow/red.

### 2. Entity / Game State Inspector

A tree view of live game state, read directly from the Lua VM:

- Expand `G` to see all modules.
- Expand any Lua table to see its contents (recursive, with depth limit).
- Userdata shows its metatable type and fields.
- Values update in real time (re-evaluated each frame the panel is open).
- Double-click a number to edit it in-place (writes back to the Lua table).
- Filter/search bar to find specific keys.

Implementation: walk Lua tables with `lua_next()`, format values, use
`ImGui::TreeNode` for nested tables. The inspector runs in
`ProcessReplQueue`-like fashion — reads are safe because we're on the main
thread between update and draw.

### 3. Physics Debug

Visualization controls for the Box2D physics world:

- **Toggle physics debug draw** — wire-frame rendering of all physics
  shapes, joints, AABBs, contact points (implements the `b2Draw`
  interface proposed in the Physics expansion design doc).
- **World settings** — sliders for gravity X/Y, velocity/position
  iterations, pixels-per-meter. Changes apply immediately.
- **Body list** — table of all active bodies with type, position, velocity,
  mass. Click to highlight in the viewport.
- **Pause/step physics** — pause the Box2D step, advance one step at a
  time.

### 4. Collision Debug

Controls for the standalone collision system (`G.collision`):

- **Toggle collision debug draw** — render all colliders (circles, AABBs)
  with color-coded categories.
- **Spatial hash grid** — visualize the grid cells and occupancy.
- **Trigger state** — list active triggers with enter/exit events.

### 5. Audio

- **Active streams** — table of playing sounds with name, position,
  volume, pitch, pan, loop status.
- **Waveform** — real-time waveform of the mixed output buffer (read from
  `SoundCallback`'s output).
- **Stream slots** — visual grid showing used/free slots.
- **Global volume** slider.

### 6. Renderer

- **Batch stats** — draw calls, vertices, commands per frame (same data
  as the performance panel but detailed).
- **Texture atlas** — display loaded textures with zoom and UV
  highlighting.
- **Shader list** — active shaders with uniform values.
- **Canvas list** — active FBOs with resolution and format.
- **Blend mode** and **stencil state** readout.

### 7. Memory

- **Allocator overview** — arena used/total, Lua heap used/total,
  mimalloc stats.
- **Per-subsystem breakdown** (if module budgets are implemented).
- **Allocation timeline** — sparkline of arena position over time.

### 8. Log Console

- Scrollable log output with level/channel filtering (replaces the need
  to read terminal output).
- Color-coded by level (debug=gray, info=white, warn=yellow, error=red).
- Lua eval input at the bottom (same as the REPL but in-engine).
- Clear button, auto-scroll toggle, regex filter.

### 9. Camera

- **Current camera state** — position, zoom, rotation, target,
  deadzone visualization.
- **Override controls** — drag to pan, scroll to zoom, temporarily
  override the game camera for inspection.

## Implementation status

| Panel | Status | Notes |
|-------|--------|-------|
| Performance | Done | FPS counter, frame time graph (PlotLines), draw call breakdown with flush reasons and redundant skips, Lua memory graph, command buffer fill bar, window size controls (presets + custom). |
| Log Console | Done | Captures all engine log messages (including startup) via LogSink intercept. Color-coded by level, per-level toggle checkboxes, text filter, auto-scroll, Copy to clipboard. Lua eval input with Up/Down history. |
| Entity Inspector | Done | Recursive Lua table walker for `G` and `_Game`. Editable numbers (DragFloat), booleans (Checkbox), color tables (`{r,g,b,a}` detected and rendered as ColorEdit3/4). Key filter. |
| Physics | Done | Body/joint/contact counts, editable gravity sliders, solver iteration inputs, scrollable body table (type, position, velocity, angle, mass). |
| Audio | Done | Global volume slider, stream slot progress bar, active streams table (name, status, volume, pitch, pan, loop, type). |
| Memory | Done | Engine arena and frame allocator progress bars, Lua heap sparkline, string table stats. |
| Renderer | Done | Batch stats table, flush reason breakdown, redundant skip counts, command buffer bar, loaded image list, shader program enumeration, current blend mode/shader/viewport readout. |
| Camera | Done | Position, zoom, rotation display. Follow target with lerp. Deadzone status. World bounds. Shake state. |
| Asset Viewer | Done | Tabbed (Images, Sprites, Audio, Scripts, Shaders, Fonts). Image thumbnails via ImGui::Image. Sprite preview cut from spritesheet UVs. Audio Play/Stop preview. Text filter across all tabs. |
| Collision Debug | Deferred | CollisionWorld lives as Lua userdata per-script, not an engine-level instance. Collider state is visible through the Entity Inspector. |

### Infrastructure

| Feature | Status | Notes |
|---------|--------|-------|
| ImGui vendor | Done | Dear ImGui v1.91.8, SDL3 + OpenGL3 backends, compiled behind `GAME_WITH_IMGUI`. |
| Menu bar | Done | Panels menu with checkbox toggles (Performance + Log Console default visible). Actions menu (Screenshot, Hot Reload, Run GC). Time controls (Play/Pause + 0-4x slider) inline. FPS readout on the right. |
| Compile-time guard | Done | All code behind `#ifdef GAME_WITH_IMGUI` with matching no-op stub class. |
| Allocator integration | Done | ImGui allocations routed through SystemAllocator. |
| Input routing | Done | SDL events forwarded to ImGui; WantCaptureMouse/Keyboard gates game input. |
| Engine integration | Done | Single `SetEngine(Engine*)` call. `DrawAll(FrameContext)` dispatches to enabled panels. |

## Proposed improvements

### High value

- **Frame stepping.** The Pause button exists but there is no "advance one
  frame" button. When debugging physics or animations, single-stepping is
  invaluable. Needs a bool flag in the game loop that runs one update tick
  then re-pauses.

- **Console history persistence.** Eval history is lost on restart. Save/load
  to a small file (e.g. `.claude/eval_history`) so useful debug commands
  survive across sessions.

### Medium value

- **Physics body filter.** The body table is unfiltered. Add a type filter
  (dynamic/static/kinematic) and a click-to-highlight that draws a box
  around the selected body in the viewport.

- **Log export to file.** The Copy button copies to clipboard. For bug
  reports, add a "Save to file" button that writes the full (unfiltered)
  log to a timestamped file in the write directory.

- **Keyboard shortcuts for panels.** F1-F9 to toggle specific panels without
  navigating the Panels menu.

### Lower value / polish

- **Camera drag-to-pan.** In the Camera panel, add mouse drag controls to
  temporarily override the game camera for off-screen inspection. Release
  snaps back.

- **Texture zoom.** In the asset viewer Images tab, click a texture to open
  a zoomable/pannable fullscreen preview at 1:1 pixel scale.

- **Performance history reset.** Button to clear the frame time and Lua
  memory graph buffers (useful after a startup spike to see steady-state
  performance).

## Style

Dark theme matching the game's development aesthetic. Semi-transparent
window backgrounds so the game is visible behind panels. Panels dock to
screen edges by default. The ImGui demo window style is a reasonable
starting point — override the accent color to match the engine's identity.

## Decisions

1. **ImGui over Nuklear.** ImGui has official SDL3 + OpenGL3 backends,
   richer widgets, better keyboard navigation, and a larger ecosystem.
   Nuklear's C purity is appealing but doesn't outweigh the practical
   advantages.

2. **Render after the batch renderer, not through it.** ImGui manages its
   own GL state. Routing ImGui draw calls through our `BatchRenderer` would
   require teaching it about ImGui's vertex format, index buffers, and
   scissor rects. Letting ImGui render directly is simpler and the official
   backend handles state save/restore.

3. **Single `debug_ui.cc` file.** All panel code in one file, not split
   per-panel. ImGui panels are typically 30-80 lines each. Splitting into
   separate files would create many tiny files with heavy include overlap.
   If the file grows past ~1500 lines, split then.

4. **No ImGui docking (initially).** The docking branch adds complexity
   (multi-viewport, platform windows). Start with the core branch. Add
   docking later if panel management becomes painful.
