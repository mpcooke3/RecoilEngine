# ARM64 vs x86_64 Cross-Architecture Desync Debug Log

## The Problem
ARM64 (Apple Silicon) headless build desyncs when replaying an x86_64 demo. All 4 original x86_64 players had identical checksums.

## Machines
- **ARM64 Mac (local)**: `/Users/matthewcooke/CLionProjects/RecoilEngine` (build dir: `build-desync-test`)
- **x86_64 Linux WSL2**: `ssh -p 2222 matt@192.168.1.174`, repo at `/home/matt/RecoilEngine` (build dir: `build-headless`)
- **Demo file**: `/tmp/desync-test/demos/2025-08-24_11-11-45-534_Full Metal Plate 1_2025.04.08.sdfz` (on both machines)
- **Branch**: `arm64-desync-test` (same on both machines)

## Build & Run Commands
```bash
# ARM64
cd build-desync-test && make engine-headless -j10
./spring-headless --write-dir /tmp/desync-test --isolation-dir /tmp/desync-test \
  "/tmp/desync-test/demos/2025-08-24_11-11-45-534_Full Metal Plate 1_2025.04.08.sdfz"

# x86_64
cd build-headless && make engine-headless -j$(nproc)
./spring-headless --write-dir /tmp/desync-test --isolation-dir /tmp/desync-test \
  "/tmp/desync-test/demos/2025-08-24_11-11-45-534_Full Metal Plate 1_2025.04.08.sdfz"
```

## Ruled Out
- SmoothHeightMesh SSE intrinsics (identical hash on both platforms)
- Matrix44f multiply via sse2neon (10,000 self-checks, zero mismatches)
- FMA contraction (proven by SSE vs scalar self-check with `-ffp-contract=off`)
- streflop math functions (all synced checksums match)
- isqrt (replacing it breaks from frame 0; original produces matching results)
- ARM64 self-consistency PROVEN (21/21 checksums match between two ARM64 runs)

---

## Desync #1: `short()` UB in CobInstance (FIXED)

### Status: FIXED AND VERIFIED

### Summary
`short(heading * RAD2TAANG)` in `CobInstance::AimWeapon` is undefined behavior when `heading > pi`, because the product exceeds `short` range [-32768, 32767]. ARM64 returns stale garbage while x86_64 wraps modulo 2^16.

### Fix Applied
Replaced `short(float)` with well-defined wrapping in 3 locations in `CobInstance.cpp` (AimWeapon, StartBuilding, WindChanged) plus debug logging in `Weapon.cpp`:
```cpp
// Before (UB for heading > pi):
callinArgs[1] = short(heading * RAD2TAANG);

// After (well-defined wrapping, matches x86_64 behavior):
callinArgs[1] = static_cast<int16_t>(static_cast<uint16_t>(static_cast<int32_t>(heading * RAD2TAANG)));
```

### Verification (Test Run 7)
- x86_64: **597 MATCH, 0 DESYNC** (all checksums match demo through entire replay)
- ARM64: **470 MATCH, 0 DESYNC through frame 28140** — fix resolved the original desync at frame 21480
- ARM64 now produces `6e4dbeac` at frame 21480 (matches x86_64 and demo exactly)

### Original Desync Details
- First desync was at frame 21480 (ARM64: da397037, demo: 6e4dbeac)
- Root cause: COB script received +61234 on ARM64 vs -4302 on x86_64 for same heading
- Non-linear COB operations (division, comparison) on these different integers caused turret rotation to diverge at frame 21444
- Weapon fired at different times -> synced RNG diverged -> checksum desync

---

## Desync #2: Piece Rotation Divergence (INVESTIGATING)

### Status: ROOT CAUSE NARROWED — PIECE ROTATIONS DIVERGE PRE-FRAME 28140

### Test Run 8 Findings (per-frame SyncMid + phase logging, frames 28140-28200)

**Per-frame SyncMid analysis:**
- Frames 28140-28155: ALL checksums MATCH
- Frame 28156: Checksums match but UnitHandler RNG counts differ (+13 x86)
- Frame 28157: Checksums match but RNG wobble continues
- **Frame 28158: FIRST CHECKSUM DIVERGENCE** — ARM64=40ab26ed, x86=e5b9a10b

**UnitHandler phase breakdown at frame 28156 (first RNG divergence):**

| Phase | ARM64 | x86_64 | Diff |
|-------|-------|--------|------|
| del/move/qdel/los | 0 | 0 | 0 |
| **slow** | **403** | **403** | **0** |
| **upd** | **1195** | **1199** | **+4 x86** |
| **wpn** | **75** | **84** | **+9 x86** |
| total | 1673 | 1686 | +13 |

SlowUpdate is identical. The divergence is in the **unit Update** and **Weapons** phases.

**SetRot analysis:**
- x86 has 8 more SetRot entries (17024 vs 17016) at frame 28156
- When sorted by content (stripping pointers and callers), **piece=1 rotation values diverge**:
  - ARM64: `(0x1.921fb6p+0, 0, 0x1.2d97c8p+2)` ≈ **(π/2, 0, 3π/2)**
  - x86_64: `(0x1.92164ap+1, 0, 0x1.922922p+1)` ≈ **(3.14148, 0, 3.14183)**
