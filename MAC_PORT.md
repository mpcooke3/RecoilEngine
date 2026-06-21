# macOS arm64 Port of RecoilEngine — Engineering Notes

This document tracks every change made to the upstream
`beyond-all-reason/RecoilEngine` to get Beyond All Reason (BAR) running
on an Apple-silicon Mac. It is intended as a hand-off doc so another
engineer (or another LLM) can pick up where the work left off without
re-deriving the context.

The branch is `mac-zink`. Run `git log origin/master..HEAD --oneline`
for the canonical commit list; this doc adds the *why* behind each.

## Layout

- Source + build under `~/WebstormProjects/BAR/` (was originally a
  sibling clone called `RecoilEngine/`; renamed and the old user's
  scratch project preserved in `/tmp/bar-scratch-backup/`).
- `build/spring` is the engine binary.
- `build/run-mac.sh` is the launcher that sets up Mesa env vars.
- `build/skirmish-bar.txt` is the script for a single-player BAR
  skirmish (vs. NullAI).
- `build/skirmish.txt` is the legacy Balanced Annihilation script
  (still works; useful for regression-checking engine changes).
- `~/mesa-native/` contains a custom Mesa build with Zink + KosmicKrisp
  drivers — the engine links `libEGL.dylib` from there at runtime via
  rpath.

## Toolchain choice — GL→Vulkan→Metal

macOS deprecated OpenGL in 10.14 and Apple ships an OpenGL 4.1
implementation only. RecoilEngine wants GL 4.5+ (compute, SSBOs,
indirect draw, …). The route taken here is:

```
RecoilEngine GL calls
   → Mesa libGL  (in-tree)
   → Zink Gallium driver (GL→Vulkan translator)
   → KosmicKrisp Vulkan driver (Vulkan→Metal, in-tree replacement
     for MoltenVK, ships in Mesa main)
   → Apple Metal
```

