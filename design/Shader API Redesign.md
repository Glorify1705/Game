
## Problem

The current shader system has a critical bug: the renderer's `flush()` calls
`MUST(shaders_->SetUniform("tex", ...))` for every draw call, which crashes
when a user shader doesn't reference the `tex` uniform and the GLSL compiler
optimizes it away. This makes shaders like `testshader.frag` (which only uses
screen coordinates, not the texture) crash the engine.

More broadly, the shader API deserves a review against established 2D engines
to identify improvements.

## Current System

### Architecture

User shaders define an `effect()` function in GLSL:

```glsl
vec4 effect(vec4 color, sampler2D tex, vec2 tex_coord, vec2 screen_coord) {
    return texture(tex, tex_coord) * color;
}
```

The engine wraps this in a preamble + postamble (`shaders.cc`):

- **Preamble**: `#version 460 core` + `#line 1`
- **Postamble**: declares `out frag_color`, `in tex_coord`, `in screen_coord`,
  `in out_color`, `uniform sampler2D tex`, and a `main()` that calls
  `effect(out_color, tex, tex_coord, screen_coord)`.

The built-in `pre_pass.frag` shader directly samples the texture and multiplies
by vertex color. User shaders always link against `pre_pass.vert`.

### Lua API

```lua
G.graphics.attach_shader("myshader.frag")  -- activate
G.graphics.send_uniform("iTime", t)        -- send custom uniform
G.graphics.attach_shader()                 -- reset to default
```

### Renderer Internals

The `flush()` lambda in `BatchRenderer::Render()` sets three uniforms on every
draw call:

```cpp
MUST(shaders_->SetUniform("tex", texture_unit));
MUST(shaders_->SetUniform("projection", Ortho(...)));
MUST(shaders_->SetUniform("transform", transform));
```

`MUST` crashes on error. `SetUniform` returns an error when
`glGetUniformLocation` returns -1 (uniform not found / optimized out).

### The Bug

When a user shader's `effect()` function ignores the `tex` parameter (never
calls `texture(tex, ...)`), the GLSL compiler optimizes away `uniform
sampler2D tex`. The renderer's `flush()` then fails to find the uniform and
crashes via `MUST`.

## Comparison with Other 2D Engines

### Entry Point / Shader Definition

| Engine      | Approach                         | User writes         |
|-------------|----------------------------------|---------------------|
| **Ours**    | Wrapper: `effect()` function     | `effect()` in GLSL  |
| **LOVE**    | Wrapper: `effect()` function     | `effect()` in GLSL  |
| **Godot**   | Custom language, `fragment()`    | Assignments to `COLOR` etc. |
| **GameMaker** | Raw GLSL `main()`             | Full fragment shader |
| **Unity**   | HLSL with ShaderLab metadata     | `vert()` + `frag()` |
| **Raylib**  | Raw GLSL `main()`               | Full fragment shader |
| **Defold**  | Raw GLSL `main()`               | Full fragment shader |
| **MonoGame** | HLSL effects (.fx)              | Named functions     |

Our approach is closest to LOVE's, which is the gold standard for 2D indie
engines. The `effect()` wrapper is the right level of abstraction for a 2D
engine: it hides the vertex shader and GL boilerplate while giving full control
over fragment output.

### Built-in Uniforms

| Engine      | Time   | Resolution | Texture | Projection | Color  |
|-------------|--------|------------|---------|------------|--------|
| **Ours**    | No     | No         | `tex` (parameter) | `projection` | `out_color` (varying) |
| **LOVE**    | No     | `love_ScreenSize` | Parameter | Vertex arg | Parameter |
| **Godot**   | `TIME` | `SCREEN_PIXEL_SIZE` | `TEXTURE` | Matrix built-ins | `COLOR` |
| **Unity**   | `_Time` (vec4) | `_ScreenParams` | `_MainTex` | Matrix includes | `_Color` |
| **GameMaker** | `gm_Time` (vec4) | No | `gm_BaseTexture` | `gm_Matrices` | `v_vColour` (varying) |
| **Raylib**  | No     | No         | `texture0` | `mvp`  | `colDiffuse` |

Notable: LOVE, our closest analog, does **not** auto-provide time or
resolution. Godot and Unity do because they target a broader audience. For a
LOVE-style engine, requiring the user to send `iTime`/`iResolution` explicitly
keeps the API predictable and avoids unnecessary overhead.

### Missing Uniform Behavior

| Engine      | Built-in uniform missing          | User uniform missing           |
|-------------|-----------------------------------|--------------------------------|
| **Ours**    | **Crash** (MUST)                  | Lua error (ErrorOr)            |
| **LOVE**    | N/A (parameters, not uniforms)    | Lua error                      |
| **Godot**   | N/A (always exist in language)    | Defaults to zero               |
| **GameMaker** | Silent skip (handle = -1)       | Silent skip (handle = -1)      |
| **Unity**   | Silent skip                       | Silent skip                    |
| **Raylib**  | Silent skip (GL behavior)         | Silent skip (GL behavior)      |
| **Defold**  | Warning logged, no crash          | Warning logged, no crash       |
| **MonoGame** | Null check needed                | NullReferenceException         |

