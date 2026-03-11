/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// #undef NDEBUG

#include "GeneralMoveSystem.h"

#include "Sim/Ecs/Registry.h"
#include "Sim/MoveTypes/Components/MoveTypesComponents.h"
#include "Sim/MoveTypes/MoveMath/MoveMath.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitHandler.h"

#include "System/EventHandler.h"
#include "System/TimeProfiler.h"
#include "System/Threading/ThreadPool.h"
#include "Sim/Units/UnitDef.h"

#include "System/Misc/TracyDefs.h"

#ifdef SYNCCHECK
#include "System/Sync/SyncChecker.h"
#include <cstdio>
#endif

using namespace MoveTypes;

void GeneralMoveSystem::Init() {
    RECOIL_DETAILED_TRACY_ZONE;
    CMoveMath::InitRangeIsBlockedHashes();
    Sim::systemUtils.OnPostLoad().connect<&CMoveMath::InitRangeIsBlockedHashes>();
}

void GeneralMoveSystem::Update() {
    RECOIL_DETAILED_TRACY_ZONE;
    auto view = Sim::registry.view<GeneralMoveType>();
	{
        SCOPED_TIMER("Sim::Unit::MoveType::5::Update");
#ifdef SYNCCHECK
        const bool doTraceGM = CSyncChecker::IsTracing();
        FILE* tfGM = doTraceGM ? CSyncChecker::GetTraceFile() : nullptr;
        if (tfGM) fprintf(tfGM, "%d GenMS_Update_BEGIN %08x\n", CSyncChecker::GetFrameNum(), CSyncChecker::GetChecksum());
#endif
        view.each([
#ifdef SYNCCHECK
            tfGM
#endif
        ](GeneralMoveType& unitId){
            CUnit* unit = unitHandler.GetUnit(unitId.value);
            AMoveType* moveType = unit->moveType;

            #ifndef NDEBUG
            unit->SanityCheck();
            #endif

#ifdef SYNCCHECK
            unsigned chkBefore = 0;
            if (tfGM) chkBefore = CSyncChecker::GetChecksum();
#endif
            if (moveType->Update())
                eventHandler.UnitMoved(unit);
#ifdef SYNCCHECK
            if (tfGM) {
                unsigned chkAfter = CSyncChecker::GetChecksum();
                if (chkBefore != chkAfter)
                    fprintf(tfGM, "%d GenMS_Update id=%d def=%s %08x->%08x\n",
                        CSyncChecker::GetFrameNum(), unit->id,
                        unit->unitDef->name.c_str(), chkBefore, chkAfter);
            }
#endif

            // this unit is not coming back, kill it now without any death
            // sequence (s.t. deathScriptFinished becomes true immediately)
            if (!unit->pos.IsInBounds() && (unit->speed.w > MAX_UNIT_SPEED))
                unit->ForcedKillUnit(nullptr, false, true, -CSolidObject::DAMAGE_KILLED_OOB);

            #ifndef NDEBUG
            unit->SanityCheck();
            #endif
        });
	}
}

void GeneralMoveSystem::Shutdown() {
    Sim::systemUtils.OnPostLoad().disconnect<&CMoveMath::InitRangeIsBlockedHashes>();
}
