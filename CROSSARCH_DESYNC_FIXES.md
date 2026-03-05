# Cross-Architecture Desync Fixes (ARM64 vs x86_64)

## Overview

When running BAR replays on ARM64 (Apple Silicon macOS) and x86_64 (Linux), per-frame
sync checksums diverge despite all streflop FP flags being correctly applied. This
document tracks the root causes found and the fixes applied or pending.

Branch: `arm64-upstream-061919` (based on upstream tag `2025.06.19`)

---

## Test Environment

### Machines

| Machine | Arch | OS | Compiler |
|---------|------|----|----------|
| M2 MacBook Air | ARM64 | macOS 15.5 (Darwin 24.6.0) | Apple clang 17 (libc++) |
| ethanbaby | x86_64 | Linux (Ubuntu) | GCC (libstdc++) |

### Engine

- Version: `2025.06.19-7-g2f674c6` (7 commits on top of upstream `2025.06.19` tag)
- Build: `cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_STREFLOP=TRUE -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DNO_SOUND=ON -Wno-dev`
- Flags verified: `-fno-fast-math`, `-ffp-contract=off`, `STREFLOP_ARM_NATIVE`, `SSE2NEON_PRECISE_*`

### Game Content

- Game: `Beyond All Reason test-29565-02e94b8`
- SDP hash: `5853981e73e28a167b45d5ca302e828e`
- Package file: `packages/5853981e73e28a167b45d5ca302e828e.sdp`
- Pool files: ~19,081 files in `pool/`
- Maps: 33 `.sd7` files in `maps/`

### Replays

- 70 team replays in `demos-batch3/`, all from engine version `2025.06.19`
- Downloaded from BAR API: `https://api.bar-rts.com/replays?hasBots=false&endedNormally=true&durationMs=600000..3600000&engineVersion=2025.06.19&limit=20&preset=team`
- Key test replay: `2026-03-04_10-50-45-662_Entrenched_Plains_V2_2025.06.19.sdfz` (29,689 frames)

### Data Directory Layout

Both machines use `/tmp/desync-test/` with `--write-dir` and `--isolation-dir`:
```
/tmp/desync-test/
  springsettings.cfg      # Must have DisableDemoVersionCheck = 1
  base/                   # springcontent.sdz, maphelper.sdz, cursors.sdz
  packages/               # .sdp files
  pool/                   # XX/YYYYYY.gz pool files
  maps/                   # .sd7 map archives
  demos/                  # replay .sdfz files (copied here for each test)
  rapid/repos-cdn.beyondallreason.dev/byar/versions.gz
```

### Checksum Output

Per-frame checksums are written to `/tmp/sync_checksums.txt` by the engine
(`rts/Net/NetCommands.cpp`). Format: `<frame> FrameEnd <checksum_hex>`.

Compare with `diff` after both machines complete a replay.

### Test Script

`run_sync_tests.sh` in the repo root orchestrates parallel replay runs on both
machines, copies demos via SCP, collects checksums, and generates a report.

---

## Commits on Top of Upstream `2025.06.19`

| Commit | Description |
|--------|-------------|
| `1f888807f8` | Add `-fno-fast-math` to compiler flags |
| `12d602353e` | Add ARM64 support to streflop (`STREFLOP_ARM_NATIVE`, FPCR control) |
| `45df265567` | Fix third-party libs for ARM64 (sse2neon.h, assimp, smmalloc) |
| `8385c1ad78` | macOS/ARM64 platform support (CpuTopology, Threading, cmake) |
| `679bafcc59` | Math determinism: portable floor, SSE2NEON precision flags |
| `f9166a3445` | Per-frame sync checksum file output for cross-arch testing |
| `2f674c6b3f` | **FIX #1**: Zero-initialize AnimInfo struct padding |

---

## Fix #1: AnimInfo Struct Padding (APPLIED)

**Commit**: `2f674c6b3f`
**File**: `rts/Sim/Units/Scripts/UnitScript.h`
**Impact**: Pushed desync from frame 94 to frame 542

### Root Cause

The `AnimInfo` struct (26 bytes of data) is padded to 28 bytes by the compiler.
Upstream commit `fb525606af` ("Include animation data into global synced checksum")
added checksumming of animation data via `spring::LiteHash(ai, checksum)` which
hashes **all 28 bytes** including the 2 uninitialized padding bytes after the `bool`
members.

