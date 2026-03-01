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

## Key Findings (CONFIRMED)

### What We Know
1. **Synced checksums match from frame 0 through frame 21420** - 358 comparisons every 60 frames, ALL MATCH
2. **First synced desync at frame 21480** - (local=da397037 demo=6e4dbeac)
3. **ARM64 self-consistency PROVEN** - 21/21 checksums match between two independent ARM64 runs
4. **SmoothHeightMesh computes identically** - hash a51b7d50 on ARM64
5. **Matrix multiply via sse2neon is CORRECT** - 10,000 self-checks, zero mismatches between SSE (via sse2neon) and scalar
6. **Unit 11816 weapon 0 is the first affected unit** - weapon fires on ARM64 at frame 21455, timing differs on x86_64

### Root Cause Chain (high level)
```
Non-synced muzzlePos/aimFromPos diverges
  -> wantedDir diverges
    -> angleGood differs
      -> weapon fires at different times
        -> synced RNG sequence diverges
          -> synced checksum desync at frame 21480
```

### Non-synced Value Chain (weapon fire decision)
```
piece animation (streflop math)
  -> ComposeTransform (sin/cos + float arith)
    -> matrix multiply (SSE/sse2neon)
      -> matrix*vector
        -> relWeaponMuzzlePos
          -> GetObjectSpacePos (pos + frontdir*p.z + rightdir*p.x + updir*p.y)
            -> weaponMuzzlePos
              -> currentTargetPos - aimFromPos
                -> SafeNormalize (uses isqrt)
                  -> wantedDir
                    -> heading/pitch
                      -> AimWeapon COB script
                        -> angleGood
                          -> CanFire
                            -> weapon fire
                              -> RNG (synced state diverges)
```

## Investigations & What's Been Ruled Out

### 1. SmoothHeightMesh SSE intrinsics (RULED OUT)
- **Hypothesis**: `_mm_max_ps` via sse2neon might handle signed zero/NaN differently
- **Result**: Synced checksums match from frame 0, so the height mesh is computed identically. Any divergence here would cause immediate desync.

### 2. Matrix44f matrix-matrix multiply via sse2neon (RULED OUT)
- **Hypothesis**: SSE-to-NEON translation of matrix multiply might produce different results
- **Test**: Added self-check in Matrix44f.cpp comparing SSE result with scalar result for 10,000 multiplies
- **Result**: Zero mismatches. `[MatMul]` logging confirms identical results.

### 3. FMA contraction in scalar code (RULED OUT)
- **Hypothesis**: ARM64 compiler might fuse multiply-add into FMA despite `-ffp-contract=off`
- **Test**: Matrix self-check (SSE vs scalar) proves scalar path matches SSE path
- **Result**: If FMA was happening, scalar would differ from SSE. It doesn't. `-ffp-contract=off` is working.

### 4. All synced value computation (RULED OUT)
- **Evidence**: All synced checksums match through frame 21420 (358 frames compared)
- **Conclusion**: Everything computed in synced context (between ENTER_SYNCED_CODE/LEAVE_SYNCED_CODE) is identical on both platforms

### 5. streflop math functions (RULED OUT)
- **Evidence**: All synced state matches. streflop sin/cos/atan2/sqrt etc. used extensively in synced code
- **Conclusion**: Portable libm produces identical results on both platforms

### 6. `isqrt2_nosse()` - Quake fast inverse sqrt (RULED OUT)
- **Hypothesis**: Newton-Raphson approximation might converge differently on ARM64 vs x86_64
- **Test**: Replaced `math::isqrt()` with `1.0f / __builtin_sqrtf(x)` on ARM64 only
- **Result**: DESYNC FROM FRAME 0! Much worse. This proves `isqrt2_nosse()` produces MATCHING results on both platforms, and replacing it with a different algorithm breaks things immediately.

## Logging Added (current state of code)

All logging targets unit 11816, weapon 0, frames >= 21440:

| Tag | File | What it logs |
|-----|------|-------------|
| `[DemoSync]` | GameServer.cpp | Bidirectional demo checksum comparison every 60 frames |
| `[SyncMid]` | Game.cpp | Per-subsystem checksum at key frames |
| `[PieceEvo]` | Weapon.cpp | Piece transform values (muzzlePos, aimFromPos, weaponDir) with explicit float casts |
| `[PieceAnim]` | Weapon.cpp | Piece rotation, position, model-space matrix hash for muzzle and aim pieces |
| `[WantedDir]` | Weapon.cpp | wantedDir computation inputs (targetPos, aimFromPos, diff, sqLen) |
| `[AimWpn]` | Weapon.cpp | heading/pitch/TAANG values for AimWeapon COB call |
| `[AimCB]` | Weapon.cpp | AimScriptFinished callback result (angleGood transition) |
| `[TargetPos]` | Weapon.cpp | currentTargetPos after GetLeadTargetPos |
| `[WpnFire]` | Weapon.cpp | weapon fire decision details |
| `[MatMul]` | Matrix44f.cpp | ARM64 matrix multiply self-check (can be removed) |
| `[SmoothMesh]` | SmoothHeightMesh.cpp | Mesh sample hash (can be removed) |

## ARM64 Weapon Fire Timeline (unit 11816, weapon 0)

```
f=21443: AimCB retCode=1, angleGood: 0->1  (TAANG heading=-2855, pitch=4902)
f=21450: salvoLeft=2 (salvo starting)
f=21452: CanFire=TRUE -> FIRES (shot 1)
f=21455: CanFire=TRUE -> FIRES (shot 2)
f=21458: AimCB retCode=0, angleGood: 0->0  (loses angle, TAANG heading=-1467, pitch=6872)
f=21458-21469: angleGood=0
f=21470: AimCB retCode=0
f=21478: AimCB retCode=1, angleGood: 0->1  (TAANG heading=8647, pitch=9316)
f=21478: CanFire=TRUE -> FIRES
f=21481: CanFire=TRUE -> FIRES
f=21484: CanFire=TRUE -> FIRES
f=21487: CanFire=TRUE -> FIRES
f=21490: CanFire=TRUE -> FIRES
f=21493: AimCB retCode=1, angleGood: 0->1 -> FIRES
f=21496: CanFire=TRUE -> FIRES
f=21499: CanFire=TRUE -> FIRES
```

## NARROWED: AimPiece Y Rotation Diverges at Frame 21445

### Cross-Platform Comparison (CONFIRMED)
Comparing `[PieceAnim]` logs for aimPiece (piece 16) Y rotation between ARM64 and x86_64:

```
Frame 21444: MATCH on both platforms
  rot=(0x0p+0, 0x1.77bbb8p+2, 0x0p+0)   [Y ≈ 5.934]

Frame 21445: DIVERGE!
  ARM64:  rot=(0x0p+0, 0x1.617f26p+2, 0x0p+0)   [Y ≈ 5.523, DECREASED by ~0.411]
  x86_64: rot=(0x0p+0, 0x1.809b16p+2, 0x0p+0)   [Y ≈ 6.019, INCREASED by ~0.085]
```

- This is a LARGE difference, NOT a 1-ULP rounding error
- The rotations go in OPPOSITE DIRECTIONS
- After frame 21445, x86_64 Y rotation stays constant while ARM64 continues decreasing
- This suggests either different animation destinations (from COB Turn commands) or different behavior in `TurnToward()`

### math:: Namespace Resolution (CONFIRMED)
The `math::` namespace is populated by:
1. `streflop_cond.h:33`: `namespace math { using namespace streflop; }` - ALL streflop functions
2. `FastMath.h:240`: `namespace math { using fastmath::floor; }` - OVERRIDES floor only

So in piece animation code (`TurnToward`, `ClampRad`):
- `math::fmod` = `streflop::fmod` (portable, same on both platforms)
- `math::fabsf` = `streflop::fabsf` (portable, same on both platforms)
- `math::floor` = `fastmath::floor` (DIFFERENT: ARM64 uses `streflop::floor`, x86 uses `static_cast<int>` trick)

### Animation System Context
- `TickAllAnims()` is called from `CUnitScriptEngine::Tick()` inside the synced section (Game.cpp:1859)
- `TickTurnAnim()` calls `ClampRad(cur)` then `TurnToward(cur, dest, speed/tickRate)`
- `TurnToward()` computes `delta = fmod(dest - cur + 3π, 2π) - π` to find shortest rotation path
- `Turn()` command from COB script stores `ClampRad(destination)` as animation dest

