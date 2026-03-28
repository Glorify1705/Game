-- Test program for drawing primitives: outlines, ellipses, rounded rectangles.

local Game = {}

function Game:init()
	G.window.set_title("Drawing Primitives Test")
	self.angle = 0
end

function Game:update(t, dt)
	if G.input.is_key_pressed("escape") or G.input.is_key_pressed("q") then
		G.system.quit()
	end
	self.angle = t * 0.5
end

function Game:draw()
	G.graphics.clear(0.1, 0.1, 0.15, 1)

	local w, h = G.window.dimensions()
	local col_w = w / 4
	local row_h = h / 3

	local label_h = 25
	local pad = 15

	-- Row 1: Filled shapes
	local y1 = label_h
	G.graphics.set_color(80, 160, 255, 255)
	G.graphics.draw_rect(pad, y1 + pad, col_w - pad, row_h - pad)

	G.graphics.set_color(255, 100, 100, 255)
	G.graphics.draw_circle(col_w * 1.5, y1 + (row_h - y1) / 2 + y1 / 2, 60)

	G.graphics.set_color(100, 255, 100, 255)
	G.graphics.draw_triangle(col_w * 2 + pad, row_h - pad, col_w * 2.5, y1 + pad, col_w * 3 - pad, row_h - pad)

	G.graphics.set_color(255, 200, 50, 255)
	G.graphics.draw_ellipse(col_w * 3.5, y1 + (row_h - y1) / 2 + y1 / 2, 80, 40)

	-- Row 2: Outlined shapes
	local y_off = row_h

	G.graphics.set_color(80, 160, 255, 255)
	G.graphics.draw_rect_outline(pad, y_off + label_h + pad, col_w - pad, y_off + row_h - pad)

	G.graphics.set_color(255, 100, 100, 255)
	G.graphics.draw_circle_outline(col_w * 1.5, y_off + label_h / 2 + row_h / 2, 60)

	G.graphics.set_color(100, 255, 100, 255)
	G.graphics.draw_triangle_outline(
		col_w * 2 + pad,
		y_off + row_h - pad,
		col_w * 2.5,
		y_off + label_h + pad,
		col_w * 3 - pad,
		y_off + row_h - pad
	)

	G.graphics.set_color(255, 200, 50, 255)
	G.graphics.draw_ellipse_outline(col_w * 3.5, y_off + label_h / 2 + row_h / 2, 80, 40)

	-- Row 3: Rounded rectangles and rotated outlines
	y_off = row_h * 2

	G.graphics.set_color(200, 100, 255, 255)
	G.graphics.draw_rounded_rect(pad, y_off + label_h + pad, col_w - pad, y_off + row_h - pad, 20)

	G.graphics.set_color(100, 200, 255, 255)
	G.graphics.draw_rounded_rect_outline(col_w + pad, y_off + label_h + pad, col_w * 2 - pad, y_off + row_h - pad, 15)

	-- Rotated rectangle outline
	G.graphics.set_color(255, 150, 50, 255)
	local cx = col_w * 2.5
	local cy = y_off + label_h / 2 + row_h / 2
	G.graphics.draw_rect_outline(cx - 50, cy - 35, cx + 50, cy + 35, self.angle)

	-- Concentric ellipses
	G.graphics.set_color(150, 255, 200, 255)
	G.graphics.draw_ellipse_outline(col_w * 3.5, y_off + label_h / 2 + row_h / 2, 80, 50)
	G.graphics.set_color(255, 150, 200, 255)
	G.graphics.draw_ellipse_outline(col_w * 3.5, y_off + label_h / 2 + row_h / 2, 50, 80)

	-- Labels
	G.graphics.set_color(255, 255, 255, 255)
	G.graphics.print("Filled", 10, 5)
	G.graphics.print("Outlined", 10, row_h + 5)
	G.graphics.print("Rounded / Rotated", 10, row_h * 2 + 5)
end

return Game