```
Offset  Member         Size  Initialized?
0-3     animType       4     YES
4-7     axis           4     YES
8-11    piece          4     YES
12-15   speed          4     YES
16-19   dest           4     YES
20-23   accel          4     YES
24      done           1     YES
25      hasWaiting     1     YES
26-27   [PADDING]      2     NO  <-- causes desync
```

Different heap allocators (Apple libc on ARM64 vs glibc on x86_64) leave different
garbage in the padding bytes, causing different hashes on every frame with active
animations.

### Fix

Added a constructor that `memset`s the struct to zero before setting non-zero defaults:

```cpp
AnimInfo() {
    std::memset(this, 0, sizeof(AnimInfo));
    animType = ANone;  // -1
    axis = -1;
    piece = -1;
}
```

### Verification

- Before fix: desync at frame 94 (all 12 completed replays)
- After fix: frames 0-541 match perfectly, desync at frame 542

---

## Fix #2: ClampRadPi for Heading Angles (APPLIED — necessary but NOT sufficient)

**Upstream commit**: `ad716fe785` ("Do proper modulo for heading (#2827)")
**Cherry-picked as**: `ef8647af3f`
**Impact**: Fixes UB in `short()` cast and is correct practice, but does NOT fix frame 542 desync

**THIS FIX WAS PRESENT IN PHASE 1 BUT WAS LOST DURING REBASE TO `2025.06.19`.**
The upstream version (`ad716fe785`) was merged on 2026-03-02, AFTER the
`2025.06.19` tag. It must be cherry-picked onto any branch based on that tag.

**IMPORTANT**: Testing confirmed this fix alone does NOT resolve the frame 542
desync. The raw `dest` values reaching `CUnitScript::Turn()` are unchanged —
the ±2π difference originates inside the Lua game script's angle computation,
not from the AimWeapon heading parameter. See Fix #5 investigation below.

### Root Cause

The engine passes weapon aim headings to Lua scripts via `ClampRad()`, which
normalizes to [0, 2π). Headings should be in [-π, π) because:

1. `GetHeadingFromVectorF()` returns values in [-π, π]
2. Subtracting the unit's own heading can produce values outside [0, 2π)
3. `ClampRad(x)` vs `ClampRad(x ± 2π)` can differ by 1 ULP due to FP rounding
4. This 1-ULP difference propagates into `AnimInfo.dest` and breaks the sync checksum

### Evidence

Turn call logging for uid=15846 (corcom nanolathe, piece=18 axis=1) at frame 542:

```
ARM64:  dest= 5.511497021 (0x40b05e2f) → ClampRad = 5.511497021 (0x40b05e2f)
x86_64: dest=-0.7716882229 (0xbf458d5c) → ClampRad = 5.511497498 (0x40b05e30)  ← 1 ULP off!
```

The raw destination angles differ by exactly 2π (same logical angle). All other
33 Turn calls for this unit are bit-identical — only the nanolathe aim angle diverges.

### Fix — 4 call sites changed from `ClampRad` → `ClampRadPi`

```
rts/Sim/Weapons/Weapon.cpp:416       — AimWeapon heading
rts/Sim/Units/Unit.cpp:2408          — WindChanged heading
rts/Sim/Units/UnitTypes/Builder.cpp  — StartBuilding heading
rts/Lua/LuaSyncedRead.cpp            — GetUnitHeading (radian mode)
```

Plus new function `ClampRadPi()` in `rts/System/SpringMath.inl` and declaration
in `rts/System/SpringMath.h`.

`ClampRadPi` first wraps to [0, 2π) then shifts to [-π, π) with an `if`
branch rather than a second `floor` call, avoiding the FP rounding ambiguity
at the 0/2π boundary.

### How to Apply

```bash
git cherry-pick ad716fe785
# Resolve conflict in Weapon.cpp: keep our formula but change ClampRad → ClampRadPi:
#   ClampRadPi(heading - owner->heading * TAANG2RAD)
```

---

## Fix #3: std::stable_sort in Animation Tick (APPLIED — no effect)

**File**: `rts/Sim/Units/Scripts/UnitScript.cpp:203`
**Impact**: None — sort keys are unique, so sort order is deterministic

Changed `std::sort` → `std::stable_sort` as a precaution. Testing confirmed
this had no effect on the desync (frame 542 still diverged). The root cause
was Fix #2 (ClampRadPi), not sort ordering.

---

## Fix #4: fastmath::floor Divergence (ALREADY HANDLED)

**File**: `rts/System/FastMath.h:212-224`

### Background

