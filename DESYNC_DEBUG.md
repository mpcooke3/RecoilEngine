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
6. **Unit 11816 weapon 0 is the first affected unit** 
6. - weapon fires on ARM64 at frame 21455, timing differs on x86_64

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
| `[SetRot]` | 3DModel.cpp | All SetRotation calls for piece 16 with caller ID, old/new rot, ptr |
| `[TurnNow]` | UnitScript.cpp | TurnNow calls for unit 11816 with rawDest and clampDest |
| `[TickTurn]` | UnitScript.cpp | TickTurnAnim for piece 16 or unit 11816 |
| `[TickSpin]` | UnitScript.cpp | TickSpinAnim for piece 16 or unit 11816 |
| `[AimMat]` | Weapon.cpp | All 16 model-space matrix elements for aimPiece (piece 16) |
| `[AimPSMat]` | Weapon.cpp | All 16 pieceSpaceMat elements for aimPiece (local transform) |
| `[ParentMat]` | Weapon.cpp | Parent piece index, rotation, and all 16 model-space matrix elements |
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

### Critical Discovery: Piece 16 Rotation Set by COB TurnNow (RESOLVED)
**Previous mystery**: Test Runs 1-4 showed no TickTurn/TickSpin/TurnNow entries for piece 16,
yet PieceAnim showed local rotation changing. This was caused by stale binaries and logging bugs.

**Test Run 5 resolution**: ALL rotation changes come from `TurnNow` (caller=3). The COB
AimWeapon script calls `turn-now piece_16 y-axis heading` every frame to point the turret.
This is the TURN_NOW opcode, NOT a Turn animation (no TickTurnAnim involved).

The local rotation timeline on ARM64:
```
f=21420-21429: rot Y = 0x0p+0           (weapon idle, heading=0)
f=21430:       rot Y = 0x1.77bbb8p+2    (weapon starts aiming, heading ~5.93)
f=21431-21443: rot Y = 0x1.77bbb8p+2    (CONSTANT - steady tracking)
f=21444:       rot Y → 0x1.6caap+2 → 0x1.617f26p+2  (heading changes, Y DECREASING)
f=21445:       rot Y → 0x1.56544cp+2 → 0x1.4b2974p+2 → 0x1.3ffe9ap+2  (rapid decrease)
f=21449:       rot Y oscillation begins between 0x1.8a4e84p+1 and 0x1.a071f4p+1
```

### Previous Plan (COMPLETED)
1. ~~Rebuild ARM64 binary~~ (done)
2. ~~Commit + push all changes to both machines~~ (done)
3. ~~Add logging to `SetPieceSpaceMatrix()` and `SetRotation()` on `LocalModelPiece` for piece 16~~ (done)
4. ~~Identify what code path changes piece 16's rotation at frame 21445~~ → **TurnNow (COB TURN_NOW opcode from AimWeapon script)**

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

### Test Run 4: Pointer-Based Cross-Reference (2026-03-01)

#### Changes
- Added `ptr=%p` to `[SetRot]` and `[PieceAnim]` logs for cross-referencing
- Widened `[TickTurn]` to log ALL units with `ai.piece == 16` (not just unit 11816)

#### Results
- **Unit 11816 aimPiece ptr = 0x36fa9ab80** (confirmed via PieceAnim)
- **SetRot for ptr=0x36fa9ab80 fires 2x/frame** (f=21431-21443): both no-ops (Y stays 0x1.77bbb8p+2)
- **SetRot for ptr=0x36fa9ab80 at f=21444**: THREE calls:
  1. `Y: 0x1.77bbb8p+2 -> 0x1.77bbb8p+2` (no-op, axis 0 or 2 anim)
  2. `Y: 0x1.77bbb8p+2 -> 0x1.6caap+2` (first Y change)
  3. `Y: 0x1.6caap+2 -> 0x1.617f26p+2` (second Y change, matches PieceAnim at f=21445)
