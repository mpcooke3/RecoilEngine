/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "Weapon.h"
#include "WeaponDefHandler.h"
#include "WeaponMemPool.h"
#include "Game/GameHelper.h"
#include "Game/TraceRay.h"
#include "Game/Players/Player.h"
#include "Lua/LuaConfig.h"
#include "Map/Ground.h"
#include "Sim/Misc/CollisionHandler.h"
#include "Sim/Misc/CollisionVolume.h"
#include "Sim/Misc/GlobalSynced.h"
#include "Sim/Misc/InterceptHandler.h"
#include "Sim/Misc/ModInfo.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/Misc/QuadField.h"
#include "Sim/MoveTypes/AAirMoveType.h"
#include "Sim/Projectiles/ProjectileHandler.h"
#include "Sim/Projectiles/WeaponProjectiles/WeaponProjectile.h"
#include "Sim/Units/Scripts/CobInstance.h"
#include "Sim/Units/Scripts/NullUnitScript.h"
#include "Sim/Units/CommandAI/CommandAI.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitDef.h"
#include "Sim/Weapons/Cannon.h"
#include "Sim/Weapons/NoWeapon.h"
#include "System/EventHandler.h"
#include "System/SpringMath.h"
#include "System/creg/DefTypes.h"
#include "System/Sound/ISoundChannels.h"
#include "System/Log/ILog.h"

#include "System/Misc/TracyDefs.h"

CR_BIND_DERIVED_POOL(CWeapon, CObject, , weaponMemPool.allocMem, weaponMemPool.freeMem)
CR_REG_METADATA(CWeapon, (
	CR_MEMBER(owner),
	CR_MEMBER(slavedTo),
	CR_MEMBER(weaponDef),
	CR_MEMBER(damages),

	CR_MEMBER(aimFromPiece),
	CR_MEMBER(muzzlePiece),

	CR_MEMBER(reaimTime),
	CR_MEMBER(reloadTime),
	CR_MEMBER(reloadStatus),

	CR_MEMBER(salvoDelay),
	CR_MEMBER(salvoSize),
	CR_MEMBER(projectilesPerShot),
	CR_MEMBER(nextSalvo),
	CR_MEMBER(salvoLeft),
	CR_MEMBER(salvoWindup),
	CR_MEMBER(ttl),

	CR_MEMBER(range),
	CR_MEMBER(projectileSpeed),
	CR_MEMBER(accuracyError),
	CR_MEMBER(sprayAngle),
	CR_MEMBER(predictSpeedMod),

	CR_MEMBER(hasBlockShot),
	CR_MEMBER(hasTargetWeight),
	CR_MEMBER(angleGood),
	CR_MEMBER(avoidTarget),
	CR_MEMBER(onlyForward),
	CR_MEMBER(muzzleFlareSize),
	CR_MEMBER(doTargetGroundPos),
	CR_MEMBER(noAutoTarget),
	CR_MEMBER(alreadyWarnedAboutMissingPieces),

	CR_MEMBER(badTargetCategory),
	CR_MEMBER(onlyTargetCategory),

	CR_MEMBER(buildPercent),
	CR_MEMBER(numStockpiled),
	CR_MEMBER(numStockpileQued),

	CR_MEMBER(lastAimedFrame),
	CR_MEMBER(lastTargetRetry),

	CR_MEMBER(maxForwardAngleDif),
	CR_MEMBER(maxMainDirAngleDif),

	CR_MEMBER(heightBoostFactor),
	CR_MEMBER(autoTargetRangeBoost),

	CR_MEMBER(avoidFlags),
	CR_MEMBER(collisionFlags),
	CR_MEMBER(weaponNum),

	CR_MEMBER(relAimFromPos),
	CR_MEMBER(aimFromPos),
	CR_MEMBER(relWeaponMuzzlePos),
	CR_MEMBER(weaponMuzzlePos),
	CR_MEMBER(weaponDir),
	CR_MEMBER(mainDir),
	CR_MEMBER(wantedDir),
	CR_MEMBER(lastRequestedDir),
	CR_MEMBER(salvoError),
	CR_MEMBER(errorVector),
	CR_MEMBER(errorVectorAdd),

	CR_MEMBER(currentTarget),
	CR_MEMBER(currentTargetPos),

	CR_MEMBER(incomingProjectileIDs),

	CR_MEMBER(weaponAimAdjustPriority),
	CR_MEMBER(fastAutoRetargeting),
	CR_MEMBER(fastQueryPointUpdate),
	CR_MEMBER(burstControlWhenOutOfArc)
))



//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CWeapon::CWeapon(CUnit* owner, const WeaponDef* def):
	owner(owner),
	slavedTo(nullptr),
	weaponDef(def),
	damages(nullptr),

	weaponNum(-1),
	aimFromPiece(-1),
	muzzlePiece(-1),

	reaimTime(GAME_SPEED >> 1),
	reloadTime(1),
	reloadStatus(0),

	salvoDelay(0),
	salvoSize(1),
	projectilesPerShot(1),
	nextSalvo(0),
	salvoLeft(0),
	salvoWindup(0),
	ttl(1),

	range(1.0f),
	projectileSpeed(1.0f),
	accuracyError(0.0f),
	sprayAngle(0.0f),
	predictSpeedMod(1.0f),

	hasBlockShot(false),
	hasTargetWeight(false),
	angleGood(false),
	avoidTarget(false),
	onlyForward(false),
	doTargetGroundPos(false),
	noAutoTarget(false),
	alreadyWarnedAboutMissingPieces(false),

	badTargetCategory(0),
	onlyTargetCategory(0xffffffff),

	buildPercent(0),
	numStockpiled(0),
	numStockpileQued(0),

	lastAimedFrame(0),
	lastTargetRetry(-100),

	maxForwardAngleDif(0.0f),
	maxMainDirAngleDif(-1.0f),

	heightBoostFactor(-1.0f),
	autoTargetRangeBoost(0.0f),

	avoidFlags(0),
	collisionFlags(0),

	relAimFromPos(UpVector),
	aimFromPos(ZeroVector),
	relWeaponMuzzlePos(UpVector),
	weaponMuzzlePos(ZeroVector),
	weaponDir(ZeroVector),
	mainDir(FwdVector),
	wantedDir(UpVector),
	lastRequestedDir(-UpVector),
	salvoError(ZeroVector),
	errorVector(ZeroVector),
	errorVectorAdd(ZeroVector),

	muzzleFlareSize(1),

	weaponAimAdjustPriority(1.f),
	fastAutoRetargeting(false),
	fastQueryPointUpdate(false),
	burstControlWhenOutOfArc(0)
{
	assert(weaponMemPool.alloced(this));
}


CWeapon::~CWeapon()
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(weaponMemPool.mapped(this));
	DynDamageArray::DecRef(damages);

	if (weaponDef->interceptor)
		interceptHandler.RemoveInterceptorWeapon(this);
}


inline bool CWeapon::CobBlockShot() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!hasBlockShot)
		return false;

	return owner->script->BlockShot(weaponNum, currentTarget.unit, currentTarget.isUserTarget);
}


float CWeapon::TargetWeight(const CUnit* targetUnit) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return owner->script->TargetWeight(weaponNum, targetUnit);
}


void CWeapon::UpdateWeaponPieces(const bool updateAimFrom)
{
	RECOIL_DETAILED_TRACY_ZONE;
	hasBlockShot = owner->script->HasBlockShot(weaponNum);
	hasTargetWeight = owner->script->HasTargetWeight(weaponNum);

	muzzlePiece = owner->script->QueryWeapon(weaponNum);

	if (updateAimFrom)
		aimFromPiece = owner->script->AimFromWeapon(weaponNum);

	// some scripts only implement one of these
	const bool aimExists = owner->script->PieceExists(aimFromPiece);
	const bool muzExists = owner->script->PieceExists(muzzlePiece);

	if (aimExists && muzExists)
		return; // everything fine

	if (!aimExists && muzExists) {
		aimFromPiece = muzzlePiece;
		return;
	}
	if (aimExists && !muzExists) {
		muzzlePiece = aimFromPiece;
		return;
	}

	if (!alreadyWarnedAboutMissingPieces && (owner->script != &CNullUnitScript::value) && !weaponDef->isShield && (dynamic_cast<CNoWeapon*>(this) == nullptr)) {
		LOG_L(L_WARNING, "%s: weapon%i: Neither AimFromWeapon nor QueryWeapon defined or returned invalid pieceids", owner->unitDef->name.c_str(), weaponNum + LUA_WEAPON_BASE_INDEX);
		alreadyWarnedAboutMissingPieces = true;
	}

	aimFromPiece = -1;
	muzzlePiece = -1;
}


