# macOS GL→Vulkan→Metal Stack — Known Issues

Catalogue of rendering bugs we have hit (or strongly suspect) that are
**not in the engine** but in the layered translation stack we use to run
OpenGL on Apple Silicon:

```
RecoilEngine (GL 4.5+ core / compat)
   │
   ▼
Mesa libGL / libgallium
   │
   ▼
Zink Gallium driver  (GL → Vulkan)
   │
   ▼
KosmicKrisp Vulkan driver (Vulkan → Metal 4)  ← Apple-silicon, macOS 26+
   │
   ▼
Apple Metal
```

For each issue, sections are: **Symptom**, **Where we hit it**,
**Root cause / hypothesis**, **Engine-side workaround**, **Upstream
status** (last checked 2026-06-20).

Sources for upstream commentary:
- KosmicKrisp docs — <https://docs.mesa3d.org/drivers/kosmickrisp.html>
- KK workarounds — <https://docs.mesa3d.org/drivers/kosmickrisp/workarounds.html>
- Mesa 26.0 release notes — <https://docs.mesa3d.org/relnotes/26.0.0.html>
- Phoronix — "KosmicKrisp Achieves MoltenVK Feature Parity" —
  <https://www.phoronix.com/news/KosmicKrisp-Parity>
- LunarG XDC 2025 talk —
  <https://www.lunarg.com/lunarg-at-xdc-2025-kosmickrisp-overview/>
- Minecraft-on-Zink+KK gist (lucamignatti) —
  <https://gist.github.com/lucamignatti/5312f5e937de2ba44256ecba6de54cc2>
  — first-hand notes on what breaks. Quoted below.

The Mesa GitLab issue tracker (`gitlab.freedesktop.org/mesa/mesa`) is
behind Anubis anti-bot and we couldn't fetch issues directly during this
audit; numbers below come from our earlier session notes.

---

## 1. `fillModeNonSolid` is not advertised — polygon mode LINE / POINT broken

**Symptom.** BAR's drag-select rectangle and any in-engine code that
uses `glPolygonMode(GL_FRONT_AND_BACK, GL_LINE)` renders as a **solid
filled quad** instead of a wireframe outline.

**Where we hit it.** `LuaOpenGL::Rect` — Lua widgets that draw a quad
after `gl.PolygonMode(GL_LINE)`. Selection box being the most visible.

**Root cause.** Apple Metal does not support a wireframe / point fill
mode for the rasterizer. KosmicKrisp therefore does not advertise the
Vulkan `fillModeNonSolid` device feature. Zink, on top of that, decides
that since the underlying driver can't do non-solid fill, requesting
`VK_POLYGON_MODE_LINE` is invalid — so the call silently falls back to
`GL_FILL`.

This is a fundamental Metal limitation, not a KosmicKrisp implementation
gap. A proper fix requires either:
1. Software wireframe emulation in Zink (a NIR pass that geometry-shader-
   expands triangles into line strips), or
2. Per-call emulation by the engine (what we do).

**Engine-side workaround.** `rts/Lua/LuaOpenGL.cpp` — intercept
`gl.PolygonMode` to track the last requested mode in a file-scope
`sMacAppleLastPolygonMode`, and in `gl.Rect` emit `GL_LINE_LOOP` of
the four corners when the mode is `GL_LINE`. Documented in `MAC_PORT.md`.

**Upstream status.**
- Mesa GitLab issue **#14209** tracks KosmicKrisp Vulkan-feature parity
  (general — not just this one).
- Mesa MR **!22277** added a NIR pass in the dzn driver that emulates
  `VK_POLYGON_MODE_LINE` by emitting line lists; conceptually the same
  approach is needed in Zink+KK but isn't merged for KK.
- Mesa MR **!38897** changed Zink so that requesting an unsupported
  polygon mode is a warning rather than a hard fallback. Helps the
  validation noise but doesn't actually draw lines.
- **No downstream fix as of Mesa 26.0 / 26.1-devel.** The Mesa 26.0
  release notes do not list wireframe polygon mode in KK; the 26.1
  parity push (Phoronix, May 2026) is about closing remaining MoltenVK
  parity gaps, not adding Metal-impossible features.
- Our emulation stays.

---

## 2. `glBlitFramebuffer` / `vkCmdBlitImage` is unreliable on KosmicKrisp