- **TickTurn for piece=16 fires for OTHER units** (529, 4158, 9822, 12189, 9435) - all axis=2
- **TickTurn for unit 11816 piece=16: STILL EMPTY**
- **TickSpin for unit 11816: EMPTY**

#### Key Paradox
`SetRotation()` IS definitely being called on unit 11816's piece 16 (ptr confirmed), but NONE of
the 3 known callers (TickTurnAnim, TickSpinAnim, TurnNow) fire for this piece. There are ONLY 3
callers of `LocalModelPiece::SetRotation()` in the entire codebase (verified via exhaustive grep).

### Test Run 5: Caller Tracing — BREAKTHROUGH (2026-03-01)

#### Changes (commits 94b2bf5c3f, 775bd82c78)
Added global `g_setRotCaller` variable in 3DModel.cpp:
- Set to 1 before `SetRotation` in `TickTurnAnim`
- Set to 2 before `SetRotation` in `TickSpinAnim`
- Set to 3 before `SetRotation` in `TurnNow`
- Logged in `SetPosOrRot` as `caller=%d`
- Reset to 0 unconditionally in `SetPosOrRot` (after logging block, for ALL pieces)

Also added `[TurnNow]` logging in `CUnitScript::TurnNow()` with rawDest and clampDest values.

#### Results — ALL calls are TurnNow (caller=3)

**100% of SetRot entries for unit 11816 piece 16 show `caller=3` (TurnNow).** No unknown callers.
No TickTurnAnim (caller=1) or TickSpinAnim (caller=2). The COB TURN_NOW opcode is the exclusive
source of all rotation changes on this piece.

The earlier "missing TurnNow" mystery (Test Runs 3-4) was caused by stale binaries and logging
conditions that didn't match. The Test Run 5 build with corrected logging shows TurnNow firing
abundantly.

#### TurnNow Destination Timeline (ARM64, unit 11816 piece 16 axis 1)

```
Phase 1: Weapon idle (f=21420-21429)
  rawDest=0x0p+0 (zero), 1 TurnNow call/frame for piece 16
  → weapon not aiming, COB AimWeapon returns heading=0

Phase 2: Weapon starts aiming (f=21430)
  rawDest=-0x1.655b2ep-2 → clampDest=0x1.7bca04p+2
  → first non-zero heading, weapon acquired target

Phase 3: Steady tracking (f=21431-21443)
  rawDest=0x1.77bbb8p+2 (constant), 2 TurnNow calls/frame
  → weapon maintains stable heading, COB script calls turn-now twice per frame

Phase 4: RE-AIM / DIVERGENCE ONSET (f=21444) ← CRITICAL FRAME
  Call 1: rawDest=0x1.77bbb8p+2 → clampDest=0x1.77bbb8p+2  (old heading, no-op)
  Call 2: rawDest=-0x1.2badb2p-1 → clampDest=0x1.6caap+2   (NEW heading! Y decreases by ~0.17)
  Call 3: rawDest=-0x1.85047ep-1 → clampDest=0x1.617f26p+2  (heading continues decreasing)
  → 3 calls this frame, heading starts changing rapidly

Phase 5: Rapid heading decrease (f=21445-21448)
  f=21445: 3 calls, Y decreasing: 0x1.56544cp+2 → 0x1.4b2974p+2 → 0x1.3ffe9ap+2
  f=21446: 3 calls, Y decreasing: 0x1.34d3cp+2 → 0x1.29a8e8p+2 → 0x1.1e7e0ep+2
  f=21447: 3 calls, Y decreasing: 0x1.135334p+2 → 0x1.08285cp+2 → 0x1.f9fb02p+1
  f=21448: 3 calls, Y decreasing: 0x1.e3a55p+1 → 0x1.cd4f9cp+1 → 0x1.b6f9eap+1

Phase 6: Reversal + oscillation (f=21449+)
  f=21449: Call 1: clampDest=0x1.a0a438p+1 (still decreasing)
           Call 2: clampDest=0x1.8a4e84p+1 (still decreasing)
           Call 3: clampDest=0x1.a071f4p+1 (REVERSAL! Y goes back UP)
  f=21450+: OSCILLATION between 0x1.8a4e84p+1 and 0x1.a071f4p+1 every call
  → weapon "jitters" between two aim positions, COB calls turn-now 3x/frame
```