void CWeapon::UpdateWeaponErrorVector()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// update conditional cause last SlowUpdate maybe longer away than UNIT_SLOWUPDATE_RATE
	// i.e. when the unit got stunned (neither is SlowUpdate exactly called at UNIT_SLOWUPDATE_RATE, it's only called `close` to that)
	float3 newErrorVector = (errorVector + errorVectorAdd);
	if (newErrorVector.SqLength() <= 1.0f)
		errorVector = newErrorVector;
}

void CWeapon::AimScriptFinished(bool retCode)
{
	const bool prevAngleGood = angleGood;
	angleGood = retCode;

	// Log AimWeapon callback result for unit 11816
	if (gs->frameNum >= 28140 && gs->frameNum <= 28200) {
		LOG("[AimCB] f=%d unit=%d wpn=%d retCode=%d angleGood: %d->%d", gs->frameNum, owner->id, weaponNum, (int)retCode, (int)prevAngleGood, (int)angleGood);
	}
}

void CWeapon::UpdateWeaponVectors()
{
	ZoneScoped;

	relAimFromPos = owner->script->GetPiecePos(aimFromPiece);
	owner->script->GetEmitDirPos(muzzlePiece, relWeaponMuzzlePos, weaponDir);

	aimFromPos = owner->GetObjectSpacePos(relAimFromPos);
	weaponMuzzlePos = owner->GetObjectSpacePos(relWeaponMuzzlePos);
	weaponDir = owner->GetObjectSpaceVec(weaponDir).SafeNormalize();

	// hope that we are underground because we are a popup weapon and will come above ground later
	if (aimFromPos.y < CGround::GetHeightReal(aimFromPos.x, aimFromPos.z)) {
		aimFromPos = owner->pos + UpVector * 10;
	}

	// Comprehensive piece transform evolution logging for unit 11816
	// NOTE: SyncedFloat3 members (frontdir/rightdir/updir) MUST be cast to (float) for variadic LOG
	if (gs->frameNum >= 28140 && gs->frameNum <= 28200) {
		LOG("[PieceEvo] f=%d muzzlePos=(%a,%a,%a) aimFrom=(%a,%a,%a) relMuzzle=(%a,%a,%a) relAim=(%a,%a,%a) ownerPos=(%a,%a,%a) front=(%a,%a,%a) right=(%a,%a,%a) up=(%a,%a,%a) wDir=(%a,%a,%a) aimPc=%d mzlPc=%d",
			gs->frameNum,
			weaponMuzzlePos.x, weaponMuzzlePos.y, weaponMuzzlePos.z,
			aimFromPos.x, aimFromPos.y, aimFromPos.z,
			relWeaponMuzzlePos.x, relWeaponMuzzlePos.y, relWeaponMuzzlePos.z,
			relAimFromPos.x, relAimFromPos.y, relAimFromPos.z,
			owner->pos.x, owner->pos.y, owner->pos.z,
			(float)owner->frontdir.x, (float)owner->frontdir.y, (float)owner->frontdir.z,
			(float)owner->rightdir.x, (float)owner->rightdir.y, (float)owner->rightdir.z,
			(float)owner->updir.x, (float)owner->updir.y, (float)owner->updir.z,
			weaponDir.x, weaponDir.y, weaponDir.z,
			aimFromPiece, muzzlePiece);

		// Log piece animation state (rotation/position) and model-space matrix hash
		if (owner->script->PieceExists(muzzlePiece)) {
			const LocalModelPiece* mzlP = owner->script->GetScriptLocalModelPiece(muzzlePiece);
			const float3 mzlRot = mzlP->GetRotation();
			const float3 mzlPos = mzlP->GetPosition();
			const CMatrix44f& mzlMat = mzlP->GetModelSpaceMatrix();
			// Hash the 16 matrix floats for compact comparison
			unsigned int mzlMatHash = 0;
			for (int i = 0; i < 16; ++i) {
				unsigned int val; memcpy(&val, &mzlMat.m[i], sizeof(val));
				mzlMatHash ^= val + 0x9e3779b9 + (mzlMatHash << 6) + (mzlMatHash >> 2);
			}
			LOG("[PieceAnim] f=%d mzlPiece=%d rot=(%a,%a,%a) pos=(%a,%a,%a) matHash=%08x mat00=%a mat12=%a mat13=%a mat14=%a",
				gs->frameNum, muzzlePiece,
				mzlRot.x, mzlRot.y, mzlRot.z,
				mzlPos.x, mzlPos.y, mzlPos.z,
				mzlMatHash,
				mzlMat.m[0], mzlMat.m[12], mzlMat.m[13], mzlMat.m[14]);
		}
		if (owner->script->PieceExists(aimFromPiece) && aimFromPiece != muzzlePiece) {
			const LocalModelPiece* aimP = owner->script->GetScriptLocalModelPiece(aimFromPiece);
			const float3 aimRot = aimP->GetRotation();
			const float3 aimPos2 = aimP->GetPosition();
			const CMatrix44f& aimMat = aimP->GetModelSpaceMatrix();
			unsigned int aimMatHash = 0;
			for (int i = 0; i < 16; ++i) {
				unsigned int val; memcpy(&val, &aimMat.m[i], sizeof(val));
				aimMatHash ^= val + 0x9e3779b9 + (aimMatHash << 6) + (aimMatHash >> 2);
			}
			// Log all 16 matrix elements for aimPiece to find exact divergence
			LOG("[PieceAnim] f=%d aimPiece=%d ptr=%p rot=(%a,%a,%a) pos=(%a,%a,%a) matHash=%08x",
				gs->frameNum, aimFromPiece, (void*)aimP,
				aimRot.x, aimRot.y, aimRot.z,
				aimPos2.x, aimPos2.y, aimPos2.z,
				aimMatHash);
			LOG("[AimMat] f=%d m0=%a m1=%a m2=%a m3=%a m4=%a m5=%a m6=%a m7=%a m8=%a m9=%a m10=%a m11=%a m12=%a m13=%a m14=%a m15=%a",
				gs->frameNum,
				aimMat.m[0], aimMat.m[1], aimMat.m[2], aimMat.m[3],
				aimMat.m[4], aimMat.m[5], aimMat.m[6], aimMat.m[7],
				aimMat.m[8], aimMat.m[9], aimMat.m[10], aimMat.m[11],
				aimMat.m[12], aimMat.m[13], aimMat.m[14], aimMat.m[15]);

			// Log piece 16's pieceSpaceMat (local transform, before parent composition)
			const CMatrix44f& psMat = aimP->GetPieceSpaceMatrix();
			LOG("[AimPSMat] f=%d m0=%a m1=%a m2=%a m3=%a m4=%a m5=%a m6=%a m7=%a m8=%a m9=%a m10=%a m11=%a m12=%a m13=%a m14=%a m15=%a",
				gs->frameNum,
				psMat.m[0], psMat.m[1], psMat.m[2], psMat.m[3],
				psMat.m[4], psMat.m[5], psMat.m[6], psMat.m[7],
				psMat.m[8], psMat.m[9], psMat.m[10], psMat.m[11],
				psMat.m[12], psMat.m[13], psMat.m[14], psMat.m[15]);

			// Log parent piece matrix to isolate whether divergence is local or inherited
			const LocalModelPiece* parentP = aimP->parent;
			if (parentP != nullptr) {
				const CMatrix44f& parentMat = parentP->GetModelSpaceMatrix();
				unsigned int parentMatHash = 0;
				for (int i = 0; i < 16; ++i) {
					unsigned int val; memcpy(&val, &parentMat.m[i], sizeof(val));
					parentMatHash ^= val + 0x9e3779b9 + (parentMatHash << 6) + (parentMatHash >> 2);
				}
				LOG("[ParentMat] f=%d parentPiece=%d rot=(%a,%a,%a) matHash=%08x m0=%a m1=%a m2=%a m3=%a m4=%a m5=%a m6=%a m7=%a m8=%a m9=%a m10=%a m11=%a m12=%a m13=%a m14=%a m15=%a",
					gs->frameNum, parentP->GetScriptPieceIndex(),
					parentP->GetRotation().x, parentP->GetRotation().y, parentP->GetRotation().z,
					parentMatHash,
					parentMat.m[0], parentMat.m[1], parentMat.m[2], parentMat.m[3],
					parentMat.m[4], parentMat.m[5], parentMat.m[6], parentMat.m[7],
					parentMat.m[8], parentMat.m[9], parentMat.m[10], parentMat.m[11],
					parentMat.m[12], parentMat.m[13], parentMat.m[14], parentMat.m[15]);
			}
		}
	}
}


