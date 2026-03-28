local Object = require("classic")

local Starfield = Object:extend()

function Starfield:new(screen_w, screen_h, rng)
	self.screen_w = screen_w
	self.screen_h = screen_h
	self.layers = {}

	self.layers[1] = { stars = {}, speed = 5, sprite = nil }
	for i = 1, 40 do
		self.layers[1].stars[i] = {
			x = G.random.sample(rng, 0, screen_w),
			y = G.random.sample(rng, 0, screen_h),
		}
	end

	self.layers[2] = { stars = {}, speed = 15, sprite = "star1" }
	for i = 1, 20 do
		self.layers[2].stars[i] = {
			x = G.random.sample(rng, 0, screen_w),
			y = G.random.sample(rng, 0, screen_h),
		}
	end

	self.layers[3] = { stars = {}, speed = 30, sprite = "star2" }
	for i = 1, 10 do
		self.layers[3].stars[i] = {
			x = G.random.sample(rng, 0, screen_w),
			y = G.random.sample(rng, 0, screen_h),
		}
	end
end

function Starfield:update(dt)
	for _, layer in ipairs(self.layers) do
		for _, star in ipairs(layer.stars) do
			star.y = star.y + layer.speed * dt
			if star.y > self.screen_h then
				star.y = star.y - self.screen_h
			end
		end
	end
end

function Starfield:draw()
	for _, layer in ipairs(self.layers) do
		if layer.sprite then
			G.graphics.set_color(255, 255, 255, 180)
			for _, star in ipairs(layer.stars) do
				G.graphics.draw_sprite(layer.sprite, star.x, star.y)
			end
		else
			G.graphics.set_color(255, 255, 255, 120)
			for _, star in ipairs(layer.stars) do
				G.graphics.draw_circle(star.x, star.y, 1)
			end
		end
	end
	G.graphics.set_color("white")
end

return Starfield