**Symptom (suspected).** Soft-edge particles render with collapsed
alpha math; possibly the cause of the **"explosions render as opaque
black blocks"** bug we are currently investigating.

**Where we hit it.** `rts/Rendering/DepthBufferCopy.cpp::MakeDepthBufferCopy`
calls `FBO::Blit(srcFBO, dstFBO, …, GL_DEPTH_BUFFER_BIT, GL_NEAREST)`
to copy the main depth attachment into a sampleable texture each frame.
That texture is then sampled by the particle fragment shader
(`cont/base/springcontent/shaders/GLSL/ProjFXFragProg.glsl`, `SMOOTH_PARTICLES`
branch) to fade particles into the geometry behind them. If the copy
silently no-ops or returns garbage, the smoothstep math collapses and
particles either disappear or render at full opaque coverage.

`rts/Rendering/Env/BumpWater.cpp` also uses `glCopyTexSubImage2D` for
its refraction screen-copy — same family of operation, same risk.

**Root cause / quote from upstream.** From the Minecraft-on-Zink+KK
gist (Luca Mignatti):

> *"The blit is emulated and lies"* — the author had to force format-
> conversion blits onto Gallium's `util_blitter` because direct
> `vkCmdBlitImage` calls produced intermittent **black frames** on KK,
> accepting ~2–3 % perf cost for stability.

Mesa's KK docs list 11 workarounds (`KK_WORKAROUND_1..11`), most of
them MSL compiler issues, but none of them is "blit". The gist's
evidence is anecdotal but matches MoltenVK's history (issue
KhronosGroup/MoltenVK#22 — `vkCmdBlitImage` only supports identical
formats and is shifted) — Metal has never had a fully general blit.

**Engine-side workaround.** **Not yet applied.** Options:
1. Disable soft particles on macOS (`SoftParticles=0` config, or hard-
   disable in `ProjectileDrawer::Init` under `__APPLE__`). Removes the
   depth-copy dependency entirely; particles draw without depth fade.
2. Replace the `FBO::Blit(GL_DEPTH_BUFFER_BIT)` with a fullscreen
   depth-copy shader (sample the default depth, write to the depth
   texture) — bypasses the broken blit path.
3. Wait for Zink/KK to fix.

Recommended first move when investigating the black-explosion bug is
**(1)** — flip `SoftParticles` off and see if the symptom disappears.
If yes, root cause confirmed; we then choose between hard-disable on
Apple or implementing (2).

**Upstream status.** No specific KK issue or MR identified for the
blit problem. The gist author works around it locally in their fork.
Worth opening an issue against KosmicKrisp with a reproducer once we
confirm the engine-side symptom matches the Minecraft author's
description. **No downstream fix.**

---

## 3. Metal compositor honours framebuffer alpha → opaque-black wipe when blending into alpha-0 background

**Symptom.** A fullscreen (or sprite-sized) quad drawn at the end of
the frame with `glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)` over an
otherwise-correctly-rendered scene **wipes the affected region to
solid black**.

**Where we hit it.**
- `rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.cpp` `EndFrame` —
  the RmlUi compositor draws a fullscreen passthrough over FBO 0.
  Symptom: completely black screen with only the engine cursor visible.
  Documented at length in `MAC_PORT.md`.
- **Suspected** same family for the "black explosion blocks" — see #2.

**Root cause.** From the gist:

> *"the gray areas all had `alpha = 0`* because Minecraft renders
> terrain without transparency. The solution was forcing
> *`alpha = 1.0`* in the blit shaders since **Metal's compositor
> respects alpha values, unlike typical OpenGL implementations**."

Standard OpenGL semantics: the default framebuffer's alpha channel is
irrelevant — the desktop compositor doesn't see it. On Metal we are
rendering into a `CAMetalLayer` whose compositor **does** consume the
framebuffer alpha. If anything in the engine's render pipeline ever
clears or leaves a region with `alpha=0`, the post-composite output is
"transparent" → presents as black against the SDL/Metal layer
background.

In our case we additionally route through `glReadPixels` → CPU buffer
→ `SDL_UpdateTexture` (see `MacGLBackend.cpp`) which may launder the
alpha problem differently than a direct Metal-layer present would —
but the GL3 RmlUi blend equation under Zink also appears to collapse
on this exact path, producing identical "opaque black".

