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

## Fix #2: std::sort in Animation Tick (PENDING)

**File**: `rts/Sim/Units/Scripts/UnitScript.cpp:203`
**Impact**: Likely cause of desync at frame 542

### Root Cause

```cpp
std::sort(anims.begin(), anims.end(), [](const auto& lhs, const auto& rhs) {
    return std::tie(lhs.piece, lhs.animType, lhs.axis) < std::tie(rhs.piece, rhs.animType, rhs.axis);
});
```

`std::sort` is **not stable** — elements that compare equal may be reordered
differently by different standard library implementations. Apple libc++ and GNU
libstdc++ use different sort algorithms (introsort variants with different
partitioning strategies).

While `FindAnim` enforces that `(piece, animType, axis)` tuples are unique for
**live** animations, the `doneAnims.emplace_back(ai)` at line 217 copies done
animations before they are erased at line 224. If multiple animations finish on the
same frame, the sort could encounter duplicate keys in transient states, or the sort
itself is simply implemented differently even for unique keys (equal partitioning
choices differ).

Spring Engine has fixed this **three times** in other synced code:
- `0cb670361f` — "switch to std::stable_sort in the hope it syncs"
- `98a5a80ca8` — "sort can be implemented differently on different compilers"
- `32c79bdc60` — "s/sort/stable_sort (JIC)"

### Fix

```diff
-std::sort(anims.begin(), anims.end(), [](const auto& lhs, const auto& rhs) {
+std::stable_sort(anims.begin(), anims.end(), [](const auto& lhs, const auto& rhs) {
```

---

## Fix #3: fastmath::floor Divergence (ALREADY HANDLED)

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
| TBD | + std::stable_sort | TBD | Next fix to test |

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

Phase 1 fixes (already in upstream, not cherry-picked):
1. `short()` UB in CobInstance — upstream commit `ad716fe7` (ClampRadPi) makes it safe
2. ClampRad heading clamping — upstream PR #2827 already applied

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