void CWeapon::UpdateWantedDir()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!onlyForward) {
		wantedDir = (currentTargetPos - aimFromPos).SafeNormalize();
	} else {
		wantedDir = owner->frontdir;
	}

	// Log wantedDir, currentTargetPos, aimFromPos for unit 11816
	if (gs->frameNum >= 28140 && gs->frameNum <= 28200) {
		const float3 diff = currentTargetPos - aimFromPos;
		const float sql = diff.SqLength();
		LOG("[WantedDir] f=%d tgtPos=(%a,%a,%a) aimFrom=(%a,%a,%a) diff=(%a,%a,%a) sqLen=%a wDir=(%a,%a,%a) onlyFwd=%d",
			gs->frameNum,
			currentTargetPos.x, currentTargetPos.y, currentTargetPos.z,
			aimFromPos.x, aimFromPos.y, aimFromPos.z,
			diff.x, diff.y, diff.z, sql,
			wantedDir.x, wantedDir.y, wantedDir.z,
			(int)onlyForward);
	}
}


float CWeapon::GetPredictedImpactTime(float3 p) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	//TODO take target's speed into account? (not just its position)
	return aimFromPos.distance(p) / projectileSpeed;
}

void CWeapon::Update()
{
	ZoneScoped;

	// Fast auto targeting needs to trigger an immediate retarget once the target is dead.
	bool fastAutoRetargetRequired = fastAutoRetargeting && HaveTarget()
									&& currentTarget.unit != nullptr && currentTarget.unit->isDead;
	if (fastAutoRetargetRequired) {
		// switch to unit's target if it has one - see next bit below
		bool ownerTargetIsValid = (owner->curTarget.type == Target_Unit && currentTarget.unit != nullptr && !currentTarget.unit->isDead)
								|| (owner->curTarget.type != Target_Unit && owner->curTarget.type != Target_None);
		if (ownerTargetIsValid)
			DropCurrentTarget();
		else
			AutoTarget();
	}

	// SlowUpdate() only generates targets when we are in range
	// esp. for bombs this is often too late (SlowUpdate gets only called twice per second)
	// so check unit's target this check every frame (unit target has highest priority even in SlowUpdate!)
	if (!HaveTarget() && owner->curTarget.type != Target_None)
		Attack(owner->curTarget);

	currentTargetPos = GetLeadTargetPos(currentTarget);

	// Log currentTargetPos for unit 11816
	if (gs->frameNum >= 28140 && gs->frameNum <= 28200) {
		LOG("[TargetPos] f=%d wpn=%d tgtPos=(%a,%a,%a) tgtType=%d tgtUnit=%d",
			gs->frameNum, weaponNum,
			currentTargetPos.x, currentTargetPos.y, currentTargetPos.z,
			(int)currentTarget.type,
			(currentTarget.type == Target_Unit && currentTarget.unit) ? currentTarget.unit->id : -1);
	}

	if (!UpdateStockpile())
		return;

	UpdateAim();
	UpdateFire();
	UpdateSalvo();
}


void CWeapon::UpdateAim()
{
	ZoneScoped;
	if (!HaveTarget())
		return;

	UpdateWantedDir();
	CallAimingScript(!weaponDef->allowNonBlockingAim);
}

bool CWeapon::CheckAimingAngle() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// check fire angle constraints
	// TODO: write a per-weapontype CheckAim()?
	const float3 worldTargetDir = (currentTargetPos - owner->pos).SafeNormalize();
	const float3 worldMainDir = owner->GetObjectSpaceVec(mainDir);

	// weapon finished a previously started AimWeapon thread and wants to
	// fire, but target is no longer within constraints --> wait for re-aim
	return (CheckTargetAngleConstraint(worldTargetDir, worldMainDir));
}


bool CWeapon::CanCallAimingScript(bool validAngle) const {
	RECOIL_DETAILED_TRACY_ZONE;
	constexpr float maxAimOffset = 0.93969262078590838405410927732473; // math::cos(20.0f * math::DEG_TO_RAD);

	bool ret = (gs->frameNum >= (lastAimedFrame + reaimTime));

	ret |= (wantedDir.dot(lastRequestedDir) <= weaponDef->maxFireAngle);
	ret |= (wantedDir.dot(lastRequestedDir) <= maxAimOffset);

	// NOTE: angleGood checks unit/maindir, not the weapon's current dir
	// ret |= (!validAngle);
	return ret;
}

bool CWeapon::CallAimingScript(bool waitForAim)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// periodically re-aim the weapon (by calling the script's AimWeapon
	// every N=15 frames regardless of current angleGood state; interval
	// can be artificially shrunk by larger maxFireAngle [firetolerance]
	// *or* via Spring.SetUnitWeaponState)
	// if it does not (eg. because AimWeapon always spawns a thread to
	// aim the weapon and defers setting angleGood to it) then this can
	// lead to irregular/stuttering firing behavior, even in scenarios
	// when the weapon does not have to re-aim
	if (!CanCallAimingScript(angleGood &= CheckAimingAngle()))
		return false;

	// if false, block further firing until AimWeapon has finished
	angleGood &= !waitForAim;

	lastRequestedDir = wantedDir;
	lastAimedFrame = gs->frameNum;

	const float heading = GetHeadingFromVectorF(wantedDir.x, wantedDir.z);
	const float pitch = math::asin(std::clamp(wantedDir.dot(owner->updir), -1.0f, 1.0f));

	// for COB, this sets <angleGood> to AimWeapon's return value when finished
	// for LUS, there exists a callout to set the <angleGood> member directly
	// FIXME: convert CSolidObject::heading to radians too.
	const float aimHeading = ClampRad(heading - owner->heading * TAANG2RAD);

	// Log AimWeapon inputs for all units near desync zone
	if (gs->frameNum >= 28140 && gs->frameNum <= 28200) {
		const int16_t taangH = static_cast<int16_t>(static_cast<uint16_t>(static_cast<int32_t>(aimHeading * RAD2TAANG)));
		const int16_t taangP = static_cast<int16_t>(static_cast<uint16_t>(static_cast<int32_t>(pitch * RAD2TAANG)));
		LOG("[AimWpn] f=%d unit=%d wpn=%d heading=%a pitch=%a aimH=%a ownerHdg=%hd taangH=%hd taangP=%hd angleGood=%d wDir=(%a,%a,%a)",
			gs->frameNum, owner->id, weaponNum,
			heading, pitch, aimHeading, owner->heading,
			taangH, taangP, (int)angleGood,
			wantedDir.x, wantedDir.y, wantedDir.z);
	}
	// Detailed logging for unit 24203 around divergence
	if (owner->id == 24203 && gs->frameNum >= 28128 && gs->frameNum <= 28136) {
		LOG("[U24203AimWpnW] f=%d wpn=%d aimH=%a pitch=%a aimFrom=(%a,%a,%a) wDir=(%a,%a,%a)",
			gs->frameNum, weaponNum, aimHeading, pitch,
			aimFromPos.x, aimFromPos.y, aimFromPos.z,
			wantedDir.x, wantedDir.y, wantedDir.z);
	}

	owner->script->AimWeapon(weaponNum, aimHeading, pitch);
	return true;
}


