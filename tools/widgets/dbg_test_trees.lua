local widget = widget ---@type Widget

function widget:GetInfo()
	return {
		name      = "DBG Test Trees",
		desc      = "Captures both a living and a dead tree feature for comparison.",
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

local TREE_KEYWORDS  = { "tree", "elm", "oak", "pine", "fir", "acacia", "poplar" }
local DEAD_KEYWORDS  = { "dead", "burn", "trunk", "stump", "fallen" }

local function isTreeName(n)
	for _, k in ipairs(TREE_KEYWORDS) do if n:find(k) then return true end end
	return false
end
local function isDeadName(n)
	for _, k in ipairs(DEAD_KEYWORDS) do if n:find(k) then return true end end
	return false
end

local function findTreeFeature(wantAlive)
	local features = Spring.GetAllFeatures()
	for _, fID in ipairs(features) do
		local fdID = Spring.GetFeatureDefID(fID)
		local fd = FeatureDefs and FeatureDefs[fdID]
		if fd then
			local name = lower(fd.name)
			local tree = isTreeName(name)
			local dead = isDeadName(name)
			if tree and ((wantAlive and not dead) or (not wantAlive and dead)) then
				local x, y, z = Spring.GetFeaturePosition(fID)
				if x then return fID, fd.name, x, y, z end
			end
		end
	end
	return nil
end

local function pointCameraAt(x, y, z, height, dist)
	Spring.SetCameraState({
		mode = 4,
		px = x,         py = y + (height or 250),   pz = z + (dist or 200),
		dx = 0,         dy = -0.7,                  dz = -0.7,
		rx = -0.7,      ry = 0,                     rz = 0,
	}, 0)
end

-- Three distances to span the range where the "bright blue trees" bug
-- triggers. Selection test (camera over commander, height=600 dist=500)
-- reproduces blue cleanly; close-up shots look green; very-high shots
-- look black silhouettes. Distance/LOD interaction is suspect.
local jobs = {
	{ alive = true,  label = "live-close",  height = 250,  dist = 200  },
	{ alive = true,  label = "live-mid",    height = 600,  dist = 500  },
	{ alive = true,  label = "live-far",    height = 1200, dist = 900  },
	{ alive = false, label = "dead-mid",    height = 600,  dist = 500  },
}

function widget:GameStart()
	startTime = Spring.GetGameSeconds()
end

function widget:GameFrame(_)
	if not startTime then return end
	local elapsed = Spring.GetGameSeconds() - startTime
	local jobIdx = math.floor(stage / 2) + 1
	local sub    = stage % 2
	local job    = jobs[jobIdx]
	if not job then
		Spring.Echo("[dbg-trees] all jobs done, quitting")
		Spring.SendCommands("quit")
		Spring.SendCommands("quitforce")
		return
	end
	-- Each job runs for ~1.2 s: 0.0s frame the tree, ~0.7s screenshot.
	-- Be aggressive — BAR sometimes triggers an autoquit early.
	local jobStart = (jobIdx - 1) * 1.2 + 0.5
	if elapsed < jobStart then return end

	if sub == 0 then
		local fID, name, x, y, z = findTreeFeature(job.alive)
		if fID then
			Spring.Echo(string.format("[dbg-trees] job %d (%s): %s @ (%.0f,%.0f,%.0f)",
				jobIdx, job.label, name, x, y, z))
			pointCameraAt(x, y, z, job.height, job.dist)
			stage = stage + 1
		else
			Spring.Echo(string.format("[dbg-trees] job %d (%s): no matching tree found", jobIdx, job.label))
			stage = stage + 2  -- skip the screenshot
		end
	elseif sub == 1 and elapsed > jobStart + 0.4 then
		Spring.SendCommands("screenshot dbg-trees-" .. job.label .. ".png")
		Spring.Echo("[dbg-trees] shot taken: " .. job.label)
		stage = stage + 1
	end
end
