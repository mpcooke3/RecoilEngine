# ARM64 vs x86_64 Cross-Architecture Desync Debug Log

## The Problem
ARM64 (Apple Silicon) headless build desyncs when replaying an x86_64 demo. All 4 original x86_64 players had identical checksums. Our ARM64 checksums diverge starting at frame 21480.

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

## Confirmed Facts

1. **Synced checksums match from frame 0 through frame 21420** (358 comparisons, ALL MATCH)
2. **First synced desync at frame 21480** (local=da397037 demo=6e4dbeac)
3. **ARM64 self-consistency PROVEN** (21/21 checksums match between two ARM64 runs)
4. **All synced-context computation is deterministic** (streflop, matrix multiply, isqrt, SmoothHeightMesh)
5. **Unit 11816 weapon 0 is the first affected unit** (weapon fires at different times per platform)

## Ruled Out
- SmoothHeightMesh SSE intrinsics (identical hash on both platforms)
- Matrix44f multiply via sse2neon (10,000 self-checks, zero mismatches)
- FMA contraction (proven by SSE vs scalar self-check with `-ffp-contract=off`)
- streflop math functions (all synced checksums match)
- isqrt (replacing it breaks from frame 0; original produces matching results)

## Root Cause Chain
```
short() UB in CobInstance::AimWeapon (CONFIRMED ROOT CAUSE)
  -> COB script receives different integer heading on ARM64 vs x86_64
    -> COB arithmetic produces genuinely different turn-now destination
      -> piece 16 (turret) rotation diverges at frame 21444
        -> aimFromPos diverges -> wantedDir diverges -> angleGood differs
          -> weapon fires at different times -> synced RNG diverges
            -> synced checksum desync at frame 21480
```

## The Smoking Gun: `short()` Undefined Behavior

### Location
`rts/Sim/Units/Scripts/CobInstance.cpp` line 436:
```cpp
void CCobInstance::AimWeapon(int weaponNum, float heading, float pitch)
{
    std::array<int, 1 + MAX_COB_ARGS> callinArgs;
    callinArgs[0] = 2;
    callinArgs[1] = short(heading * RAD2TAANG);   // <-- HERE
    callinArgs[2] = short(  pitch * RAD2TAANG);   // <-- AND HERE
    Call(COBFN_AimPrimary + COBFN_Weapon_Funcs * weaponNum, callinArgs, CBAimWeapon, weaponNum, nullptr);
}
```

### The Bug
`heading` is in [0, 2pi) (from `ClampRad`). `RAD2TAANG = 65536 / (2*pi)`. So `heading * RAD2TAANG` is in [0, 65536). For headings in [pi, 2pi), the product is in [32768, 65536) which **overflows `short` range [-32768, 32767]**.

Converting a float outside the target integer type's range is **undefined behavior** in C++.

### Platform-Specific Behavior (CONFIRMED by cross-platform test)
Standalone test compiled with `clang++ -O2` (ARM64) and `g++ -O2` (x86_64) confirms:
- **x86_64**: `short(61235.0f)` wraps modulo 2^16 to **-4302**
- **ARM64**: `short(61235.0f)` returns **32440** (stale value from last valid conversion — not even wrapping, just garbage)

For ALL overflow cases (product >= 32768), ARM64 returns the same stale value while x86_64 wraps correctly. The well-defined fix (`int32_t` -> `uint16_t` -> `int16_t`) produces results **bit-identical to x86_64** on both platforms.

### Evidence from Test Run 6

