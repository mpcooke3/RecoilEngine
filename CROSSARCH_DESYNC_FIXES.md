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

- Version: `2025.06.19-19-gdafd37c` (19 commits on top of upstream `2025.06.19` tag)
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

### Test Scripts

**`run_replay_batch.sh`** — Standalone batch runner (preferred for overnight runs).
Runs all replays in a directory independently on a single machine. Start on both
machines separately; compare results afterwards. Survives SSH disconnections and
laptop sleep since each machine runs autonomously.

```bash
# On ARM64 Mac:
nohup ./run_replay_batch.sh ./build-desync-test/spring-headless \
  /tmp/desync-test /tmp/desync-test/demos-batch3 > /tmp/desync-test/batch_run.log 2>&1 &

# On x86_64 Linux (via SSH, then disconnect freely):
nohup /tmp/desync-test/run_replay_batch.sh \
  /home/matt/RecoilEngine/build-headless/spring-headless \
  /tmp/desync-test /tmp/desync-test/demos-batch3 > /tmp/desync-test/batch_run.log 2>&1 &
```

Results are saved as `<results-dir>/<replay-name>.checksums` (one per replay).
Compare after both machines finish by SCPing x86_64 results to the Mac.

**`run_sync_tests.sh`** — Orchestrated runner (requires Mac to stay awake).
Manages both machines over SSH, copies demos via SCP, and generates a comparison
report automatically. Simpler but fragile — SSH drops if the Mac sleeps or
Tailscale auth expires.

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
| `ef8647af3f` | **FIX #2**: ClampRadPi cherry-pick (upstream `ad716fe785`, PR #2827) |
| `dafd37c501` | **FIX #5**: Fix headless atlas rendering infinite loop |

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

## Fix #2: ClampRadPi for Heading Angles (APPLIED — CONFIRMED EFFECTIVE)

**Upstream commit**: `ad716fe785` ("Do proper modulo for heading (#2827)")
**Cherry-picked as**: `ef8647af3f`
**Impact**: Fixes the frame 542 desync. 8,705 frames verified identical across platforms.

**THIS FIX WAS PRESENT IN PHASE 1 BUT WAS LOST DURING REBASE TO `2025.06.19`.**
The upstream version (`ad716fe785`) was merged on 2026-03-02, AFTER the
`2025.06.19` tag. It must be cherry-picked onto any branch based on that tag.

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

## Fix #5: Headless Atlas Rendering Infinite Loop (APPLIED)

**Commit**: `dafd37c501`
**File**: `rts/Rendering/IconHandler.cpp`
**Impact**: Fixes headless replays dying early from LuaRAM exhaustion

### Root Cause

In headless mode, `CIconHandler::Update()` attempts to render icon atlas textures
every frame. `CTextureRenderAtlas::CreateAtlasTexture()` requires FBO/GL operations
which are stubs in headless builds — FBO creation succeeds (`FBO::IsReady()` returns
true because `globalRendering->active` is true) but the FBO is never valid, so
`atlasRendered` stays false. The `atlasNeedsUpdate` flag is never cleared, causing
the atlas creation to retry every frame indefinitely.

This produces ~300,000 log lines of atlas spam per replay, consumes CPU, and
eventually triggers "Emergency garbage collection due to exceeding 1.2GB LuaRAM"
which kills the process — typically after only a few thousand frames instead of
the full 30,000+.

### Fix

Skip `CIconHandler::Update()` atlas processing entirely in headless builds:

```cpp
void CIconHandler::Update()
{
    if (atlasNeedsUpdate.none())
        return;

#ifdef HEADLESS
    atlasNeedsUpdate.reset();
    return;
#endif
    // ... normal atlas rendering code
}
```

An earlier attempt set `atlasRendered = true` in `CreateAtlasTexture()`, but this
caused a segfault when `DisownTexture()` dereferenced the never-created atlas
texture pointer.

### Verification

- Before fix: replays die at ~8,000 frames with LuaRAM crash, infolog ~300K lines
- After fix: replays run to completion (~30,000-60,000 frames), infolog ~3K lines

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

### 5. Run Full Test Suite (Independent Batch Mode)