#### Interpretation

The TurnNow `rawDest` values ARE the heading angle passed to the COB AimWeapon script by the
weapon system. The COB script receives `(heading, pitch)` as parameters, computes `turn-now`
for piece 16 (turret Y) and piece 17 (barrel pitch), and calls the TURN_NOW opcode.

The heading is computed from:
```
wantedDir = SafeNormalize(currentTargetPos - aimFromPos)
heading = GetHeadingFromVector(wantedDir.x, wantedDir.z)  // in TAANG units
```
Then converted to radians and passed to AimWeapon. The `rawDest` in the log is this radian value.

**Root cause**: `aimFromPos` depends on piece transforms (model-space matrices), which are computed
via non-synced float math (ComposeTransform + matrix chain). If the model-space matrix differs
between ARM64 and x86_64 — even by 1 ULP in a parent piece — the `aimFromPos` will differ,
producing a different `wantedDir`, which produces a different heading, which gets passed to
TurnNow, which sets piece 16's rotation differently, which FURTHER changes the model-space matrix
in a feedback loop.

The key question is: **what causes the FIRST divergence in the model-space matrix?** All known
operations are deterministic:
- streflop sin/cos: proven deterministic (FPDeterminism test)
- Matrix multiply: proven deterministic (self-check test, sse2neon vs scalar)
- isqrt: proven deterministic (cross-arch test)

Need x86_64 TurnNow data at frame 21444 to confirm whether the rawDest values differ.

#### Cross-platform comparison (PieceAnim, previously confirmed)
```
Frame 21444: MATCH on both platforms
  rot=(0x0p+0, 0x1.77bbb8p+2, 0x0p+0)   [Y ≈ 5.934]

Frame 21445: DIVERGE!
  ARM64:  rot=(0x0p+0, 0x1.617f26p+2, 0x0p+0)   [Y ≈ 5.523, DECREASED]
  x86_64: rot=(0x0p+0, 0x1.809b16p+2, 0x0p+0)   [Y ≈ 6.019, INCREASED]
```

On ARM64, TurnNow starts changing heading at frame 21444 (call 2: clampDest=0x1.6caap+2).
On x86_64, the heading apparently stays constant at 0x1.77bbb8p+2 through frame 21444 and
changes at frame 21445 to 0x1.809b16p+2 (OPPOSITE direction — increase vs decrease).

This is NOT a 1-ULP rounding error. The headings go in OPPOSITE DIRECTIONS. This means the
weapon system computes fundamentally different aim directions on the two platforms.

#### Next steps
1. **Get x86_64 TurnNow data** — rebuild and run x86_64 to get `[TurnNow]` rawDest values
   at frames 21430-21450 for direct comparison
2. **Compare rawDest at frame 21430** — the FIRST non-zero heading. If this already differs,
   the root cause is in the initial aimFromPos computation
3. **Log aimFromPos/muzzlePos at the transition** — add `%a` logging of aimFromPos and
   muzzlePos vectors at frames 21430-21445 to find exactly which float value diverges first
4. **Log ComposeTransform inputs/outputs** for piece 16 and its parent chain at the
   transition frame to find the exact operation that produces a different result

### Test Run 6: Single-Shot Root Cause Identification (PLANNED)

#### Goal
Identify the EXACT float operation or value that first diverges between ARM64 and x86_64,
causing the AimWeapon heading to differ at frame 21444. This run should produce enough data
to pinpoint the root cause without needing further iterations.

#### What's missing from current logging
1. **Frame range gap**: PieceEvo/WantedDir/PieceAnim only log at frames >= 21440 (or every
   100 frames). The weapon starts aiming at frame 21430 — we're missing 10 critical frames
   where the initial aimFromPos is computed.
