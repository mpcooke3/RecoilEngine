# Cross-Architecture Desync Fixes (ARM64 vs x86_64)

## Overview

When running BAR replays on ARM64 and x86_64, per-frame sync checksums can diverge
despite all streflop FP flags being correctly applied. This document tracks the root
causes found and the fixes applied or pending.

Branch: `arm64-replay-testing-v3` (based on upstream tag `2025.06.19` + supaku's ARM64 patches from PR #2819)

Previous branch: `arm64-upstream-061919` (macOS ARM64 — fixes #1-#5 discovered there)

---

## Test Environment

### Machines

| Machine | Arch | OS | Compiler |
|---------|------|----|----------|
| ARM64 Linux VM | ARM64 | Ubuntu 24.04 (tart on Apple Silicon) | GCC |
| ethanbaby | x86_64 | Linux (Ubuntu, Ryzen 5 8500G) | GCC |

### Engine

- Version: `2025.06.19-10-g8aafac2 arm64-replay-testing-v3`
- Build: `cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DHEADLESS=ON ..`
- Flags verified: `-ffp-contract=off`, `-frounding-math`, `-fsingle-precision-constant`, `STREFLOP_NEON`

### Game Content

- Game: `Beyond All Reason test-29565-02e94b8`
- Pool files: ~19,081 files in `pool/`
- Maps: 33 `.sd7` files in `maps/`

### Replays

- 70 team replays in `demos-batch3/`, all from engine version `2025.06.19`

### Checksum Output

Per-frame checksums are written when `SYNC_CHECKSUMS_PATH` env var is set.
Format: `<frame> FrameEnd <checksum_hex>`.

---

## Commits on Top of Upstream `2025.06.19`

The v3 branch cherry-picks only the **platform support** commits from supaku's
PR #2819. Sync-affecting changes (ClampRadPi, COB cast fix) were deliberately
excluded because they change x86_64 behavior and break demo compatibility.

| Commit | Description |
|--------|-------------|
| (7 commits) | supaku's ARM64 platform patches (streflop NEON, sse2neon, cmake, etc.) |
| various | Sync checksum output infrastructure (SYNC_CHECKSUMS_PATH, DemoSpeedFactor) |
| `8aafac2f09` | **Fix #5**: Headless atlas rendering infinite loop |
| `02b9b8a269` | Move sync checksum output from SYNCRESPONSE to NEWFRAME handler |
| (uncommitted) | **Fix #6**: `FloatToIntPortable()` for COB `int(float)` UB |

---

## Fix #1: AnimInfo Struct Padding (NOT NEEDED on v3)

**Original commit**: `2f674c6b3f` (on `arm64-upstream-061919`)

Not needed on v3 because v3 is based on `2025.06.19` which includes upstream commit
`fb525606af` that added animation checksumming. The AnimInfo padding bug only
manifested with macOS libc vs Linux glibc heap allocators — both machines on v3
run Linux with glibc, so padding bytes happen to match. However, this fix should
be applied if macOS ARM64 builds are ever tested.

---

## Fix #2: ClampRadPi for Heading Angles (NOT INCLUDED in v3)

**Upstream commit**: `ad716fe785` ("Do proper modulo for heading (#2827)")

This fix changes `ClampRad()` to `ClampRadPi()` at 4 call sites (AimWeapon,
WindChanged, StartBuilding, GetUnitHeading). It was confirmed effective on the
old branch — resolves a 1-ULP rounding divergence in `ClampRad(x)` vs
`ClampRad(x + 2π)`.

**Not included in v3** because it changes gameplay behavior on ALL platforms
(returns [-π, π) instead of [0, 2π)), which causes x86_64 to desync against
demo replays recorded with the stock engine. This fix is already merged upstream
and will be present in future engine versions.

---

## Fix #3: std::stable_sort in Animation Tick (NOT NEEDED)

No effect — sort keys are unique, so sort order is deterministic. Applied as a
precaution on the old branch but confirmed irrelevant.

---

## Fix #4: fastmath::floor Divergence (INCLUDED via supaku's patches)

ARM64 `FCVTZS` saturates on overflow while x86 `CVTTSS2SI` returns `0x80000000`.
The ARM64 path in `fastmath::floor` uses `streflop::floor` instead of
`static_cast<int>` to avoid this divergence. Included in supaku's ARM64 patches.

---

## Fix #5: Headless Atlas Rendering Infinite Loop (APPLIED)

**Commit**: `8aafac2f09`
**Files**: `rts/Rendering/IconHandler.cpp`, `rts/Rendering/Textures/TextureRenderAtlas.cpp`

In headless mode, atlas texture creation retries every frame because FBO stubs
never produce a valid result. This causes ~300K log lines of spam, CPU waste,
and eventual LuaRAM exhaustion crash.

Fix: `#ifdef HEADLESS` guards that clear `atlasNeedsUpdate` and set
`atlasRendered = true` immediately.

---

## Fix #6: `int(float)` Platform-Dependent UB in COB Scripts (APPLIED — uncommitted)

**File**: `rts/Sim/Units/Scripts/UnitScript.cpp`
**Impact**: Fixes the Hellas Basin desync (frame 46870) — the only desync in 26+ replays tested

### Root Cause

`static_cast<int>(float)` is **undefined behavior in C++** when the float value is
NaN, infinity, or outside the representable range of `int`. The two architectures
produce different results:

| Input         | x86 (CVTTSS2SI)        | ARM64 (FCVTZS)      |
|---------------|------------------------|----------------------|
| NaN           | `0x80000000` (INT_MIN) | `0`                  |
| +Infinity     | `0x80000000` (INT_MIN) | `INT_MAX`            |
| -Infinity     | `0x80000000` (INT_MIN) | `INT_MIN`            |
| > INT_MAX     | `0x80000000` (INT_MIN) | `INT_MAX` (saturates)|
| < INT_MIN     | `0x80000000` (INT_MIN) | `INT_MIN` (saturates)|
| Normal range  | Truncation toward 0    | Truncation toward 0  |

x86 CVTTSS2SI returns `0x80000000` for **all** exceptional inputs.
ARM64 FCVTZS returns 0 for NaN and saturates for overflow.

### Test Program (`test_int_nan.cpp`)

A standalone test reproduces the exact divergence. Compile and run on both architectures:

```bash
g++ -O2 -std=c++17 -o test_int_nan test_int_nan.cpp -lm && ./test_int_nan
```

**ARM64 output (Ubuntu 24.04, GCC, aarch64):**
```
Input                          float value       int(float)
-------------------------  ---------------  ---------------
NaN                                    nan                0
+Infinity                              inf       2147483647
-Infinity                             -inf      -2147483648
3e9 (> INT_MAX)                      3e+09       2147483647
-3e9 (< INT_MIN)                    -3e+09      -2147483648
42.5 (normal)                         42.5               42

COB POW(-2979761, 32768):
  int(NaN * 65536) = 0
  --> ARM64 behavior: int(NaN) = 0
  ATAN(446740, 0) = PI/2 = 90 degrees (ARM64 path)
```

**x86_64 output (Ubuntu, GCC, Ryzen 5 8500G):**
```
Input                          float value       int(float)
-------------------------  ---------------  ---------------
NaN                                    nan      -2147483648
+Infinity                              inf      -2147483648
-Infinity                             -inf      -2147483648
3e9 (> INT_MAX)                      3e+09      -2147483648
-3e9 (< INT_MIN)                    -3e+09      -2147483648
42.5 (normal)                         42.5               42

COB POW(-2979761, 32768):
  int(NaN * 65536) = -2147483648
  --> x86_64 behavior: int(NaN) = INT_MIN (-2147483648)
  ATAN(446740, -2147483648) ≈ PI = 180 degrees (x86 path)
```

The same `int(NaN)` produces 0 on ARM64 vs INT_MIN on x86_64, leading to a 90°
vs 180° turret rotation — the exact desync trigger in the Hellas Basin replay.

### Affected Code

The bug is in `CUnitScript::GetUnitVal()` in `rts/Sim/Units/Scripts/UnitScript.cpp`.
Multiple `return int(...)` statements can produce NaN or overflow:

```cpp
// POW (GET constant 80) — CONFIRMED DESYNC TRIGGER
case POW:
    return int(math::pow((p1 * 1.0f) / COBSCALE, (p2 * 1.0f) / COBSCALE) * COBSCALE);
    // pow(negative, 0.5) = NaN → int(NaN * COBSCALE) diverges

// KTAN (GET constant 137) — can produce infinity
case KTAN:
    return int(1024*math::tanf(TAANG2RAD*(float)p1));
    // tan(PI/2) = ±infinity → int(±inf) diverges

// KSIN/KCOS (GET constants 135/136) — safe range but protected for consistency
case KSIN: return int(1024*math::sinf(TAANG2RAD*(float)p1));
case KCOS: return int(1024*math::cosf(TAANG2RAD*(float)p1));

// SQRT (GET constant 134) — NaN for negative input
case SQRT: return int(math::sqrt((float)p1));
```

### Upstream Status (as of 2026-03-12)

- **Upstream HEAD** (`f506f68fc1`): Bug exists — unprotected `return int(math::pow(...))`
- **supaku/upstream/arm64-support** (PR #2819): Bug exists — same code, not addressed
- **This is a new discovery** not covered by any existing ARM64 porting work
- The bug is latent on x86-only builds (CVTTSS2SI behavior is consistent, just not C++ standard)

### The Specific Desync Trigger (Hellas Basin)

The legphoenix unit (a Legion T2 bomber in BAR) has a COB script `FakeUprightTurn()`
that keeps its weapon turret visually upright during flight banking. It runs every
frame during a strafing run (~65 frames per strafe):

```bos
FakeUprightTurn() {
    var angle_x, angle_z, dy_x, dy_z;
    dy_x = GET PIECE_Y(x) - GET PIECE_Y(base);
    dy_z = GET PIECE_Y(z) - GET PIECE_Y(base);
    // Compute: angle = atan2(sin(tilt), cos(tilt))
    // where cos(tilt) = sqrt(1 - sin²(tilt))
    angle_x = GET ATAN(dy_x, GET POW(65536 - GET POW(dy_x, 131072), 32768));
    //                                       ^^^^^^^^^^^^^^^^^^^^^^^^
    //                                       sin²(tilt) * COBSCALE
    //                        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    //                        sqrt(1 - sin²) — NEGATIVE when tilt > ~57°!
    //                        pow(negative, 0.5) = NaN
    angle_z = GET ATAN(dy_z, GET POW(65536 - GET POW(dy_z, 131072), 32768));
    turn triparent to x-axis angle_x now;
    turn triparent to z-axis angle_z now;
}
```

The `x` and `z` pieces are offset by 1 elmo from `base` along perpendicular axes.
When the aircraft banks, the Y-position difference between these probe pieces and
`base` gives `sin(tilt) * COBSCALE`. The script computes `cos(tilt)` via
`sqrt(1 - sin²(tilt))`, but when `dy_x > COBSCALE` (tilt > ~57°), `sin² > 1`
and the sqrt argument becomes negative.

Traced values at frame 46828 for uid=16872:
- `dy_x` = 446740 (= 6.82 in COBSCALE units, meaning ~82° tilt)
- `POW(446740, 131072)` = `pow(6.82, 2) * 65536` = 3045297
- `65536 - 3045297` = -2979761 (negative!)
- `POW(-2979761, 32768)` = `pow(-45.47, 0.5) * 65536` = **NaN * 65536**
  - ARM64: `int(NaN)` = **0** → `ATAN(446740, 0)` = PI/2 = **16384 TAANG**
  - x86: `int(NaN)` = **INT_MIN** → `ATAN(446740, -2147483648)` ≈ PI = **32765 TAANG**

Result: triparent (piece 1) rotated to 90° on ARM64 vs ~180° on x86. This changes
the beam laser weapon muzzle position by ~10 elmos, causing the beam to hit different
targets, which cascades into a full desync by frame 46870.

### The Fix

```cpp
// Portable float-to-int that matches x86 CVTTSS2SI behavior on ARM64.
static inline int FloatToIntPortable(float v) {
#if defined(__aarch64__) || defined(__arm__)
    if (__builtin_expect(math::isnan(v) || v > 2147483520.0f || v < -2147483648.0f, 0))
        return INT_MIN;
#endif
    return static_cast<int>(v);
}
```

Applied to `POW`, `KSIN`, `KCOS`, `KTAN`, `SQRT` return values in `GetUnitVal()`.
The fix is ARM64-only and a no-op on x86.

### Verification

- Before fix: ARM64 checksum at frame 46870 = `dce7332e` (DESYNC, 5415 warnings)
- After fix: ARM64 checksum at frame 46870 = `207b06f5` (matches x86 and demo exactly)
- Full replay completed: 69002 frames, 0 desync warnings

### Broader Implications

Any `int(float)` cast in synced engine code where the float can be NaN or out-of-range
will diverge between ARM64 and x86. Candidates for audit:

1. All 30+ `return int(...)` in `GetUnitVal()` — most are safe (positions, percentages)
   but `XZ_HYPOT`, `HYPOT` could overflow for extreme inputs
2. Any `int(float)` in movement types, weapon code, or other synced paths
3. `FloatToIntPortable()` should be promoted to a shared utility in `System/SpringMath.h`
   for use across the codebase

---

## COB float-to-short Cast (INVESTIGATED — NOT THE CAUSE)

**Original fix**: `5cc3c0fc3e` (supaku's branch) — `short(heading * RAD2TAANG)` →
`static_cast<short>(static_cast<int>(...))`

This was investigated extensively on v3 and ruled out as a desync cause:

- **GCC -O2 UB behavior depends on context**: `short(50000.0f)` with local var
  saturates (32767) but via noinline/volatile wraps (-15536). In actual engine code
  with runtime values, ARM64 FCVTZS+SXTH wraps — same as the explicit two-step cast.
- **`FloatToShortWrap` logged zero out-of-range casts** in the entire Hellas Basin replay
- A saturation-based fix (`FloatToShortSaturate`) broke ALL replays (0→5799 desyncs)
- The wrapping approach (`FloatToShortWrap`) is a no-op on ARM64 for runtime values

**Not included in v3** because it changes x86_64 behavior at -O2 (GCC constant-folds
differently than runtime execution), breaking demo compatibility.

---

## Batch Run Results (v3 branch, 2025.06.19)

### ARM64 (results in `~/desync-test/results/20260311_100740/`)

| # | Replay | Frames | Result |
|---|--------|--------|--------|
| 1 | Archsimkats Valley V1 | 46983 | OK |
| 2 | All That Glitters v2.2.3 (06-27) | 63308 | OK |
| 3 | All That Glitters v2.2.3 (06-29) | 52959 | OK |
| 4 | Azurite Shores 1.0.2 | 43407 | OK |
| 5 | Sphagnum Bog v1.2.1 (06-34) | 21254 | OK |
| 6 | Supreme Isthmus v2.1 (06-42) | 26753 | OK |
| 7 | **Hellas Basin v1.4** | 69002 | **DESYNC at frame 46870** (before Fix #6) |
| 8 | Bismuth Valley v2.4.1 | 67194 | OK |
| 9 | All That Glitters v2.2.3 (06-50) | 40397 | OK |
| 10 | Sphagnum Bog v1.2.1 (06-54) | 45900 | OK |
| 11 | Supreme Isthmus v2.1 (06-54) | 5294 | OK |
| 12 | Supreme Isthmus v2.1 (07-02) | — | OK |
| 13+ | ... | — | Running |

Post Fix #6: Hellas Basin verified — 69002 frames, 0 desyncs, checksum matches x86/demo.

### x86_64 (results on ethanbaby)
- 23+ replays completed, **0 desyncs** on all including Hellas Basin
- x86_64 checksums match demo checksums exactly

### Cross-Architecture Comparison
- **12/13 replays tested pre-fix**: bit-identical checksums between ARM64 and x86_64
- **1/13 replays** (Hellas Basin): ARM64 diverged at frame 46870 — **FIXED by Fix #6**
- Full 70-replay batch with Fix #6 pending

---

## Results Timeline

| Date | Fix Applied | Result | Notes |
|------|-------------|--------|-------|
| 2026-03-05 | AnimInfo padding (Fix #1) | Desync at frame 542 | Old branch, macOS vs Linux |
| 2026-03-06 | + ClampRadPi (Fix #2) | 8,705+ frames match | Old branch |
| 2026-03-06 | + Headless atlas (Fix #5) | 40,591 frames match | Old branch, full replay |
| 2026-03-10 | v3 branch (platform patches only) | 12/13 replays OK | Linux ARM64 VM |
| 2026-03-11 | v3 batch run | Hellas Basin desync at 46870 | Only 1/26 replays affected |
| 2026-03-12 | + FloatToIntPortable (Fix #6) | Hellas Basin 69002 frames, 0 desyncs | Root cause: int(NaN) UB |
| 2026-03-12 | Full 70-replay batch with Fix #6 | **PENDING** | |

---

## Previously Ruled Out

The following were exhaustively investigated and confirmed NOT to cause desyncs:

- SmoothHeightMesh SSE intrinsics (identical hash on both platforms)
- Matrix44f multiply via sse2neon (10,000 self-checks, zero mismatches)
- FMA contraction (proven by SSE vs scalar self-check with `-ffp-contract=off`)
- streflop math functions (sin, cos, atan2, floor, fmod — all use portable C libm)
- `math::isqrt` (isqrt2_nosse) — same Newton-Raphson, `-ffp-contract=off`
- `math::sqrt` — IEEE 754 correctly rounded on both (FSQRT on ARM64, SQRTSS on x86)
- ARM64 self-consistency — PROVEN (21/21 checksums match between two ARM64 runs)
- Denormal handling — FZ=0, DAZ=0, FTZ=0 on both
- COB VM — pure integer stack machine, no float math (except GET operations)
- SSE2NEON rsqrt/rcp precision — NOT in sync code (only squish texture lib)
- `math::sinf`/`cosf`/`tanf` bypassing STREFLOP — confirmed they delegate to `streflop::sin`/`cos`/`tan` via `SMath.h`
- Lua FP operations — `lua_Number` is `float`, all math goes through STREFLOP
- `-ffp-contract=off` — applied globally to all compilation units including Lua

---

## FP Flag Verification Summary

All ARM64 FP determinism flags confirmed correct:

- `-ffp-contract=off` in ARM64-specific cmake block (all targets)
- `-frounding-math` and `-fsingle-precision-constant` set
- `STREFLOP_NEON` mode configured (hardware NEON FPU with FPCR control)
- `math::sin`/`math::cos` → `streflop::sin`/`streflop::cos` (via `using namespace streflop`)
- `math::sqrt` → `_mm_sqrt_ss` → `vsqrtq_f32` (IEEE 754 exact, matches)
- `math::isqrt` uses `isqrt2_nosse` (bit-manipulation, platform-independent)

---

## Pre-Test Checklist

Before running any cross-arch comparison test, **ALL** of the following must be true
on **BOTH** machines:

- [ ] **Same git commit** on both machines (`git log --oneline -1` matches)
- [ ] **All fixes applied**: headless atlas fix, FloatToIntPortable
- [ ] **Rebuilt** after the latest commit (`make spring-headless` completed cleanly)
- [ ] **Same game content**: identical SDP hash, pool files, and map archives
- [ ] **Same replay file** (verify with `md5sum`)
- [ ] **`springsettings.cfg`** has `DisableDemoVersionCheck = 1` on both machines
- [ ] **Previous sync output cleared**: delete old checksum files before each run

### Workflow for each code change

```
1. Make changes, commit locally
2. git push mpcooke3 arm64-replay-testing-v3
3. ssh matt@ethanbaby "cd ~/RecoilEngine && git pull mpcooke3 arm64-replay-testing-v3"
4. Verify: same commit hash on both machines
5. Rebuild on BOTH machines
6. Run replay on both, compare checksums
```
