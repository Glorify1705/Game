-- Easing showcase: interactive demo that animates a collection of objects
-- using the selected easing. Cycles through easings with Left/Right arrows.
-- Press Space to replay. Esc to quit.

local Game = {}

local EASINGS = {
	"linear",
	"in-quad",
	"out-quad",
	"in-out-quad",
	"in-cubic",
	"out-cubic",
	"in-out-cubic",
	"in-quart",
	"out-quart",
	"in-out-quart",
	"in-quint",
	"out-quint",
	"in-out-quint",
	"in-sine",
	"out-sine",
	"in-out-sine",
	"in-expo",
	"out-expo",
	"in-out-expo",
	"in-circ",
	"out-circ",
	"in-out-circ",
	"in-back",
	"out-back",
	"in-out-back",
	"in-elastic",
	"out-elastic",
	"in-out-elastic",
	"in-bounce",
	"out-bounce",
	"in-out-bounce",
}

local NUM_RINGS = 12
local NUM_BARS = 20
local DURATION = 2.0

function Game:init()
	G.window.set_dimensions(1280, 800)
	G.window.set_title("Easing Showcase")
	self.easing_idx = 1
	self.ring = {}
	self.bars = {}
	self.pendulum = { angle = 0 }
	self.square = { rotation = 0, size = 0 }
	self:start()
end

function Game:current_easing()
	return EASINGS[self.easing_idx]
end

function Game:start()
	G.timer.cancel_all()
	local easing = self:current_easing()

	-- Rings: expand outward from center.
	self.ring = {}
	for i = 1, NUM_RINGS do
		local r = { radius = 0, alpha = 255 }
		self.ring[i] = r
		local target_r = 30 + (i - 1) * 22
		G.timer.after((i - 1) * 0.06, function()
			G.timer.tween(DURATION, r, { radius = target_r }, easing)
			G.timer.tween(DURATION * 0.8, r, { alpha = 255 - (i - 1) * 18 }, easing)
		end)
	end

	-- Bars: equalizer-style rise.
	self.bars = {}
	for i = 1, NUM_BARS do
		local b = { height = 0 }
		self.bars[i] = b
		local peak = 40 + math.sin(i * 0.7) * 80 + math.cos(i * 1.3) * 60
		G.timer.after(i * 0.03, function()
			G.timer.tween(DURATION, b, { height = peak }, easing, function()
				G.timer.tween(DURATION * 0.6, b, { height = peak * 0.3 }, easing)
			end)
		end)
	end

	-- Pendulum swing.
	self.pendulum = { angle = -1.2 }
	G.timer.tween(DURATION, self.pendulum, { angle = 1.2 }, easing, function()
		G.timer.tween(DURATION, self.pendulum, { angle = 0 }, easing)
	end)

	-- Rotating square scale-up.
	self.square = { rotation = 0, size = 0 }
	G.timer.tween(DURATION, self.square, { rotation = math.pi * 2, size = 80 }, easing, function()
		G.timer.tween(DURATION * 0.5, self.square, { rotation = math.pi * 3, size = 40 }, easing)
	end)
end