2. **Incomplete matrix**: PieceAnim logs only 4 of 16 matrix elements (mat[0], mat[12-14]).
   If the divergence is in another element, we'd see matHash differ but not know which one.
3. **No parent piece data**: Piece 16's model-space matrix = pieceSpaceMat * parent->modelSpaceMat.
   If a parent piece's matrix diverges, we need to see it.

#### What's already sufficient
- `[TurnNow]` rawDest/clampDest with `%a` format (frames 21420-21470)
- `[AimWpn]` with TAANG integers (`taangH=%hd taangP=%hd`) — already present
- `[WantedDir]` with aimFromPos, diff, wantedDir in `%a` format
- `[PieceEvo]` with muzzlePos, aimFromPos, relMuzzle, relAim in `%a` format
- `[SetRot]` with caller trace, old/new values, ptr

#### Changes for Test Run 6
1. **Extend frame range to 21420** — change `gs->frameNum >= 21440` to `>= 21420` for
   PieceEvo, WantedDir, PieceAnim, TargetPos logging blocks in Weapon.cpp
2. **Log all 16 matrix elements** for aimPiece (piece 16) in PieceAnim using `%a` format
3. **Log parent piece matrix** — add `[ParentMat]` logging for piece 16's parent:
   parent scriptPieceIndex, parent rot, and all 16 parent model-space matrix elements
4. **Log piece 16 pieceSpaceMat** — separate from modelSpaceMat, to isolate whether the
   divergence is in the local transform or inherited from the parent chain

#### Expected analysis workflow
After collecting ARM64 and x86_64 data:
```
1. Compare [TurnNow] rawDest at frame 21430 — does the FIRST heading match?
   → YES: divergence accumulates over frames 21431-21443
   → NO: aimFromPos already differs at frame 21430 (proceed to step 3)

2. If YES at step 1: compare rawDest frame-by-frame 21431-21443
   → Find the first frame where rawDest differs (or where TAANG quantization hides a diff)
   → Go to that frame's [WantedDir] to see aimFromPos

3. Compare [WantedDir] aimFromPos at the divergence frame
   → Which component (x, y, z) differs first?
   → Does targetPos match? (synced — should always match)

4. Compare [PieceAnim] full 16-element matrix at the divergence frame
   → Which matrix element diverges first?

5. Compare [ParentMat] — is the divergence in piece 16's local transform or parent?
   → If parent matrix matches but piece 16 matrix differs: issue in piece 16's pieceSpaceMat
     (RotateEulerYXZ or local position)
   → If parent matrix differs: recursively check parent's parent (may need another run)

6. If pieceSpaceMat differs with same rotation inputs:
   → Issue is in RotateEulerYXZ implementation (sin/cos or matrix ops)
   → But we proved sin/cos deterministic... so check for intermediate precision issues
```

#### Run procedure
```bash
# 1. Commit changes on ARM64, push
git add -A && git commit -m "Test Run 6: enhanced logging for root cause"
git push origin arm64-desync-test

# 2. On x86_64: pull and build
ssh -p 2222 matt@192.168.1.174
cd /home/matt/RecoilEngine && git pull && cd build-headless && make engine-headless -j$(nproc)

# 3. On ARM64: build
cd build-desync-test && make engine-headless -j10

# 4. Run on BOTH machines simultaneously
# ARM64:
./spring-headless --write-dir /tmp/desync-test --isolation-dir /tmp/desync-test \
  "/tmp/desync-test/demos/2025-08-24_11-11-45-534_Full Metal Plate 1_2025.04.08.sdfz"
# x86_64:
./spring-headless --write-dir /tmp/desync-test --isolation-dir /tmp/desync-test \
  "/tmp/desync-test/demos/2025-08-24_11-11-45-534_Full Metal Plate 1_2025.04.08.sdfz"

# 5. Copy x86_64 log locally for comparison
scp -P 2222 matt@192.168.1.174:/tmp/desync-test/infolog.txt /tmp/desync-test/infolog-x86.txt

# 6. Compare critical logs
diff <(grep '\[TurnNow\].*piece=16' /tmp/desync-test/infolog.txt) \
     <(grep '\[TurnNow\].*piece=16' /tmp/desync-test/infolog-x86.txt)
diff <(grep '\[WantedDir\]' /tmp/desync-test/infolog.txt) \
     <(grep '\[WantedDir\]' /tmp/desync-test/infolog-x86.txt)
diff <(grep '\[PieceAnim\].*aimPiece' /tmp/desync-test/infolog.txt) \
     <(grep '\[PieceAnim\].*aimPiece' /tmp/desync-test/infolog-x86.txt)
diff <(grep '\[ParentMat\]' /tmp/desync-test/infolog.txt) \
     <(grep '\[ParentMat\]' /tmp/desync-test/infolog-x86.txt)
```

