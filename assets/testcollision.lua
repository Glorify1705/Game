-- Interactive collision system test
-- Controls:
--   WASD / Arrow keys: move player
--   1: switch to circle player shape
--   2: switch to AABB player shape
--   Space: cast ray from player toward mouse
--   T: toggle trigger zone visibility
--   Q: quit

local Game = {}

local world
local player
local player_shape
local player_is_circle = true
local obstacles = {}
local triggers = {}
local raycast_hit = nil
local trigger_log = {}
local MAX_LOG = 8

local PLAYER_SPEED = 200
local PLAYER_RADIUS = 16
local PLAYER_BOX_W = 28
local PLAYER_BOX_H = 28

-- Collision filter categories
local CAT_PLAYER = 1
local CAT_WALL   = 2
local CAT_TRIGGER = 4

function Game:init()
	G.window.set_title("Collision System Test")
	local W, H = G.window.dimensions()

	world = G.collision.new_world(64)

	-- Player shape
	player_shape = G.collision.circle(PLAYER_RADIUS)
	player = world:add(player_shape, W / 2, H / 2, {
		category = CAT_PLAYER,
		mask = CAT_WALL + CAT_TRIGGER,
		userdata = "player",
	})

	-- Boundary walls (static AABBs)
	local wall_t = 16
	self:add_wall(W / 2, wall_t / 2, W, wall_t)           -- top
	self:add_wall(W / 2, H - wall_t / 2, W, wall_t)       -- bottom
	self:add_wall(wall_t / 2, H / 2, wall_t, H)           -- left
	self:add_wall(W - wall_t / 2, H / 2, wall_t, H)       -- right

	-- Some obstacles in the middle
	self:add_wall(200, 200, 80, 80)
	self:add_wall(500, 300, 120, 40)
	self:add_wall(350, 450, 40, 120)
	self:add_wall(600, 150, 60, 60)

	-- A circle obstacle
	local circ_shape = G.collision.circle(30)
	local circ = world:add(circ_shape, 400, 250, {
		category = CAT_WALL,
		mask = CAT_PLAYER,
		userdata = { type = "circle_wall", x = 400, y = 250, r = 30 },
	})
	table.insert(obstacles, { handle = circ, type = "circle", x = 400, y = 250, r = 30 })

	-- Trigger zones
	self:add_trigger(150, 400, 60)
	self:add_trigger(650, 400, 50)
	self:add_trigger(400, 100, 45)

	-- Set up trigger callbacks
	world:on_trigger_enter(function(a, b)
		local msg = string.format("ENTER: %s <-> %s", tostring(a), tostring(b))
		table.insert(trigger_log, 1, msg)
		if #trigger_log > MAX_LOG then table.remove(trigger_log) end
	end)

	world:on_trigger_exit(function(a, b)
		local msg = string.format("EXIT:  %s <-> %s", tostring(a), tostring(b))
		table.insert(trigger_log, 1, msg)
		if #trigger_log > MAX_LOG then table.remove(trigger_log) end
	end)

	raycast_hit = nil
end

function Game:add_wall(cx, cy, w, h)
	local shape = G.collision.aabb(w, h)
	local handle = world:add(shape, cx, cy, {
		category = CAT_WALL,
		mask = CAT_PLAYER,
		userdata = { type = "wall", x = cx, y = cy, w = w, h = h },
	})
	table.insert(obstacles, { handle = handle, type = "aabb", x = cx, y = cy, w = w, h = h })
end

function Game:add_trigger(cx, cy, r)
	local shape = G.collision.circle(r)
	local handle = world:add(shape, cx, cy, {
		category = CAT_TRIGGER,
		mask = CAT_PLAYER,
		trigger = true,
		userdata = { type = "trigger", x = cx, y = cy, r = r },
	})
	table.insert(triggers, { handle = handle, x = cx, y = cy, r = r, active = false })
end

