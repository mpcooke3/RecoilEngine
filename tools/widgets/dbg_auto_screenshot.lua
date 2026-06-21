local widget = widget ---@type Widget

function widget:GetInfo()
	return {
		name      = "Auto Screenshot (mac debug)",
		desc      = "Takes engine screenshots on a timer; for the self-test harness.",
		author    = "claude",
		date      = "2026-06-21",
		license   = "GPL",
		layer     = -1000,
		enabled   = true,
	}
end

local shots = {
	{ delay = 1,  label = "01-spawn-instant"      },
	{ delay = 3,  label = "02-spawn-3s"           },
	{ delay = 7,  label = "03-spawn-7s-mid-life"  },
	{ delay = 14, label = "04-spawn-14s-fading"   },
}

-- Also auto-quit after the last shot + a bit, so the harness doesn't
-- need to SIGTERM (cleaner shutdown = full log flush).
local quitAt = shots[#shots].delay + 3

local startTime = nil
local taken = {}

function widget:GameStart()
	startTime = Spring.GetGameSeconds()
	Spring.SendCommands("luaui say widget Auto Screenshot armed at game start")
end

function widget:GameFrame(frameNum)
	if not startTime then return end
	local now = Spring.GetGameSeconds()
	local elapsed = now - startTime
	for i, shot in ipairs(shots) do
		if not taken[i] and elapsed >= shot.delay then
			taken[i] = true
			Spring.SendCommands("screenshot " .. shot.label .. ".png")
			Spring.Echo(string.format("[auto-screenshot] %s @ t=%.1fs", shot.label, elapsed))
		end
	end
	if elapsed >= quitAt then
		Spring.Echo("[auto-screenshot] all shots taken, quitting")
		Spring.SendCommands("quit")
		Spring.SendCommands("quitforce")
	end
end
