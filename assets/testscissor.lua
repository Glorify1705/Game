-- Test program for scissor rect clipping.
-- Demonstrates axis-aligned rectangular clipping that restricts all drawing
-- to a screen-space rectangle, independent of the transform stack.

local Game = {}

function Game:init()
	G.window.set_title("Scissor Test")
	self.time = 0
end

function Game:update(t, dt)
	if G.input.is_key_pressed("escape") or G.input.is_key_pressed("q") then
		G.system.quit()
	end
	self.time = t
end

function Game:draw()
	local w, h = G.window.dimensions()
	local t = self.time

	G.graphics.clear(0.12, 0.12, 0.15, 1)

	-- Draw a background grid (visible everywhere).
	G.graphics.set_color(40, 40, 50, 255)
	for x = 0, w, 32 do
		G.graphics.draw_line(x, 0, x, h)
	end
	for y = 0, h, 32 do
		G.graphics.draw_line(0, y, w, y)
	end

	-- Scissor region 1: a fixed box on the left.
	-- Only drawing inside this 300x250 rect is visible.
	G.graphics.set_scissor(30, 60, 300, 250)

	G.graphics.set_color(30, 30, 60, 255)
	G.graphics.draw_rect(0, 0, w, h)
	-- Circles that extend beyond the scissor get clipped.
	G.graphics.set_color(255, 100, 80, 255)
	G.graphics.draw_circle(180, 185, 120)
	G.graphics.set_color(80, 200, 255, 255)
	G.graphics.draw_circle(180, 185, 80)
	G.graphics.set_color(255, 255, 100, 255)
	G.graphics.draw_circle(180, 185, 40)

	G.graphics.clear_scissor()

	-- Scissor region 2: an animated box on the right.
	local sx = 380 + math.sin(t) * 40
	local sy = 80 + math.cos(t * 0.7) * 30
	G.graphics.set_scissor(sx, sy, 250, 200)

	G.graphics.set_color(20, 40, 20, 255)
	G.graphics.draw_rect(0, 0, w, h)
	-- A rotating square that gets clipped by the moving scissor.
	G.graphics.set_color(100, 255, 100, 255)
	G.graphics.push()
	G.graphics.translate(sx + 125, sy + 100)
	G.graphics.rotate(t)
	G.graphics.draw_rect(-80, -80, 80, 80)
	G.graphics.pop()
	-- Text also gets clipped.
	G.graphics.set_color(255, 255, 255, 255)
	G.graphics.draw_text("terminus.ttf", 20,
		"This text is clipped by the scissor rect!", sx + 10, sy + 10)

	G.graphics.clear_scissor()

	-- Scissor region 3: a small viewport for a minimap-like area.
	G.graphics.set_scissor(30, 340, 200, 200)

	G.graphics.set_color(10, 10, 30, 255)
	G.graphics.draw_rect(30, 340, 230, 540)
	-- Pulsing concentric rings.
	local cx, cy = 130, 440
	for i = 1, 6 do
		local r = i * 25 + math.sin(t * 2 + i) * 10
		local brightness = 80 + i * 25
		G.graphics.set_color(brightness, 50, 255 - brightness, 255)
		G.graphics.draw_circle_outline(cx, cy, r)
	end

	G.graphics.clear_scissor()

	-- Draw borders around the scissor regions (outside scissor, so always visible).
	G.graphics.set_color(255, 255, 255, 100)
	G.graphics.draw_rect_outline(30, 60, 330, 310)
	G.graphics.draw_rect_outline(sx, sy, sx + 250, sy + 200)
	G.graphics.draw_rect_outline(30, 340, 230, 540)

	-- Labels.
	G.graphics.set_color(255, 255, 255, 255)
	G.graphics.draw_text("terminus.ttf", 16, "Fixed scissor", 30, 40)
	G.graphics.draw_text("terminus.ttf", 16, "Moving scissor", sx, sy - 20)
	G.graphics.draw_text("terminus.ttf", 16, "Mini viewport", 30, 320)
	G.graphics.draw_text("terminus.ttf", 16, "Press Q to quit", 10, h - 25)
end

return Game
