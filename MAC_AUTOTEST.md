# Autonomous Mac BAR self-test harness

This document captures how I (Claude) set up a hands-off test loop for
debugging Mesa/Zink/KK rendering bugs in BAR on macOS, so I can iterate
without the user clicking through the launcher and lobby every cycle.

## What the harness does

`tools/self-test-trace.sh [DURATION_SECONDS]`:

1. Kills any stale spring / launcher processes.
2. Patches `tools/skirmish-trace.txt`'s `gametype=` line so it matches the
   actually-installed BAR version (parsed from the previous `infolog.txt`).
3. Exports Mesa env vars (`LIBGL_DRIVERS_PATH`, `VK_DRIVER_FILES`,
   `EGL_PLATFORM=surfaceless`, `MESA_LOADER_DRIVER_OVERRIDE=zink`,
   `MESA_GL_VERSION_OVERRIDE=4.6`, `MESA_GLSL_VERSION_OVERRIDE=460`)
   — same set as the Electron launcher's `config.json`.
4. Optionally enables Vulkan API tracing via the LunarG SDK layers
   (`~/VulkanSDK/1.4.350.1/macOS/share/vulkan/explicit_layer.d`).
5. Launches `spring --write-dir <writedir> tools/skirmish-trace.txt`
   directly — no launcher, no Electron, no lobby.
6. Waits for the in-game `Auto Screenshot (mac debug)` widget to take
   screenshots on a timer (`build/LuaUI/Widgets/dbg_auto_screenshot.lua`)
   and quit when done.
7. Reports the size of `vk-trace.log`, `mesa-debug.log`, `infolog.txt`,
   and lists the captured screenshots.

End-to-end runtime: ~30 seconds for a 4-screenshot session.

## Pieces that go into making this work

### 1. Spring boots straight into a skirmish

`tools/skirmish-trace.txt` is a copy of a known-good in-game script
(`build/_script.txt` from a real session, with `gametype` auto-patched).

Key fields:
- `startpostype=1` — random pick inside start box. **This is load-bearing.**
  If left at `2` (manual pick), spring sits at the start-position picker
  forever waiting for a click that won't come.
- `gamestartdelay=5` is fine.
- `ai0` is `BARb` profile `Hard` so the AI scripts exercise the same
  render paths as a real game.

`spring --write-dir <writedir> <script>` is enough — no `--menu`, no
extra args, no engine_path resolution dance.

### 2. The Mesa env vars are mandatory

Without `LIBGL_DRIVERS_PATH`/`VK_DRIVER_FILES`/`EGL_PLATFORM=surfaceless`,
spring's window creation in `MacGLBackend::Init` works but the GL context
silently bails after first GL call. The launcher sets these from
`launcher/src/config.json`; the harness mirrors them.

### 3. In-game screenshot widget

`build/LuaUI/Widgets/dbg_auto_screenshot.lua` triggers
`Spring.SendCommands("screenshot <label>.png")` at fixed game-second
offsets after `widget:GameStart()`, then calls `quit`/`quitforce`. PNGs
land in `build/screenshots/`.

**Watch out:** BAR's widget loader respects `build/LuaUI/Config/BYAR.lua`'s
order map. A new widget is auto-discovered (it shows up in the file with
`order = 0`) but isn't loaded until you set it to `1`:

```sh
sed -i.bak 's/\["Auto Screenshot (mac debug)"\] = 0,/["Auto Screenshot (mac debug)"] = 1,/' \
    build/LuaUI/Config/BYAR.lua
```

Spring's `screencapture` (macOS native) is unreliable because spring's
window isn't foreground and `osascript activate` needs accessibility
permissions. The in-game widget bypasses all of that.

### 4. Vulkan API trace (incomplete — open issue)

`BAR_VK_TRACE=1` enables the LunarG `VK_LAYER_LUNARG_api_dump` layer.
Loader successfully *inserts* the layer (verifiable via
`VK_LOADER_DEBUG=layer,error,warn`) but no output ends up in
`build/vk-trace.log` despite setting `VK_APIDUMP_LOG_FILENAME`,
`VK_APIDUMP_FILE=true`, `VK_APIDUMP_OUTPUT_FORMAT=text` and
`build/vk_layer_settings.txt`. Suspected cause: api_dump's setting-name
discovery requires either the modern `VK_<LAYER_SHORT>_<KEY>` (e.g.
`VK_LUNARG_API_DUMP_LOG_FILENAME`) or `vk_layer_settings.txt` in the
process CWD specifically. Open follow-up.

For now the harness still produces useful data without the Vulkan trace:
- Engine `infolog.txt` shows GL renderer, shader compile errors, blend
  state changes.
- Screenshots show the actual rendering result.

## How to use the harness for visual-regression debugging

1. Make a hypothesised change in the engine or shader.
2. `cmake --build build --target engine-legacy basecontent -j8` (fast — both
   targets are incremental).
3. `tools/self-test-trace.sh 30`.
4. Inspect screenshots in `build/screenshots/` and `infolog.txt`.
5. Iterate.

For comparing buggy vs. working state:

- Keep the workaround flag (e.g. `MAC_FX_DARK_SAFE`) as the toggle. Same
  shader, two compiles, two screenshots, side-by-side comparison.
- Or vary one shader expression per run and screenshot the result —
  the screenshot lets me see colour/alpha output as actual pixels.

## Adding new probes

To visualise something the shader knows but the rendering normally hides
(an intermediate value, e.g. `fragColor.a` or a derivative), inject a
debug write at the end of the fragment shader:

```glsl
// TEMP DEBUG: visualise <thing>
fragColor.rgb = vec3(<thing>.x, <thing>.y, <thing>.z);
fragColor.a = 1.0;
```

…then run the harness. Screenshots show the raw value as a colour.
Standard graphics-debugging technique — but the harness automates the
"now go play the same 12 seconds again" part.

## Rebuilding from scratch

1. Restore `tools/skirmish-trace.txt` and `tools/self-test-trace.sh`.
2. Drop `dbg_auto_screenshot.lua` into `build/LuaUI/Widgets/`.
3. Set its order to `1` in `build/LuaUI/Config/BYAR.lua`.
4. `chmod +x tools/self-test-trace.sh`.
5. Sanity-run `tools/self-test-trace.sh 30` and check
   `build/screenshots/`.