## CRITICAL: `math::floor` Platform Difference

**`math::floor` uses DIFFERENT implementations on ARM64 vs x86_64!**

File: `rts/System/FastMath.h` (line 213)
```cpp
template<typename T>
inline T floor(T f)
{
#if defined(__aarch64__) || defined(__arm64__)
    // ARM64: uses portable streflop::floor
    return streflop::floor(f);
#else
    // x86_64: uses integer truncation + correction
    T truncX = static_cast<T>(static_cast<int>(f));
    return truncX - static_cast<T>(truncX > f);
#endif
}
```

This is exposed via `namespace math { using fastmath::floor; }` which OVERRIDES `streflop::floor`.

### Where `math::floor` is used in the desync path
- **`ClampRad()`** (SpringMath.inl:154): `f = f - math::TWOPI * math::floor(f / math::TWOPI);`
- ClampRad is called by `TurnNow()` to normalize piece rotation to [0, 2π)
- ClampRad is also called by `TickTurnAnim()`, `Turn()`, and others

### Risk assessment
For normal-range values (|f| < 2^31), both implementations produce identical results:
- Both correctly compute floor for positive values (truncation is correct)
- Both correctly compute floor for negative values (x86_64 applies `-(truncX > f)` correction)
- The `-0.0f` edge case is handled by ClampRad's `f += 0.0f` preconditioning

The implementations ONLY differ for:
1. Values outside int range (|f| > 2^31) — not applicable (TAANG angles are small)
2. `-0.0f` input — handled by ClampRad
3. Values where `static_cast<int>(f)` overflow behavior differs — not applicable

**Verdict**: Likely NOT the root cause, but must be verified by comparing ClampRad outputs
([TurnNow] clampDest) cross-platform. If rawDest matches but clampDest differs, `floor` IS
the culprit.

## Key Architecture Notes
- **SyncedPrimitive<T>**: Every write to SyncedFloat/SyncedInt calls `Sync::Assert()` -> `CSyncChecker::Sync()` -> feeds value into XXH3 running hash
- **CSyncChecker**: Running hash starts at `0xfade1eaf` each frame, accumulated via XXH3
- Only code between `ENTER_SYNCED_CODE()` and `LEAVE_SYNCED_CODE()` contributes to checksum
- `math::sqrt` -> `fastmath::sqrt_sse` -> `__builtin_sqrtf` on ARM64 (IEEE 754 FSQRT)
- `math::atan2/sin/cos/etc` -> `streflop::` -> portable libm
- **`math::floor`** -> `fastmath::floor` -> **PLATFORM-SPECIFIC** (see above)
- `SyncedFloat3` operator* returns `float3` (implicit conversion from SyncedFloat to float happens)
- **SyncedFloat3 variadic LOG bug**: Must use `(float)` cast when logging SyncedPrimitive values in variadic functions like LOG()
- **`GetHeadingFromVectorF`** uses polynomial atan approximation (NOT `math::atan2`), all IEEE 754 ops
- **COB VM** operates on pure integers — no float math. TAANG→radian conversion happens only at the C++ boundary (`int * TAANG2RAD`)

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
