local widget = widget ---@type Widget

function widget:GetInfo()
	return {
		name      = "DBG Test Trees",
		desc      = "Flies camera to the first tree feature, screenshots, quits.",
		author    = "claude",
		date      = "2026-06-21",
		license   = "GPL",
		layer     = -1000,
		enabled   = false,
	}
end

local startTime = nil
local stage = 0

local function lower(s) return s and s:lower() or "" end

local function findTreeFeature()
	-- Spring exposes FeatureDefs as a table; FeatureDefs[fdID].name like
	-- "tree_elm_dead_01" or similar.
	local features = Spring.GetAllFeatures()
	for _, fID in ipairs(features) do
		local fdID = Spring.GetFeatureDefID(fID)
		local fd = FeatureDefs and FeatureDefs[fdID]
		if fd then
			local name = lower(fd.name)
			if name:find("tree") or name:find("elm") or name:find("oak") or name:find("pine") then
				local x, y, z = Spring.GetFeaturePosition(fID)
				if x then
					return fID, fd.name, x, y, z
				end
			end
		end
	end
	return nil
end

function widget:GameStart()
	startTime = Spring.GetGameSeconds()
end

function widget:GameFrame(_)
	if not startTime then return end
	local elapsed = Spring.GetGameSeconds() - startTime
	if stage == 0 and elapsed > 1 then
		local fID, name, x, y, z = findTreeFeature()
		if fID then
			Spring.Echo(string.format("[dbg-trees] camera on %s @ (%.0f,%.0f,%.0f)", name, x, y, z))
			-- Position camera ~ 250 above and 200 north of the tree, looking down at it.
			Spring.SetCameraState({
				mode = 4,                       -- TA / free style
				px = x,         py = y + 250,   pz = z + 200,
				dx = 0,         dy = -0.7,      dz = -0.7,
				rx = -0.7,      ry = 0,         rz = 0,
			}, 0)
			stage = 1
		else
			Spring.Echo("[dbg-trees] no tree feature found")
			stage = 99
		end
	elseif stage == 1 and elapsed > 3 then
		Spring.SendCommands("screenshot dbg-trees-01-overhead.png")
		stage = 2
	elseif stage == 2 and elapsed > 5 then
		-- second angle: ground-level looking at trees
		Spring.SetCameraState({
			mode = 4,
			py = (Spring.GetCameraState() or {}).py - 200,
		}, 0)
		Spring.SendCommands("screenshot dbg-trees-02-closer.png")
		stage = 3
	elseif (stage == 3 and elapsed > 7) or (stage == 99 and elapsed > 3) then
		Spring.Echo("[dbg-trees] done, quitting")
		Spring.SendCommands("quit")
		Spring.SendCommands("quitforce")
		stage = 100
	end
end
