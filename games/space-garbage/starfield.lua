local Object = require("classic")

local Starfield = Object:extend()

function Starfield:new(screen_w, screen_h, rng)
	self.screen_w = screen_w
	self.screen_h = screen_h
	self.layers = {}

	-- each layer: parallax factor controls how much camera movement affects it
	-- lower = farther away, moves less
	self.layers[1] = { stars = {}, parallax = 0.02, sprite = nil, alpha = 120 }
	for i = 1, 60 do
		self.layers[1].stars[i] = {
			x = G.random.sample(rng, 0, screen_w),
			y = G.random.sample(rng, 0, screen_h),
		}
	end

	self.layers[2] = { stars = {}, parallax = 0.05, sprite = "star1", alpha = 150 }
	for i = 1, 30 do
		self.layers[2].stars[i] = {
			x = G.random.sample(rng, 0, screen_w),
			y = G.random.sample(rng, 0, screen_h),
		}
	end

	self.layers[3] = { stars = {}, parallax = 0.1, sprite = "star2", alpha = 180 }
	for i = 1, 15 do
		self.layers[3].stars[i] = {
			x = G.random.sample(rng, 0, screen_w),
			y = G.random.sample(rng, 0, screen_h),
		}
	end
end

function Starfield:update(dt) end

function Starfield:draw(cam_x, cam_y)
	cam_x = cam_x or 0
	cam_y = cam_y or 0
	local sw = self.screen_w
	local sh = self.screen_h
	for _, layer in ipairs(self.layers) do
		local ox = (cam_x * layer.parallax) % sw
		local oy = (cam_y * layer.parallax) % sh
		if layer.sprite then
			G.graphics.set_color(255, 255, 255, layer.alpha)
			for _, star in ipairs(layer.stars) do
				local sx = (star.x - ox) % sw
				local sy = (star.y - oy) % sh
				G.graphics.draw_sprite(layer.sprite, sx, sy)
			end
		else
			G.graphics.set_color(255, 255, 255, layer.alpha)
			for _, star in ipairs(layer.stars) do
				local sx = (star.x - ox) % sw
				local sy = (star.y - oy) % sh
				G.graphics.draw_circle(sx, sy, 1)
			end
		end
	end
	G.graphics.set_color("white")
end

return Starfield