Frame 21430 call 2 (weapon 1's first TurnNow for piece 16 axis=1):
- ARM64: `rawDest=0x1.77bbb8p+2` -> COB int = **+61234** (positive, large)
- x86_64: `rawDest=-0x1.a63fecp-2` -> COB int = **-4302** (negative, small)
- Both `clampDest=0x1.77bbb8p+2` (same angle, since 61234 ≡ -4302 mod 65536)

These integers differ by exactly 65536 (one full rotation in TAANG units). For **linear** operations in the COB script (add, subtract), the difference is preserved and ClampRad eliminates it. But for **non-linear** operations (division, comparison, abs, min, max), the results diverge:
- `61234 / 2 = 30617` vs `-4302 / 2 = -2151` (differ by 32768 = π radians!)
- `61234 > 0` = true vs `-4302 > 0` = false (different branch taken)

### The Divergence Timeline

Frame 21430-21443 (14 frames): COB script does linear operations only, so clampDest stays identical despite different integer representations.

**Frame 21444 call 2**: First non-linear operation produces genuinely different angles:
- ARM64: `clampDest=0x1.6caap+2` (~5.70 rad)
- x86_64: `clampDest=0x1.809b16p+2` (~6.01 rad)
- Difference: ~0.31 rad (~18 degrees), headings go in **opposite directions**

After frame 21444: ARM64 heading sweeps through a wide range of values while x86_64 locks to a single value. By frame 21449, ARM64 oscillates between two positions while x86_64 stays stable.

### Multiple Weapons on Same Turret
Unit 11816 has **at least 3 weapons** all aiming via the same turret pieces:
- **Weapon 0**: TurnNow call pair 1 each frame (we log AimWpn for this one)
- **Weapon 1**: TurnNow call pair 2 (the one that diverges first)
- **Weapon 2**: TurnNow call pair 3 (appears at frame 21444)

Our `[AimWpn]` and `[WantedDir]` logging only covers `weaponNum == 0`. The divergence at frame 21444 call 2 comes from **weapon 1**, which we're blind to. But the root cause is the same `short()` conversion in `CobInstance::AimWeapon` which is shared by all weapons.

### Supporting Evidence
- **Piece 17 (pitch) matches** at all calls: the pitch is smaller, so `pitch * RAD2TAANG` stays within `short` range → no UB → no divergence
- **All synced state at frame 21444 is IDENTICAL**: checksums, RNG counts, unit SlowUpd deltas, ownerPos, front/right/up vectors, parentMat
- **All TickTurn animations at frame 21444 are IDENTICAL**: every cur/dest/speed/delta/sign/newCur matches exactly
- **The rawDest representation difference starts at the FIRST aiming frame (21430)** and is consistent with the `short()` wrapping difference

### Proposed Fix
Replace the undefined `short()` conversion with explicit wrapping:
```cpp
// Before (UB for heading > pi):
callinArgs[1] = short(heading * RAD2TAANG);
callinArgs[2] = short(  pitch * RAD2TAANG);

// After (well-defined wrapping):
callinArgs[1] = static_cast<int16_t>(static_cast<uint16_t>(static_cast<int>(heading * RAD2TAANG)));
callinArgs[2] = static_cast<int16_t>(static_cast<uint16_t>(static_cast<int>(  pitch * RAD2TAANG)));
```
Or using a helper:
```cpp
static inline int16_t float_to_taang(float rad) {
    return static_cast<int16_t>(static_cast<uint16_t>(static_cast<int32_t>(rad * RAD2TAANG)));
}
```

### Verification Status
1. ~~**Confirm the theory**: Compile a small test on both platforms~~ **DONE** — confirmed `short(61235.0f)` returns 32440 on ARM64 vs -4302 on x86_64
2. **Apply the fix** to CobInstance::AimWeapon — NEXT
3. **Run both platforms** and verify rawDest representations now match
4. **Confirm synced checksums match** past frame 21480

## Other Notes

### `math::floor` Platform Difference
`math::floor` uses `streflop::floor` on ARM64 vs integer truncation on x86_64 (FastMath.h). Used in `ClampRad()`. This is a **secondary concern** — it could cause issues in other code paths, but the primary desync is caused by the `short()` UB above. Should be fixed separately for correctness.

### KSIN/KCOS in COB VM
The COB GET opcodes `KSIN`, `KCOS`, `KTAN` (UnitScript.cpp:1264-1269) use `math::sinf`/`math::cosf`/`math::tanf` and truncate to int. If the COB script uses these with the different integer representations (61234 vs -4302), the float inputs will have different precision characteristics even though they represent the same angle. This could amplify the divergence further, but is a consequence of the `short()` UB, not a separate root cause.

### Architecture Notes
- **COB VM**: Pure integer stack machine. No float math. TAANG<->radian conversion at C++ boundary only.
- **SyncedPrimitive<T>**: Writes call `CSyncChecker::Sync()` -> XXH3 running hash
- **`GetHeadingFromVectorF`**: Polynomial atan approximation (NOT `math::atan2`), all IEEE 754 ops
- `math::sqrt` -> `__builtin_sqrtf` on ARM64 (IEEE 754 FSQRT)
- `math::atan2/sin/cos/etc` -> `streflop::` -> portable libm

### Log Files (Test Run 6)
- ARM64: `/tmp/arm64_run6.log` (7250 lines)
- x86_64: `/tmp/x86_run6.log` (26832 lines)
