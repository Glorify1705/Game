-- Camera system test.
-- Controls:
--   WASD       move the ship (camera follows)
--   Mouse      world cursor position shown
--   Scroll     zoom in/out
--   R          rotate camera
--   Space      screen shake
--   1          toggle deadzone
--   2          toggle bounds
--   3          cycle parallax demo
--   Q          quit

local g = {}

local WORLD_W = 3000
local WORLD_H = 2000
local SPEED = 200

local ship_x, ship_y
local ship_angle = 0
local deadzone_on = false
local bounds_on = true
local parallax_mode = 0

local stars = {}
local meteors = {}

function g:init()
	ship_x = WORLD_W / 2
	ship_y = WORLD_H / 2

	-- Scatter stars across the world.
	for i = 1, 80 do
		stars[i] = {
			x = math.random(0, WORLD_W),
			y = math.random(0, WORLD_H),
			sprite = "star" .. math.random(1, 3),
		}
	end

	-- Scatter some meteors.
	for i = 1, 15 do
		meteors[i] = {
			x = math.random(100, WORLD_W - 100),
			y = math.random(100, WORLD_H - 100),
			angle = math.random() * 6.28,
			speed = (math.random() - 0.5) * 2,
			sprite = (i % 2 == 0) and "meteorBrown_big1" or "meteorGrey_big1",
		}
	end

	G.camera.set(ship_x, ship_y)
	G.camera.set_lerp(0.08, 0.08)
	G.camera.set_bounds(0, 0, WORLD_W, WORLD_H)
end

function g:update(t, dt)
	if G.input.is_key_pressed("q") then
		G.system.quit()
	end

	-- Ship movement.
	local dx, dy = 0, 0
	if G.input.is_key_down("w") then
		dy = -1
	end
	if G.input.is_key_down("s") then
		dy = 1
	end
	if G.input.is_key_down("a") then
		dx = -1
	end
	if G.input.is_key_down("d") then
		dx = 1
	end
	if dx ~= 0 or dy ~= 0 then
		local len = math.sqrt(dx * dx + dy * dy)
		ship_x = ship_x + (dx / len) * SPEED * dt
		ship_y = ship_y + (dy / len) * SPEED * dt
	end
	-- Face movement direction.
	if dx ~= 0 or dy ~= 0 then
		ship_angle = math.atan2(dy, dx) + math.pi / 2
	end

	-- Camera follow.
	G.camera.follow(ship_x, ship_y)

	-- Zoom with scroll wheel.
	local _, scroll = G.input.mouse_wheel()
	if scroll ~= 0 then
		local z = G.camera.get_zoom()
		z = z + scroll * 0.1
		if z < 0.25 then
			z = 0.25
		end
		if z > 4.0 then
			z = 4.0
		end
		G.camera.set_zoom(z)
	end

	-- Rotate camera.
	if G.input.is_key_pressed("r") then
		local angle = G.camera.get_rotation()
		G.camera.set_rotation(angle + 0.2)
	end

	-- Screen shake.
	if G.input.is_key_pressed("space") then
		G.camera.shake(8, 0.4)
	end

	-- Toggle deadzone.
	if G.input.is_key_pressed("1") then
		deadzone_on = not deadzone_on
		if deadzone_on then
			G.camera.set_deadzone(0.15, 0.15)
		else
			G.camera.clear_deadzone()
		end
	end

	-- Toggle bounds.
	if G.input.is_key_pressed("2") then
		bounds_on = not bounds_on
		if bounds_on then
			G.camera.set_bounds(0, 0, WORLD_W, WORLD_H)
		else
			G.camera.clear_bounds()
		end
	end

	-- Cycle parallax mode.
	if G.input.is_key_pressed("3") then
		parallax_mode = (parallax_mode + 1) % 2
	end

	-- Rotate meteors.
	for _, m in ipairs(meteors) do
		m.angle = m.angle + m.speed * dt
	end
end

local function draw_grid()
	G.graphics.set_color(40, 40, 40, 255)
	for x = 0, WORLD_W, 100 do
		G.graphics.draw_line(x, 0, x, WORLD_H)
	end
	for y = 0, WORLD_H, 100 do
		G.graphics.draw_line(0, y, WORLD_W, y)
	end
end

local function draw_world_border()
	G.graphics.set_color("red")
	G.graphics.draw_line(0, 0, WORLD_W, 0)
	G.graphics.draw_line(WORLD_W, 0, WORLD_W, WORLD_H)
	G.graphics.draw_line(WORLD_W, WORLD_H, 0, WORLD_H)
	G.graphics.draw_line(0, WORLD_H, 0, 0)
end

function g:draw()
	G.graphics.clear()

	-- Background parallax layer (stars move slowly).
	if parallax_mode == 1 then
		G.camera.attach(0.3, 0.3)
		G.graphics.set_color(100, 100, 255, 128)
		for _, s in ipairs(stars) do
			G.graphics.draw_sprite(s.sprite, s.x, s.y)
		end
		G.camera.detach()
	end

	-- Main camera layer.
	G.camera.attach()

	draw_grid()
	draw_world_border()

	-- Draw stars on main layer when parallax is off.
	if parallax_mode == 0 then
		G.graphics.set_color("white")
		for _, s in ipairs(stars) do
			G.graphics.draw_sprite(s.sprite, s.x, s.y)
		end
	end

	-- Draw meteors.
	G.graphics.set_color("white")
	for _, m in ipairs(meteors) do
		G.graphics.draw_sprite(m.sprite, m.x, m.y, m.angle)
	end

	-- Draw ship.
	G.graphics.set_color("white")
	G.graphics.draw_sprite("playerShip1_green", ship_x, ship_y, ship_angle)

	-- Draw mouse cursor in world space.
	local wx, wy = G.camera.mouse_world()
	G.graphics.set_color("yellow")
	G.graphics.draw_sprite("numeralX", wx, wy)

	G.camera.detach()

	-- HUD (screen space).
	G.graphics.set_color("white")
	local cx, cy = G.camera.get()
	local zoom = G.camera.get_zoom()
	local status = string.format(
		"Camera: %.0f, %.0f  Zoom: %.1fx  Deadzone: %s  Bounds: %s  Parallax: %s",
		cx,
		cy,
		zoom,
		deadzone_on and "ON" or "OFF",
		bounds_on and "ON" or "OFF",
		parallax_mode == 1 and "ON" or "OFF"
	)
	G.graphics.draw_text("ponderosa.ttf", 18, status, 10, 10)

	local help = "WASD=move  Scroll=zoom  R=rotate  Space=shake  1=deadzone  2=bounds  3=parallax"
	local vw, vh = G.window.dimensions()
	G.graphics.draw_text("ponderosa.ttf", 14, help, 10, vh - 25)

	-- Show world mouse position.
	local wmx, wmy = G.camera.mouse_world()
	local mouse_info = string.format("Mouse world: %.0f, %.0f", wmx, wmy)
	G.graphics.draw_text("ponderosa.ttf", 14, mouse_info, 10, 35)
end

return g
