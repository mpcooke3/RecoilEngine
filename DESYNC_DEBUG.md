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

## Desync #2: Unknown (INVESTIGATING)

### Status: FIRST DESYNC AT FRAME 28200

### What We Know
- **Last matching frame**: 28140 (both platforms: chk=f50e61fd, rngCnt=26196480)
- **First desyncing frame**: 28200 (ARM64: 00081195, x86_64/demo: 1737b544)
- **x86_64 is fully synced** through the entire replay (597 MATCH, 0 DESYNC)
- **ARM64 desyncs from frame 28200 onward** (697 DESYNCs after frame 28200)

### SyncMid Breakdown at Frame 28200
The checksum already differs at FrameStart, meaning the divergence happened between frame 28141-28199:

| Phase | ARM64 chk | ARM64 rngCnt | x86_64 chk | x86_64 rngCnt |
|-------|-----------|--------------|------------|---------------|
| FrameStart | 5329ec09 | 26458266 | ec3f8a45 | 26459176 |
| UnitHandler | 1c39a1f3 | 26459969 | c7ca5568 | 26460936 |
| Features+Scripts | 00081195 | 26460246 | 1737b544 | 26461200 |

RNG count difference at FrameStart: x86_64 has **910 more** RNG calls, suggesting a weapon/projectile timing difference cascaded (similar pattern to Desync #1).

### Suspects
1. **`math::floor` platform difference** — `math::floor` uses `streflop::floor` on ARM64 vs integer truncation on x86_64 (`FastMath.h`). Used in `ClampRad()`. Could cause subtle angle differences in other code paths.
2. **Other `short()` casts in MoveTypes** — Found in:
   - `HoverAirMoveType.cpp:674,676`: `short(turnRate)` and `short(-turnRate)` — likely safe (turnRate is small)
   - `GroundMoveType.cpp:1306`: `short(owner->heading - wantedHeading)` — both are shorts, difference could overflow
   - `IPathController.cpp:57,59`: `short(maxTurnRate)` — likely safe
3. **Another instance of the same `short()` UB pattern** in a code path we haven't found yet

### Current Debug Logging State
All debug logging is currently targeted at the old desync range (frames 21420-21470). For desync #2 we need to retarget to frames 28140-28200. Logging locations:

| File | Tags | Current Frame Range |
|------|------|-------------------|
| UnitScript.cpp | [TurnNow], [TurnCmd], [TickTurn], [TickSpin], [SpinCmd], [TurnTwd] | 21420-21470 |
| Weapon.cpp | [AimWpn], [AimCB], [PieceEvo], [WantedDir], [TargetPos], [TryTarget] | 21420+ (no upper) |
| UnitHandler.cpp | [SlowUpd], [UnitHandler] | 21420+/21440+ |
| 3DModel.cpp | [SetRot] | 21430-21470 |
| Game.cpp | [SyncMid] | every 60 frames + 21440-21490 |

### Investigation Plan
1. **Retarget SyncMid** to per-frame granularity between frames 28140-28200 (Game.cpp)
2. **Retarget unit-specific logging** — we don't know which unit diverges yet; first find the exact frame via SyncMid, then narrow down by subsystem phase
3. **Widen UnitHandler [SlowUpd] logging** to cover frames 28140-28200 to identify which unit's RNG diverges first
4. **Investigate `math::floor` in `ClampRad`** — compare ARM64 `streflop::floor` vs x86_64 integer truncation (FastMath.h). This is the top suspect since it affects every angle computation.
5. **Search for additional `short(float)` UB** patterns across the full codebase (not just `rts/Sim`)
6. **Remove or disable old frame 21420-21470 logging** to reduce log noise

### Changes Required Before Test Run 8
- Game.cpp: Change SyncMid extended range to 28140-28200 (per-frame)
- UnitHandler.cpp: Change [SlowUpd] range to 28140-28200
- UnitScript.cpp: Change frame ranges to 28140-28200 (but unit ID unknown — may need to log all units initially or remove unit filter)
- Weapon.cpp: Change frame range start to 28140
- 3DModel.cpp: Change frame range to 28140-28200

## Architecture Notes
- **COB VM**: Pure integer stack machine. No float math. TAANG<->radian conversion at C++ boundary only.
- **SyncedPrimitive<T>**: Writes call `CSyncChecker::Sync()` -> XXH3 running hash
- **`GetHeadingFromVectorF`**: Polynomial atan approximation (NOT `math::atan2`), all IEEE 754 ops
- `math::sqrt` -> `__builtin_sqrtf` on ARM64 (IEEE 754 FSQRT)
- `math::atan2/sin/cos/etc` -> `streflop::` -> portable libm
- **RAD2TAANG** = 65536 / (2*pi), **TAANG2RAD** = 2*pi / 65536

### Log Files
- Test Run 7 (with short() fix): ARM64 `/tmp/arm64_run7.log`, x86_64 `/tmp/x86_run7.log`
- Test Run 6 (pre-fix): ARM64 `/tmp/arm64_run6.log`, x86_64 `/tmp/x86_run6.log`
