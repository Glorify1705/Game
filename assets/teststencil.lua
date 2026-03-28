-- Test program for stencil buffer operations.
-- Demonstrates using arbitrary geometry as masks: fog of war, shaped viewports,
-- and boolean mask composition.

local Game = {}

function Game:init()
	G.window.set_title("Stencil Buffer Test")
	self.time = 0
end

function Game:update(t, dt)
	if G.input.is_key_pressed("escape") or G.input.is_key_pressed("q") then
		G.system.quit()
	end
	self.time = t
end

local function draw_scene(x_off, y_off)
	-- A colorful scene: checkerboard + shapes.
	for row = 0, 9 do
		for col = 0, 14 do
			local px = x_off + col * 24
			local py = y_off + col * 24 -- intentional: diagonal pattern
			if (row + col) % 2 == 0 then
				G.graphics.set_color(60, 100, 180, 255)
			else
				G.graphics.set_color(40, 70, 140, 255)
			end
			G.graphics.draw_rect(x_off + col * 24, y_off + row * 24, x_off + col * 24 + 24, y_off + row * 24 + 24)
		end
	end
	-- Some shapes on top.
	G.graphics.set_color(255, 200, 50, 255)
	G.graphics.draw_circle(x_off + 180, y_off + 120, 40)
	G.graphics.set_color(50, 255, 150, 255)
	G.graphics.draw_rect(x_off + 60, y_off + 80, x_off + 120, y_off + 160)
	G.graphics.set_color(255, 80, 80, 255)
	G.graphics.draw_triangle(x_off + 250, y_off + 60, x_off + 220, y_off + 160, x_off + 280, y_off + 160)
end

function Game:draw()
	local w, h = G.window.dimensions()
	local t = self.time

	G.graphics.clear(0.08, 0.08, 0.1, 1)

	-- Demo 1: Circular reveal (fog of war style).
	-- Write a circle into the stencil, then only draw the scene inside it.
	local cx1 = 180 + math.sin(t * 0.8) * 60
	local cy1 = 150 + math.cos(t * 0.6) * 40

	G.graphics.stencil_begin("replace", 1)
	G.graphics.draw_circle(cx1, cy1, 100)
	G.graphics.stencil_end()

	G.graphics.set_stencil_test("equal", 1)
	G.graphics.set_color(255, 255, 255, 255)
	draw_scene(0, 30)
	G.graphics.clear_stencil_test()

	-- Draw a dim version of the scene outside the reveal (fog).
	G.graphics.set_stencil_test("notequal", 1)
	G.graphics.set_color(30, 30, 40, 255)
	G.graphics.draw_rect(0, 30, 360, 270)
	G.graphics.clear_stencil_test()

	-- Demo 2: Triangle mask.
	-- The scene is only visible through a rotating triangle.
	local cx2 = 540
	local cy2 = 150
	local tri_r = 100

	G.graphics.stencil_begin("replace", 2)
	G.graphics.push()
	G.graphics.translate(cx2, cy2)
	G.graphics.rotate(t * 0.5)
	G.graphics.draw_triangle(-tri_r, tri_r * 0.7, tri_r, tri_r * 0.7, 0, -tri_r)
	G.graphics.pop()
	G.graphics.stencil_end()

	G.graphics.set_stencil_test("equal", 2)
	G.graphics.set_color(255, 255, 255, 255)
	draw_scene(360, 30)
	G.graphics.clear_stencil_test()

	-- Dim background outside.
	G.graphics.set_stencil_test("notequal", 2)
	G.graphics.set_color(30, 30, 40, 255)
	G.graphics.draw_rect(360, 30, 720, 270)
	G.graphics.clear_stencil_test()

	-- Demo 3: Multiple stencil values (composite mask).
	-- Two overlapping circles with different stencil values. Show three zones:
	--   value=1 only (left circle exclusive)   -> red tint
	--   value=2 only (right circle exclusive)  -> blue tint
	--   We use increment so the overlap area gets value=2.
	local left_cx = 180
	local right_cx = 280
	local demo3_cy = 420

	G.graphics.stencil_begin("increment", 1)
	G.graphics.draw_circle(left_cx, demo3_cy, 90)
	G.graphics.draw_circle(right_cx, demo3_cy, 90)
	G.graphics.stencil_end()

	-- Where stencil == 1 (single circle, no overlap): green.
	G.graphics.set_stencil_test("equal", 1)
	G.graphics.set_color(50, 180, 50, 255)
	G.graphics.draw_rect(0, 300, 460, 560)
	G.graphics.clear_stencil_test()

	-- Where stencil == 2 (overlap): bright yellow.
	G.graphics.set_stencil_test("equal", 2)
	G.graphics.set_color(255, 220, 50, 255)
	G.graphics.draw_rect(0, 300, 460, 560)
	G.graphics.clear_stencil_test()

	-- Dim surround.
	G.graphics.set_stencil_test("equal", 0)
	G.graphics.set_color(20, 20, 30, 255)
	G.graphics.draw_rect(0, 300, 460, 560)
	G.graphics.clear_stencil_test()

	-- Demo 4: Rounded-rect portal.
	-- Use a rounded rectangle as a stencil mask to create a portal window.
	local portal_x = 490
	local portal_y = 320
	local portal_w = 220
	local portal_h = 200
	local portal_r = 30

	G.graphics.stencil_begin("replace", 3)
	G.graphics.draw_rounded_rect(portal_x, portal_y, portal_x + portal_w, portal_y + portal_h, portal_r)
	G.graphics.stencil_end()

	G.graphics.set_stencil_test("equal", 3)
	-- Animated scene inside the portal.
	G.graphics.set_color(15, 15, 40, 255)
	G.graphics.draw_rect(portal_x, portal_y, portal_x + portal_w, portal_y + portal_h)
	-- Orbiting dots.
	for i = 1, 12 do
		local angle = t * (0.5 + i * 0.1) + i * (math.pi * 2 / 12)
		local r = 40 + i * 5
		local px = portal_x + portal_w / 2 + math.cos(angle) * r
		local py = portal_y + portal_h / 2 + math.sin(angle) * r
		local hue = (i / 12) * 255
		G.graphics.set_color(255, hue, 255 - hue, 255)
		G.graphics.draw_circle(px, py, 8)
	end
	G.graphics.clear_stencil_test()

	-- Portal border.
	G.graphics.set_color(200, 200, 220, 255)
	G.graphics.draw_rounded_rect_outline(portal_x, portal_y, portal_x + portal_w, portal_y + portal_h, portal_r)

	-- Labels.
	G.graphics.set_color(255, 255, 255, 255)
	G.graphics.draw_text("terminus.ttf", 16, "Circular reveal (fog of war)", 10, 10)
	G.graphics.draw_text("terminus.ttf", 16, "Rotating triangle mask", 370, 10)
	G.graphics.draw_text("terminus.ttf", 16, "Increment (overlap = yellow)", 10, 285)
	G.graphics.draw_text("terminus.ttf", 16, "Rounded-rect portal", 490, 300)
	G.graphics.draw_text("terminus.ttf", 16, "Press Q to quit", 10, h - 25)
end

return Game