function Game:update(t, dt)
	if G.input.is_key_pressed("q") then
		G.system.quit()
		return
	end

	-- Switch player shape
	if G.input.is_key_pressed("1") and not player_is_circle then
		player_is_circle = true
		player_shape = G.collision.circle(PLAYER_RADIUS)
		world:set_shape(player, player_shape)
	elseif G.input.is_key_pressed("2") and player_is_circle then
		player_is_circle = false
		player_shape = G.collision.aabb(PLAYER_BOX_W, PLAYER_BOX_H)
		world:set_shape(player, player_shape)
	end

	-- Player movement
	local vx, vy = 0, 0
	if G.input.is_key_down("w") or G.input.is_key_down("up") then vy = -PLAYER_SPEED end
	if G.input.is_key_down("s") or G.input.is_key_down("down") then vy = PLAYER_SPEED end
	if G.input.is_key_down("a") or G.input.is_key_down("left") then vx = -PLAYER_SPEED end
	if G.input.is_key_down("d") or G.input.is_key_down("right") then vx = PLAYER_SPEED end

	-- Normalize diagonal movement
	if vx ~= 0 and vy ~= 0 then
		local inv = 1.0 / math.sqrt(2)
		vx = vx * inv
		vy = vy * inv
	end

	local px, py, contacts = world:move_and_slide(player, vx * dt, vy * dt)

	-- Raycast from player toward mouse on Space
	raycast_hit = nil
	if G.input.is_key_down("space") then
		local mx, my = G.input.mouse_position()
		local dx, dy = mx - px, my - py
		local len = math.sqrt(dx * dx + dy * dy)
		if len > 0.001 then
			dx, dy = dx / len, dy / len
			raycast_hit = world:raycast(px, py, dx, dy, 1000, CAT_WALL)
			if raycast_hit then
				raycast_hit.ox = px
				raycast_hit.oy = py
			end
		end
	end

	-- Query overlapping triggers
	for _, tr in ipairs(triggers) do
		tr.active = false
	end
	local overlaps = world:get_overlaps(player)
	for _, ov in ipairs(overlaps) do
		for _, tr in ipairs(triggers) do
			if tr.handle == ov.other then
				tr.active = true
			end
		end
	end

	-- Update collision world (rebuilds spatial hash, fires trigger callbacks)
	world:update()
end

function Game:draw()
	G.graphics.clear()
	local W, H = G.window.dimensions()

	-- Draw obstacles
	for _, ob in ipairs(obstacles) do
		G.graphics.set_color("gray")
		if ob.type == "aabb" then
			G.graphics.draw_rect(ob.x - ob.w / 2, ob.y - ob.h / 2,
				ob.x + ob.w / 2, ob.y + ob.h / 2)
		elseif ob.type == "circle" then
			G.graphics.draw_circle(ob.x, ob.y, ob.r)
		end
	end

	-- Draw trigger zones
	for _, tr in ipairs(triggers) do
		if tr.active then
			G.graphics.set_color(0, 255, 0, 80)
		else
			G.graphics.set_color(0, 100, 255, 60)
		end
		G.graphics.draw_circle(tr.x, tr.y, tr.r)
	end

	-- Draw raycast
	if raycast_hit then
		-- Ray line
		G.graphics.set_color("yellow")
		G.graphics.draw_line(raycast_hit.ox, raycast_hit.oy, raycast_hit.x, raycast_hit.y)
		-- Hit point
		G.graphics.set_color("red")
		G.graphics.draw_circle(raycast_hit.x, raycast_hit.y, 4)
		-- Normal
		G.graphics.set_color("green")
		G.graphics.draw_line(raycast_hit.x, raycast_hit.y,
			raycast_hit.x + raycast_hit.nx * 20,
			raycast_hit.y + raycast_hit.ny * 20)
	end

	-- Draw player
	local px, py = world:get_position(player)
	G.graphics.set_color("white")
	if player_is_circle then
		G.graphics.draw_circle(px, py, PLAYER_RADIUS)
	else
		G.graphics.draw_rect(px - PLAYER_BOX_W / 2, py - PLAYER_BOX_H / 2,
			px + PLAYER_BOX_W / 2, py + PLAYER_BOX_H / 2)
	end

	-- Point query under mouse cursor
	local mx, my = G.input.mouse_position()
	local under_mouse = world:query_point(mx, my)
	if #under_mouse > 0 then
		G.graphics.set_color("yellow")
		G.graphics.draw_circle(mx, my, 6)
	end

	-- HUD
	G.graphics.set_color("white")
	local shape_name = player_is_circle and "Circle" or "AABB"
	G.graphics.print(string.format("Shape: %s [1/2]  Pos: %.0f, %.0f", shape_name, px, py), 10, 10)
	G.graphics.print("WASD: move  Space: raycast  Q: quit", 10, 38)

	-- Trigger log
	G.graphics.set_color(180, 220, 255, 200)
	for i, msg in ipairs(trigger_log) do
		G.graphics.print(msg, 10, H - 30 - (i - 1) * 28)
	end

	G.graphics.set_color("white")
end

return Game
