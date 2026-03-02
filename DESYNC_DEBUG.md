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

## Final Status: ALL DESYNCS RESOLVED

**906 sync checkpoints across entire 54,492-frame replay: ZERO cross-platform mismatches**

Both fixes applied on branch `arm64-desync-test`:
1. **Desync #1** (frame 21480): `short()` UB in CobInstance float-to-TAANG conversions — commit `67e08a16`
2. **Desync #2** (frame 28158): `ClampRad` heading clamping [0,2π) instead of [-π,π) — commit `07de6019`

---

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

## Desync #2: ClampRad Heading Clamping → Piece Rotation Divergence (FIXED)

### Status: FIXED AND VERIFIED

### Root Cause & Fix

`ClampRad()` wraps angles to `[0, 2π)`, but heading values should be in `[-π, π)`. At boundary
values near ±π, `ClampRad`'s `floor(f / TWOPI)` computation produces platform-dependent rounding
on ARM64 vs x86_64, giving COB scripts different heading integers via `GetUnitVal` calls.

**Fix**: Added `ClampRadPi()` (wraps to `[-π, π)`) and applied it at the same 4 sites as
upstream PR [#2827](https://github.com/beyond-all-reason/RecoilEngine/pull/2827):

1. `Weapon.cpp:532` — `ClampRadPi(heading - owner->heading * TAANG2RAD)` for AimWeapon
2. `Unit.cpp:2388` — `ClampRadPi(GetHeadingFromVectorF(...) - heading * TAANG2RAD)` for wind heading
3. `Builder.cpp:929` — `ClampRadPi(h - heading * TAANG2RAD)` for StartBuilding
4. `LuaSyncedRead.cpp:4545` — `ClampRadPi(math::PI / 32768.0f * heading)` for GetUnitHeading

**Verification (Test Run 17 — commit `07de6019`):**
- **906 sync checkpoints across entire 54,492-frame replay: ZERO cross-platform mismatches**
- Both platforms desync from demo (expected — ClampRadPi changes game behavior)
- Piece rotation hashes now identical at old divergence point (frames 28132–28133)
- Unit 24203 triparent TurnNow values were: ARM64 TAANG=16384, x86 TAANG=32765 (before fix)

### Detailed Investigation Trail

### Test Run 11 Findings (per-frame sync checksums to `/tmp/sync_checksums.txt`)

**CRITICAL DISCOVERY: Per-frame sync checksums logged to dedicated file for ALL frames.**

**Sync checksum comparison (ARM64 vs x86_64):**
- **Frames 0-28155: ALL CHECKSUMS BIT-FOR-BIT IDENTICAL** — synced state is perfectly deterministic
- **Frame 28156: Checksums still identical, but RNG counts diverge** (ARM64=26261055, x86=26261068, diff=+13 x86)
- **Frame 28157: Checksums still identical, RNG gap flips** (ARM64=26262919, x86=26262915, diff=-4 x86)
- **Frame 28158: FIRST CHECKSUM DIVERGENCE** — ARM64=40ab26ed, x86=e5b9a10b

**Frame 28156 per-subsystem breakdown:**

| Subsystem | ARM64 chk | x86 chk | ARM64 RNG | x86 RNG | Match? |
|-----------|-----------|---------|-----------|---------|--------|
| FrameStart | 2bccafbc | 2bccafbc | 26259382 | 26259382 | YES |
| GameFrame | 2bccafbc | 2bccafbc | 26259382 | 26259382 | YES |
| Map | 2bccafbc | 2bccafbc | 26259382 | 26259382 | YES |
| **UnitHandler** | **2b3b01ac** | **2b3b01ac** | **26261055** | **26261068** | chk=YES, **rng=NO (+13)** |
| Projectiles | 2b3b01ac | 2b3b01ac | 26261057 | 26261070 | chk=YES, rng=NO |
| **Features+Scripts** | **c8b5c417** | **c8b5c417** | **26261133** | **26261137** | chk=YES, **rng=NO (+4)** |
| FrameEnd | c8b5c417 | c8b5c417 | 26261133 | 26261137 | chk=YES, rng=NO (+4) |

**Interpretation:**
- Synced state entering frame 28156 is IDENTICAL (same checksum, same RNG count)
- During UnitHandler, x86 makes +13 more gsRNG calls despite identical synced state
- The extra RNG calls must come from weapon code whose execution depends on UNSYNCED piece rotations
- gsRNG (game-synced RNG) is consumed by weapon firing/accuracy code
- After Features+Scripts, the RNG gap reduces to +4 (animations partially compensate)
- By frame 28158, the accumulated RNG offset causes different synced decisions → checksum diverges

### Proven Mechanism (Desync #2)

```
1. UNSYNCED piece rotations diverge between platforms (WHEN? TBD)
     ↓
2. Weapon aim computes aimFromPos from piece model space matrix (UNSYNCED)
     ↓
3. Different aimFromPos → different wantedDir → different heading/pitch
     ↓
4. Different weapon aim → different firing decisions
     ↓
5. Different firing → different gsRNG consumption (accuracy spread, salvo error)
     ↓
6. gsRNG offset accumulates over frames
     ↓
7. Eventually (frame 28158), RNG offset causes different synced state → checksum diverges
```

### Key Proven Facts (Test Runs 10-11)
1. **Float math is bit-identical**: DoSpin, ClampRad, speed accumulation all produce identical results on both platforms (unit 5415 spin tracked through 10000+ frames)
2. **All synced state identical through frame 28155**: 28156 frames of perfect sync (every subsystem, every frame)
3. **FPU settings match**: Both platforms use round-to-nearest, denormals enabled (no FZ/DAZ/FTZ)
4. **No FMA on either platform**: x86 uses `-mno-fma`, 0 FMA instructions in binary. ARM64 uses `-ffp-contract=off`
5. **streflop math functions identical**: sin, cos, atan2, floor, fmod all use portable C libm
6. **`math::fmod` uses streflop**: Despite `streflop_cond.h` using `std::fmod` when streflop disabled, the enabled path goes through `streflop_libm::__ieee754_fmodf` (portable C)

### Additional Fixes Applied (did NOT fix desync #2)
- `short(turnRate)` UB in HoverAirMoveType.cpp (lines 674, 676) — clamped to short range
- `short(maxTurnRate)` UB in IPathController.cpp (lines 57, 59) — clamped to short range

### Test Run 12 Findings: Piece Rotation Hash Comparison

**CRITICAL: Piece rotations first diverge at frame 28133, unit 24203.**

Per-frame piece rotation hash (XOR of all piece rotations before weapon update):
- **Frames 0–28132: ALL HASHES BIT-FOR-BIT IDENTICAL** between ARM64 and x86
- **Frame 28133: FIRST DIVERGENCE** — ARM64=`85602099`, x86=`2929d19b`
- Piece rotations continue diverging for all subsequent frames
- Synced state (checksums + RNG counts) remain IDENTICAL through frame 28155 despite piece rotation divergence
- Piece rotation divergence at frame 28133 → RNG consumption divergence at frame 28156 → checksum divergence at frame 28158

### Test Run 13 Findings: Per-Unit Hash Identification

Added per-unit piece rotation hash for frames 28130–28136.

**First divergent unit: unit 24203 (18 pieces)**

| Frame | ARM64 hash | x86 hash | Match? |
|-------|-----------|----------|--------|
| 28130 | c2a73f35 | c2a73f35 | YES |
| 28131 | 4d40bdf5 | 4d40bdf5 | YES |
| 28132 | 71f289cc | 71f289cc | YES |
| **28133** | **87127fbb** | **a3eafeed** | **NO** |
| 28134 | 7723f8c7 | 53db7991 | NO |

**Second divergent unit: unit 28641 (18 pieces)** — diverges one frame later at 28134.

### Test Run 14 Findings: Per-Piece Rotation Divergence

PieceDetail logging dumps every non-zero piece rotation for unit 24203 at frames 28132–28134 (using localModel piece indices).

**First divergent piece: localModel piece 9 (frame 28133)**

| Platform | rx (hex) | rx (float) | ry | rz (hex) | rz (float) |
|----------|----------|-----------|-----|----------|-----------|
| ARM64 | 3fc90fdb | pi/2 (1.5708) | 0 | 4096cbe4 | 3pi/2 (4.7124) |
| x86_64 | 40490b25 | ~pi (3.1413) | 0 | 40491491 | ~pi (3.1419) |

- At frame 28132: piece 9 has (0,0,0) on BOTH platforms — no divergence
- At frame 28133: piece 9 has COMPLETELY DIFFERENT values — not close, not drift, entirely different rotations
- All other pieces are BIT-FOR-BIT IDENTICAL at frame 28133

**Key observation: the values are NOT incrementally close (pi/2 vs pi). This is NOT floating-point drift.**

### Test Run 15 Findings: CRITICAL DISCOVERY — Script vs Model Piece Index Mismatch

Added spin/turn animation tracing for script piece 9 of unit 24203.

**The spin animations are BIT-FOR-BIT IDENTICAL on both platforms:**
All spin tick values (cur, destSpd, speed, accel) for script piece 9, axes 0 and 1, match perfectly across ARM64 and x86 through all frames 28125–28138. The spin is a slow rotation accumulating ~0.07 rad/frame.

**BUT: the spin tick `cur` values DON'T match the PieceDetail values.**
- Spin tick at f=28132, axis=0: `cur=0x1.73bc0cp-3` (0.181)
- PieceDetail at f=28132, localModel piece 9: `rx=0` (all zeros — piece not logged)

**Root cause of the discrepancy: SCRIPT PIECE INDEX != LOCALMODEL PIECE INDEX.**

`CobInstance::MapScriptToModelPieces()` maps COB script piece names to model pieces by **name lookup**, not by index. So `script->pieces[9]` (a pointer) may point to `localModel.pieces[N]` where N != 9.

The PieceDetail hash iterates `localModel.pieces` by index. So what we called "localModel piece 9" is not the same as "script piece 9". The spin animation is correctly tracked on script piece 9 (matching on both platforms), but the divergence is on a DIFFERENT localModel piece whose localModel index happens to be 9.

### CRITICAL QUESTION: What is setting localModel piece 9's rotation?

Since script piece 9's animations are identical, the divergence must be on a different script piece that maps to localModel index 9. We need:
1. The piece name mapping (script index → localModel index → name) for unit 24203
2. Which script piece maps to localModel index 9
3. What animation/command is changing THAT piece

### Frame Update Order (affects when piece rotations change)

```
Frame N:
  1. UnitHandler (includes UpdateUnitWeapons)
     a. Piece rotation hash computed (reflects state from Frame N-1's changes)
     b. UpdateWeaponVectors — reads piece rotations for aim positions
     c. UpdateWeapons — weapon AimWeapon callins can call TurnNow (INSTANT rotation set)
  2. PathManager
  3. ProjectileHandler
  4. Features+Scripts (animation engine tick)
     a. cobEngine->Tick() — processes COB thread sleeps/wakes
     b. Turn/Spin animation ticks (TickTurnAnim/TickSpinAnim)
     c. This is where spin animations update piece rotations incrementally
```

Piece 9 in localModel goes from (0,0,0) at hash time frame 28132 to (pi/2,0,3pi/2) at hash time frame 28133. Between these hash computations:
- Frame 28132's weapon update (step 1c) — could call TurnNow
- Frame 28132's animation tick (step 4b) — could have spin/turn finishing
- Frame 28133 steps before hash — no known path changes pieces

### Hypotheses (Updated)

1. **A TurnNow from a weapon AimWeapon callin** sets localModel piece 9 during frame 28132's weapon update. The callin parameters differ because the COB script reads heading from weapon aim, which depends on piece rotations — but all piece rotations are identical at that point, so parameters should be identical. UNLESS the callin happens at a different time due to weapon timing.

2. **A Turn animation completes (snaps to destination)** on localModel piece 9 during frame 28132's animation tick. If the destination was set by a COB callin that received platform-different heading, the snap target would differ. But we need to identify which script piece maps to localModel 9.

3. **AnimationMT**: If multi-threaded animation ticking causes nondeterministic results when two animations on the same piece interact (turn overriding spin, etc.), this could produce different rotations per platform.

### Ruled Out for Desync #2
1. **`math::floor` platform difference** — RULED OUT. Portable C on both.
2. **`math::fmod` platform difference** — RULED OUT. Portable C on both.
3. **`short(float)` UB in movement code** — FIXED but did not affect desync
4. **Denormal handling** — RULED OUT. FZ=0, DAZ=0, FTZ=0 on both
5. **`math::sqrt`** — RULED OUT. IEEE 754 correctly rounded on both
6. **`math::isqrt` (isqrt2_nosse)** — Same Newton-Raphson, `-ffp-contract=off`
7. **`-ffp-contract=off`** — Verified in CMakeLists.txt
8. **sse2neon precision** — All PRECISE flags set
9. **Script piece 9 spin animations** — RULED OUT. Bit-identical on both platforms.

### Plan for Run 16: Comprehensive Logging to Single-Shot Root Cause

Need to add ALL of the following in one run:

1. **Piece name mapping** for unit 24203: dump `localModel index → scriptPieceIndex → name` for all pieces (once at frame 28132)
2. **PieceDetail with script index**: add `scriptPieceIndex` to each PieceDetail log line so we can correlate localModel pieces with script pieces
3. **ALL SetRotation calls** for unit 24203 around frames 28130–28135: log in `3DModel.cpp::SetPosOrRot()` for g_setRotUnitId == 24203, including caller ID, script piece index, old/new values
4. **ALL Turn/TurnNow/Spin commands** for unit 24203 (all pieces) around frames 28130–28135: catch any animation command on any piece of this unit
5. **ALL animation ticks** for unit 24203 (all pieces) around frames 28130–28135: catch TickTurnAnim/TickSpinAnim on any piece
6. **COB callin parameters** for unit 24203: log AimWeapon heading/pitch values passed to COB around these frames

This should capture the COMPLETE chain from: COB callin → animation command → SetRotation → piece rotation value, for every piece of unit 24203, on both platforms. The diff will show exactly where the divergence enters.

### Debug Logging State (Current — Run 15)

| File | What | Output |
|------|------|--------|
| Game.cpp | Per-frame sync checksums (all subsystems, ALL frames) | `/tmp/sync_checksums.txt` |
| UnitHandler.cpp | Per-frame piece rotation hash (before weapon update) | `/tmp/piece_rot_hash.txt` |
| UnitHandler.cpp | Per-unit piece rotation hash | `/tmp/piece_rot_per_unit.txt` (frames 28130-28136) |
| UnitHandler.cpp | Per-piece rotation detail for unit 24203 | LOG output (frames 28132-28134) |
| UnitHandler.cpp | Sub-phase RNG consumption | frames 28140-28200 |
| UnitScript.cpp | Unit 24203 piece 9 (script) Turn/TurnNow/Spin/TickSpin | LOG (frames 28125-28140) |
| UnitScript.cpp | Unit 5415 piece=1 rotation tracking | all frames |
| Weapon.cpp | [AimWpn] logging | frames 28140-28200 |

## Architecture Notes
- **COB VM**: Pure integer stack machine. No float math. TAANG<->radian conversion at C++ boundary only.
- **SyncedPrimitive<T>**: Writes call `CSyncChecker::Sync()` -> XXH3 running hash
- **`GetHeadingFromVectorF`**: Polynomial atan approximation (NOT `math::atan2`), all IEEE 754 ops
- `math::sqrt` -> `__builtin_sqrtf` on ARM64 (IEEE 754 FSQRT)
- `math::atan2/sin/cos/etc` -> `streflop::` -> portable libm
- **RAD2TAANG** = 65536 / (2*pi), **TAANG2RAD** = 2*pi / 65536
- **AnimationMT**: Default=true, ticks animations in parallel per-unit via `for_mt()`, with sequential cleanup

### gsRNG Consumption in Weapon Path
- `SlowUpdate()`: 2 calls (errorVectorAdd, predictSpeedMod) — unconditional, runs every SLOWUPDATE tick
- `UpdateFire()`: 1 call (salvoError) — conditional on weapon firing
- `FireImpl()`: 1-2 calls (spray angle, TTL) — conditional on weapon firing
- All `FireImpl` calls depend on `TryTarget()` which uses `aimFromPos` (piece-rotation-dependent)

### Log Files
- Test Run 11 (per-frame checksums): ARM64 `/tmp/arm64_run11.log`, x86_64 `/tmp/x86_run11.log`
- Test Run 11 sync checksums: ARM64 `/tmp/arm64_sync_checksums.txt`, x86_64 via SSH
- Test Run 10 (unit 5415 tracking): ARM64 `/tmp/arm64_run10.log`
- Test Run 8 (phase logging): ARM64 `/tmp/arm64_run8.log`, x86_64 `/tmp/x86_run8.log`