Pbuffer surfaces are used for offscreen rendering (no real display
window from Mesa's POV), and the rendered image is read back with
`glReadPixels` then blitted via `SDL_Renderer` Metal backend to an
`SDL_WINDOW_METAL` window. See `rts/Rendering/MacGLBackend.{h,cpp}`.

A direct CAMetalLayer present from KosmicKrisp would be faster but
needs API plumbing that doesn't exist upstream yet — the read-back path
is good enough for single-player skirmish.

## Build prerequisites

```sh
brew install cmake ninja sdl2 freetype fontconfig glew libdevil \
             p7zip openal-soft libomp pkg-config

# Mesa with Zink and KosmicKrisp must be built separately and
# installed to $HOME/mesa-native (see commit `ce3ae3bbe` and the
# Mesa repo's docs for KosmicKrisp on macOS).
```

The engine CMake defaults `MESA_NATIVE_DIR` to `$HOME/mesa-native` and
will fail configuration if it's not there.

## Committed fixes (chronological)

### `ce3ae3bbe` Add macOS ARM64 platform support: CPU topology, threading, crash handler
- Replaced the Linux-only `sched_getaffinity`/`/proc/cpuinfo` paths
  with Mach `host_processor_info`-based P-core/E-core detection.
- Stubbed POSIX signal-based crash handling for Darwin (no
  `siginfo_t->si_addr` register dumps).

### `c182fa73c` Fix macOS Clang C++ compatibility issues
- Adjustments for libc++ vs libstdc++ differences (notably
  `std::span` and `std::format` usage that's GCC-specific).

### `27a117b7c` Fix vendored library compilation on macOS/Clang
- Patched a few third-party libs that had `-Wno-…` flags Clang
  rejects.

### `9cfb18075` Fix macOS CMake: SDL2 paths, libunwind, GLAD, OpenAL EFX, linker warnings
- Homebrew puts SDL2 in `/opt/homebrew/lib`, not `/usr/local`.
- libunwind: Apple's system one differs from LLVM's — skip Linux paths.
- GLAD: don't try to load symbols Apple's GL doesn't export.
- OpenAL EFX: optional on macOS; tolerate its absence.
- Suppress `-no_warn_duplicate_libraries` warning (lots of static
  libs pull in same deps).

### `5ec45be5d` Fix DevIL include path and add missing headless GL stubs
- DevIL header lives in `IL/il.h` on Homebrew, not `il.h`.
- Headless builds reference a few GL symbols that the stub set
  didn't cover.

### `7407f2c53` Fix INLINE macro collision between smmalloc and simdjson
- Both vendored libraries `#define INLINE` to incompatible things.
  Renamed smmalloc's macro to `SMMALLOC_INLINE`.

### `453e81358` Fix macOS ARM64 build against upstream master
- Rebased on top of newer upstream; resolved a handful of minor
  conflicts around new `std::format` usage and a header
  reorganisation.

## Uncommitted work-in-progress

These are the changes currently sitting in the working tree (run
`git diff` to see them in full). They're documented here because
they're load-bearing for actually running the game and shouldn't be
lost.

### `rts/Rendering/MacGLBackend.{h,cpp}` (new files)
The macOS-only GL backend.

- **Init**: creates a Metal SDL renderer + streaming ARGB texture
  matching the renderer's drawable size, then initialises EGL on
  `EGL_DEFAULT_DISPLAY`, picks a config with `EGL_PBUFFER_BIT |
  EGL_OPENGL_BIT`, creates a pbuffer surface of that size, and
  requests an OpenGL **compatibility** profile context.

  Why compat profile, not core: the engine's `aGui/Gui.cpp` and a few
  other places still call `glMatrixMode` / `glLoadIdentity` /
  `gluOrtho2D`. In core profile those silently no-op, leaving the
  shader matrix uniform unset, which makes menu quads land in NDC
  `[0,1]` (the upper-right quadrant of the screen). Compatibility
  profile keeps the fixed-function matrix stack alive.

- **Present**: `glFinish` (block until GPU done — without this,
  KosmicKrisp can race `glReadPixels` against in-flight Vulkan
  command buffers, producing visible flicker), then `glReadPixels`
  into a CPU buffer, then **CPU row-reversal in place** (so the
  SDL texture is naturally top-first and `SDL_RenderCopy` needs no
  vertical flip — keeps mouse coordinates aligned with what the user
  sees), then `SDL_UpdateTexture` + `SDL_RenderCopy` +
  `SDL_RenderPresent`.

  The CPU row flip is the simplest robust fix for the mouse-Y
  inversion that `SDL_FLIP_VERTICAL` on the render-copy caused. It
  costs an extra memcpy per frame; if performance ever becomes a
  problem, switch to a 2-rect blit or render with `gl_FragCoord.y`
  flipped.

- **Resize**: hooked into `GlobalRendering::UpdateGLGeometry` so the
  pbuffer + texture + pixbuf are recreated when the window changes
  size. Required for overlays not to disappear when the user
  maximises the window.

### `rts/Rendering/GlobalRendering.cpp` (5 small `#ifdef __APPLE__` branches)
- Window creation: `SDL_WINDOW_METAL` instead of `SDL_WINDOW_OPENGL`.
- `CreateGLContext`: early-return invoking `MacGL::Init`; the return
  value is a sentinel `reinterpret_cast<SDL_GLContext>(0x1)` so the
  rest of the engine sees a non-null context.
- After context creation: `gladLoadGLLoader(MacGL::ProcAddress)` so
  GLAD's function pointers come from Mesa, not Apple's libGL.
- `DestroyWindowAndContext`: call `MacGL::Shutdown` first; do **not**
  call `SDL_GL_DeleteContext(0x1)` (it crashes).
- `SwapBuffers`: route through `MacGL::Present`, not
  `SDL_GL_SwapWindow`.
- `UpdateGLGeometry`: also call `MacGL::Resize(w, h)` so the offscreen
  pbuffer matches the engine's viewport.

### `rts/Rendering/CMakeLists.txt`
Added `MacGLBackend.cpp` to the `sources_engine_Rendering` list.

### `rts/builds/legacy/CMakeLists.txt`
- Sets `MESA_NATIVE_DIR` (defaults to `$ENV{HOME}/mesa-native`).
- Adds Mesa's include dir and links `libEGL.dylib` directly.
- Embeds the Mesa lib dir in the binary's rpath so `libgallium`,
  `libGLESv2`, the DRI drivers, etc. load at runtime.
- `target_link_options(... LINKER:-no_warn_duplicate_libraries)` —
  static-lib transitive deps cause harmless duplicate-symbol
  warnings.

### `rts/System/Cpp23Compat.hpp`
Consolidated `enumerate_view` constructors. Clang in C++23 mode would
otherwise complain about two overloads that resolve to the same
signature.

### `rts/Game/UI/MouseHandler.cpp`
One whitespace-only line removed. (Leftover from temporary
instrumentation; safe to drop if rebasing.)

### `rts/Net/GameServer.cpp`
Currently unmodified (diagnostics that were used to debug the
"BAR-doesn't-start" symptom were reverted once the root cause turned
out to be the log buffer, not the server). If you ever need to
re-instrument, the load-bearing state to log is `players[a].myState`,
`players[a].IsReadyToStart()`, and `teams[players[a].team].IsActive()`
inside `CheckForGameStart`.

### `rts/Rml/Backends/RmlUi_Backend.cpp` — early-return on macOS
**This is the fix for the "BAR loads but the screen is black" bug.**

`RmlGui::RenderFrame()` runs near the end of `CGame::Draw()`. Its
GL3 renderer (`rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.cpp`) ends
each frame by binding FBO 0 and drawing a fullscreen passthrough quad
with `glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`, intending to
composite the RmlUi layer (mostly transparent) over the 3D scene.

Under Mesa Zink + KosmicKrisp on Apple-silicon, that blend mode does
not behave correctly: drawing the RmlUi layer (which is cleared to
`(0,0,0,0)` and has no in-game content for our skirmish setup) ends
up writing **opaque black** to FBO 0 — wiping the entire previously
rendered frame. Without this fix the user sees a black screen with
only the engine cursor visible.

Stop-gap: skip `RmlGui::RenderFrame()` on `__APPLE__`. BAR's
in-game UI doesn't use RmlUi (it's all `gui_*.lua` widgets via
`DrawInterfaceWidgets()`), so the skirmish is fully playable without
it. The cost is any RmlUi-driven menu overlays (rare).

A proper fix requires understanding why the GL3_Recoil
`BeginFrame`/`EndFrame` blend path produces opaque-black under Zink.
Likely candidates: an sRGB/linear blend mismatch (the renderer
explicitly `glDisable(GL_FRAMEBUFFER_SRGB)` in BeginFrame which may
be a no-op under Zink), or the GL_ONE,GL_ONE_MINUS_SRC_ALPHA
equation collapsing to GL_ONE,GL_ZERO somewhere in the
Vulkan-pipeline-state cache.

### `rts/System/Log/FileSink.cpp` — `FlushOnWrite` returns true on `__APPLE__`
Spring buffers `infolog.txt` writes and only flushes on `LOG_LEVEL_ERROR+`.
When spring gets force-killed (frequent during macOS porting) the last
few seconds of log are lost — including the lines that explain what
just happened. Always flushing on `__APPLE__` makes the diagnostic
workflow tractable. Cost is one extra `fflush()` per log line; harmless.

### `rts/Rendering/GlobalRendering.cpp` — `MakeCurrentContext` is a no-op for SDL on macOS
`SDL_GL_MakeCurrent(window, glContext)` with our sentinel-`0x1` GL
context would try to bind a SDL OpenGL context to a Metal SDL_Window,
which fails and can unbind the real EGL context that MacGL owns. We
route through `MacGL::MakeCurrent()` instead, which calls
`eglMakeCurrent` on the cached EGL display/pbuffer/context.

### `rts/Rendering/Textures/NamedTextures.cpp` — bind a 1×1 black fallback for missing textures
**Symptom.** Tree (and other feature) models on maps that reference
non-existent textures rendered as solid bright colors that varied between
runs — bright blue one session, white the next, green another. Engine
log emitted e.g.:

```
Warning: Couldn't find texture "unittextures/tree_elm_dead_normal.dds"!
Warning: Couldn't find texture "unittextures/dead_tree_trunk_normal.dds"!
```

Confirmed on **Ravaged Remake v1.2** (lots of dead-tree decorations).
The texture filenames are genuinely missing from current BAR content —
not a path/case issue. Worth filing upstream as a content bug.

**Root cause.** When `bitmap.Load` failed, the engine logged the
warning and called `GenInsertTex(texName, texInfo, false, false, true, false)`
— but with `genTex=false` no GL texture was actually generated. The
sampler in any shader looking up this name stayed bound to texID 0.
On Linux Mesa a sampler bound to 0 returns `vec4(0,0,0,1)` and the
artifact is dark/invisible — which is wrong but unobtrusive. Under
Zink+KosmicKrisp on Apple Silicon it samples **undefined memory**,
yielding a different solid color every session.

**Fix.** In the `if (!bitmap.Load(...))` branch, generate a 1×1
black-with-alpha=1 GL texture, populate the `TexInfo` with its real
`id`, and insert that into the named-texture map. Now every shader
that samples a "missing" name gets a deterministic `vec4(0,0,0,1)`
sample on every platform — Linux behaviour unchanged in spirit, Mac
no longer random.

### `AI/Skirmish/BARb/data/AIInfo.lua` + `AI/Interfaces/C/CMakeLists.txt` — auto-copy AI data files into the build tree
BARb's `libSkirmishAI.dylib` built fine but the engine logged
`Error: [FetchSkirmishAILibrary] unknown skirmish AI   specified`
because the AI's `AIInfo.lua` / `AIOptions.lua` weren't in the build
directory alongside the .dylib — the `install(DIRECTORY ...)` rules in
`configure_native_skirmish_ai` only fire on `cmake --install`, not on
a normal `cmake --build`. Patched the macro to add an
`add_custom_command(POST_BUILD ... copy_if_different)` for each
expected data file. The AI now loads, initialises ("Loading the
Terrain-Map ..."), reads its config, and plays. Documented under
`MAC_GL_STACK_ISSUES.md` references because this is not a stack issue
but is part of the same Mac-port commit chain.

### `rts/Map/SMF/SMFRenderState.cpp` + `cont/.../SMFFragProg.glsl` — `MAC_SMF_ADV_SAFE` shader flag
Symptom: enabling **Advanced Map Shading** (`/AdvMapShading`) under
Zink+KosmicKrisp made the entire terrain render as solid white.

The Adv variant of `SMFFragProg.glsl` mistranslates somewhere along
the Zink → SPIR-V → MSL chain. Adding `MAC_SMF_ADV_SAFE` (set true on
`__APPLE__` from `SMFRenderState.cpp` when configuring the Adv
shaders) routes two specific fragments around the broken paths:

1. The post-`GetShadeInt` diffuse-write at line ~376 falls through to
   the Std-shader formula (`fragColor.rgb = (diffuseCol + detailCol) *
   shadingTex.rgb`).
2. The specular-add block at lines ~410–423 is skipped entirely.

**Iteration history (2026-06-20):**
- First attempt bypassed the entire ADV diffuse formula by falling
  through to `texture2D(shadingTex, …)` (the Std-shader path). That
  failed to compile because `shadingTex` is declared only inside
  `#ifndef SMF_ADV_SHADING`, so the Adv variant produced
  `'shadingTex' undeclared` at compile time, the engine fell back to
  a broken/stale program, and downstream model + UI state went off
  the rails (translucent UI widgets, black/blue trees).
- Second attempt (current): keep the existing `GetShadeInt(...)`
  formula but pass `vec3(1.0)` for shadowCoeff. The terrain renders
  correctly without shadow contribution; `shadingTex` is no longer
  needed in the Adv path so the compile error is gone.

The narrowing now points strongly at the shadow-sampling path (the
`shadow2DProj` call returning out-of-range values under KK) as the
specific Zink mistranslation. The specular block is still bypassed
under `MAC_SMF_ADV_SAFE` as a belt-and-suspenders — un-bypassing it
to confirm specular is innocent is a non-blocking follow-up. See
`MAC_GL_STACK_ISSUES.md` §4 for the real-fix path.

## Runtime config & invocation

`build/run-mac.sh` sets:

| env var                     | value                                                                   |
| --------------------------- | ----------------------------------------------------------------------- |
| `LIBGL_DRIVERS_PATH`        | `$HOME/mesa-native/lib/dri`                                             |
| `VK_DRIVER_FILES`           | `$HOME/mesa-native/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json` |
| `EGL_PLATFORM`              | `surfaceless`                                                           |
| `MESA_LOADER_DRIVER_OVERRIDE` | `zink`                                                                |
| `MESA_GL_VERSION_OVERRIDE`  | `4.6`                                                                   |
| `MESA_GLSL_VERSION_OVERRIDE`| `460`                                                                   |
| `SPRING_DATADIR`            | `$(pwd)` (so the engine finds `base/`, `packages/`, `pool/`, `maps/`)   |
| `PRD_RAPID_REPO_MASTER`     | `https://repos-cdn.beyondallreason.dev/repos.gz`                        |
| `PRD_RAPID_USE_STREAMER`    | `false`                                                                 |

Then `./spring <scriptpath>`.

## Mod content acquisition

The engine ships only the base content. To get BAR + maps:

```sh
cd build
./tools/pr-downloader/src/pr-downloader \
   --filesystem-writepath . \
   --download-game byar:test

# common maps
for m in "Red Comet" "Red Comet Remake 1.8" "Quicksilver 1.1"; do
  ./tools/pr-downloader/src/pr-downloader --filesystem-writepath . \
     --download-map "$m"
done
```

The pool grows to ~2.7 GB.

The full BAR mod name (used as `GameType` in script.txt) is
`Beyond All Reason test-30212-63c6ded` (or whatever the most recent
tag is; check `pool/<hash>/modinfo.lua`).

## What works today

- Engine builds and launches.
- Window opens, Metal renderer present path works.
- **Beyond All Reason skirmish runs end to end** on
  `build/skirmish-bar.txt` (vs NullAI on Red Comet). Commander
  spawns, terrain renders, HUD (top bar, build menu, minimap,
  selected-unit panel, resource ticker) all draw. Sim ticks at
  ~30 fps, input works, two-finger-tap right-click issues
  movement orders.
- **Balanced Annihilation skirmish** (`build/skirmish.txt`) also
  works end to end (the legacy regression target).
- Mesa Zink+KosmicKrisp reports GL 4.6 (forced by env override) and
  compiles all of the engine's GLSL → SPIR-V → MSL.

### `rts/System/FileSystem/FileSystem.cpp` — `FindFilesStd` now returns dir-relative paths
The `std::filesystem`-backed `FindFilesStd` (which replaced the older
posix-`opendir`-based `FindFiles` and is now the only path; the legacy
one is `#if`-d out) emitted **absolute** paths via
`entry.path().generic_u8string()`. The legacy posix variant returned
`dir + ep->d_name` — i.e. the input `dir` argument concatenated with
just the entry's filename — and lots of Lua widgets depend on that
exact shape.

Concrete failure that surfaced this: BAR's `gui_loadgame.lua` does

```lua
saveData.filename = string.sub(path, SAVE_DIR_LENGTH, -5)
-- SAVE_DIR_LENGTH = string.len("Saves") + 2  = 7
```

expecting each `path` to look like `"Saves/20260527_134254.lua"` so
that the substring gives the bare filename. With absolute paths
(`/Users/matthewcooke/.../Saves/20260527_134254.lua`) it instead chops
off `/Users/` and feeds the rest of the absolute path back to spring
as if it were a name — spring then complains the save doesn't exist and
nothing happens.

The fix puts the relative-path convention back into `FindFilesStd`:
build the returned string as `dirStr + filename` (UTF-8) instead of
the full `entry.path()`. Behaviour now matches the old posix variant
on all platforms, and the BAR load-game flow plus any other widget
doing the same trick (`gui_replays_window`, `gui_scenario_window`, …)
gets its expected input shape back.

This is not strictly a macOS-only patch — the std::filesystem regression
would affect any platform that's switched to `FindFilesStd`. But the
symptom only surfaced for us because we're the first ones running
BAR with the new build.

### `rts/System/Platform/Linux/CrashHandler.cpp` — skip handler install on macOS
The Linux crash handler is shared with Mac via `#include`. It registers
`HandleSignal` for SIGSEGV/SIGFPE/SIGBUS/SIGPIPE/SIGCONT/etc with the
default signal stack (no `SA_ONSTACK`, no per-thread `sigaltstack`).

`HandleSignal` itself uses a substantial stack — large local arrays,
libc++ string ops, popen calls into `atos`. On macOS, when a signal
arrives on a worker thread (the `std::async` "pregame" thread is the
canonical case here), the function prologue's `__chkstk_darwin` probes
the next page, hits the stack guard, raises a SIGBUS, runs the handler
again, ad infinitum — a stuck process you can't even attach lldb to
because everything is in kernel signal delivery.

Apple's default behaviour for the truly-fatal signals is fine (Crash
Reporter writes a full .ips report including a usable backtrace). The
one signal whose behaviour we want to change is `SIGPIPE` (the
spring-launcher → spring bridge socket closes during `Spring.Reload`,
generating a SIGPIPE we don't care about), so explicitly `SIG_IGN` it
and leave the rest to the OS.

Effect: real crashes are visible via the system Crash Reporter
(`~/Library/Logs/DiagnosticReports/spring-*.ips`); recoverable signals
no longer cause undebuggable hangs.

### `rts/Game/PreGame.cpp` — pregame async worker gets an 8 MiB stack on macOS
Save-loading uses `CPreGame::AsyncExecute(&CPreGame::LoadSaveFile, ...)`,
which spawns the worker via `std::async(std::launch::async, ...)`.
That gives the thread libpthread's default 512 KiB stack. The save-load
call chain (`LoadGameStartInfo` → `CGZFileHandler::CGZFileHandler` →
`Open` → `TryReadFromRawFS` → `ReadToBuffer` with an 8 KiB local
`unzipBuffer`) plus the std::async wrapper layers consumes almost all
of that, and `__chkstk_darwin` faults on the stack guard when entering
`ReadToBuffer` (verified from `~/Library/Logs/DiagnosticReports/spring-*.ips`:
`EXC_BAD_ACCESS (SIGBUS) KERN_PROTECTION_FAILURE`, faulting thread =
"pregame", PC inside `___chkstk_darwin`, fault address one byte below
the bottom of thread 28's stack guard).

On Linux the same code runs fine because glibc's default pthread stack
is 8 MiB, so there is no symptom for upstream to notice. The macOS
fix: on `__APPLE__`, spawn the pregame worker via `pthread_create` with
an 8 MiB stack (matching Linux) instead of `std::async`. A
`std::packaged_task<void()>` bridges the work into the pre-existing
`pendingTask` future field so the rest of the engine sees no API
change.

This was the actual root cause of "loading a saved game does nothing /
spring hangs / Spring.Reload silently fails". The earlier visible
symptom ("nothing happens, gets stuck on Waiting for game to start"
or "fully crashes") was the same stack overflow in two different
guises — first the in-tree signal handler caught the SIGBUS and went
into its own `__chkstk_darwin` loop (`HandleSignal` allocates a big
stack frame too), making the process unkillable. After we disabled
the handler on macOS (see the previous CrashHandler section), the
OS reported the crash cleanly and the `.ips` file pointed straight
at the real culprit.

### `AI/Skirmish/BARb/` — get the BAR skirmish AI to actually build on arm64

Symptom: in a skirmish vs BARbarianAI, the AI does nothing — units idle on
the start position. The commander still auto-fires at any enemy that walks
into range (engine-level unit defense, not AI), so it *looks* like a half-
working AI; everything that requires the controller (build orders, scouting,
production, attacks) is missing.

Root cause: `build/AI/Skirmish/BARb/data/libSkirmishAI.dylib` was never
produced. With no controller library, the engine instantiates the team but
no `SkirmishAI` plugin handles its events. Two separate build failures were
masking this:

1. **`SPullMtoS` operator misqualification** (`src/circuit/module/EconomyManager.h`).
   The nested struct declared `bool operator<` and `bool operator()` as
   non-const members. libstdc++ (Linux) tolerates this in `std::sort` /
   `std::lower_bound`; libc++ (Apple Clang) requires the comparator to be
   const-callable. Fix: add `const` to both members. (Upstream `CircuitAI`
   already has the `const` on `operator<` only — BARb is a drift.)

2. **AngelScript native-call assembly never linked**. AngelScript's
   `as_callfunc_arm64.cpp` defers to assembly stubs (`_CallARM64`,
   `_CallARM64Float`, `_GetHFAReturnDouble`, …) defined in
   `as_callfunc_arm64_xcode.S` (Apple) / `as_callfunc_arm64_gcc.S` (Linux).
   The `configure_native_skirmish_ai` macro globs `.c/.cpp/.c++/.cxx` only,
   so the `.S` files were never seen — undefined symbols at link time.
   Fix: in `AI/Skirmish/BARb/CMakeLists.txt`, when
   `CMAKE_SYSTEM_PROCESSOR` is `arm64|aarch64`, `enable_language(ASM)` and
   append the matching `_xcode.S` (Apple) or `_gcc.S` (other) to
   `additionalSources`. Scoped to the BARb target so it doesn't perturb
   other AIs (CircuitAI/CppTestAI also fail to build but BAR doesn't use
   them — left as a follow-up).

After both fixes the dylib (~8 MiB) lands at
`build/AI/Skirmish/BARb/data/libSkirmishAI.dylib` and the engine picks it
up automatically from that path.

## What doesn't work / open issues

1. **RmlUi rendering is disabled on macOS** (see the `RmlUi_Backend.cpp`
   section above). Stop-gap: skipping `RenderFrame()` on `__APPLE__`.
   Proper fix needs to chase down why the GL3 renderer's blend pipeline
   produces opaque-black under Zink. Symptom otherwise: in-game scene
   gets wiped by a fullscreen quad each frame.

2. **HUD overlay flicker on zoom-in (BA skirmish).** Likely a
   Vulkan-pipeline-state race in KosmicKrisp under heavy LOD changes.
   `glFinish` before `glReadPixels` in MacGLBackend cut it down a lot
   but didn't fully eliminate it.

3. **Dark/muddy terrain rendering (BA skirmish).** Visual quality
   regression vs. native Windows. Could be missing/wrong shaders, GL
   state differences, or KosmicKrisp texture-filter implementation.
   Not deeply investigated. BAR doesn't exhibit this — its terrain
   shader path produces correct colours.

4. **BAR's "potato Graphics Card detected" branch.** BAR's quality
   detection labels Zink+KosmicKrisp as low-end and disables some
   effects. That's harmless and probably correct given the throughput
   of the readback path.

5. **BAR Chobby (the lobby).** Downloaded by pr-downloader but BAR's
   modern menu requires their Electron `spring-launcher` wrapper to
   run, which we don't have. Skirmishes work via direct
   `script.txt` only.

6. **Mesa warning at startup:** `Incorrect rendering will happen because
   the Vulkan device doesn't support the 'fillModeNonSolid' feature`.
   This is KosmicKrisp's limitation (Metal doesn't expose wireframe
   polygon mode). Doesn't affect normal play; only matters if a Lua
   gadget tries `glPolygonMode(GL_LINE)` for a debug overlay.

## How the breakthrough happened (BAR-loads-but-black-screen)

In case the same class of bug shows up elsewhere — the diagnostic
ladder that found it was, in order:

1. Realised the "BAR is hung" reading was a log-flush artefact. Patched
   `FileSink.cpp` to always flush on `__APPLE__`, immediately saw the
   sim was actually running at 30 fps.
2. Confirmed `MacGL::Present` was being called and the EGL context was
   current via `eglGetCurrentContext()` check.
3. Confirmed FBO 0 + glReadPixels round-trip works mid-frame by
   clearing the pbuffer to a known colour and reading it back.
4. Added a corner-pixel probe right after `worldDrawer.Draw()` —
   showed `(157,102,74)` (sand). World draws DO land in FBO 0.
5. Added probes after EACH step of `CGame::Draw`'s "Screen pass" —
   found the corner went to `(0,0,0)` exactly after
   `RmlGui::RenderFrame()`.
6. Read RmlUi's GL3 renderer; saw the EndFrame fullscreen-quad
   composite step. Skipping `RenderFrame()` on `__APPLE__` restored
   the entire frame.

Take-away: when "things rendered but the screen is empty," instrument
the engine's draw flow with single-pixel readbacks between every
discrete step. It localises wipes in O(log n) iterations.

## Misc tips for future debugging

- The infolog at `~/.config/spring/infolog.txt` is **buffered**. If
  spring exits unexpectedly, the last second or two of log is lost.
  `tail -f /tmp/spring-*.log` (stdout redirected) is usually a few
  lines ahead of the infolog and survives crashes.
- Window IDs change every launch; use Quartz `CGWindowListCopyWindowInfo`
  to find the spring window for `screencapture -l <id>`. A Swift
  one-liner that prints `id=<n> pid=<p>` for any process whose owner
  name is `spring` is the fastest way.
- macOS trackpad's two-finger tap = right-click. Single-tap is not.
  This caught us out for a while when testing the "right-click to
  move" path; the engine and SDL were behaving correctly.
- **Always `pkill -f build/spring` before launching a new
  instance** — stuck instances build up fast when debugging start
  hangs.