- These are **wildly different angles** — NOT a small rounding error
- The SAME divergence exists at **frame 28140** (the start of our logging window!)
- The divergent pieces are `aimPiece=1` (weapon aim pieces) on 3 different units

**TickSpin/TickTurn analysis:**
- All TickSpin `cur`, `speed`, `accel` values **MATCH** between platforms at frame 28140
- The divergent piece=1 rotation is on the **X and Z axes** (not the spin axis Y)
- These are STATIC rotations — set once and never changed by active animations
- x86 has 12 more TickTurn entries (additional weapon aim turns triggered by different targeting)

### Causation Chain
1. Weapon aim pieces have different static X/Z rotations (set before frame 28140)
2. Different piece geometry → different muzzle positions and aim vectors
3. Different targeting decisions → some weapons aim on x86 but not ARM64 (and vice versa)
4. Different aim commands → different RNG consumption in Update (aim callbacks) and Weapons phases
5. Checksum diverges at frame 28158 when the accumulated RNG difference affects synced state

### Key Question: WHEN Were the Divergent Rotations Set?

The ARM64 values are clean TAANG conversions:
- π/2 = 16384 × TAANG2RAD (exact)
- 3π/2 = 49152 × TAANG2RAD (exact, or -16384 as int16_t)

The x86 values are NOT clean TAANG conversions (3.14148 / TAANG2RAD ≈ 32757.4).
This suggests the x86 values were modified by animation accumulation from a different starting point,
or set from a different source value entirely.

### Ruled Out for Desync #2
1. **`math::floor` platform difference** — RULED OUT. streflop::floor is portable C. Cross-platform test confirms identical.
2. **Other `short(int)` casts** — RULED OUT. Integer narrowing is well-defined.
3. **`int(float)` UB** — RULED OUT for game values.
4. **All `short(float)` casts** — FIXED (3 in CobInstance.cpp + 2 in Weapon.cpp debug logging).
5. **`math::sqrt`** — RULED OUT. ARM64 `__builtin_sqrtf` (FSQRT) and x86 `SQRTSS` are both IEEE 754 correctly rounded.
6. **`math::atan2`** — Uses `streflop_libm::__ieee754_atan2f` on both platforms (portable C implementation).
7. **`isqrt2_nosse`** — Same Newton-Raphson on both platforms, with `-ffp-contract=off`.
8. **`-ffp-contract=off`** — Verified set in top-level CMakeLists.txt for all ARM64 builds.
9. **sse2neon precision** — `SSE2NEON_PRECISE_DIV=1`, `SSE2NEON_PRECISE_MINMAX=1`, `SSE2NEON_PRECISE_DP=1` all set.

### Next Steps
1. **Add unit IDs to SetRot logging** — currently SetRot only has piece index and pointer, no unit ID
2. **Add targeted logging at early frames** — trace when the divergent piece=1 X/Z rotations were first set to non-zero
3. **Expand logging range** — need to go back much earlier than frame 28140 to find when the divergence began
4. **Check model initialization** — verify S3O model loading doesn't set non-zero piece rotations

### Debug Logging State (Test Run 8)

| File | Tags | Frame Range |
|------|------|-------------|
| UnitScript.cpp | [TurnNow], [TurnCmd], [TickTurn], [TickSpin], [SpinCmd], [TurnTwd] | 28140-28200 |
| Weapon.cpp | [AimWpn], [AimCB], [PieceEvo], [WantedDir], [TargetPos], [TryTarget] | 28140-28200 |
| UnitHandler.cpp | [SlowUpd], [UnitHandler] | 28140-28200 |
| 3DModel.cpp | [SetRot] | 28140-28200 |
| Game.cpp | [SyncMid] | every 60 frames + 28140-28200 per-frame |

## Architecture Notes
- **COB VM**: Pure integer stack machine. No float math. TAANG<->radian conversion at C++ boundary only.
- **SyncedPrimitive<T>**: Writes call `CSyncChecker::Sync()` -> XXH3 running hash
- **`GetHeadingFromVectorF`**: Polynomial atan approximation (NOT `math::atan2`), all IEEE 754 ops
- `math::sqrt` -> `__builtin_sqrtf` on ARM64 (IEEE 754 FSQRT)
- `math::atan2/sin/cos/etc` -> `streflop::` -> portable libm
- **RAD2TAANG** = 65536 / (2*pi), **TAANG2RAD** = 2*pi / 65536

### Log Files
- Test Run 8 (phase logging): ARM64 `/tmp/arm64_run8.log`, x86_64 `/tmp/x86_run8.log`
- Test Run 7 (with short() fix): ARM64 `/tmp/arm64_run7.log`, x86_64 `/tmp/x86_run7.log`
- Test Run 6 (pre-fix): ARM64 `/tmp/arm64_run6.log`, x86_64 `/tmp/x86_run6.log`