bool CWeapon::CanFire(bool ignoreAngleGood, bool ignoreTargetType, bool ignoreRequestedDir) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!ignoreAngleGood && !angleGood)
		return false;

	if ((salvoLeft > 0) || (nextSalvo > gs->frameNum))
		return false;

	if (!ignoreTargetType && !HaveTarget())
		return false;

	if (reloadStatus > gs->frameNum)
		return false;

	if (weaponDef->stockpile && numStockpiled == 0)
		return false;

	// muzzle is underwater but we cannot fire underwater
	if (!weaponDef->fireSubmersed && aimFromPos.y <= 0.0f)
		return false;

	// sanity check to force new aim
	if (weaponDef->maxFireAngle > -1.0f) {
		if (!ignoreRequestedDir && wantedDir.dot(lastRequestedDir) <= weaponDef->maxFireAngle)
			return false;
	}

	// if in FPS mode, player must be pressing at least one button to fire
	const CPlayer* fpsPlayer = owner->fpsControlPlayer;
	if (fpsPlayer != nullptr && !fpsPlayer->fpsController.mouse1 && !fpsPlayer->fpsController.mouse2)
		return false;

	return true;
}

void CWeapon::UpdateFire()
{
	ZoneScoped;
	// Debug: weapon fire logging for unit 11816 - periodic tracking + detailed near desync
	const bool isUnit11816 = (owner->id == 11816 && weaponNum == 0);
	const bool dbgWpn = isUnit11816 && (gs->frameNum >= 21450 || (gs->frameNum % 100 == 0));

	if (!CanFire(false, false, false)) {
		if (dbgWpn) {
			LOG("[WpnFire] f=%d unit=%d CanFire=FALSE angleGood=%d salvoLeft=%d nextSalvo=%d reloadStatus=%d",
				gs->frameNum, owner->id, (int)angleGood, salvoLeft, nextSalvo, reloadStatus);
		}
		return;
	}

	if (fastQueryPointUpdate) {
		UpdateWeaponPieces(false);
		UpdateWeaponVectors();
	}

	if (dbgWpn) {
		LOG("[WpnFire] f=%d unit=%d CanFire=TRUE muzzlePos=(%a,%a,%a) tgtPos=(%a,%a,%a)",
			gs->frameNum, owner->id,
			weaponMuzzlePos.x, weaponMuzzlePos.y, weaponMuzzlePos.z,
			currentTargetPos.x, currentTargetPos.y, currentTargetPos.z);
		// Log TryTarget sub-checks
		const bool testTarget = TestTarget(currentTargetPos, currentTarget);
		const bool testRange = TestRange(currentTargetPos, currentTarget);
		const float groundH = CGround::GetHeightReal(weaponMuzzlePos.x, weaponMuzzlePos.z);
		const bool groundOk = !(weaponMuzzlePos.y < groundH);
		LOG("[WpnFire] f=%d unit=%d TestTarget=%d TestRange=%d groundOk=%d (muzzY=%a groundH=%a) aimFromPos=(%a,%a,%a)",
			gs->frameNum, owner->id, (int)testTarget, (int)testRange, (int)groundOk,
			weaponMuzzlePos.y, groundH,
			aimFromPos.x, aimFromPos.y, aimFromPos.z);
		if (testRange) {
			const float heightDiff = currentTargetPos.y - aimFromPos.y;
			const float targetDist = aimFromPos.SqDistance2D(currentTargetPos);
			LOG("[WpnFire] f=%d unit=%d range detail: heightDiff=%a targetDist=%a range=%a cylinderTgt=%a",
				gs->frameNum, owner->id, heightDiff, targetDist, range, weaponDef->cylinderTargeting);
		}
	}

	if (!TryTarget(currentTargetPos, currentTarget, true)) {
		if (dbgWpn) {
			LOG("[WpnFire] f=%d unit=%d TryTarget=FALSE (weapon did NOT fire)", gs->frameNum, owner->id);
		}
		return;
	}

	// pre-check if we got enough resources (so CobBlockShot gets only called when really possible to shoot)
	if (!weaponDef->stockpile && !owner->HaveResources(weaponDef->cost))
		return;

	if (CobBlockShot())
		return;

	if (!weaponDef->stockpile) {
		// use resource for shoot
		CTeam* ownerTeam = teamHandler.Team(owner->team);
		if (!owner->UseResources(weaponDef->cost)) {
			// not enough resource, update pull (needs factor cause called each ::Update() and not at reloadtime!)
			const int minPeriod = std::max(1, int(reloadTime / owner->reloadSpeed));
			const float averageFactor = 1.0f / minPeriod;
			ownerTeam->resPull += weaponDef->cost * averageFactor;
			return;
		}
		ownerTeam->resPull += weaponDef->cost;
	} else {
		const int oldCount = numStockpiled;
		numStockpiled--;
		owner->commandAI->StockpileChanged(this);
		eventHandler.StockpileChanged(owner, this, oldCount);
	}

	reloadStatus = gs->frameNum + int(reloadTime / owner->reloadSpeed);

	salvoLeft = salvoSize;
	nextSalvo = gs->frameNum + salvoWindup;
	salvoError = gsRNG.NextVector() * (owner->IsMoving()? weaponDef->movingAccuracy: accuracyError);

	owner->lastMuzzleFlameSize = muzzleFlareSize;
	owner->lastMuzzleFlameDir = wantedDir;
	owner->script->FireWeapon(weaponNum);
}


bool CWeapon::UpdateStockpile()
{
	ZoneScoped;
	if (!weaponDef->stockpile)
		return true;

	if (numStockpileQued > 0) {
		const float p = 1.0f / weaponDef->stockpileTime;

		if (owner->UseResources(weaponDef->cost * p))
			buildPercent += p;

		if (buildPercent >= 1) {
			const int oldCount = numStockpiled;
			buildPercent = 0;
			numStockpileQued--;
			numStockpiled++;
			owner->commandAI->StockpileChanged(this);
			eventHandler.StockpileChanged(owner, this, oldCount);
		}
	}

	return (numStockpiled > 0) || (salvoLeft > 0);
}


void CWeapon::UpdateSalvo()
{
	ZoneScoped;
	if (!salvoLeft || nextSalvo > gs->frameNum)
		return;

	salvoLeft--;
	nextSalvo = gs->frameNum + salvoDelay;

	if (burstControlWhenOutOfArc) {
		bool haveTarget = HaveTarget();
		bool targetInArc = haveTarget;
		if (targetInArc && weaponDef->maxFireAngle > -1.0f) {
			const float3 currentTargetDir = (currentTargetPos - aimFromPos).SafeNormalize2D();
			const float3 simpleWeaponDir = float3(weaponDir).SafeNormalize2D();

			if (simpleWeaponDir.dot2D(currentTargetDir) < weaponDef->maxFireAngle)
				targetInArc = false;
		}

		if (!targetInArc || !CheckAimingAngle()) {
			if (burstControlWhenOutOfArc == UnitDefWeapon::BURST_CONTROL_OUT_OF_ARC_HOLD) {
				// Hold fire, but continue to aim towards the target.
				UpdateWeaponPieces(false); // calls script->QueryWeapon()
				UpdateWeaponVectors();

				// Special case needed here if the last shot of the salvo has been cancelled.
				if (salvoLeft == 0) {
					owner->script->EndBurst(weaponNum);

					const bool searchForNewTarget = (currentTarget == owner->curTarget);
					owner->commandAI->WeaponFired(this, searchForNewTarget, false);
				}
				return;
			} else {
				// Fire indiscriminately wherever the the weapon is pointing.
				// currentTargetPos gets restored every frame in Update(), so we can change it here without breaking aiming
				// when the target is back in arc. If we don't have a target, then the currentTargetPos will be pointing at
				// the last target point and so can be left.
				if (haveTarget)
					currentTargetPos = aimFromPos + (weaponDir * range);
			}
		}
	}

	// Decloak
	if (owner->unitDef->decloakOnFire)
		owner->ScriptDecloak(HaveUnitTarget()? currentTarget.unit: nullptr, this);

	for (int i = 0; i < projectilesPerShot; ++i) {
		owner->script->Shot(weaponNum);
		// Update Muzzle Piece/Pos
		UpdateWeaponPieces(false); // calls script->QueryWeapon()
		UpdateWeaponVectors();

		Fire(false);
	}

	// Rock the unit in the direction of fire
	if (owner->script->HasRockUnit())
		owner->script->WorldRockUnit((-wantedDir).SafeNormalize2D());

	const bool searchForNewTarget = (salvoLeft == 0) && (currentTarget == owner->curTarget);
	owner->commandAI->WeaponFired(this, searchForNewTarget);

	if (salvoLeft == 0)
		owner->script->EndBurst(weaponNum);
}