```bash
# 1. Copy demos and batch script to x86_64:
scp demos-batch3/*.sdfz matt@ethanbaby:/tmp/desync-test/demos-batch3/
scp run_replay_batch.sh matt@ethanbaby:/tmp/desync-test/

# 2. Start x86_64 (survives SSH disconnect):
ssh matt@ethanbaby "nohup /tmp/desync-test/run_replay_batch.sh \
  /home/matt/RecoilEngine/build-headless/spring-headless \
  /tmp/desync-test /tmp/desync-test/demos-batch3 \
  > /tmp/desync-test/batch_run.log 2>&1 &"

# 3. Start ARM64 locally:
nohup ./run_replay_batch.sh ./build-desync-test/spring-headless \
  /tmp/desync-test /tmp/desync-test/demos-batch3 \
  > /tmp/desync-test/batch_run.log 2>&1 &

# 4. Monitor progress:
tail -5 /tmp/desync-test/batch_run.log                          # ARM64
ssh matt@ethanbaby "tail -5 /tmp/desync-test/batch_run.log"     # x86_64

# 5. After both finish, compare results:
scp -r matt@ethanbaby:/tmp/desync-test/results/<timestamp>/ /tmp/x86_results/
# Then compare matching .checksums files (see comparison script below)
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
| 2026-03-05 | + ClampRadPi cherry-pick (ad716fe785) | Frame 542 | First test showed NO EFFECT (old binary on one machine — see lesson learned below) |
| 2026-03-05 | TAANG reverse-engineering | Frame 542 | dest values = exact TAANG×TAANG2RAD multiples → **COB script**, TAANG ints differ (57487 vs -8049 = same angle ± 65536) |
| 2026-03-05 | Comprehensive Turn path logging | — | Added logging at all COB/Builder/AimWeapon Turn entry points |
| 2026-03-06 | ClampRadPi CONFIRMED EFFECTIVE | **8,705+ frames MATCH** | Desync at frame 542 FIXED. Turn came via StartBuilding (not AimWeapon). Both platforms: dest_taang=-8048, dest_rad=-0.7715923786 (0xbf458714), clamped=5.511592865 (0x40b05ef8) — bit-for-bit identical. x86_64 replay ended early (frame 8705) due to unrelated LuaRAM/atlas rendering issue. |
| 2026-03-06 | + Headless atlas fix | **40,591 frames MATCH** | Full replay "All That Simmers" — all frames identical. Atlas fix allows replays to run to completion. |
| 2026-03-06 | 70-replay batch run | **IN PROGRESS** | Both machines running independently (`run_replay_batch.sh`). Commit `dafd37c501`. ARM64 results: `/private/tmp/desync-test/results/20260306_214824/`. x86_64 results: `/tmp/desync-test/results/20260306_214713/`. |

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

## Resolved Investigation: Frame 542 Desync Root Cause

The earlier "Fix #5" investigation was based on a **false negative** — the first
ClampRadPi test appeared to have no effect because the old binary was still running
on x86_64 (the pre-test checklist was not followed rigorously). Once both machines
were verified running the same commit (`535eb2ea75`) with comprehensive logging,
the results confirmed ClampRadPi (Fix #2) fully resolves the issue.

### What the comprehensive logging revealed

The Turn at frame 542 for uid=15846 came through **StartBuilding** (not AimWeapon):
- `sync_aimweapon.txt` — NOT CREATED on either platform (AimWeapon not called at f=542)
- `sync_startbuilding.txt` — present on both, values bit-for-bit identical

```
# Both ARM64 and x86_64 (identical):
COB::Turn p=18 a=1 dest_taang=-8048 speed_taang=54600 dest_rad=-0.7715923786
Turn(p=18,a=1) dest=-0.7715923786 (0xbf458714) clamped=5.511592865 (0x40b05ef8)
StartBuilding heading=-0.4226865768 (0xbed86a60) hTaangF=-4408.78 hTaangS=-4408
```

### Key observations

1. The Turn came via `CBuilder::ScriptStartBuilding` → `CCobInstance::StartBuilding`,
   NOT via AimWeapon. This is why AimWeapon logging was empty.
2. With ClampRadPi applied at `Builder.cpp` (`ClampRadPi(h - heading * TAANG2RAD)`),
   the heading is properly normalized to [-π, π) before TAANG conversion.
3. The `short()` cast is safe: heading ∈ [-π, π) → TAANG ∈ [-32768, 32768).
4. Both platforms produce identical TAANG integers (-8048), identical float
   destinations (-0.7715923786, hex 0xbf458714), and identical clamped values
   (5.511592865, hex 0x40b05ef8).

### Lesson learned

The first test appeared to show "no effect" because the Pre-Test Checklist was
not followed — the x86_64 machine was running an old binary. The checklist
(verify same commit, rebuild, copy binary) is essential for every test run

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
