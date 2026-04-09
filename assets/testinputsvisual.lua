-- Visual + audio + collision test driven by the test-input coroutine.
-- Run with:
--   game run --test -- testinputsvisual
--
-- The game sets up three colored trigger zones around a player. Each zone
-- plays a unique sound on enter. The test coroutine drives the player through
-- all three zones using injected key presses, then asserts that every zone
-- was visited and every sound fired.

local M = {}

local CAT_PLAYER = 1
local CAT_TRIGGER = 2

local SPEED = 220
local PLAYER_R = 18

local zones = {}
local visited = {}
local trigger_events = {}

function M:init()
	G.window.set_title("Visual Input Test")
	local W, H = G.window.dimensions()

	self.W = W
	self.H = H

	G.sound.add_effect("laser.ogg")
	G.sound.add_effect("gunshot.ogg")
	G.sound.add_effect("pong-blip1.ogg")

	self.world = G.collision.new_world(64)

	self.player_shape = G.collision.circle(PLAYER_R)
	self.player = self.world:add(self.player_shape, W / 2, H / 2, {
		category = CAT_PLAYER,
		mask = CAT_TRIGGER,
		userdata = "player",
	})

	zones = {
		{ name = "red", x = W * 0.2, y = H * 0.5, r = 50, sound = "laser.ogg", color = { 220, 60, 60 } },
		{ name = "green", x = W * 0.8, y = H * 0.5, r = 50, sound = "gunshot.ogg", color = { 60, 220, 60 } },
		{ name = "blue", x = W * 0.5, y = H * 0.2, r = 50, sound = "pong-blip1.ogg", color = { 60, 120, 220 } },
	}

	for _, z in ipairs(zones) do
		local shape = G.collision.circle(z.r)
		z.handle = self.world:add(shape, z.x, z.y, {
			category = CAT_TRIGGER,
			mask = CAT_PLAYER,
			trigger = true,
			userdata = z.name,
		})
	end

	self.world:on_trigger_enter(function(a, b)
		for _, z in ipairs(zones) do
			if z.handle == a or z.handle == b then
				visited[z.name] = true
				table.insert(trigger_events, z.name)
				G.sound.play_effect(z.sound)
			end
		end
	end)
end

function M:update(t, dt)
	local vx, vy = 0, 0
	if G.input.is_key_down("right") then
		vx = SPEED
	end
	if G.input.is_key_down("left") then
		vx = -SPEED
	end
	if G.input.is_key_down("down") then
		vy = SPEED
	end
	if G.input.is_key_down("up") then
		vy = -SPEED
	end

	if vx ~= 0 or vy ~= 0 then
		self.world:move_and_slide(self.player, vx * dt, vy * dt)
	end
	self.world:update()
end

function M:draw()
	G.graphics.clear()

	for _, z in ipairs(zones) do
		local r, g, b = z.color[1], z.color[2], z.color[3]
		local alpha = visited[z.name] and 200 or 80
		G.graphics.set_color(r, g, b, alpha)
		G.graphics.draw_circle(z.x, z.y, z.r)
		G.graphics.set_color("white")
		G.graphics.print(z.name, z.x - 16, z.y - 6)
	end

	local px, py = self.world:get_position(self.player)
	G.graphics.set_color("white")
	G.graphics.draw_circle(px, py, PLAYER_R)

	G.graphics.print("Visited: " .. (#trigger_events) .. " / " .. #zones, 10, 10)
end

-- Holds a key down for `frames` frames, releasing it afterward. Yields the
-- coroutine for the duration so the engine sees movement each frame.
local function press_for(key, frames)
	G.test.key_down(key)
	G.test.wait_frames(frames)
	G.test.key_up(key)
	G.test.wait_frames(1)
end

function M:test_inputs()
	print("testinputsvisual: starting")

	-- Wait one frame so init has produced the first valid frame.
	G.test.wait_frames(1)

	-- Move left into the red zone.
	press_for("left", 90)
	G.test.assert_true(visited["red"], "expected to enter red zone")

	-- Cross to the right zone (twice the distance).
	press_for("right", 180)
	G.test.assert_true(visited["green"], "expected to enter green zone")

	-- Now move up-left toward the blue zone.
	G.test.key_down("left")
	G.test.key_down("up")
	G.test.wait_frames(120)
	G.test.key_up("left")
	G.test.key_up("up")
	G.test.wait_frames(2)
	G.test.assert_true(visited["blue"], "expected to enter blue zone")

	G.test.assert_true(#trigger_events >= 3, "expected at least 3 trigger events")

	print("testinputsvisual: PASS, events = " .. table.concat(trigger_events, ", "))
end

return M
