-- Test for physics expansion: kinematic bodies, raycasts, and world config.
--
-- Demonstrates:
--   1. Kinematic body (moving platform that oscillates left-right)
--   2. Static body (floor)
--   3. Dynamic body (ball that falls under gravity and bounces)
--   4. Raycast from the ball downward to measure distance to ground
--   5. World gravity configuration (sideview mode)
--   6. Iteration count tuning
--
-- Controls:
--   G: toggle gravity (0 vs 500 px/s^2)
--   R: reset ball to starting position
--   Space: apply upward impulse to ball
--   Left/Right: nudge ball horizontally
--   Q: quit

local Game = {}

local W, H
local ball, platform, floor_body
local ray_hit = nil
local gravity_on = true
local frame = 0

local BALL_RADIUS = 15
local BALL_START_X, BALL_START_Y
local PLATFORM_W, PLATFORM_H = 200, 20
local PLATFORM_SPEED = 120
local GRAVITY = 500

function Game:init()
	G.window.set_title("Physics Expansion Test")
	W, H = G.window.dimensions()

	BALL_START_X = W / 2
	BALL_START_Y = H / 4

	G.physics.set_collision_categories({ "ball", "platform", "floor" })

	-- Sideview gravity: objects fall downward.
	G.physics.set_gravity(0, GRAVITY)

	-- Slightly higher solver iterations for better stacking.
	G.physics.set_iterations(8, 3)

	-- Ground body required before adding shapes.
	G.physics.create_ground(false)

	-- Floor: static box along the bottom.
	local floor_ud = { kind = "floor" }
	floor_body = G.physics.add_box(0, H - 30, W, H, 0, floor_ud, {
		body_type = "static",
		restitution = 0.3,
		category = "floor",
		collides_with = { "ball" },
	})

	-- Moving platform: kinematic box that oscillates horizontally.
	local px = W / 2 - PLATFORM_W / 2
	local py = H * 0.6
	local platform_ud = { kind = "platform" }
	platform = {
		handle = G.physics.add_box(px, py, px + PLATFORM_W, py + PLATFORM_H, 0, platform_ud, {
			body_type = "kinematic",
			restitution = 0.2,
			category = "platform",
			collides_with = { "ball" },
		}),
		dir = 1,
	}
	G.physics.set_linear_velocity(platform.handle, PLATFORM_SPEED, 0)

	-- Ball: dynamic circle that falls and bounces.
	local ball_ud = { kind = "ball" }
	ball = {
		handle = G.physics.add_circle(BALL_START_X, BALL_START_Y, BALL_RADIUS, ball_ud, {
			restitution = 0.6,
			density = 1.0,
			category = "ball",
			collides_with = { "platform", "floor" },
		}),
	}

	G.physics.on_begin_contact(function(a, b)
		if a == nil or b == nil then return end
		local ak = type(a) == "table" and a.kind or "?"
		local bk = type(b) == "table" and b.kind or "?"
		print(string.format("[testphysics2] contact: %s <-> %s", ak, bk))
	end)

	print("[testphysics2] initialized")
	local gx, gy = G.physics.gravity()
	print(string.format("[testphysics2] gravity = %.0f, %.0f", gx, gy))
	print(string.format("[testphysics2] ppm = %.0f", G.physics.pixels_per_meter()))
end

function Game:update(t, dt)
	frame = frame + 1

	if G.input.is_key_pressed("q") then
		G.system.quit()
		return
	end

	-- Toggle gravity.
	if G.input.is_key_pressed("g") then
		gravity_on = not gravity_on
		if gravity_on then
			G.physics.set_gravity(0, GRAVITY)
		else
			G.physics.set_gravity(0, 0)
		end
	end

	-- Reset ball.
	if G.input.is_key_pressed("r") then
		G.physics.set_position(ball.handle, BALL_START_X, BALL_START_Y)
		G.physics.set_linear_velocity(ball.handle, 0, 0)
		G.physics.set_angular_velocity(ball.handle, 0)
	end

	-- Impulse upward.
	if G.input.is_key_pressed("space") then
		G.physics.apply_linear_impulse(ball.handle, 0, -8)
	end

	-- Horizontal nudge.
	if G.input.is_key_down("left") then
		G.physics.apply_force(ball.handle, -200, 0)
	end
	if G.input.is_key_down("right") then
		G.physics.apply_force(ball.handle, 200, 0)
	end

	-- Oscillate the kinematic platform.
	local px, py = G.physics.position(platform.handle)
	if px > W - PLATFORM_W / 2 then
		platform.dir = -1
		G.physics.set_linear_velocity(platform.handle, -PLATFORM_SPEED, 0)
	elseif px < PLATFORM_W / 2 then
		platform.dir = 1
		G.physics.set_linear_velocity(platform.handle, PLATFORM_SPEED, 0)
	end

	-- Raycast downward from the ball.
	local bx, by = G.physics.position(ball.handle)
	ray_hit = G.physics.raycast(bx, by, bx, by + 2000)
end

function Game:draw()
	G.graphics.clear(0.1, 0.1, 0.15, 1)

	-- Draw floor.
	G.graphics.set_color(100, 100, 100, 255)
	G.graphics.draw_rect(0, H - 30, W, H)

	-- Draw platform.
	local px, py = G.physics.position(platform.handle)
	G.graphics.set_color(80, 180, 255, 255)
	G.graphics.draw_rect(
		px - PLATFORM_W / 2, py - PLATFORM_H / 2,
		px + PLATFORM_W / 2, py + PLATFORM_H / 2
	)

	-- Draw ball.
	local bx, by = G.physics.position(ball.handle)
	G.graphics.set_color(255, 200, 80, 255)
	G.graphics.draw_circle(bx, by, BALL_RADIUS)

	-- Draw raycast line and hit point.
	if ray_hit then
		G.graphics.set_color(255, 80, 80, 120)
		G.graphics.draw_line(bx, by, ray_hit.x, ray_hit.y)
		G.graphics.set_color(255, 50, 50, 255)
		G.graphics.draw_circle(ray_hit.x, ray_hit.y, 4)
	end

	-- HUD.
	G.graphics.set_color("white")
	G.graphics.print("Physics Expansion Test", 10, 10)
	G.graphics.print("G: toggle gravity  R: reset  Space: jump  Left/Right: nudge  Q: quit", 10, 35)

	local gx, gy = G.physics.gravity()
	G.graphics.print(string.format("Gravity: %.0f, %.0f", gx, gy), 10, 65)

	local vx, vy = G.physics.linear_velocity(ball.handle)
	G.graphics.print(string.format("Ball vel: %.1f, %.1f", vx, vy), 10, 85)

	if ray_hit then
		local dist = math.sqrt((ray_hit.x - bx) * (ray_hit.x - bx) + (ray_hit.y - by) * (ray_hit.y - by))
		local kind = type(ray_hit) == "table" and "hit" or "?"
		G.graphics.print(string.format("Raycast: dist=%.0f  nx=%.2f ny=%.2f", dist, ray_hit.nx, ray_hit.ny), 10, 105)
	else
		G.graphics.print("Raycast: no hit", 10, 105)
	end

	G.graphics.set_color("white")
end

return Game