**Engine-side workaround.** `rts/Rml/Backends/RmlUi_Backend.cpp` —
early-return on `__APPLE__` from `RmlGui::RenderFrame()`. BAR doesn't
use RmlUi for in-game HUD (it's all `gui_*.lua` widgets), so this is
acceptable. Documented in `MAC_PORT.md`.

**Proper fix candidates** (not yet investigated):
1. Force the final framebuffer alpha to 1.0 in `MacGL::Present`
   before the `glReadPixels` (clear alpha channel post-render).
2. Patch the RmlUi GL3 renderer's `EndFrame` to use a blend equation
   that doesn't write alpha (`glColorMask(true,true,true,false)` for
   that pass).
3. Audit every late-frame composite for the same risk: BAR-side LUPS
   bloom/shockwave widgets are the next suspect for the explosion bug.

**Upstream status.** This is intrinsic to running OpenGL on Metal via
any translation layer that hits `CAMetalLayer` — MoltenVK has the
same caveat. **Not a Zink/KK bug**; the gist treats it as expected
behaviour we have to accommodate.

---

## 4. Engine `SMFShaderGLSL-Forward-Adv` produces all-white terrain under Zink+KK

**Symptom.** Toggling **Advanced Map Shading** on (engine command
`/AdvMapShading`, also surfaced as the "Advanced Map Shading" graphics
option in the BAR UI) makes the entire terrain render as pure white.
Toggling it back off restores correct terrain.

**Confirmed by log evidence (2026-06-20 session).** With
`SPRING_LOG_SECTIONS=Shader,CSMFGroundTextures` exported by the launcher:

```
[t=00:00:00.003685]   AdvMapShading = 0          ← config default
[t=00:00:33.485494]   map shaders is disabled!   ← user toggled it OFF
```

The `"map shaders is disabled!"` string is **not** a Lua widget — it is
emitted by the engine at `rts/Game/UnsyncedGameCommands.cpp:425`
(`LogSystemStatus("map shaders", gd->UseAdvShading())`). The toggle
flips `CSMFGroundDrawer::advShading` which `SMFRenderState.cpp:64`
swaps `SMFShaderGLSL-Forward-Std` ↔ `SMFShaderGLSL-Forward-Adv`.

**Where we hit it.** `cont/base/springcontent/shaders/GLSL/SMFFragProg.glsl`,
compiled with `SMF_ADV_SHADING=1`. The Std variant works; the Adv
variant outputs `vec4(~1,~1,~1,1)` everywhere. No GLSL compile error
appears in `infolog.txt` even with the Shader log section enabled, so
the shader **links cleanly but runs wrong** — classic Zink → SPIR-V →
MSL translation bug in some construct only the Adv path uses.

**Suspect constructs unique to the Adv path** (from reading
`SMFFragProg.glsl:201-424`):
1. `shadow2DProj(shadowTex, vertexShadowPos)` legacy compat-profile
   sampler call. Modern Zink may not translate the implicit
   `GL_TEXTURE_COMPARE_MODE` / `GL_COMPARE_R_TO_TEXTURE` state on the
   sampler correctly, returning 1.0 (fully lit) for every pixel.
   Multiplied through `GetShadeInt(...)` this overlights to white.
2. `texture(shadowColorTex, vertexShadowPos.xy)` two-stage shadow tint
   with `mix(vec3(1.0), …, density)` — if both the shadow factor and
   the color tex degenerate to 1.0, terrain is unmodulated full-lit.
3. The `cos(angle)`/`pow` specular-lighting block at lines 410–423 adds
   `specularInt` on top of `fragColor`; if uninitialised it could
   contribute white, but only on top of correctly-lit base — unlikely
   to be the sole cause.

The **first** is the most likely candidate. The compatibility-profile
fixed-function shadow sampler call path is exactly where translation
layers historically break.

**Engine-side workaround (applied 2026-06-20, confirmed visually).**
Added shader flag `MAC_SMF_ADV_SAFE` set in `SMFRenderState.cpp:108`
under `#ifdef __APPLE__`. `SMFFragProg.glsl` checks the flag in two
places:

1. At the diffuse-write block (`#ifdef SMF_ADV_SHADING` inside
   `#ifndef DEFERRED_MODE` at line ~376): under the safe-path it
   uses the Std-shader formula instead of `GetShadeInt(...)`.