### Critical Discovery: Piece 16 Has No Turn/Spin Animations!
The aim piece (piece 16) has NO active Turn or Spin animations at frames 21440-21470:
- 0 `[TickTurn]` entries for piece 16
- 0 `[TickSpin]` entries for piece 16
- 0 `[TurnNow]` entries for piece 16
- 0 `[TurnCmd]` entries for piece 16

Yet the PieceAnim LOCAL rotation changes:
```
f=21440-21444: rot Y = 0x1.77bbb8p+2 (CONSTANT - local rot doesn't change)
f=21445:       rot Y = 0x1.617f26p+2 (JUMPS! Decreased by ~0.411)
f=21446:       rot Y = 0x1.3ffe9ap+2 (continuing to decrease)
f=21447:       rot Y = 0x1.1e7e0ep+2 (continuing to decrease)
```

But the model-space MATRIX (matHash) changes EVERY frame even when local rot is constant,
because PARENT pieces have active turn animations that affect the cumulative matrix.

**Implication**: The local rotation jump at frame 21445 is NOT from the animation system.
Something ELSE sets piece 16's rotation. Need to investigate:
1. `SetPieceSpaceMatrix()` - external matrix override
2. COB `set-piece-rotation` or similar opcode
3. Some weapon system code that directly sets piece rotation
4. Check if piece 16 is actually being used as a "blockScriptAnims" piece where the matrix is set externally

### Previous Plan (completed)
1. ~~Rebuild ARM64 binary~~ (done)
2. ~~Commit + push all changes to both machines~~ (done)
3. ~~Add logging to `SetPieceSpaceMatrix()` and `SetRotation()` on `LocalModelPiece` for piece 16~~ (done)
4. Identify what code path changes piece 16's rotation at frame 21445

## Test Run 3: SetRotation/SetPieceSpaceMatrix Instrumentation (2026-02-28)

### Goal
Determine WHAT CODE PATH modifies piece 16's Y rotation at frame 21445. Previous runs showed
rot changing without any TurnCmd/TickTurn/TurnNow activity for piece 16, which shouldn't be possible
since `SetRotation()` is the only way to modify `rot` (private member).

### Changes (commit 35bc184f1f)
1. **Added `[SetRot]` logging** in `LocalModelPiece::SetPosOrRot()` - catches ALL rotation changes
   to piece 16 (scriptPieceIndex == 16) at frames 21430-21470. Logs old and new rotation values.
2. **Added `[SetPieceMat]` logging** in `LocalModelPiece::SetPieceSpaceMatrix()` - catches any
   external matrix override for piece 16 (e.g. from Lua `SetUnitPieceMatrix`).
3. **Moved `SetPieceSpaceMatrix()` from header to .cpp** to enable logging.
4. **Removed per-unit RNG logging** - the `[UnitUpd]` and `[WpnUpd]` per-unit RNG delta logging
   was producing 6.5M lines per run. Kept the per-frame `[UnitHandler]` sub-phase summary.

### What to look for
- If `[SetRot]` entries appear for piece 16: the animation system IS involved (maybe a Turn animation
  was active but previous logging had a bug or frame range issue)
- If `[SetPieceMat]` entries appear: Lua is overriding the piece matrix externally
- If NEITHER appears but `[PieceAnim]` still shows rot changing: something bypasses `SetRotation()`
  entirely (serialization? direct memory access?)

### Expected code paths for piece rotation modification
Only 3 callers of `LocalModelPiece::SetRotation()`:
1. `UnitScript::TickTurnAnim()` (line 195) - Turn animation tick
2. `UnitScript::TickSpinAnim()` (line 211) - Spin animation tick
3. `UnitScript::TurnNow()` (line 526) - COB TURN_NOW opcode (immediate set)

Only 1 caller of `LocalModelPiece::SetPieceSpaceMatrix()`:
1. `LuaSyncedCtrl::SetUnitPieceMatrix()` (LuaSyncedCtrl.cpp:3562) - Lua script override

### Results from Test Run 3