function Game:update(t, dt)
	if G.input.is_key_pressed("escape") then
		G.system.quit()
	end

	local changed = false
	if G.input.is_key_pressed("right") then
		self.easing_idx = (self.easing_idx % #EASINGS) + 1
		changed = true
	end
	if G.input.is_key_pressed("left") then
		self.easing_idx = ((self.easing_idx - 2) % #EASINGS) + 1
		changed = true
	end
	if G.input.is_key_pressed("space") then
		changed = true
	end
	if changed then
		self:start()
	end
end

function Game:draw_rings(cx, cy)
	for i = NUM_RINGS, 1, -1 do
		local r = self.ring[i]
		if r and r.radius > 1 then
			local hue = (i - 1) / NUM_RINGS
			local red = math.floor(128 + 127 * math.sin(hue * math.pi * 2))
			local green = math.floor(128 + 127 * math.sin(hue * math.pi * 2 + 2.1))
			local blue = math.floor(128 + 127 * math.sin(hue * math.pi * 2 + 4.2))
			G.graphics.set_color(red, green, blue, math.floor(math.max(r.alpha, 0)))
			G.graphics.draw_circle_outline(cx, cy, r.radius)
		end
	end
end

function Game:draw_bars(bx, by, total_w, max_h)
	local bar_w = total_w / NUM_BARS
	local gap = 3
	for i = 1, NUM_BARS do
		local b = self.bars[i]
		if b and b.height > 0 then
			local hue = (i - 1) / NUM_BARS
			local red = math.floor(60 + 195 * hue)
			local green = math.floor(220 - 140 * hue)
			local blue = math.floor(255 - 200 * hue)
			G.graphics.set_color(red, green, blue, 220)
			local x1 = bx + (i - 1) * bar_w + gap
			local x2 = bx + i * bar_w - gap
			local h = math.min(b.height, max_h)
			G.graphics.draw_rounded_rect(x1, by - h, x2, by, 3)
		end
	end
end

function Game:draw_pendulum(cx, cy, length)
	local a = self.pendulum.angle
	local ex = cx + math.sin(a) * length
	local ey = cy + math.cos(a) * length

	-- Rod.
	G.graphics.set_color(120, 120, 140, 180)
	G.graphics.draw_line(cx, cy, ex, ey)

	-- Pivot.
	G.graphics.set_color(200, 200, 210, 255)
	G.graphics.draw_circle(cx, cy, 5)

	-- Bob.
	G.graphics.set_color(255, 180, 60, 255)
	G.graphics.draw_circle(ex, ey, 14)
	G.graphics.set_color(255, 220, 140, 150)
	G.graphics.draw_circle(ex, ey, 7)
end

function Game:draw_square(cx, cy)
	local s = self.square.size
	if s < 1 then
		return
	end
	local half = s * 0.5

	G.graphics.push()
	G.graphics.translate(cx, cy)
	G.graphics.rotate(self.square.rotation)

	-- Outer square.
	G.graphics.set_color(100, 220, 255, 200)
	G.graphics.draw_rounded_rect_outline(-half, -half, half, half, 6)

	-- Inner filled square.
	local inner = half * 0.6
	G.graphics.set_color(100, 220, 255, 60)
	G.graphics.draw_rounded_rect(-inner, -inner, inner, inner, 4)

	G.graphics.pop()
end

function Game:draw()
	G.graphics.clear(0.05, 0.05, 0.09, 1)
	local ww, wh = G.window.dimensions()

	-- Title with easing name.
	local name = self:current_easing()
	G.graphics.set_color(255, 255, 255, 255)
	G.graphics.draw_text("debug_font.ttf", 36, name, 40, 24)

	-- Page indicator.
	G.graphics.set_color(100, 100, 120, 255)
	G.graphics.print(string.format("%d / %d", self.easing_idx, #EASINGS), 40, 68)

	-- Layout: 2x2 quadrants.
	local mid_x = ww * 0.5
	local mid_y = (wh + 80) * 0.5

	-- Top-left: concentric rings.
	G.graphics.set_color(60, 60, 80, 120)
	G.graphics.draw_rounded_rect_outline(20, 100, mid_x - 10, mid_y - 10, 8)
	self:draw_rings(mid_x * 0.5, (100 + mid_y - 10) * 0.5)

	-- Top-right: equalizer bars.
	G.graphics.set_color(60, 60, 80, 120)
	G.graphics.draw_rounded_rect_outline(mid_x + 10, 100, ww - 20, mid_y - 10, 8)
	self:draw_bars(mid_x + 30, mid_y - 30, (ww - mid_x) - 60, mid_y - 150)

	-- Bottom-left: pendulum.
	G.graphics.set_color(60, 60, 80, 120)
	G.graphics.draw_rounded_rect_outline(20, mid_y + 10, mid_x - 10, wh - 40, 8)
	local pend_cx = (20 + mid_x - 10) * 0.5
	local pend_cy = mid_y + 30
	local pend_len = (wh - 40 - mid_y - 30) * 0.75
	self:draw_pendulum(pend_cx, pend_cy, pend_len)

	-- Bottom-right: rotating square.
	G.graphics.set_color(60, 60, 80, 120)
	G.graphics.draw_rounded_rect_outline(mid_x + 10, mid_y + 10, ww - 20, wh - 40, 8)
	local sq_cx = (mid_x + 10 + ww - 20) * 0.5
	local sq_cy = (mid_y + 10 + wh - 40) * 0.5
	self:draw_square(sq_cx, sq_cy)

	-- Footer.
	G.graphics.set_color(100, 100, 120, 255)
	G.graphics.print("Left/Right: change easing   Space: replay   Esc: quit", 20, wh - 24)
end

return Game