2. At the specular-addition block (lines ~403–424): under the safe-path
   it skips the `fragColor.rgb += specularInt` add entirely.

Visually under the workaround: Adv Map Shading produces correctly-lit
terrain (no white). The engine still logs
`SMFShaderGLSL-Forward-Adv is not valid` at link-validate time, but
the shader runs correctly — likely an informational warning about
sampler-binding state when the Adv path is initially linked before
shadow textures are bound. Has not been chased.

**Real-fix investigation plan** (follow-up, not blocking):

1. **Narrow which sub-piece is the actual mistranslation.** The current
   workaround bypasses BOTH the `GetShadeInt` formula AND the specular
   add. Re-enable each independently to find the real culprit:
   - Re-enable `GetShadeInt` (drop the diffuse-block guard) — if
     terrain stays correct, the bug is purely in the specular add.
   - Re-enable specular add (drop the specular-block guard) — if
     terrain stays correct, the bug is purely in `GetShadeInt`
     / shadow handling.
   - If both must stay bypassed for correct output, both paths contain
     independent bugs.
2. **For the actual fix**: depending on (1), either rewrite the failing
   construct using modern GLSL forms (e.g. replace `shadow2DProj` with
   `textureProj(sampler2DShadow, …)`) or refactor the uniform/sampler
   setup that's tripping Zink → SPIR-V.
3. **Open a KK issue** with a minimal repro once isolated. The KK team
   is responsive per the XDC 2025 talk; their workaround list grew this
   way.

**Upstream status.** Not tracked. No matching entry in
`gitlab.freedesktop.org/mesa/mesa` issues we could find (Anubis
blocks headless lookup; manual search recommended).
**No downstream fix.**

---

## 5. KosmicKrisp does not advertise `geometryShader` — BAR's GL4 widgets bail

**Symptom.** During game load you see, in `infolog.txt`:

```
[t=00:00:21.534870] EEP: initGL4 failed, bailing
[t=00:00:21.566264] Nano Particles GL4: GL init failed; falling back to engine spray.
```

EEP (Enhanced Explosion Particles, `luarules/gadgets/gfx_energy_explosion_particles_gl4.lua`)
and any other BAR gadget/widget that pivots on
`gl.LuaShader.isGeometryShaderSupported` falls back or disables itself.
Visually: fancy energy-explosion particle effects don't render; the
engine's standard CEG sprites take over.

**Where the gate is.** `modules/graphics/LuaShader.lua:38-44`
(rapid-packed inside the BAR archive, **fetched from GitHub during
this audit** — not on disk):

```lua
local function IsGeometryShaderSupported()
    local hasGeometryShaderExtension =
        gl.HasExtension("GL_ARB_geometry_shader4") or
        gl.HasExtension("GL_EXT_geometry_shader4") or
        gl.HasExtension("GL_OES_geometry_shader")
    return hasGeometryShaderExtension
        and (gl.SetShaderParameter ~= nil
             or gl.SetGeometryShaderParameter ~= nil)
end
```

Then EEP `:initGL4()` at line 734:

```lua
if not LuaShader.isGeometryShaderSupported then
    goodbye("geometry shader not supported")
    return false
end
```

**Root cause.** Apple Metal has **no geometry-shader stage**. KK does
not advertise the Vulkan `geometryShader` feature, so Zink does not
expose `GL_ARB_geometry_shader4` (or the EXT/OES variants). The
check above evaluates `false`. This is a Metal-level limitation, not
a KK implementation gap — the same applies to MoltenVK.

**Is this the cause of the "black-block explosions" user-visible bug?**
Probably **not, on its own**. EEP's bail is `return false` followed by
the gadget being inert — no draw calls. The engine falls back to
its standard CEG sprite path, which itself works (other particle types
like smoke and laser beams render correctly). So the black-block bug
likely belongs to a different render path. Documented here because:

1. The infolog entry is the most prominent warning and looked
   suspicious; useful to record that it's a red herring.
2. Multiple BAR widgets gate on the same flag (search the rapid archive
   for `isGeometryShaderSupported` to enumerate). Anyone investigating
   missing visual effects on macOS should know this is the reason.

**Engine-side workaround.** None possible — Metal cannot do this.

**Real-fix candidates:**

