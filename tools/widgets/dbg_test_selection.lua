local widget = widget ---@type Widget

function widget:GetInfo()
	return {
		name      = "DBG Test Selection",
		desc      = "Cheat-spawns a small group of units, then captures (a) " ..
		            "unselected baseline and (b) all-selected, so the selection " ..
		            "indicator (or lack thereof) is visible side-by-side.",
		author    = "claude",
		date      = "2026-06-21",
		license   = "GPL",
		layer     = -1000,
		enabled   = false,
	}
end

local startTime = nil
local stage = 0
local stageTime = 0
local cameraCentre = { 0, 0, 0 }
local givenUnits = {}

local function nextStage(t)
	stage = stage + 1
	stageTime = t
end

local function pointCameraAt(x, y, z)
	-- ~600 above and 500 south, looking down at -0.7. Wide enough to fit
	-- a ~5-unit cluster comfortably so any selection rings / boxes are visible.
	Spring.SetCameraState({
		mode = 4,
		px = x,      py = y + 600,    pz = z + 500,
		dx = 0,      dy = -0.7,       dz = -0.7,
		rx = -0.7,   ry = 0,          rz = 0,
	}, 0)
end

function widget:GameStart()
	startTime = Spring.GetGameSeconds()
	stageTime = startTime
end

local function recordGivenUnits()
	local myTeam = Spring.GetMyTeamID() or 0
	local units = Spring.GetTeamUnits(myTeam) or {}
	local out = {}
	for _, u in ipairs(units) do
		local udID = Spring.GetUnitDefID(u)
		local ud   = UnitDefs and UnitDefs[udID]
		if ud and ud.name then
			-- skip the commander; we want the cheat-given grunts.
			if not ud.canManualFire then
				table.insert(out, u)
			end
		end
	end
	return out
end

function widget:GameFrame(_)
	if not startTime then return end
	local now = Spring.GetGameSeconds()
	local elapsed = now - startTime
	local sinceStage = now - stageTime

	if stage == 0 and elapsed > 3.0 then
		-- Centre on the commander so the cheat-give puts units near us.
		local myTeam = Spring.GetMyTeamID() or 0
		local units = Spring.GetTeamUnits(myTeam) or {}
		local cmdr = units[1]
		if cmdr then
			local x, y, z = Spring.GetUnitPosition(cmdr)
			cameraCentre = { x, y, z }
			pointCameraAt(x, y, z)
		end
		Spring.SendCommands("cheat")
		nextStage(now)
	elseif stage == 1 and sinceStage > 0.5 then
		Spring.Echo("[dbg-selection] giving cluster of test units")
		Spring.SendCommands("give 5 armpw")        -- pawns (small bots)
		Spring.SendCommands("give 3 armrock")      -- rockos (rocket bots)
		Spring.SendCommands("give 2 armham")       -- hammers (heavier bots)
		nextStage(now)
	elseif stage == 2 and sinceStage > 0.5 then
		-- Step back from the commander slightly so the new units fit in frame
		pointCameraAt(cameraCentre[1], cameraCentre[2], cameraCentre[3])
		givenUnits = recordGivenUnits()
		Spring.Echo(string.format("[dbg-selection] recorded %d non-commander units", #givenUnits))
		nextStage(now)
	elseif stage == 3 and sinceStage > 0.5 then
		-- Baseline: clear selection and capture
		Spring.SelectUnitMap({}, false)
		Spring.SendCommands("screenshot dbg-selection-1-unselected.png")
		Spring.Echo("[dbg-selection] shot: unselected")
		nextStage(now)
	elseif stage == 4 and sinceStage > 0.8 then
		-- Select all the cheat-given units
		Spring.SelectUnitArray(givenUnits, false)
		Spring.SendCommands("screenshot dbg-selection-2-all-selected.png")
		Spring.Echo(string.format("[dbg-selection] shot: all-selected (%d units)", #givenUnits))
		nextStage(now)
	elseif stage == 5 and sinceStage > 0.8 then
		-- Single-unit selection (often handled differently visually)
		if #givenUnits > 0 then
			Spring.SelectUnitArray({ givenUnits[1] }, false)
		end
		Spring.SendCommands("screenshot dbg-selection-3-one-selected.png")
		Spring.Echo("[dbg-selection] shot: one-selected")
		nextStage(now)
	elseif stage == 6 and sinceStage > 0.8 then
		Spring.Echo("[dbg-selection] done, quitting")
		Spring.SendCommands("quit")
		Spring.SendCommands("quitforce")
		nextStage(now)
	elseif elapsed > 30 then
		Spring.Echo("[dbg-selection] timeout, quitting")
		Spring.SendCommands("quit")
		Spring.SendCommands("quitforce")
	end
end