bool CWeapon::Attack(const SWeaponTarget& newTarget)
{
	ZoneScoped;
	if (newTarget == currentTarget)
		return true;

	UpdateWeaponVectors();

	switch (newTarget.type) {
		case Target_None: {
			SetAttackTarget(newTarget);
			return true;
		} break;
		case Target_Unit:
		case Target_Pos:
		case Target_Intercept: {
			if (!TryTarget(newTarget))
				return false;

			SetAttackTarget(newTarget);
			avoidTarget = false;
			return true;
		} break;
	};
	return false;
}


void CWeapon::SetAttackTarget(const SWeaponTarget& newTarget)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (newTarget == currentTarget)
		return;

	DropCurrentTarget();
	currentTarget = newTarget;

	if (newTarget.type == Target_Unit)
		AddDeathDependence(newTarget.unit, DEPENDENCE_TARGETUNIT);

	currentTargetPos = GetLeadTargetPos(newTarget);
	UpdateWantedDir();
}


void CWeapon::DropCurrentTarget()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (HaveUnitTarget())
		DeleteDeathDependence(currentTarget.unit, DEPENDENCE_TARGETUNIT);

	currentTarget = SWeaponTarget();
}


bool CWeapon::AllowWeaponAutoTarget() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const int checkAllowed = eventHandler.AllowWeaponTargetCheck(owner->id, weaponNum, weaponDef->id);
	if (checkAllowed >= 0)
		return checkAllowed;

	//FIXME these need to be merged
	if (weaponDef->noAutoTarget || noAutoTarget)
		return false;
	if (owner->fireState < FIRESTATE_FIREATWILL)
		return false;
	if (slavedTo != nullptr)
		return false;
	if (weaponDef->interceptor)
		return false;

	// if CAI has an auto-generated attack order, do not interfere
	if (!owner->commandAI->CanWeaponAutoTarget(this))
		return false;

	if (!HaveTarget())
		return true;
	if (avoidTarget)
		return true;

	if (HaveUnitTarget()) {
		if (!TryTarget(SWeaponTarget(currentTarget.unit, currentTarget.isUserTarget))) {
			// if we have a user-target (ie. a user attack order)
			// then only allow generating opportunity targets iff
			// it is not possible to hit the user's chosen unit
			// TODO: this makes it easy to add toggle-able locking
			//
			// this will switch <targetUnit>, but the CAI will keep
			// calling AttackUnit while the original order target is
			// alive to put it back when possible
			//
			// note that the CAI itself only auto-picks a target
			// when a unit has no commands left in its queue, so
			// it can not interfere
			return true;
		}
		if (!currentTarget.isUserTarget) {
			if (currentTarget.unit->category & badTargetCategory)
				return true;
		}
	}

	if (currentTarget.isUserTarget)
		return false;

	return (gs->frameNum > (lastTargetRetry + 65));
}

bool CWeapon::AutoTarget()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!AllowWeaponAutoTarget())
		return false;

	// search for other in-range targets
	lastTargetRetry = gs->frameNum;

	const CUnit* avoidUnit = (avoidTarget && HaveUnitTarget()) ? currentTarget.unit : nullptr;

	CUnit* goodTargetUnit = nullptr;
	CUnit*  badTargetUnit = nullptr;

	auto& targetPairs = helper->targetPairs;

	// NOTE:
	//   GenerateWeaponTargets sorts by INCREASING order of priority, so lower equals better
	//   <targetPairs> is normally sorted such that all bad TargetCategory units live at the
	//   end, but Lua can mess with the ordering arbitrarily
	for (size_t i = 0, n = CGameHelper::GenerateWeaponTargets(this, avoidUnit, targetPairs); i < n; i++, assert(n == targetPairs.size())) {
		CUnit* unit = targetPairs[i].second;

		// save the "best" bad target in case we have no other
		// good targets (of higher priority) left in <targets>
		const bool isBadTarget = (unit->category & badTargetCategory);

		if (isBadTarget && (badTargetUnit != nullptr))
			continue;

		// set isAutoTarget s.t. TestRange result is ignored
		// (which enables pre-aiming at targets out of range)
		if (!TryTarget(SWeaponTarget(unit, false, autoTargetRangeBoost > 0.0f)))
			continue;

		if (unit->IsNeutral() && (owner->fireState < FIRESTATE_FIREATNEUTRAL))
			continue;

		if (isBadTarget) {
			badTargetUnit = unit;
			continue;
		}

		goodTargetUnit = unit;
		break;
	}

	if (goodTargetUnit == nullptr)
		goodTargetUnit = badTargetUnit;

	if (goodTargetUnit != nullptr) {
		// pick our new target
		SetAttackTarget(SWeaponTarget(goodTargetUnit));
		return true;
	}

	return false;
}


void CWeapon::SlowUpdate()
{
	RECOIL_DETAILED_TRACY_ZONE;
	errorVectorAdd = (gsRNG.NextVector() - errorVector) * (1.0f / UNIT_SLOWUPDATE_RATE);
	predictSpeedMod = 1.0f + (gsRNG.NextFloat() - 0.5f) * 2 * ExperienceErrorScale();

	UpdateWeaponPieces();
	UpdateWeaponVectors();

	// HoldFire: if Weapon Target isn't valid
	HoldIfTargetInvalid();

	// SlavedWeapon: Update Weapon Target
	if (slavedTo != nullptr) {
		// clone targets from the weapon we are slaved to
		SetAttackTarget(slavedTo->currentTarget);
	} else
	if (weaponDef->interceptor) {
		// keep track of the closest projectile heading our way (if any)
		UpdateInterceptTarget();
	} else
	if (owner->curTarget.type != Target_None) {
		// If unit got an attack target, clone the job (independent of AutoTarget!)
		// Also do this unconditionally (owner's target always has priority over weapon one!)
		Attack(owner->curTarget);
	} else
	if (!HaveTarget() && owner->lastAttacker != nullptr && owner->fireState == FIRESTATE_RETURNFIRE) {
		//Try to return fire
		Attack(owner->lastAttacker);
	}
	// AutoTarget: Find new/better Target
	AutoTarget();
}


void CWeapon::HoldIfTargetInvalid()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!HaveTarget())
		return;

	if (!TryTarget(currentTarget)) {
		DropCurrentTarget();
		return;
	}
}


void CWeapon::DependentDied(CObject* o)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (o == currentTarget.unit)      { DropCurrentTarget(); }
	if (o == currentTarget.intercept) { DropCurrentTarget(); }

	// NOTE: DependentDied is called from ~CObject-->Detach, object is just barely valid
	if (weaponDef->interceptor || weaponDef->isShield) {
		spring::VectorErase(incomingProjectileIDs, static_cast<CWeaponProjectile*>(o)->id);
	}
}


bool CWeapon::TargetUnderWater(const float3 tgtPos, const SWeaponTarget& target)
{
	RECOIL_DETAILED_TRACY_ZONE;
	switch (target.type) {
		case Target_None: return false;
		case Target_Unit: return target.unit->IsUnderWater();
		case Target_Pos:  return (tgtPos.y < 0.0f); // consistent with CSolidObject::IsUnderWater (LT)
		case Target_Intercept: return (target.intercept->pos.y < 0.0f);
		default: return false;
	}
}


