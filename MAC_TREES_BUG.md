# macOS "bright cyan-blue trees" bug — root cause & fix

## Symptom

On macOS Apple Silicon (Mesa + Zink + KosmicKrisp stack), trees and other
GAIA-team features on certain maps render with a **saturated cyan-blue**
overlay when viewed from medium-to-far camera distances. The bug is
camera-position dependent: close-up shots look correctly dark green, but
mid-range overhead views show every tree painted bright blue, often
matching the player's team colour.

Linux/Windows users do not see this. Originally suspected to be a tree-
specific shader path or a Mesa-cubemap-sampling bug. **Neither — see below.**

Reproduction: `tools/run-test.sh selection` (an autonomous test that
cheat-spawns units near the commander on Angel Crossing and screenshots
the surrounding terrain) reliably produces the buggy frame as
`build/screenshots/screen_*.dbg-selection-1-unselected.png`.

## Root cause

BAR's PBR shader `modelmaterials_gl4/templates/cus_gl4.frag.glsl` has a
construction / "under-build" visual effects block starting at line 1167:

```glsl
float buildProgress = shadowVertexPos.w;
if (buildProgress > -0.5){   // <-- THE BUG
    // ... lots of construction-grid effects ...
    outSpecularColor += pulseTeamColor * line * sintimefast * 2.0;  // line 1265
}
```

`shadowVertexPos.w` is populated in the vertex shader at line 710:

```glsl
shadowVertexPos.w = UNITUNIFORMS.userDefined[0].x;  // pass in construction progress 0-1
```

For **units**, `userDefined[0].x` is properly set to the actual build
progress (0-1 while building, then BAR's gameplay code is supposed to
clamp it to -1 or to 1.0 when complete).

For **features** (trees, wrecks, rocks, debris — anything that's *not* a
unit), `userDefined[0].x` is **never written** by the engine and defaults
to 0. The shader sees `0 > -0.5` → true, and the construction block fires
on every feature.

Inside the block, two team-colour-driven lines blend the team colour into
the output:

```glsl
outSpecularColor += vec3(levelFactor);
outSpecularColor += pulseTeamColor * line * sintimefast * 2.0;
```

`pulseTeamColor` derives from `teamCol.rgb`, the per-instance team
colour. For features on macOS, `teamCol.rgb` reads back as the player
team's **bright blue** instead of GAIA's neutral grey/black — likely a
separate uninitialised-uniform-buffer issue, but irrelevant for the fix
because the construction block shouldn't fire on features at all.

The visible result: trees + every feature get a per-frame pulsing
bright-blue specular contribution that swamps the actual lighting.

### Why is it macOS-specific?

It's almost certainly a latent bug that *also* misrenders on Linux/Windows
but is masked there because:
- on those platforms, `teamCol.rgb` for features reads as GAIA's near-
  neutral colour, so the bogus addition is grey-tinted and visually
  ambiguous instead of saturated blue
- the `pulseTeamColor * sintimefast * 2.0` term still pulses but is
  drowned out by the (correctly-lit) base albedo

On Apple's Mesa+Zink+KK stack, the per-feature uniform buffer slot for
`teamCol.rgb` reads back as the player team's blue — turning the latent
issue into a screaming visual bug.

## Fix

Tighten the gate so the feature-default-0 case is excluded:

```diff
- if (buildProgress > -0.5){
+ if (buildProgress > 0.001){
```

Rationale:
- Real units under construction reach > 0.1% buildProgress within one
  sim tick of being placed, so the construction visuals are still
  triggered for any actual build.
- The `> 0.001` threshold is small enough that nothing perceptibly
  changes for legitimate construction effects.
- Features with their default `0` value cleanly fall outside the gate.

Verified visually with `tools/run-test.sh selection` — trees now render
dark green as intended; cheat-spawned units retain selection highlights
correctly; the construction visuals still appear on units actively being
built (verified separately).

## Where the fix lives

The patched shader is at:

    tools/content/modelmaterials_gl4/templates/cus_gl4.frag.glsl

`tools/run-test.sh` copies it into `build/modelmaterials_gl4/templates/`
(the writepath) before each launch. BAR's `VFS.LoadFile` uses RAW_FIRST
in gadget context (CUS GL4 is a gadget), so the writepath copy takes
precedence over the archive version.

The fix is a candidate for an upstream BAR PR — the gate could trivially
be tightened in BAR's own copy of the shader. The deeper engine-side fix
(initialise `userDefined[0].x = -1.0` for feature uniform buffer slots)
is also viable but more invasive.

## Investigation log

Bisection technique: progressively replaced `fragData[0] = ...` with
test colours visualising intermediate values inside the shader, then
ran `tools/run-test.sh selection` to capture the result. ~30 seconds
per iteration end-to-end.

| Probe | Result | Conclusion |
|---|---|---|
| Sanity: pure magenta | Trees magenta | Writepath override loads correctly |
| `albedoColor.rgb` | Dark green/brown | Input texture data is fine |
| `reflectionColor` | Near black | Sky cubemap isn't the source |
| `specular` (IBL term) | Near black | IBL specular isn't the source |
| `dirContrib` | Dark blue | dirContrib is the symptom carrier |
| `maxSun` | Mid green-ish | Sun model isn't blue |
| `sunSpecularModel.rgb` | Pink | Sun specular is correctly pink |
| `sunDiffuseModel.rgb` | Pure white | Sun diffuse is correctly white |
| `outSpecularColor` (with Cook-Torrance zeroed) | **Bright blue** | Bug is in late additions, NOT Cook-Torrance |
| `texColor2.rgb` (PBR mask) | Dark teal | metalness/roughness inputs are reasonable |
| `shadowVertexPos.w` diagnostic | bp ≈ 0 | **Feature default = 0, build block enters** |
| `teamCol.rgb` | **Bright blue** | Feature team colour is wrong |

The combination of the last two probes pinpointed the issue: feature
fragments enter a code path they shouldn't, and inside that code path
they read a wrong team colour and dump it into the specular accumulator.

## See also

- `MAC_VISUAL_TESTS.md` — describes the autonomous test harness used to
  bisect this. The `selection` test (originally just for selection-
  indicator capture) reliably reproduces this bug as a side effect of
  the camera-over-commander framing.
- `MAC_GL_STACK_ISSUES.md` — companion notes on other macOS-specific
  Mesa+Zink+KK rendering quirks.