Three camps:
1. **Strict/error**: LOVE, MonoGame. Forces correctness.
2. **Silent skip**: GameMaker, Raylib, Unity. Convenient, hides bugs.
3. **Crash**: Ours (for built-in uniforms). Unacceptable.

LOVE avoids the problem entirely because the texture is a function parameter,
not a uniform. Since LOVE's `effect()` receives the sampler as an argument,
there's no uniform to look up. The engine's generated `main()` always uses the
sampler to call `effect()`, and if the user ignores the parameter, the compiler
may optimize the texture sample but the uniform lookup never happens from the
engine side — LOVE doesn't call `SetUniform("tex")` at all.

## Proposed Changes

### P0: Fix the crash (must do)

**Change the renderer's built-in uniform bindings to silently skip when the
uniform is not found.** This matches Raylib, GameMaker, and Unity's behavior.

The renderer sets `tex`, `projection`, `transform`, and `global_color` as
internal plumbing — these are not user-facing uniforms. If the GLSL compiler
optimizes them away, the correct behavior is to skip the binding, not crash.

Implementation: replace `MUST(shaders_->SetUniform(...))` with calls that
check the return value and skip the draw call or ignore the error. Or add a
`SetUniformSilent` variant that returns void and ignores -1 locations.

The Lua-facing `send_uniform` should continue to raise an error on missing
uniforms (matching LOVE's strict behavior for user mistakes).

### P1: Follow LOVE's model more closely (should do)

Currently our postamble declares `uniform sampler2D tex` and passes it as a
parameter to `effect()`. This is already very close to LOVE's approach, but
with one difference: LOVE uses an opaque `Image` type as the parameter, while
we expose `sampler2D` directly. This is fine — no change needed here.

However, we should consider adding `love_ScreenSize`-equivalent as a built-in
uniform. The pattern of needing screen dimensions in shaders is extremely
common (every shadertoy-style shader needs it).

**Proposal**: Always set `g_ScreenSize` (vec2) as a built-in uniform in the
preamble, alongside `tex`. The renderer would set it in `flush()` silently.
Built-in uniforms use a `g_` prefix to distinguish them from user-defined
variables.

```glsl
// Added to kFragmentShaderPreamble:
uniform vec2 g_ScreenSize;
```

This removes the need for users to manually send resolution from Lua in every
frame, which is the #1 most common shader uniform after time.

### P2: Consider auto-providing time (nice to have)

Godot, Unity, and GameMaker all provide time automatically. It's the single
most wanted uniform. Two options:

1. **Auto-set `g_Time` uniform in the preamble.** The renderer sets it each
   frame. If the shader doesn't use it, the GLSL compiler optimizes it away
   and the silent-skip handles it. Zero cost when unused.

2. **Don't auto-provide it.** Keep the LOVE-style philosophy: explicit is
   better than implicit. Users send `iTime` themselves.

Recommendation: option 1. The overhead is negligible (one float per frame),
and it eliminates the most common boilerplate in shader scripts.

### P3: Uniform existence checking (nice to have)

Add a `G.graphics.has_uniform(name)` function so Lua scripts can check if a
uniform exists before sending it. This matches LOVE's `Shader:hasUniform()`.

Useful for shaders that conditionally support features (e.g. send `iTime` only
if the shader uses it).

### Non-goals

- **Custom shading language** (Godot-style): Too much complexity for the
  scope of this engine.
- **Shader Graph / visual editor**: Out of scope.
- **Vertex shader customization**: The current `pre_pass.vert` handles all 2D
  use cases. No need to expose vertex shader authoring.
- **Multi-pass rendering**: The post-pass system already handles screen-space
  effects.

## Implementation Plan

### Phase 1: Fix the crash
1. Add `SetUniformSilent` methods to `Shaders` that return void and skip
   missing uniforms.
2. Use them for the four built-in uniforms in `flush()`: `tex`, `projection`,
   `transform`, `global_color`.
3. Keep `SetUniform` (with ErrorOr) for the Lua-facing `send_uniform`.

### Phase 2: Add g_ScreenSize built-in
1. Add `uniform vec2 g_ScreenSize;` to `kFragmentShaderPreamble`.
2. Set it silently in `flush()` alongside the other built-ins.
3. Update `testshader.frag` to use `g_ScreenSize` instead of custom
   `iResolution`.

### Phase 3: Add g_Time built-in
1. Add `uniform float g_Time;` to `kFragmentShaderPreamble`.
2. Pass the current frame time from the game loop to the renderer.
3. Set it silently in `flush()`.
4. Update example shaders to use `g_Time` instead of custom `iTime`.

### Phase 4: Add has_uniform
1. Add `HasUniform(const char* name)` to `Shaders`.
2. Expose as `G.graphics.has_uniform(name)` in Lua.