bool CWeapon::TargetInWater(const float3 tgtPos, const SWeaponTarget& target)
{
	RECOIL_DETAILED_TRACY_ZONE;
	switch (target.type) {
		case Target_None: return false;
		case Target_Unit: return target.unit->IsInWater();
		case Target_Pos:  return (tgtPos.y <= 0.0f); // consistent with CSolidObject::IsInWater (LE)
		case Target_Intercept: return (target.intercept->pos.y <= 0.0f);
		default: return false;
	}
}


bool CWeapon::CheckTargetAngleConstraint(const float3 worldTargetDir, const float3 worldWeaponDir) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (onlyForward) {
		if (maxForwardAngleDif > -1.0f) {
			// if we are not a turret, we care about our owner's direction
			if (owner->frontdir.dot(worldTargetDir) < maxForwardAngleDif)
				return false;
		}
	} else {
		if (maxMainDirAngleDif > -1.0f) {
			if (worldWeaponDir.dot(worldTargetDir) < maxMainDirAngleDif)
				return false;
		}
	}

	return true;
}


float3 CWeapon::GetTargetBorderPos(
	const CUnit* targetUnit,
	const float3 rawTargetPos,
	const float3 rawTargetDir
) const {
	RECOIL_DETAILED_TRACY_ZONE;
	float3 targetBorderPos = rawTargetPos;

	if (weaponDef->targetBorder == 0.0f)
		return targetBorderPos;
	if (targetUnit == nullptr)
		return targetBorderPos;
	if (rawTargetDir == ZeroVector)
		return targetBorderPos;

	const float tbScale = math::fabsf(weaponDef->targetBorder);

	CollisionVolume  tmpColVol = targetUnit->collisionVolume;
	CollisionQuery   tmpColQry;

	// test for "collision" with a temporarily volume
	// (scaled uniformly by the absolute target-border
	// factor)
	tmpColVol.RescaleAxes(float3(tbScale, tbScale, tbScale));
	tmpColVol.SetBoundingRadius();
	tmpColVol.SetUseContHitTest(false);

	// the DetectHit() code below clearly indicates it should go
	// CCollisionHandler::Collision() branch so force it explicitly
	tmpColVol.SetDefaultToPieceTree(false);
	tmpColVol.SetIgnoreHits(false);

	// our weapon muzzle is inside the target unit's volume (FIXME: use aimFromPos?)
	if (CCollisionHandler::DetectHit(targetUnit, &tmpColVol, targetUnit->GetTransformMatrix(true), weaponMuzzlePos, ZeroVector, nullptr))
		return (targetBorderPos = weaponMuzzlePos);

	// otherwise, perform a raytrace to find the proper length correction
	// factor for non-spherical coldet volumes based on the ray's ingress
	// (for positive TB values) or egress (for negative TB values) position;
	// this either increases or decreases the length of <targetVec> but does
	// not change its direction
	tmpColVol.SetUseContHitTest(true);
	tmpColVol.SetDefaultToPieceTree(targetUnit->collisionVolume.DefaultToPieceTree());
	tmpColVol.SetIgnoreHits(targetUnit->collisionVolume.IgnoreHits());

	// make the ray-segment long enough so it can reach the far side of the
	// scaled collision volume (helps to ensure a ray-intersection is found)
	//
	// note: ray-intersection is NOT guaranteed if the volume itself has a
	// non-zero offset, since here we are "shooting" at the target UNIT's
	// aimpoint
	const float3 targetOffset = rawTargetDir * (tmpColVol.GetBoundingRadius() * 2.0f);
	const float3 targetRayPos = rawTargetPos + targetOffset;

	// adjust the length of <targetVec> based on the targetBorder factor
	// the muzzle position must not be inside tmpColVol for this to work
	if (CCollisionHandler::DetectHit(targetUnit, &tmpColVol, targetUnit->GetTransformMatrix(true), weaponMuzzlePos, targetRayPos, &tmpColQry) && tmpColQry.AllHit())
		targetBorderPos = mix(tmpColQry.GetIngressPos(), tmpColQry.GetEgressPos(), weaponDef->targetBorder <= 0.0f);

	return targetBorderPos;
}


bool CWeapon::TryTarget(const float3 tgtPos, const SWeaponTarget& trg, bool preFire) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(GetLeadTargetPos(trg).SqDistance(tgtPos) < Square(250.0f));

	const bool dbgTT = (gs->frameNum >= 28140 && gs->frameNum <= 28200 && preFire);

	if (!TestTarget(tgtPos, trg)) {
		if (dbgTT) LOG("[TryTarget] f=%d unit=%d FAIL=TestTarget", gs->frameNum, owner->id);
		return false;
	}

	// auto-targeted units are allowed to be out of range
	// (UpdateFire will still block firing at such units)
	if (!trg.isAutoTarget && !TestRange(tgtPos, trg)) {
		if (dbgTT) {
			const float heightDiff = tgtPos.y - aimFromPos.y;
			const float targetDist = aimFromPos.SqDistance2D(tgtPos);
			float wRange = 0.0f;
			if (trg.type == Target_Pos || weaponDef->cylinderTargeting < 0.01f) {
				wRange = GetRange2D(0.0f, heightDiff * weaponDef->heightmod);
			} else {
				if ((weaponDef->cylinderTargeting * range) > (math::fabsf(heightDiff) * weaponDef->heightmod))
					wRange = GetRange2D(0.0f, 0.0f);
			}
			LOG("[TryTarget] f=%d unit=%d FAIL=TestRange targetDist=%a wRange2=%a diff=%a",
				gs->frameNum, owner->id, targetDist, wRange * wRange,
				targetDist - wRange * wRange);
		}
		return false;
	}

	// no LOF if aim-position is below ground (not in HFLOF, is overridden)
	if (preFire && (weaponMuzzlePos.y < CGround::GetHeightReal(weaponMuzzlePos.x, weaponMuzzlePos.z))) {
		if (dbgTT) LOG("[TryTarget] f=%d unit=%d FAIL=GroundCheck muzzleY=%a groundH=%a",
			gs->frameNum, owner->id, weaponMuzzlePos.y,
			CGround::GetHeightReal(weaponMuzzlePos.x, weaponMuzzlePos.z));
		return false;
	}

	const bool lofResult = HaveFreeLineOfFire(GetAimFromPos(preFire), tgtPos, trg);
	if (dbgTT) {
		LOG("[TryTarget] f=%d unit=%d LOF=%d aimFrom=(%a,%a,%a) tgt=(%a,%a,%a)",
			gs->frameNum, owner->id, (int)lofResult,
			GetAimFromPos(preFire).x, GetAimFromPos(preFire).y, GetAimFromPos(preFire).z,
			tgtPos.x, tgtPos.y, tgtPos.z);
	}

	// TODO: add a forcedUserTarget (forced-fire mode enabled with CTRL e.g.) and skip the tests below
	return lofResult;
}


bool CWeapon::TestTarget(const float3 tgtPos, const SWeaponTarget& trg) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if ((trg.isManualFire != weaponDef->manualfire) && owner->unitDef->canManualFire)
		return false;

	switch (trg.type) {
		case Target_None: {
			return true;
		} break;
		case Target_Unit: {
			if (trg.unit == owner || trg.unit == nullptr)
				return false;
			if ((trg.unit->category & onlyTargetCategory) == 0)
				return false;
			if (trg.unit->isDead && !modInfo.fireAtKilled)
				return false;
			if (trg.unit->IsCrashing() && !modInfo.fireAtCrashing)
				return false;
			if ((trg.unit->losStatus[owner->allyteam] & (LOS_INLOS | LOS_INRADAR)) == 0)
				return false;
			if (!trg.isUserTarget && trg.unit->IsNeutral() && owner->fireState < FIRESTATE_FIREATNEUTRAL)
				return false;
			// don't fire at allied targets
			if (!trg.isUserTarget && teamHandler.Ally(owner->allyteam, trg.unit->allyteam))
				return false;

			if (trg.unit->GetTransporter() != nullptr) {
				if (!modInfo.targetableTransportedUnits)
					return false;
				// the transportee might be "hidden" below terrain, in which case we can't target it
				if (trg.unit->pos.y < CGround::GetHeightReal(trg.unit->pos.x, trg.unit->pos.z))
					return false;
			}
		} break;
		case Target_Pos: {
			if (!weaponDef->canAttackGround)
				return false;
		} break;
		case Target_Intercept: {
			if (weaponDef->interceptSolo && trg.intercept->IsBeingIntercepted())
				return false;
			if (!weaponDef->interceptor)
				return false;
			if (!trg.intercept->CanBeInterceptedBy(weaponDef))
				return false;
		} break;
		default: break;
	}

	// interceptor can only target projectiles!
	if (trg.type != Target_Intercept && weaponDef->interceptor)
		return false;

	// water weapon checks
	if (!weaponDef->waterweapon) {
		// we cannot pick targets underwater, check where target is in relation to us
		if (!owner->IsUnderWater() && TargetUnderWater(tgtPos, trg))
			return false;
		// if we are underwater but target is *not* in water, fireSubmersed gets checked
		if (owner->IsUnderWater() && TargetInWater(tgtPos, trg))
			return false;
	}

	return true;
}