1. **`[SetRot]` fires abundantly for piece 16** - many units have piece 16 with active Turn animations.
2. **`[SetPieceMat]` has ZERO entries** - Lua `SetUnitPieceMatrix` is NOT being used. Ruled out.
3. **`[TickTurn]` for unit 11816 piece 16: STILL EMPTY** on both ARM64 and x86_64.
4. **Cross-platform PieceAnim comparison confirmed AGAIN:**
   - f=21440-21444: both platforms rot Y = 0x1.77bbb8p+2, matHash identical (f2aceaaf at f=21444)
   - f=21445: ARM64 rot Y = 0x1.617f26p+2 (DOWN), x86_64 rot Y = 0x1.809b16p+2 (UP)
5. **SetRot chain at frame 21444 (ARM64):**
   - `old=(0,0x1.77bbb8p+2,0) new=(0,0x1.77bbb8p+2,0)` (no-op)
   - `old=(0,0x1.77bbb8p+2,0) new=(0,0x1.6caap+2,0)` (first Y change)
   - `old=(0,0x1.6caap+2,0) new=(0,0x1.617f26p+2,0)` (→ matches PieceAnim at f=21445)

### Key Contradiction
`SetRotation()` IS being called for pieces with Y=0x1.77bbb8p+2 (visible in [SetRot]), but
`TickTurnAnim()` for unit 11816 piece 16 is NOT firing (no [TickTurn] entries). This means either:
- The SetRot entries are from DIFFERENT units whose piece 16 happens to have similar Y values
- OR there's a bug in the logging

### Test Run 4: Pointer-Based Cross-Reference (in progress)
Adding piece memory addresses (`ptr=%p`) to both `[SetRot]` and `[PieceAnim]` to definitively match
SetRotation calls to unit 11816. Also widening `[TickTurn]` to log ALL units' piece 16 animations
(not just unit 11816) with unit ID.

### Remaining hypotheses
1. **Another unit's Turn animation writes to a piece that unit 11816 reads** - POSSIBLE if
   pieces are shared (unlikely) or there's a piece index aliasing issue
2. **Different COB Turn destinations** - Still the strongest suspect for the root cause (WHY
   the rotation directions differ), but first need to confirm WHICH unit's animation is involved
3. **TickTurnAnim is called but logging doesn't fire** - Would indicate a bug in the logging condition

## Key Architecture Notes
- **SyncedPrimitive<T>**: Every write to SyncedFloat/SyncedInt calls `Sync::Assert()` -> `CSyncChecker::Sync()` -> feeds value into XXH3 running hash
- **CSyncChecker**: Running hash starts at `0xfade1eaf` each frame, accumulated via XXH3
- Only code between `ENTER_SYNCED_CODE()` and `LEAVE_SYNCED_CODE()` contributes to checksum
- `math::sqrt` -> `fastmath::sqrt_sse` -> `__builtin_sqrtf` on ARM64 (IEEE 754 FSQRT)
- `math::atan2/sin/cos/etc` -> `streflop::` -> portable libm
- `SyncedFloat3` operator* returns `float3` (implicit conversion from SyncedFloat to float happens)
- **SyncedFloat3 variadic LOG bug**: Must use `(float)` cast when logging SyncedPrimitive values in variadic functions like LOG()

## Log Analysis Commands
```bash
# Check desync status
grep '[DemoSync].*FIRST DESYNC' /tmp/desync-test/infolog.txt
grep '[DemoSync].*MATCH' /tmp/desync-test/infolog.txt | tail -5

# Check weapon chain for unit 11816
grep '[PieceEvo]' /tmp/desync-test/infolog.txt | grep 'f=21455'
grep '[WpnFire]' /tmp/desync-test/infolog.txt | grep unit=11816
grep '[AimWpn]' /tmp/desync-test/infolog.txt | head -10
grep '[AimCB]' /tmp/desync-test/infolog.txt | head -10

# Compare logs between platforms (copy x86_64 log locally first)
# scp -P 2222 matt@192.168.1.174:/tmp/desync-test/infolog.txt /tmp/desync-test/infolog-x86.txt
# diff <(grep '\[PieceEvo\]' /tmp/desync-test/infolog.txt) <(grep '\[PieceEvo\]' /tmp/desync-test/infolog-x86.txt)
```
