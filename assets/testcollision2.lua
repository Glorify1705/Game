-- Collision system test 2: spatial queries and move_and_collide
-- Controls:
--   WASD: move player
--   Left click: query_circle at mouse (radius 60)
--   Right click: query_rect around mouse (80x80)
--   Space: raycast_all from player toward mouse
--   E: spawn a new circle at mouse position
--   R: remove the last spawned object
--   Q: quit

local Game = {}

local world
local player
local spawned = {}

local PLAYER_SPEED = 180
local PLAYER_R = 12

local query_results = {}
local query_type = nil  -- "circle", "rect", or nil
local query_cx, query_cy, query_r = 0, 0, 0
local query_x1, query_y1, query_x2, query_y2 = 0, 0, 0, 0

local raycast_hits = {}
local ray_ox, ray_oy = 0, 0

local last_contact = nil

function Game:init()
	G.window.set_title("Collision Test 2: Queries & Spawning")
	local W, H = G.window.dimensions()

	world = G.collision.new_world(48)

	-- Player
	local shape = G.collision.circle(PLAYER_R)
	player = world:add(shape, W / 2, H / 2, {
		userdata = "player",
	})

	-- Scatter some static boxes
	local rng = G.random.non_deterministic()
	for i = 1, 15 do
		local x = G.random.sample(rng, 60, W - 60)
		local y = G.random.sample(rng, 60, H - 60)
		local w = G.random.sample(rng, 20, 60)
		local h = G.random.sample(rng, 20, 60)
		local s = G.collision.aabb(w, h)
		local handle = world:add(s, x, y, {
			userdata = { type = "box", x = x, y = y, w = w, h = h },
		})
		table.insert(spawned, { handle = handle, type = "aabb", x = x, y = y, w = w, h = h })
	end

	-- A few circles too
	for i = 1, 8 do
		local x = G.random.sample(rng, 60, W - 60)
		local y = G.random.sample(rng, 60, H - 60)
		local r = G.random.sample(rng, 10, 30)
		local s = G.collision.circle(r)
		local handle = world:add(s, x, y, {
			userdata = { type = "circle", x = x, y = y, r = r },
		})
		table.insert(spawned, { handle = handle, type = "circle", x = x, y = y, r = r })
	end
end

function Game:update(t, dt)
	if G.input.is_key_pressed("q") then
		G.system.quit()
		return
	end

	-- Player movement with move_and_collide (stops at first hit)
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

	local px, py, contact = world:move_and_collide(player, vx * dt, vy * dt)
	last_contact = contact

	-- Spatial queries
	local mx, my = G.input.mouse_position()
	query_results = {}
	query_type = nil
	raycast_hits = {}

	if G.input.is_mouse_down(0) then
		-- Circle query around mouse
		query_type = "circle"
		query_cx, query_cy, query_r = mx, my, 60
		query_results = world:query_circle(mx, my, 60)
	elseif G.input.is_mouse_down(2) then
		-- Rect query around mouse
		query_type = "rect"
		query_x1 = mx - 40
		query_y1 = my - 40
		query_x2 = mx + 40
		query_y2 = my + 40
		query_results = world:query_rect(query_x1, query_y1, query_x2, query_y2)
	end

	-- Raycast all
	if G.input.is_key_down("space") then
		local dx, dy = mx - px, my - py
		local len = math.sqrt(dx * dx + dy * dy)
		if len > 0.001 then
			dx, dy = dx / len, dy / len
			ray_ox, ray_oy = px, py
			raycast_hits = world:raycast_all(px, py, dx, dy, 800)
		end
	end

	-- Spawn circle at mouse
	if G.input.is_key_pressed("e") then
		local r = 15
		local s = G.collision.circle(r)
		local handle = world:add(s, mx, my, {
			userdata = { type = "circle", x = mx, y = my, r = r },
		})
		table.insert(spawned, { handle = handle, type = "circle", x = mx, y = my, r = r })
	end

	-- Remove last spawned
	if G.input.is_key_pressed("r") and #spawned > 0 then
		local last = table.remove(spawned)
		world:remove(last.handle)
	end

	world:update()
end

function Game:draw()
	G.graphics.clear()

	-- Draw spawned objects
	for _, obj in ipairs(spawned) do
		local highlighted = false
		for _, qh in ipairs(query_results) do
			if qh == obj.handle then
				highlighted = true
				break
			end
		end

		if highlighted then
			G.graphics.set_color("yellow")
		else
			G.graphics.set_color(100, 100, 120, 255)
		end

		if obj.type == "aabb" then
			G.graphics.draw_rect(obj.x - obj.w / 2, obj.y - obj.h / 2,
				obj.x + obj.w / 2, obj.y + obj.h / 2)
		else
			G.graphics.draw_circle(obj.x, obj.y, obj.r)
		end
	end

	-- Draw query region
	if query_type == "circle" then
		G.graphics.set_color(0, 200, 255, 40)
		G.graphics.draw_circle(query_cx, query_cy, query_r)
	elseif query_type == "rect" then
		G.graphics.set_color(0, 200, 255, 40)
		G.graphics.draw_rect(query_x1, query_y1, query_x2, query_y2)
	end

	-- Draw raycast_all hits
	if #raycast_hits > 0 then
		-- Draw ray line to furthest hit
		local last = raycast_hits[#raycast_hits]
		G.graphics.set_color(255, 255, 0, 100)
		G.graphics.draw_line(ray_ox, ray_oy, last.x, last.y)

		for i, hit in ipairs(raycast_hits) do
			-- Hit point
			G.graphics.set_color("red")
			G.graphics.draw_circle(hit.x, hit.y, 5)
			-- Normal
			G.graphics.set_color("green")
			G.graphics.draw_line(hit.x, hit.y, hit.x + hit.nx * 15, hit.y + hit.ny * 15)
			-- Label
			G.graphics.set_color("white")
			G.graphics.print(string.format("#%d t=%.1f", i, hit.t), hit.x + 8, hit.y - 8)
		end
	end

	-- Draw player
	local px, py = world:get_position(player)
	if last_contact then
		G.graphics.set_color("red")
	else
		G.graphics.set_color("white")
	end
	G.graphics.draw_circle(px, py, PLAYER_R)

	-- Contact info
	if last_contact then
		G.graphics.set_color("cyan")
		G.graphics.draw_line(px, py,
			px + last_contact.nx * 30,
			py + last_contact.ny * 30)
	end

	-- HUD
	G.graphics.set_color("white")
	G.graphics.print(string.format("Pos: %.0f, %.0f  Objects: %d", px, py, #spawned), 10, 10)
	G.graphics.print("WASD: move  LMB: circle query  RMB: rect query", 10, 26)
	G.graphics.print("Space: raycast_all  E: spawn  R: remove  Q: quit", 10, 42)

	if #query_results > 0 then
		G.graphics.set_color("yellow")
		G.graphics.print(string.format("Query hits: %d", #query_results), 10, 62)
	end

	G.graphics.set_color("white")
end

return Game
