local widget = widget ---@type Widget

function widget:GetInfo()
	return {
		name      = "DBG Test Explosions",
		desc      = "Captures the first explosion (natural or self-destruct).",
		author    = "claude",
		date      = "2026-06-21",
		license   = "GPL",
		layer     = -1000,
		enabled   = false,
	}
end

local CMD_SELFD = (CMD and CMD.SELFD) or 65

local startTime = nil
local explosionDetectTime = nil
local explosionWasNatural = false
local screenshotsTaken = {}

local SHOT_OFFSETS = { 0.0, 0.3, 1.0, 2.0, 4.0 }

-- State machine for cheat-spawning a sacrificial unit
local cheatState = 0
local cheatStateTime = 0

local function pointCameraAt(x, y, z)
	-- Roughly 45° angle ~400 above and 400 south. Close enough that a
	-- medium / large unit explosion fills a useful fraction of the frame
	-- but high enough that the resulting scorch + shockwave both fit.
	Spring.SetCameraState({
		mode = 4,
		px = x,         py = y + 400,   pz = z + 400,
		dx = 0,         dy = -0.7,      dz = -0.7,
		rx = -0.7,      ry = 0,         rz = 0,
	}, 0)
end

local function snapAt(t)
	for _, offset in ipairs(SHOT_OFFSETS) do
		local label = string.format("dbg-explosions-%03dms", math.floor(offset * 1000))
		if math.abs(t - offset) < 0.06 and not screenshotsTaken[label] then
			screenshotsTaken[label] = true
			Spring.SendCommands("screenshot " .. label .. ".png")
			Spring.Echo(string.format("[dbg-explosions] %s @ t+%.2fs", label, t))
		end
	end
end

function widget:UnitDestroyed(unitID, unitDefID, unitTeam)
	if explosionDetectTime then return end
	local x, y, z = Spring.GetUnitPosition(unitID)
	if not x then return end
	pointCameraAt(x, y, z)
	explosionDetectTime = Spring.GetGameSeconds()
	Spring.Echo(string.format(
		"[dbg-explosions] %s explosion at (%.0f,%.0f,%.0f) (unit %d)",
		explosionWasNatural and "natural" or "forced", x, y, z, unitID))
end

function widget:GameStart()
	startTime = Spring.GetGameSeconds()
	cheatStateTime = startTime
end

function widget:GameFrame(_)
	if not startTime then return end
	local now = Spring.GetGameSeconds()
	local elapsed = now - startTime

	-- Cheat state machine. Each stage waits half a second to make sure
	-- the chat command actually takes effect before the next one.
	if cheatState == 0 and elapsed > 4.0 then
		-- Center camera on our commander before giving units, so the cheat
		-- spawn lands on terrain near us (not at whatever sea coordinate
		-- the default camera happens to look at).
		local myTeam = Spring.GetMyTeamID() or 0
		local units = Spring.GetTeamUnits(myTeam) or {}
		local commander = nil
		for _, u in ipairs(units) do
			local udID = Spring.GetUnitDefID(u)
			local ud = UnitDefs and UnitDefs[udID]
			if ud and ud.canManualFire then    -- canManualFire ≈ commander DGun
				commander = u; break
			end
		end
		if not commander then commander = units[1] end
		if commander then
			local cx, cy, cz = Spring.GetUnitPosition(commander)
			if cx then
				Spring.Echo(string.format("[dbg-explosions] centring camera on commander (%d, %d, %d)", cx, cy, cz))
				pointCameraAt(cx, cy, cz)
			end
		end
		Spring.Echo("[dbg-explosions] step 1: enable cheats")
		Spring.SendCommands("cheat")
		cheatState = 1; cheatStateTime = now
	elseif cheatState == 1 and now - cheatStateTime > 0.5 then
		-- Big-explosion candidates: fusion reactor (stored energy → massive
		-- boom), heavy tank, advanced tank. armfus = Armada Fusion Reactor.
		Spring.Echo("[dbg-explosions] step 2: give a fusion reactor + back-up units")
		Spring.SendCommands("give 1 armfus")
		Spring.SendCommands("give 1 armbull")    -- Bulldog (heavy tank, large explosion)
		Spring.SendCommands("give 1 armham")
		cheatState = 2; cheatStateTime = now
	elseif cheatState == 2 and now - cheatStateTime > 0.5 then
		local myTeam = Spring.GetMyTeamID() or 0
		local units = Spring.GetTeamUnits(myTeam) or {}
		-- Find a small unit (NOT the commander, otherwise game-over fires
		-- via BAR's selfd_resign gadget and the camera goes spectator).
		local target = nil
		local targetName = "?"
		-- Prefer the biggest explosion: fusion reactor > heavy tank > others.
		local prefs = { "armfus", "armbull", "armham" }
		for _, want in ipairs(prefs) do
			for _, u in ipairs(units) do
				local udID = Spring.GetUnitDefID(u)
				local ud = UnitDefs and UnitDefs[udID]
				if ud and ud.name == want then
					target = u; targetName = ud.name; break
				end
			end
			if target then break end
		end
		if not target then
			Spring.Echo("[dbg-explosions] no small unit available — bailing (won't selfd commander)")
			Spring.SendCommands("quit")
			Spring.SendCommands("quitforce")
			return
		end
		Spring.Echo(string.format("[dbg-explosions] step 3: selfd on unit %d (%s)", target, targetName))
		explosionWasNatural = false
		Spring.GiveOrderToUnit(target, CMD_SELFD, {}, 0)
		cheatState = 3
	end

	-- After the explosion is detected, take screenshots
	if explosionDetectTime then
		local t = now - explosionDetectTime
		snapAt(t)
		if t > SHOT_OFFSETS[#SHOT_OFFSETS] + 1.0 then
			Spring.Echo("[dbg-explosions] done, quitting")
			Spring.SendCommands("quit")
			Spring.SendCommands("quitforce")
		end
	elseif elapsed > 40 then
		Spring.Echo("[dbg-explosions] timeout, no explosion captured")
		Spring.SendCommands("quit")
		Spring.SendCommands("quitforce")
	end
end
