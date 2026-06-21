# Mac BAR visual-regression tests

Three autonomous tests for capturing specific render-path bugs without
having to click around the launcher / lobby / game manually each time.
All three use the same base harness (`tools/self-test-trace.sh`) and an
in-game Lua widget that handles camera positioning, screenshots, and
clean shutdown.

| Test | Captures | Widget | Time |
|---|---|---|---|
| `scorch`     | commander beam-down + dark scorch CEG (the original black-square bug) | `dbg_auto_screenshot.lua` | ~30 s |
| `trees`      | first tree feature on the map (Ravaged Remake's dead elms) | `dbg_test_trees.lua` | ~25 s |
| `explosions` | a cheat-spawned fusion-reactor self-destruct (the explosion CEG path) | `dbg_test_explosions.lua` | ~40 s |
| `selection` | a cluster of cheat-spawned bots, captured unselected / all-selected / single-selected (also incidentally captures the "bright blue trees" repro) | `dbg_test_selection.lua` | ~25 s |

## Running a test

```sh
tools/run-test.sh <scorch|trees|explosions> [duration_seconds]
```

Each test:

1. Edits `build/LuaUI/Config/BYAR.lua` to enable exactly one of the
   `DBG Test *` widgets (and disable the others).
2. Launches `spring --write-dir build tools/skirmish-trace.txt` directly
   — no Electron launcher, no lobby.
3. The widget controls everything from inside the game: camera position,
   when to screenshot, when to quit.
4. Screenshots land in `build/screenshots/screen_<timestamp>.<label>.png`.

If a widget hasn't been seen by BAR before (first ever run), BAR will
discover it but leave it at `order=0` (disabled). Rerun the command to
actually exercise it.

## Test 1 — scorch (the original bug)

Reproduces the commander beam-down scorch CEG that previously rendered
as a solid black square under premultiplied-alpha blending on Mac.

Widget: `build/LuaUI/Widgets/dbg_auto_screenshot.lua`

Takes 4 screenshots at fixed offsets after `GameStart`:
- `01-spawn-instant` — t+1 s, beam just landed
- `02-spawn-3s`     — t+3 s, brightest part of the CEG
- `03-spawn-7s-mid-life` — t+7 s, when the dark colormap kicks in
- `04-spawn-14s-fading` — t+14 s, scorch fading out

Expected with `MAC_FX_DARK_SAFE` active: bright teleport glow in
01-02, no visible dimming or black patch in 03-04.

## Test 2 — trees

Flies the camera to the first tree-shaped feature on the map (the test
script targets Ravaged Remake, which has dead elm-tree features that
previously triggered the random-color-tree bug due to missing texture
fallback).

Widget: `build/LuaUI/Widgets/dbg_test_trees.lua`

Two screenshots: overhead and a closer angle. Expected with our
`NamedTextures::Load` fallback: trees render as deterministic black
silhouettes (the underlying scar50 texture file is genuinely missing
in BAR's content — see MAC_GL_STACK_ISSUES.md §6 — so the fallback's
black is the right behaviour).

## Test 3 — explosions

Uses BAR's cheat system to spawn and self-destruct a unit. The
explosion CEG is the same one used by all weapon impacts and unit
deaths, so this exercises the same blend / FX-shader path as a real
battle explosion.

Widget: `build/LuaUI/Widgets/dbg_test_explosions.lua`

Sequence:
1. At t=4 s, centres the camera over the player's commander (so the
   following cheat-give places units on land near us, not at whatever
   sea coordinate the default camera happens to look at).
2. `cheat`, then `give 1 armfus`, `give 1 armbull`, `give 1 armham` —
   tries a fusion reactor first (largest explosion), then a heavy
   tank, then a rocket bot.
3. `GiveOrderToUnit(unit, CMD_SELFD, …)` on the largest unit it found
   that *isn't* the commander (selfd-ing the commander would trigger
   BAR's `game_selfd_resign` gadget, ending the game and switching to
   spectator view).
4. On `widget:UnitDestroyed`, takes 5 screenshots at fixed offsets
   after the explosion (`000ms`, `300ms`, `1000ms`, `2000ms`, `4000ms`).

Expected: visible flash, particles, scorch decal. The framework works
end-to-end; whether the captured frames show a dramatic-looking
explosion depends on camera angle and unit choice — refine
`pointCameraAt` and the unit-preferences list in the widget to taste.

## Lateral approaches that didn't work

For reference / so we don't re-tread the same dead ends:

- **macOS `screencapture`** — only captures whatever window is
  frontmost. Spring isn't focused when launched headless, so we end up
  screenshotting the IDE / terminal instead. Tried `osascript activate`
  → blocked by accessibility permissions.
- **`cliclick kp:f12`** — sends F12 to whatever window is focused; same
  focus problem.
- **Spring's screencapture with `-l <windowid>`** — would work but
  needs the window ID; finding it requires another scripted step.
- **Vulkan API dump (`VK_LAYER_LUNARG_api_dump`)** — installed, layer
  loads, but doesn't write to the configured file. Likely a setting-
  name discovery issue (env var prefix). Not blocking; using engine-
  side probes + screenshots gives equivalent data for our purposes.

The in-game Lua widget bypasses all of these — it's the screenshot
mechanism the engine itself uses, fully scriptable, no host-OS focus
permission issues.

## Setup from scratch

1. Mesa+KK at `~/mesa-native/` (see `MAC_PORT.md` and
   `MAC_GL_STACK_ISSUES.md`).
2. Engine built into `build/spring` with the macOS Mac-port patches.
3. `tools/skirmish-trace.txt` present (auto-patched at run time so
   `gametype=` matches the loaded BAR archive).
4. The three `dbg_*` widgets at `build/LuaUI/Widgets/`.
5. `chmod +x tools/run-test.sh tools/self-test-trace.sh`.
6. Sanity run:
   ```sh
   tools/run-test.sh scorch 30
   ls build/screenshots/screen_*spawn*.png
   ```

If the first run says the widget hasn't been registered, just rerun
the same command — BAR adds it on first sighting.

## Adding a new test

1. Drop `build/LuaUI/Widgets/dbg_test_<name>.lua` with `GetInfo().name`
   set to a unique string (e.g. `"DBG Test Foo"`).
2. Add a `case "<name>")` branch to `tools/run-test.sh` that sets
   `WIDGET_NAME` to the same string.
3. Run once to register, run again to actually exercise.
