-- Interactive test for Box2D sensor fixtures and the begin/end contact
-- callbacks. Drive a sprite-rendered ship through three sensor zones; the
-- HUD prints enter/exit events and a running overlap count so you can
-- verify both halves of the contact pair fire.
--
-- Controls:
--   WASD / Arrow keys: move ship
--   R: reset counters
--   Q: quit

local Game = {}

local SHIP_SPRITE = "playerShip1_blue"
local SHIP_RADIUS = 30
local SHIP_SPEED = 240

local player
local sensors = {}
local enter_count = 0
local exit_count = 0
local frame = 0
local active_overlaps = 0
local event_log = {}
local MAX_LOG = 6

local function log_event(msg)
	table.insert(event_log, 1, msg)
	if #event_log > MAX_LOG then
		table.remove(event_log)
	end
end

local function describe(ud)
	if type(ud) == "table" then
		return ud.kind
	end
	return tostring(ud)
end

function Game:init()
	G.window.set_title("Sensor Test")
	local W, H = G.window.dimensions()

	-- No screen-edge walls: their userdata is 0 (-> nil in the callback)
	-- and they would generate a constant stream of contact events whenever
	-- the ship grazed them, drowning out the actual sensor traffic.
	G.physics.create_ground(false)

	-- Player: dynamic circle, no sensor. Userdata is a table so the
	-- callback can identify it by `kind`.
	local player_ud = { kind = "ship" }
	player = {
		ud = player_ud,
		handle = G.physics.add_circle(W / 2, H / 2, SHIP_RADIUS, player_ud),
	}
	G.physics.set_linear_damping(player.handle, 6)
	do
		local px, py = G.physics.position(player.handle)
		print(
			string.format(
				"[testsensors] ship requested=%.0f,%.0f actual=%.1f,%.1f r=%d",
				W / 2,
				H / 2,
				px,
				py,
				SHIP_RADIUS
			)
		)
	end

	-- Three sensors at fixed positions. Sensors are dynamic bodies with
	-- options.sensor=true; they skip the friction joint and don't push
	-- anything, so they sit where you place them.
	local function add_sensor(name, x, y, r, color)
		local ud = { kind = name }
		local handle = G.physics.add_circle(x, y, r, ud, { sensor = true })
		table.insert(sensors, {
			name = name,
			handle = handle,
			ud = ud,
			x = x,
			y = y,
			r = r,
			color = color,
			overlapping = false,
		})
	end
	add_sensor("zone-A", W * 0.25, H * 0.35, 70, { 80, 200, 255 })
	add_sensor("zone-B", W * 0.75, H * 0.35, 90, { 255, 180, 80 })
	add_sensor("zone-C", W * 0.50, H * 0.75, 60, { 180, 255, 120 })
	for _, s in ipairs(sensors) do
		local px, py = G.physics.position(s.handle)
		print(
			string.format(
				"[testsensors] sensor %s requested=%.0f,%.0f actual=%.1f,%.1f r=%d",
				s.name,
				s.x,
				s.y,
				px,
				py,
				s.r
			)
		)
	end

	-- Lookup table from userdata identity to sensor entry, so we can
	-- toggle the visual highlight on enter/exit.
	local sensor_by_ud = {}
	for _, s in ipairs(sensors) do
		sensor_by_ud[s.ud] = s
	end

	G.physics.on_begin_contact(function(a, b)
		local msg = string.format("ENTER %s <-> %s (frame=%d)", describe(a), describe(b), frame)
		print("[testsensors] " .. msg)
		-- Defensive: ignore contacts where either side has no userdata
		-- (e.g. ground/wall edges from create_ground).
		if a == nil or b == nil then
			return
		end
		enter_count = enter_count + 1
		active_overlaps = active_overlaps + 1
		local s = sensor_by_ud[a] or sensor_by_ud[b]
		if s then
			s.overlapping = true
		end
		log_event(msg)
	end)

	G.physics.on_end_contact(function(a, b)
		local msg = string.format("EXIT  %s <-> %s (frame=%d)", describe(a), describe(b), frame)
		print("[testsensors] " .. msg)
		if a == nil or b == nil then
			return
		end
		exit_count = exit_count + 1
		active_overlaps = math.max(0, active_overlaps - 1)
		local s = sensor_by_ud[a] or sensor_by_ud[b]
		if s then
			s.overlapping = false
		end
		log_event(msg)
	end)
end

function Game:update(t, dt)
	frame = frame + 1
	if frame % 60 == 0 then
		local px, py = G.physics.position(player.handle)
		local vx, vy = G.physics.linear_velocity(player.handle)
		print(
			string.format(
				"[testsensors] frame=%d pos=%.1f,%.1f vel=%.2f,%.2f",
				frame,
				px,
				py,
				vx,
				vy
			)
		)
	end
	if G.input.is_key_pressed("q") then
		G.system.quit()
		return
	end
	if G.input.is_key_pressed("r") then
		enter_count = 0
		exit_count = 0
		event_log = {}
	end

	local vx, vy = 0, 0
	if G.input.is_key_down("w") or G.input.is_key_down("up") then
		vy = vy - 1
	end
	if G.input.is_key_down("s") or G.input.is_key_down("down") then
		vy = vy + 1
	end
	if G.input.is_key_down("a") or G.input.is_key_down("left") then
		vx = vx - 1
	end
	if G.input.is_key_down("d") or G.input.is_key_down("right") then
		vx = vx + 1
	end
	local len = math.sqrt(vx * vx + vy * vy)
	if len > 0 then
		vx, vy = vx / len * SHIP_SPEED, vy / len * SHIP_SPEED
	end
	G.physics.set_linear_velocity(player.handle, vx, vy)
end

function Game:draw()
	G.graphics.clear(0.08, 0.09, 0.13, 1)

	-- Sensor zones
	for _, s in ipairs(sensors) do
		local r, g, b = s.color[1], s.color[2], s.color[3]
		local alpha = s.overlapping and 140 or 60
		G.graphics.set_color(r, g, b, alpha)
		G.graphics.draw_circle(s.x, s.y, s.r)
		G.graphics.set_color(r, g, b, 220)
		G.graphics.draw_circle_outline(s.x, s.y, s.r)
		G.graphics.set_color(255, 255, 255, 230)
		G.graphics.print(s.name, s.x - 30, s.y - 8)
	end

	-- Ship sprite, centered on the body
	local px, py = G.physics.position(player.handle)
	G.graphics.set_color(255, 255, 255, 255)
	G.graphics.draw_sprite(SHIP_SPRITE, px, py)

	-- HUD
	G.graphics.set_color("white")
	G.graphics.print("Sensor Test  --  WASD: move  R: reset  Q: quit", 10, 10)
	G.graphics.print(
		string.format(
			"Enters: %d   Exits: %d   Active: %d",
			enter_count,
			exit_count,
			active_overlaps
		),
		10,
		38
	)

	G.graphics.set_color(180, 220, 255, 220)
	local _, H = G.window.dimensions()
	for i, msg in ipairs(event_log) do
		G.graphics.print(msg, 10, H - 30 - (i - 1) * 28)
	end
	G.graphics.set_color("white")
end

return Game