bool CWeapon::TestRange(const float3 tgtPos, const SWeaponTarget& trg) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float heightDiff = tgtPos.y - aimFromPos.y;
	const float targetDist = aimFromPos.SqDistance2D(tgtPos);

	float weaponRange = 0.0f; // range modified by heightDiff and cylinderTargeting

	if (trg.type == Target_Pos || weaponDef->cylinderTargeting < 0.01f) {
		// check range in a sphere (with extra radius <heightDiff * heightMod>)
		weaponRange = GetRange2D(0.0f, heightDiff * weaponDef->heightmod);
	} else {
		// check range in a cylinder (with height <cylinderTargeting * range>)
		if ((weaponDef->cylinderTargeting * range) > (math::fabsf(heightDiff) * weaponDef->heightmod))
			weaponRange = GetRange2D(0.0f, 0.0f);
	}

	if (targetDist > (weaponRange * weaponRange))
		return false;

	// NOTE: mainDir is in unit-space
	return (CheckTargetAngleConstraint((tgtPos - aimFromPos).SafeNormalize(), owner->GetObjectSpaceVec(mainDir)));
}


bool CWeapon::HaveFreeLineOfFire(const float3 srcPos, const float3 tgtPos, const SWeaponTarget& trg) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	float3 tgtDir = tgtPos - srcPos;

	const float length = tgtDir.LengthNormalize();
	const float spread = AccuracyExperience() + SprayAngleExperience();

	if (length == 0.0f)
		return true;

	CUnit* unit = nullptr;
	CFeature* feature = nullptr;

	// ground check
	// NOTE:
	//   ballistic weapons (Cannon / Missile icw. trajectoryHeight) override this part,
	//   they rely on TrajectoryGroundCol with an external check for the NOGROUND flag
	if ((avoidFlags & Collision::NOGROUND) == 0) {
		const float gndDst = TraceRay::TraceRay(srcPos, tgtDir, length, ~Collision::NOGROUND, owner, unit, feature);
		const float tgtDst = tgtPos.SqDistance(srcPos + tgtDir * gndDst);

		// true iff ground does not block the ray of length <length> from <srcPos> along <tgtDir>
		if ((gndDst > 0.0f) && (tgtDst > Square(damages->damageAreaOfEffect)))
			return false;

		unit = nullptr;
		feature = nullptr;
	}

	// friendly, neutral & feature check
	// for projectiles that do not or barely spread out with distance
	// this reduces to a ray intersection, which is also more accurate
	// must nerf TraceRay since it scans for enemies and ground if the
	// flags are omitted, unlike TestCone which is restricted to A/N/F
	if (spread < 0.001f)
		return (TraceRay::TraceRay(srcPos, tgtDir, length, avoidFlags | Collision::NOENEMIES | Collision::NOGROUND, owner, unit, feature) >= length);

	return (!TraceRay::TestCone(srcPos, tgtDir, length, spread, owner->allyteam, avoidFlags, owner));
}


bool CWeapon::TryTarget(const SWeaponTarget& trg) const {
	RECOIL_DETAILED_TRACY_ZONE;
	return TryTarget(GetLeadTargetPos(trg), trg);
}


bool CWeapon::TryTargetRotate(const CUnit* unit, bool userTarget, bool manualFire)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float3 tempTargetPos = GetUnitLeadTargetPos(unit);
	const short weaponHeading = GetHeadingFromVector(mainDir.x, mainDir.z);
	const short enemyHeading = GetHeadingFromVector(tempTargetPos.x - aimFromPos.x, tempTargetPos.z - aimFromPos.z);
	SWeaponTarget trg(unit, userTarget);
	trg.isManualFire = manualFire;

	return TryTargetHeading(enemyHeading - weaponHeading, trg);
}


bool CWeapon::TryTargetRotate(float3 pos, bool userTarget, bool manualFire)
{
	RECOIL_DETAILED_TRACY_ZONE;
	AdjustTargetPosToWater(pos, true);
	const short weaponHeading = GetHeadingFromVector(mainDir.x, mainDir.z);
	const short enemyHeading = GetHeadingFromVector(pos.x - aimFromPos.x, pos.z - aimFromPos.z);
	SWeaponTarget trg(pos, userTarget);
	trg.isManualFire = manualFire;

	return TryTargetHeading(enemyHeading - weaponHeading, trg);
}


bool CWeapon::TryTargetHeading(short heading, const SWeaponTarget& trg)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float3 tempfrontdir(owner->frontdir);
	const float3 temprightdir(owner->rightdir);
	const short tempHeading = owner->heading;

	owner->heading = heading;
	owner->frontdir = GetVectorFromHeading(owner->heading);
	owner->rightdir = owner->frontdir.cross(owner->updir);
	UpdateWeaponVectors();

	const bool val = TryTarget(trg);

	owner->frontdir = tempfrontdir;
	owner->rightdir = temprightdir;
	owner->heading = tempHeading;
	UpdateWeaponVectors();

	return val;
}


void CWeapon::Init()
{
	RECOIL_DETAILED_TRACY_ZONE;
	UpdateWeaponPieces();
	UpdateWeaponVectors();

	muzzleFlareSize = std::min(damages->damageAreaOfEffect * 0.2f, std::min(1500.f, damages->GetDefault()) * 0.003f);

	if (weaponDef->interceptor)
		interceptHandler.AddInterceptorWeapon(this);

	if (weaponDef->stockpile) {
		owner->stockpileWeapon = this;
		owner->commandAI->AddStockpileWeapon(this);
	}

	if (weaponDef->isShield) {
		if ((owner->shieldWeapon == nullptr) ||
		    (owner->shieldWeapon->weaponDef->shieldRadius < weaponDef->shieldRadius)) {
			owner->shieldWeapon = this;
		}
	}
}


void CWeapon::Fire(bool scriptCall)
{
	RECOIL_DETAILED_TRACY_ZONE;
	owner->lastFireWeapon = gs->frameNum;

	// target-leading can nudge currentTargetPos into an adjacent quadfield cell
	// such that tracing a ray to it does not touch the cell in which our target
	// unit actually resides
	// to prevent this, temporarily add unit to cell at currentTargetPos as well
	bool qfAddUnit = (HaveUnitTarget() && weaponDef->IsHitScanWeapon());
	bool qfHasUnit = false;

	if (qfAddUnit)
		qfHasUnit = quadField.InsertUnitIf(currentTarget.unit, currentTargetPos);

	FireImpl(scriptCall);

	if (qfHasUnit)
		quadField.RemoveUnitIf(currentTarget.unit, currentTargetPos);

	if (salvoLeft == (salvoSize - 1) || !weaponDef->soundTrigger)
		Channels::Battle->PlayRandomSample(weaponDef->fireSound, owner);
}


