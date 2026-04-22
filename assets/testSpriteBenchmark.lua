-- Sprite stress benchmark for profiling the batched quad renderer.
-- Spawns N animated sprites from sheet.sprites.json that bounce around
-- the screen with rotation and per-sprite tints. Unlike testBenchmark
-- (which is collision-heavy and uses draw_rect/draw_circle), this
-- exercises the textured-quad path, rotation, and SetColor state
-- changes — the things a real 2D game spends its frame budget on.
--
-- Controls:
--   1-9: Set sprite count (1000, 2500, 5000, 10000, 20000, 40000, 100000, 200000, 500000)
--   T:   Toggle random tints (forces SetColor state changes)
--   R:   Toggle rotation
--   Esc: Quit

local Game = {}

local target_count = 5000
local tints_on = true
local rotation_on = true

local SPRITES = {
	"meteorBrown_big1",
	"meteorBrown_big2",
	"meteorGrey_big1",
	"meteorGrey_big2",
	"meteorBrown_med1",
	"meteorGrey_med1",
	"meteorBrown_small1",
	"meteorGrey_small1",
	"star1",
	"star2",
	"star3",
	"playerShip1_blue",
	"playerShip1_green",
	"playerShip1_red",
}

function Game:init()
	G.window.set_dimensions(1280, 800)
	G.window.set_title("Sprite Benchmark")
	self.w, self.h = G.window.dimensions()
	self.sprites = {}
	self:spawn(target_count)
end

function Game:spawn(n)
	self.sprites = {}
	for i = 1, n do
		self.sprites[i] = {
			x = math.random() * self.w,
			y = math.random() * self.h,
			vx = (math.random() - 0.5) * 300,
			vy = (math.random() - 0.5) * 300,
			angle = math.random() * math.pi * 2,
			vangle = (math.random() - 0.5) * 4,
			sprite = SPRITES[math.random(#SPRITES)],
			r = math.random(128, 255),
			g = math.random(128, 255),
			b = math.random(128, 255),
		}
	end
end

function Game:update(t, dt)
	if G.input.is_key_pressed("escape") then
		G.system.quit()
	end
	if G.input.is_key_pressed("t") then
		tints_on = not tints_on
	end
	if G.input.is_key_pressed("r") then
		rotation_on = not rotation_on
	end

	local counts = { 1000, 2500, 5000, 10000, 20000, 40000, 100000, 200000, 500000 }
	for i, n in ipairs(counts) do
		if G.input.is_key_pressed(tostring(i)) then
			target_count = n
			self:spawn(n)
		end
	end

	local w, h = self.w, self.h
	for i = 1, #self.sprites do
		local s = self.sprites[i]
		s.x = s.x + s.vx * dt
		s.y = s.y + s.vy * dt
		if s.x < 0 then
			s.x = 0
			s.vx = -s.vx
		elseif s.x > w then
			s.x = w
			s.vx = -s.vx
		end
		if s.y < 0 then
			s.y = 0
			s.vy = -s.vy
		elseif s.y > h then
			s.y = h
			s.vy = -s.vy
		end
		if rotation_on then
			s.angle = s.angle + s.vangle * dt
		end
	end
end

function Game:draw()
	G.graphics.clear(13, 13, 25, 255)

	if tints_on then
		for i = 1, #self.sprites do
			local s = self.sprites[i]
			G.graphics.set_color(s.r, s.g, s.b, 255)
			G.graphics.draw_sprite(s.sprite, s.x, s.y, s.angle)
		end
	else
		G.graphics.set_color(255, 255, 255, 255)
		for i = 1, #self.sprites do
			local s = self.sprites[i]
			G.graphics.draw_sprite(s.sprite, s.x, s.y, s.angle)
		end
	end

	G.graphics.set_color(230, 230, 240, 255)
	G.graphics.print(
		string.format(
			"Sprites: %d   Tints: %s   Rotation: %s",
			#self.sprites,
			tints_on and "ON" or "OFF",
			rotation_on and "ON" or "OFF"
		),
		16,
		16
	)
	G.graphics.print("1-9: count   T: tints   R: rotation   Esc: quit", 16, 38)
end

return Game
