-- Collision system test 3: standalone collision.test() and trigger zones
-- A simple platformer-style test where a player slides along walls
-- and trigger zones change color and track enter/exit events.
--
-- Controls:
--   WASD: move player
--   Tab: cycle through overlap display for player
--   Q: quit

local Game = {}

local world
local player
local walls = {}
local coins = {}
local collected = 0

local PLAYER_SPEED = 150
local PLAYER_W = 24
local PLAYER_H = 24

local CAT_PLAYER = 1
local CAT_WALL   = 2
local CAT_COIN   = 4

-- For standalone test visualization
local test_shape_a, test_shape_b
local test_result = { hit = false }

function Game:init()
	G.window.set_title("Collision Test 3: Triggers & Standalone Test")
	local W, H = G.window.dimensions()

	world = G.collision.new_world(64)

	-- Player (AABB)
	local pshape = G.collision.aabb(PLAYER_W, PLAYER_H)
	player = world:add(pshape, W / 2, H / 2, {
		category = CAT_PLAYER,
		mask = CAT_WALL + CAT_COIN,
		userdata = "player",
	})

	-- Build a simple maze-like layout
	local wall_defs = {
		-- Borders
		{ W / 2, 8, W, 16 },
		{ W / 2, H - 8, W, 16 },
		{ 8, H / 2, 16, H },
		{ W - 8, H / 2, 16, H },
		-- Internal walls
		{ 200, 150, 200, 16 },
		{ 200, 350, 200, 16 },
		{ 500, 150, 16, 200 },
		{ 500, 400, 16, 150 },
		{ 350, 250, 100, 16 },
	}

	for _, def in ipairs(wall_defs) do
		local s = G.collision.aabb(def[3], def[4])
		local h = world:add(s, def[1], def[2], {
			category = CAT_WALL,
			mask = CAT_PLAYER,
		})
		table.insert(walls, { handle = h, x = def[1], y = def[2], w = def[3], h = def[4] })
	end

	-- Coins (trigger circles)
	local coin_positions = {
		{ 120, 100 }, { 350, 100 }, { 600, 100 },
		{ 120, 300 }, { 350, 400 }, { 600, 300 },
		{ 250, 500 }, { 550, 500 },
	}
	for _, pos in ipairs(coin_positions) do
		local s = G.collision.circle(14)
		local h = world:add(s, pos[1], pos[2], {
			category = CAT_COIN,
			mask = CAT_PLAYER,
			trigger = true,
			userdata = { type = "coin", alive = true },
		})
		table.insert(coins, { handle = h, x = pos[1], y = pos[2], alive = true })
	end

	-- Trigger callback: collect coins (defer removal to avoid modifying world
	-- during trigger iteration)
	world:on_trigger_enter(function(a, b)
		-- One of a/b is the player, the other is a coin
		for _, coin in ipairs(coins) do
			if coin.alive and (coin.handle == a or coin.handle == b) then
				coin.alive = false
				collected = collected + 1
			end
		end
	end)

	-- Set up the standalone test shapes (displayed in top-right corner)
	test_shape_a = G.collision.circle(25)
	test_shape_b = G.collision.aabb(40, 40)

	collected = 0
end

function Game:update(t, dt)
	if G.input.is_key_pressed("q") then
		G.system.quit()
		return
	end

	-- Player movement with slide
	local vx, vy = 0, 0
	if G.input.is_key_down("w") then vy = -PLAYER_SPEED end
	if G.input.is_key_down("s") then vy = PLAYER_SPEED end
	if G.input.is_key_down("a") then vx = -PLAYER_SPEED end
	if G.input.is_key_down("d") then vx = PLAYER_SPEED end

	if vx ~= 0 and vy ~= 0 then
		local inv = 1.0 / math.sqrt(2)
		vx = vx * inv
		vy = vy * inv
	end

	world:move_and_slide(player, vx * dt, vy * dt)
	world:update()

	-- Remove collected coins (deferred from trigger callback)
	for _, coin in ipairs(coins) do
		if not coin.alive and coin.handle then
			world:remove(coin.handle)
			coin.handle = nil
		end
	end

	-- Standalone collision.test(): circle follows mouse, box is fixed
	local W = G.window.dimensions()
	local mx, my = G.input.mouse_position()
	local box_cx, box_cy = W - 100, 100
	local hit, nx, ny, depth = G.collision.test(
		test_shape_a, mx, my,
		test_shape_b, box_cx, box_cy
	)
	test_result.hit = hit
	test_result.nx = nx or 0
	test_result.ny = ny or 0
	test_result.depth = depth or 0
	test_result.ax, test_result.ay = mx, my
	test_result.bx, test_result.by = box_cx, box_cy
end

function Game:draw()
	G.graphics.clear()
	local W, H = G.window.dimensions()

	-- Draw walls
	G.graphics.set_color(80, 80, 100, 255)
	for _, w in ipairs(walls) do
		G.graphics.draw_rect(w.x - w.w / 2, w.y - w.h / 2,
			w.x + w.w / 2, w.y + w.h / 2)
	end

	-- Draw coins
	for _, coin in ipairs(coins) do
		if coin.alive then
			G.graphics.set_color(255, 220, 50, 255)
			G.graphics.draw_circle(coin.x, coin.y, 14)
			G.graphics.set_color(200, 170, 30, 255)
			G.graphics.draw_circle(coin.x, coin.y, 8)
		end
	end

	-- Draw player
	local px, py = world:get_position(player)
	G.graphics.set_color("white")
	G.graphics.draw_rect(px - PLAYER_W / 2, py - PLAYER_H / 2,
		px + PLAYER_W / 2, py + PLAYER_H / 2)

	-- Draw standalone test visualization (top-right area)
	-- Box (static)
	if test_result.hit then
		G.graphics.set_color(255, 80, 80, 200)
	else
		G.graphics.set_color(60, 60, 80, 200)
	end
	G.graphics.draw_rect(test_result.bx - 20, test_result.by - 20,
		test_result.bx + 20, test_result.by + 20)

	-- Circle (follows mouse)
	if test_result.hit then
		G.graphics.set_color(255, 100, 100, 180)
		-- Draw separation normal
		G.graphics.set_color("green")
		G.graphics.draw_line(test_result.ax, test_result.ay,
			test_result.ax + test_result.nx * test_result.depth * 2,
			test_result.ay + test_result.ny * test_result.depth * 2)
		G.graphics.set_color(255, 100, 100, 180)
	else
		G.graphics.set_color(100, 100, 140, 180)
	end
	G.graphics.draw_circle(test_result.ax, test_result.ay, 25)

	-- HUD
	G.graphics.set_color("white")
	G.graphics.print(string.format("Coins: %d / %d", collected, #coins), 10, 10)
	G.graphics.print("WASD: move  Q: quit", 10, 38)
	G.graphics.print("Move mouse near top-right box for standalone test", 10, 66)

	if test_result.hit then
		G.graphics.set_color("red")
		G.graphics.print(string.format("collision.test: HIT  depth=%.1f  n=(%.2f, %.2f)",
			test_result.depth, test_result.nx, test_result.ny), 10, 94)
	else
		G.graphics.set_color(100, 100, 100, 255)
		G.graphics.print("collision.test: no hit", 10, 94)
	end

	G.graphics.set_color("white")
end

return Game