void CWeapon::UpdateInterceptTarget()
{
	RECOIL_DETAILED_TRACY_ZONE;
	CWeaponProjectile* newTarget = nullptr;
	float minInterceptTargetDistSq = std::numeric_limits<float>::max();

	if (currentTarget.type == Target_Intercept)
		minInterceptTargetDistSq = aimFromPos.SqDistance(currentTarget.intercept->pos);

	for (const int projID: incomingProjectileIDs) {
		CProjectile* p = projectileHandler.GetProjectileBySyncedID(projID);
		CWeaponProjectile* wp = static_cast<CWeaponProjectile*>(p);

		const float curInterceptTargetDistSq = aimFromPos.SqDistance(wp->pos);

		// set by CWeaponProjectile's ctor when the interceptor fires
		if (weaponDef->interceptSolo && wp->IsBeingIntercepted()) //FIXME add bad target?
			continue;

		if (curInterceptTargetDistSq >= minInterceptTargetDistSq)
			continue;

		minInterceptTargetDistSq = curInterceptTargetDistSq;

		// trigger us to auto-fire at this incoming projectile
		// we do not really need to set targetPos here since it
		// will be read from params.target (GetProjectileParams)
		// when our subclass Fire()'s
		newTarget = wp;
	}

	if (newTarget) {
		DropCurrentTarget();
		currentTarget = SWeaponTarget(newTarget);
	}
}


ProjectileParams CWeapon::GetProjectileParams()
{
	RECOIL_DETAILED_TRACY_ZONE;
	ProjectileParams params;
	params.weaponNum = weaponNum;
	params.owner = owner;
	params.weaponDef = weaponDef;

	switch (currentTarget.type) {
		case Target_None     : {                                          } break;
		case Target_Unit     : { params.target = currentTarget.unit;      } break;
		case Target_Pos      : {                                          } break;
		case Target_Intercept: { params.target = currentTarget.intercept; } break;
	}

	return params;
}



float CWeapon::GetStaticRange2D(const CWeapon* w, const WeaponDef* wd, float modHeightDiff, float modProjGravity)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(w == nullptr);

	float baseRange = wd->range;
	float projSpeed = wd->projectilespeed;

	switch (wd->projectileType) {
		case WEAPON_EXPLOSIVE_PROJECTILE: {
			return (CCannon::GetStaticRange2D({baseRange, modHeightDiff}, {projSpeed, modProjGravity}, {-1.0f, wd->heightBoostFactor}));
		} break;
		case WEAPON_LASER_PROJECTILE: {
			// emulate LaserCannon::UpdateRange
			baseRange = std::max(1.0f, math::floor(baseRange / projSpeed)) * projSpeed;
		} break;
		case WEAPON_STARBURST_PROJECTILE: {
			// emulate StarburstLauncher::GetRange2D
			return (baseRange + modHeightDiff);
		} break;
		default: {
		} break;
	}


	const float rangeSq = baseRange * baseRange;
	const float ydiffSq = Square(modHeightDiff);
	const float    root = rangeSq - ydiffSq;
	return (math::sqrt(std::max(root, 0.0f)));
}


float CWeapon::GetRange2D(float boost, float ydiff) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float rangeSq = Square(range + boost); // c^2 (hyp)
	const float ydiffSq = Square(ydiff); // b^2 (opp)
	const float    root = rangeSq - ydiffSq; // a^2 (adj)
	return (math::sqrt(std::max(root, 0.0f)));
}


bool CWeapon::StopAttackingTargetIf(const std::function<bool(const SWeaponTarget&)>& pred)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!pred(currentTarget))
		return false;

	DropCurrentTarget();
	return true;
}

bool CWeapon::StopAttackingAllyTeam(const int ally)
{
	RECOIL_DETAILED_TRACY_ZONE;
	return (StopAttackingTargetIf([&](const SWeaponTarget& t) { return (t.type == Target_Unit && t.unit->allyteam == ally); }));
}


////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////

// NOTE:
//   GUIHandler places (some) user ground-attack orders on the
//   water surface, others on the ocean floor and in both cases
//   without examining weapon abilities (its logic is "obtuse")
//
//   this inconsistency would be hard(er) to fix on the UI side
//   so we must adjust all such target positions in synced code
//
//   see also CommandAI::AdjustGroundAttackCommand
void CWeapon::AdjustTargetPosToWater(float3& tgtPos, bool attackGround) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!attackGround)
		return;

	tgtPos.y = std::max(tgtPos.y, CGround::GetHeightReal(tgtPos.x, tgtPos.z));
	tgtPos.y = std::max(tgtPos.y, tgtPos.y * weaponDef->waterweapon);

	// prevent range hax in FPS mode
	if (owner->UnderFirstPersonControl() && dynamic_cast<const CCannon*>(this) != nullptr) {
		tgtPos.y = CGround::GetHeightAboveWater(tgtPos.x, tgtPos.z);
	}
}


float3 CWeapon::GetUnitPositionWithError(const CUnit* unit) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	float3 errorPos = unit->GetErrorPos(owner->allyteam, true);
	if (doTargetGroundPos) errorPos -= unit->aimPos - unit->pos;
	const float errorScale = (MoveErrorExperience() * GAME_SPEED * unit->speed.w);
	return errorPos + errorVector * errorScale;
}


float3 CWeapon::GetUnitLeadTargetPos(const CUnit* unit) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float3 tmpTargetPos = GetUnitPositionWithError(unit) + GetLeadVec(unit);
	const float3 tmpTargetDir = (tmpTargetPos - aimFromPos).SafeNormalize();

	float3 aimPos = GetTargetBorderPos(unit, tmpTargetPos, tmpTargetDir);

	// never target below terrain
	// never target below water if not a water-weapon
	aimPos.y = std::max(aimPos.y, CGround::GetApproximateHeight(aimPos.x, aimPos.z) + 2.0f);
	aimPos.y = std::max(aimPos.y, aimPos.y * weaponDef->waterweapon);

	return aimPos;
}


float3 CWeapon::GetLeadVec(const CUnit* unit) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float predictTime = GetPredictedImpactTime(unit->pos);
	const float predictMult = mix(predictSpeedMod, 1.0f, weaponDef->predictBoost);

	float3 lead = unit->speed * predictTime * predictMult;

	if (weaponDef->leadLimit < 0.0f)
		return lead;

	const float leadLenSq = lead.SqLength();
	const float leadBonus = weaponDef->leadLimit + weaponDef->leadBonus * owner->experience;

	if (leadLenSq > Square(leadBonus))
		lead *= (leadBonus / (math::sqrt(leadLenSq) + 0.01f));

	return lead;
}


float CWeapon::ExperienceErrorScale() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	// accuracy (error) is increased (decreased) with experience
	// scale is 1.0f - (limExperience * expAccWeight), such that
	// for weight=0 scale is 1 and for weight=1 scale is 1 - exp
	// (lower is better)
	//
	//   for accWeight=0.00 and {0.25, 0.50, 0.75, 1.0} exp, scale=(1.0 - {0.25*0.00, 0.5*0.00, 0.75*0.00, 1.0*0.00}) = {1.0000, 1.000, 1.0000, 1.00}
	//   for accWeight=0.25 and {0.25, 0.50, 0.75, 1.0} exp, scale=(1.0 - {0.25*0.25, 0.5*0.25, 0.75*0.25, 1.0*0.25}) = {0.9375, 0.875, 0.8125, 0.75}
	//   for accWeight=0.50 and {0.25, 0.50, 0.75, 1.0} exp, scale=(1.0 - {0.25*0.50, 0.5*0.50, 0.75*0.50, 1.0*0.50}) = {0.8750, 0.750, 0.6250, 0.50}
	//   for accWeight=1.00 and {0.25, 0.50, 0.75, 1.0} exp, scale=(1.0 - {0.25*1.00, 0.5*1.00, 0.75*1.00, 1.0*0.75}) = {0.7500, 0.500, 0.2500, 0.25}
	return (CUnit::ExperienceScale(owner->limExperience, weaponDef->ownerExpAccWeight));
}


float CWeapon::MoveErrorExperience() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return (ExperienceErrorScale() * weaponDef->targetMoveError);
}


float3 CWeapon::GetLeadTargetPos(const SWeaponTarget& target) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	switch (target.type) {
		case Target_None:      return currentTargetPos;
		case Target_Unit:      return GetUnitLeadTargetPos(target.unit);
		case Target_Pos: {
			float3 p = target.groundPos;
			AdjustTargetPosToWater(p, true);
			return p;
		} break;
		case Target_Intercept: return target.intercept->pos + target.intercept->speed;
	}

	return currentTargetPos;
}