1. **Geometry-shader emulation in Zink+KK** via a NIR pass that
   expands each geometry-shader invocation into compute-shader
   primitive generation, similar to how dzn does point-mode emulation
   for `fillModeNonSolid` (see #1). Hard upstream work.
2. **Per-widget rewrite to compute-shader-based primitive generation**
   on the BAR side. BAR's GL4 widgets are gradually moving to compute
   anyway. Would need to be done widget-by-widget.
3. **Per-widget fallback that renders without the fancy GL4 path** —
   EEP could fall through to engine CEGs explicitly. Already happens
   for Nano Particles ("falling back to engine spray"), didn't for EEP.

**Upstream status.** No KK plans to add geometry-shader emulation that
we could find. Phoronix coverage (2026-05) of Mesa 26.1 parity push
focuses on MoltenVK parity — and MoltenVK also lacks geometry shaders,
so 26.1 won't move the needle here. **No downstream fix; unlikely
soon.**

---

## 6. Sampler bound to texID 0 returns undefined memory under Zink+KK

**Symptom.** Tree / feature models rendered as random solid colors that
changed between sessions — bright blue, then white, then bright green.
No depth or texture detail visible, just a flat-shaded silhouette.

**Where we hit it.** `rts/Rendering/Textures/NamedTextures.cpp` line
~232. When a model references a texture file that does not exist in
the loaded archives (e.g. `unittextures/tree_elm_dead_normal.dds` on
Ravaged Remake), the engine emits a `Couldn't find texture` warning
and registers the name with a placeholder `TexInfo` whose `id` is 0.
Later, when the model shader samples this name, the GL sampler is
bound to texID 0.

**Cross-platform expected behaviour.** On Mesa/Linux a sampler bound
to GL texture name 0 returns `vec4(0, 0, 0, 1)`. This is documented
Mesa behaviour and basically every other GL implementation matches.
Models with missing textures look dark/black — wrong, but
deterministic.

**What KosmicKrisp does instead.** Under Zink → Vulkan → Metal on
Apple Silicon, a sampler bound to "no texture" returns whatever
uninitialised GPU memory backs the descriptor — **a different sample
result on every session**. Tree appeared bright blue, white,
green across the three runs we tested.

This is not documented in the KK workarounds page, and it's not a
behavioural lie about blits or polygon mode — it's the descriptor /
default-image handling. Worth filing upstream.

**Engine-side workaround (applied 2026-06-21).** In the
`if (!bitmap.Load(...))` branch we now genuinely create a 1×1
black-with-alpha=1 GL texture and store its `id` in the `TexInfo`.
Every platform now gets `vec4(0, 0, 0, 1)` for "missing texture" —
Linux behaviour unchanged in spirit, Mac no longer random. Affects
all texture lookups, not just trees, so this is a global robustness
fix that also masks any other latent missing-texture bug.

**Upstream status.** Not tracked. The Minecraft-on-Zink+KK gist
(see #2/#3) reports similar "default behaviour is wrong, must
explicitly bind something" classes of issue, so this fits the
pattern. A clean repro for a KK issue would be: bind sampler in a
draw without first calling `vkUpdateDescriptorSets` for it, observe
output. **No downstream fix.**

---

## 7. Premature fragment discard optimisation (`KK_WORKAROUND_5`)

**Symptom (not confirmed in our build, but watch for).** Fragment
shaders that `discard;` based on alpha-test (e.g. our `AlphaDiscard()`
in `ProjFXFragProg.glsl`) may have downstream side-effects reordered
incorrectly by the MSL compiler. KK's workaround inserts a barrier;
Mesa enables it automatically.

**Where we hit it.** If we ever see particles flickering on a single
frame after a unit dies, or scorch decals appearing for one frame then
vanishing, suspect this.

**Engine-side workaround.** Nothing to do — KK auto-applies its own
workaround. Listed here only as a reference if we see weird flicker.

**Upstream status.** Documented and active in Mesa main. Working as
designed.

---

## 8. Other KK MSL-compiler workarounds (auto-applied, listed for reference)

From `https://docs.mesa3d.org/drivers/kosmickrisp/workarounds.html`,
all driver-internal:

| Workaround | What it is |
| --- | --- |
| KK_WORKAROUND_1 | Uninitialised scratch variables crashing MSL compiler |
| KK_WORKAROUND_2 | MSL compiler crash on certain loop patterns |
| KK_WORKAROUND_3 | `simd_ballot` misbehaves inside conditional blocks |
| KK_WORKAROUND_4 | `simd_is_helper_thread()` wrong after discard |
| KK_WORKAROUND_5 | Premature fragment discard (see above) |
| KK_WORKAROUND_6 | Device memory coherency not per MSL spec |
| KK_WORKAROUND_7 | Sample mask ignored for stencil attachments |
| KK_WORKAROUND_8 | GPU capture heap alignment under Rosetta 2 |
| KK_WORKAROUND_9 | Metal reorders break statements in loops |
| KK_WORKAROUND_10 | MSL mishandles `bcsel` (conditional select) |
| KK_WORKAROUND_11 | Concurrent `MTL4Compiler` corruption with multithreaded device creation |

We benefit from these for free. If a shader misbehaves in a way that
matches one of these descriptions and `MESA_DEBUG=1` shows the work-
around being skipped, file a Mesa issue.

---

## 9. KosmicKrisp macOS version requirement — docs vs. reality

**Status: the docs and what we actually observe disagree.** Treat the
"floor" version as unknown until someone tests on an older system.

**What the docs claim.** The Mesa KK driver page states:
> *"KosmicKrisp is a Vulkan conformant implementation for macOS on
> Apple Silicon hardware implemented on top of Metal 4, which requires
> macOS 26 and up."*

**What we actually observe.**
- Dev box: macOS 15.6.1 (Sequoia), Build 24G90, Darwin 24.6.0, Apple
  Silicon (M-series arm64).
- Our locally-built Mesa + KK loads cleanly, `eglInitialize` succeeds,
  the engine renders. None of the gnarly bugs we hit (§1–§6) are
  version-floor failures.

So either:
1. Our locally-built Mesa+KK was built against a downlevel KK branch
   that doesn't actually require Metal 4 at runtime, or only uses Metal
   4 features when available;
2. The docs floor is conservative (Metal 4 is *recommended* but not
   strictly *required* in the build we're running);
3. The docs are stale.

**What this means for the user-facing README.** Don't promise
"requires macOS 26+" yet — we can't actually verify that floor without
testing on macOS 14 / 13. Until then, README should say "tested on
macOS 15.6.1 (Sequoia) on Apple Silicon" and leave the older-version
question open.

**Engine-side workaround.** None applicable — nothing for us to do
until somebody hits a hard floor. Listed here for the README writer.

**Upstream status.** Mesa docs page —
<https://docs.mesa3d.org/drivers/kosmickrisp.html> — has the claim.
Not a bug; a documentation/reality mismatch.

---

## 10. Items we are **not** affected by but should keep an eye on

- **`GL_MAX_TRANSFORM_FEEDBACK_BUFFERS` / indirect GL features missing**
  — per the gist, prevents some shader packs. RecoilEngine uses
  transform feedback sparingly (e.g. some particle update paths).
  If we ever enable a Lua feature that uses TF and it produces nothing,
  this is the reason.
- **Multithreaded `MTL4Compiler` corruption** (`KK_WORKAROUND_11`).
  RecoilEngine spawns a lot of threads early; if we ever see shader
  compile races, force serial shader compile on `__APPLE__`.
- **Mesa 26.1-devel parity push** (per Phoronix, May 2026). Several
  MoltenVK-parity features are landing. Worth re-checking this doc
  against the 26.1 release notes once it ships.

---

## How to re-check upstream

```sh
# Mesa release notes
open https://docs.mesa3d.org/relnotes/
# KK driver page
open https://docs.mesa3d.org/drivers/kosmickrisp.html
# KK workarounds list (grow as features land)
open https://docs.mesa3d.org/drivers/kosmickrisp/workarounds.html
# Mesa issues (note: behind Anubis bot-wall in headless tools)
open https://gitlab.freedesktop.org/mesa/mesa/-/issues/?search=kosmickrisp
# Phoronix tag
open https://www.phoronix.com/scan.php?page=news_topic&q=KosmicKrisp
```

When a stack issue is resolved upstream, the cleanup is:
1. Pull / rebuild Mesa from `~/mesa-native/`.
2. Remove the `__APPLE__` workaround in the corresponding engine file
   (the workarounds are all tagged with `// macOS Zink/KK workaround`
   comments — `git grep "macOS Zink"` finds them).
3. Update both this file and `MAC_PORT.md`.