The original `fastmath::floor` uses `static_cast<int>(f)` which has different
overflow behavior on ARM64 (FCVTZS saturates to INT_MAX/MIN) vs x86_64 (CVTTSS2SI
returns 0x80000000). This was fixed in commit `679bafcc59`:

```cpp
template<typename T>
inline T floor(T f)
{
#if defined(__aarch64__) || defined(__arm64__)
    return streflop::floor(f);  // ARM64: use streflop for determinism
#else
    T truncX = static_cast<T>(static_cast<int>(f));
    return truncX - static_cast<T>(truncX > f);
#endif
}
```

For normal game values (within int range), both paths produce identical results.
The ARM64 path handles edge cases (overflow, NaN) via streflop's portable libm.

---

## Other Potential Issues (Lower Priority)

### Integer Division in tickRate

**File**: `rts/Sim/Units/Scripts/UnitScript.cpp:210`

```cpp
const int tickRate = 1000 / deltaTime;
```

`deltaTime` is an `int` (typically 33 for 30fps). This is integer division on both
platforms and should produce identical results. Not a desync risk unless `deltaTime`
varies (which it shouldn't in synced sim).

### Sign Function at Zero

**File**: `rts/System/SpringMath.h:183`

```cpp
template<class T> constexpr T Sign(const T v) { return ((v > T(0)) * T(2) - T(1)); }
```

Returns -1 for both `0.0f` and `-0.0f`. If one architecture produces `0.0f` where
the other produces `-0.0f`, `Sign()` returns the same value. But `0.0 + 0.0f` (the
identity-preserving add in `ClampRad`) converts `-0.0f` to `+0.0f`, which should
prevent sign differences from propagating. Low risk.

### float-to-int Saturation

ARM64 `FCVTZS` saturates to `INT_MAX`/`INT_MIN` on overflow; x86 `CVTTSS2SI`
returns `0x80000000`. Already mitigated in `fastmath::floor` (see Fix #3). Should
audit other `static_cast<int>(float)` in synced code for safety.

---

## Pre-Test Checklist (CRITICAL)

Before running any cross-arch comparison test, **ALL** of the following must be true
on **BOTH** machines. Failure to verify any item risks wasting hours rediscovering
known bugs.

- [ ] **Same git commit** on both machines (`git log --oneline -1` matches)
- [ ] **All fixes applied**: AnimInfo padding, ClampRadPi cherry-pick (`ad716fe785`),
      fastmath::floor ARM64 path — check with `git log --oneline | head -10`
- [ ] **Rebuilt** after the latest commit (`make engine-headless` completed cleanly)
- [ ] **Same game content**: identical SDP hash, pool files, and map archives
- [ ] **Same replay file** (copy via SCP, verify with `md5sum`/`md5`)
- [ ] **`springsettings.cfg`** has `DisableDemoVersionCheck = 1` on both machines
- [ ] **Previous sync output cleared**: `rm -f /tmp/sync_checksums.txt` before each run

### Workflow for each code change

```
1. Make changes, commit locally
2. git push origin <branch>
3. ssh matt@ethanbaby "cd /home/matt/RecoilEngine && git pull origin <branch>"
4. Verify: ssh matt@ethanbaby "git -C /home/matt/RecoilEngine log --oneline -1"
   → must show same commit hash as local
5. Rebuild on BOTH machines (make engine-headless)
6. Run replay on both, compare /tmp/sync_checksums.txt
```

Skipping any step (especially push/pull/rebuild) means you may be comparing
different code and will rediscover bugs that are already fixed.

---

## How to Reproduce

### 1. Build on ARM64 Mac

```bash
cd RecoilEngine
git checkout arm64-upstream-061919
mkdir -p build-desync-test && cd build-desync-test
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_STREFLOP=TRUE \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DNO_SOUND=ON -Wno-dev
make engine-headless -j10
```

### 2. Build on x86_64 Linux

```bash
ssh matt@ethanbaby
cd /home/matt/RecoilEngine
git fetch origin && git checkout arm64-upstream-061919 && git pull
cd build-headless
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_STREFLOP=TRUE
make engine-headless -j$(nproc)
```

### 3. Set Up Game Content (both machines)

Download `byar:test` game content (SDP + pool files) and maps to `/tmp/desync-test/`.
The `run_sync_tests.sh` script handles map downloads automatically for replays it
processes. For manual setup:

```bash
# springsettings.cfg must contain:
DisableDemoVersionCheck = 1
```

### 4. Run a Single Replay Test

```bash
# ARM64:
rm -f /tmp/sync_checksums.txt
cd /tmp/desync-test && ./spring-headless \
  --write-dir /tmp/desync-test --isolation-dir /tmp/desync-test \
  demos/REPLAY.sdfz
cp /tmp/sync_checksums.txt /tmp/arm64_checksums.txt

# x86_64:
rm -f /tmp/sync_checksums.txt
cd /tmp/desync-test && ./spring-headless \
  --write-dir /tmp/desync-test --isolation-dir /tmp/desync-test \
  demos/REPLAY.sdfz
cp /tmp/sync_checksums.txt /tmp/x86_checksums.txt

# Compare:
diff /tmp/arm64_checksums.txt /tmp/x86_checksums.txt | head -5
```

### 5. Run Full Test Suite

```bash
./run_sync_tests.sh --demos /tmp/desync-test/demos-batch3 2>&1 | tee test_run.log
```

---

## Results Timeline

| Date | Fix Applied | First Desync Frame | Notes |
|------|------------ |-------------------|-------|
| 2026-03-04 | None (baseline) | Frame 94 | 12/12 replays desync |
| 2026-03-05 | AnimInfo padding | Frame 542 | Frames 0-541 match |
| 2026-03-05 | + std::stable_sort | Frame 542 | No change (sort keys are unique) |
| 2026-03-05 | + subsystem logging | Frame 542 | **Scripts** subsystem diverges (not UnitHandler/Path/Projectiles) |
| 2026-03-05 | + per-unit anim logging | Frame 542 | uid=15846 anim[32] (ATurn p=18 a=1) `dest` differs |
| 2026-03-05 | + Turn() call logging | Frame 542 | **ROOT CAUSE IDENTIFIED**: Lua script produces dest ±2π, ClampRad rounds differently |
| 2026-03-05 | + ClampRadPi cherry-pick (ad716fe785) | Frame 542 | **NO EFFECT** — raw Turn() dest values unchanged; ±2π originates inside Lua script, not from AimWeapon heading |
| 2026-03-05 | TAANG reverse-engineering | Frame 542 | dest values = exact TAANG×TAANG2RAD multiples → **COB script**, TAANG ints differ (57487 vs -8049 = same angle ± 65536) |
| 2026-03-05 | AimWeapon heading logging | *pending* | Log at Weapon.cpp + CCobInstance::AimWeapon to trace where ±2π enters |

---

## Investigation Log: ClampRad ±2π Root Cause Analysis

This section documents the full investigation that led to Fix #2. Kept for
reference — the fix is documented above in Fix #2.

<details>
<summary>Click to expand investigation details</summary>

### Per-unit animation logging

uid=15846 (corcom), anim[32] (ATurn piece=18 axis=1) — first divergent animation.

```
ARM64: dest=5.511497021 (0x40b05e2f)  — raw from Lua: 5.511497021
x86_64: dest=5.511497498 (0x40b05e30) — raw from Lua: -0.7716882229
```

The Lua script passes the same logical angle but offset by 2π.
`ClampRad(x)` vs `ClampRad(x + 2π)` differ by 1 ULP due to FP rounding.

### Turn call log evidence

```
# ARM64:
f=541 uid=12742 Turn(p=18,a=1) dest=4.780555248 (0x4098fa4f) clamped=4.780555248 (0x4098fa4f)
f=542 uid=15846 Turn(p=18,a=1) dest=5.511497021 (0x40b05e2f) clamped=5.511497021 (0x40b05e2f)

# x86_64:
f=541 uid=12742 Turn(p=18,a=1) dest=-1.502630115 (0xbfc0562f) clamped=4.780555248 (0x4098fa4f)
f=542 uid=15846 Turn(p=18,a=1) dest=-0.7716882229 (0xbf458d5c) clamped=5.511497498 (0x40b05e30)
```

uid=12742 at f=541 also shows ±2π split but ClampRad happens to round identically.
uid=15846 at f=542 is first case where ClampRad produces a 1-ULP difference.

### Key confirmations

- Lua math correctly uses streflop (`lmathlib.cpp` includes `streflop_cond.h`)
- `LUA_NUMBER = float` (not double) — `luaconf.h:514`
- Per-subsystem checksums: only Scripts diverges (not UnitHandler/Path/Projectiles)
- `GetPieceRotation` NOT called — Lua script doesn't read piece rotations
- Only piece=18 axis=1 (nanolathe y-rotation) differs out of 34 Turn calls
- Angle likely from `math.atan2()` in AimWeapon for nanolathe targeting

### Analysis chain

1. Engine calls `script:AimWeapon()` with heading from `ClampRad(heading - owner->heading * TAANG2RAD)`
2. ClampRad normalizes to [0, 2π) — but heading subtraction can produce values
   outside that range on one platform vs the other
3. Lua script calls `Spring.UnitScript.Turn(piece, axis, dest, speed)`
4. `CUnitScript::Turn()` calls `ClampRad(destination)` which normalizes to [0, 2π)
5. `ClampRad(x)` vs `ClampRad(x + 2π)` differ by 1 ULP
6. The 1-ULP difference in `AnimInfo.dest` causes `spring::LiteHash` to diverge

**Fix attempt**: Changed 4 heading-to-Lua sites from `ClampRad` → `ClampRadPi` (Fix #2).
**Result**: NO EFFECT on the Turn() dest values — the ±2π originates inside the Lua
script computation, not from the AimWeapon heading. See Fix #5 investigation.

</details>

---

## Fix #5: ClampRad in CUnitScript::Turn() (INVESTIGATING)

**File**: `rts/Sim/Units/Scripts/UnitScript.cpp:519`
**Impact**: Actual root cause of frame 542 desync (Fix #2 is necessary but insufficient)

### Root Cause

The Lua game script (BAR corcom AimWeapon handler) computes a Turn destination
angle for the nanolathe that lands on opposite sides of the 0/2π boundary on
ARM64 vs x86_64 — differing by exactly 2π. `CUnitScript::Turn()` calls
`ClampRad(destination)` which normalizes to [0, 2π), but:

```
ClampRad(5.511497021)   = 5.511497021 (no-op, already in range)
ClampRad(-0.7716882229) = 5.511497498 (= -0.7716882229 + 2π, differs by 1 ULP)
```

This is because `x ≠ (x - 2π) + 2π` in float arithmetic.

### Why ClampRadPi (Fix #2) didn't help

Turn call logs with ClampRadPi applied — dest values UNCHANGED from pre-fix:
```
ARM64:  f=542 uid=15846 p=18,a=1 dest= 5.511497021 (0x40b05e2f) → clamped=5.511497021 (0x40b05e2f)
x86_64: f=542 uid=15846 p=18,a=1 dest=-0.7716882229 (0xbf458d5c) → clamped=5.511497498 (0x40b05e30)
```

### TAANG reverse-engineering (key finding)

The dest values correspond exactly to TAANG integer multiples:
```
ARM64:  5.511497021  = 57487 * TAANG2RAD   (TAANG2RAD = π/32768)
x86_64: -0.7716882229 = -8049 * TAANG2RAD
```

57487 - 65536 = -8049 → same angle, different TAANG integers.

This means corcom uses a **COB script** (not LUS). The data flow is:
1. `Weapon.cpp:416` — `ClampRadPi(heading - owner->heading * TAANG2RAD)` → float in [-π, π)
2. `CCobInstance::AimWeapon` — `callinArgs[1] = short(heading * RAD2TAANG)` → TAANG int
3. COB VM — `turn aimy1 to y-axis heading speed <300.0>` (BOS script passes heading directly)
4. `CCobInstance::Turn` — `CUnitScript::Turn(piece, axis, speed * TAANG2RAD, destination * TAANG2RAD)`
5. `CUnitScript::Turn` — `ClampRad(destination)` → stored in AnimInfo.dest

The TAANG values 57487 and -8049 prove the heading passed to step 2 is
DIFFERENT on the two platforms:
- ARM64: heading ≈ 5.5115 rad (in [0, 2π), NOT [-π, π)!) → TAANG = 57487
- x86_64: heading ≈ -0.7717 rad (in [-π, π)) → TAANG = -8049

If ClampRadPi were truly applied, heading would be in [-π, π) on both platforms,
heading * RAD2TAANG would be in [-32768, 32768), and `short()` would be well-defined
and identical. The fact that ARM64 has TAANG=57487 (> 32768) means either:

**Hypothesis A**: ClampRadPi is NOT being reached for this unit's AimWeapon call
(different code path, or the AimWeapon isn't called at frame 542 — a cached heading
from before the fix is being reused by the COB script).

**Hypothesis B**: The heading value at `Weapon.cpp:410` (`GetHeadingFromVectorF`)
differs between platforms, producing 5.5115 on ARM64 and -0.7717 on x86 even
BEFORE the ClampRadPi is applied. This would mean `ClampRadPi(5.5115) = -0.7717`
on ARM64, but the COB receives 57487 TAANG, which contradicts ClampRadPi working.

**Hypothesis C**: The COB AimWeapon call at frame 542 for this unit is actually
using a heading from a PREVIOUS frame (COB threads are asynchronous — a thread
from a prior AimWeapon call may still be running). The heading from that prior
call was computed WITHOUT ClampRadPi (because it was an earlier build, or because
the COB thread was queued before the current frame).

### Test: AimWeapon heading logging (pending)

To determine which hypothesis is correct, add logging at TWO points:

1. **`Weapon.cpp:416`** — log `heading`, `owner->heading`, raw subtraction,
   and ClampRadPi result for uid=15846, frames 540-543
   → Output: `/tmp/sync_aimweapon.txt`

2. **`CCobInstance::AimWeapon`** — log the heading float, TAANG float,
   and short TAANG value for uid=15846, frames 540-543
   → Output: `/tmp/sync_cob_aim.txt`

Expected outcomes:
- If `sync_aimweapon.txt` shows different raw heading values → synced state
  diverged before this point (deeper issue)
- If `sync_aimweapon.txt` shows same ClampRadPi'd heading on both but
  `sync_cob_aim.txt` shows different TAANG → ClampRadPi implementation bug
- If `sync_aimweapon.txt` is EMPTY on frame 542 → the AimWeapon isn't called
  at frame 542 (COB thread reuse), and the heading comes from a prior frame
- If `sync_cob_aim.txt` shows heading > π → ClampRadPi is not being applied
  (code path issue)

### Constraint

Cannot simply change `ClampRad` → `ClampRadPi` in Turn() because
`TurnToward()` (line 139) asserts `dest < math::TWOPI` and `cur < math::TWOPI`.
The animation system expects angles in [0, 2π).

### Possible Fixes (once root cause confirmed)

1. **Normalize at Turn() entry**: Apply ClampRadPi first, then shift to [0, 2π):
   ```cpp
   destination = ClampRadPi(destination);  // [-π, π) — bit-exact for ±2π
   if (destination < 0.0f)
       destination += math::TWOPI;         // shift to [0, 2π)
   ```
   This avoids the `floor(x/2π)*2π` subtraction that causes rounding error.

2. **Trace the exact divergence source** and fix upstream of Turn()

---

## Previously Ruled Out (Phase 1)

The following were exhaustively investigated in Phase 1 on the old branch
(`arm64-desync-test`, based on 2025.04.08) and confirmed NOT to cause desyncs.
See [DESYNC_DEBUG.md](DESYNC_DEBUG.md) for full details.

- SmoothHeightMesh SSE intrinsics (identical hash on both platforms)
- Matrix44f multiply via sse2neon (10,000 self-checks, zero mismatches)
- FMA contraction (proven by SSE vs scalar self-check with `-ffp-contract=off`)
- streflop math functions (sin, cos, atan2, floor, fmod — all use portable C libm)
- `math::isqrt` (isqrt2_nosse) — same Newton-Raphson, `-ffp-contract=off`
- `math::sqrt` — IEEE 754 correctly rounded on both (FSQRT on ARM64, SQRTSS on x86)
- ARM64 self-consistency — PROVEN (21/21 checksums match between two ARM64 runs)
- Denormal handling — FZ=0, DAZ=0, FTZ=0 on both
- COB VM — pure integer stack machine, no float math

Phase 1 fixes (now included):
1. `short()` UB in CobInstance — upstream commit `ad716fe7` (ClampRadPi) makes it safe
2. ClampRad → ClampRadPi heading clamping — upstream PR #2827, cherry-picked as Fix #2 above

---

## FP Flag Verification Summary

All ARM64 FP determinism flags confirmed correct (verified 2026-03-05):

- `-fno-fast-math` in root CMakeLists.txt
- `-ffp-contract=off` in ARM64-specific block
- `STREFLOP_ARM_NATIVE` defined, FPCR set to round-to-nearest
- `SSE2NEON_PRECISE_MINMAX`, `SSE2NEON_PRECISE_DIV`, `SSE2NEON_PRECISE_SQRT` defined
- `math::sin`/`math::cos` → `streflop::sin`/`streflop::cos` (via `using namespace streflop`)
- `math::sqrt` → `fastmath::sqrt_sse` → `__builtin_sqrtf` on ARM64 (IEEE-754 FSQRT)
- 53 FMA instructions found in binary — all in unsynced rendering code (verified via objdump)
