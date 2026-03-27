-- SDF text effects test.
-- Demonstrates text outlines with various colors, thicknesses, and sizes.

local Game = {}

local FONT = "ponderosa.ttf"

function Game:init()
	G.window.set_title("SDF Text Effects Test")
	G.window.set_dimensions(1100, 700)
end

function Game:update(t, dt)
	if G.input.is_key_pressed("escape") or G.input.is_key_pressed("q") then
		G.system.quit()
	end
end

function Game:draw()
	local y = 30

	-- Title (no outline).
	G.graphics.set_color("white")
	G.graphics.draw_text(FONT, 28, "SDF Text Outline Effects", 20, y)
	y = y + 50

	-- Black outline on white text.
	G.graphics.set_color("white")
	G.graphics.set_text_outline(0, 0, 0, 255, 0.15)
	G.graphics.draw_text(FONT, 36, "Black outline", 20, y)
	G.graphics.clear_text_outline()
	y = y + 55

	-- Red outline on white text.
	G.graphics.set_color("white")
	G.graphics.set_text_outline(255, 40, 40, 255, 0.15)
	G.graphics.draw_text(FONT, 36, "Red outline", 20, y)
	G.graphics.clear_text_outline()
	y = y + 55

	-- Blue outline on yellow text.
	G.graphics.set_color(255, 255, 0, 255)
	G.graphics.set_text_outline(30, 60, 220, 255, 0.2)
	G.graphics.draw_text(FONT, 36, "Blue outline on yellow", 20, y)
	G.graphics.clear_text_outline()
	y = y + 55

	-- Green glow (semi-transparent outline).
	G.graphics.set_color("white")
	G.graphics.set_text_outline(0, 255, 80, 160, 0.25)
	G.graphics.draw_text(FONT, 36, "Green glow", 20, y)
	G.graphics.clear_text_outline()
	y = y + 55

	-- Varying thickness comparison.
	G.graphics.set_color("white")
	G.graphics.clear_text_outline()
	G.graphics.draw_text(FONT, 20, "Outline thickness comparison:", 20, y)
	y = y + 30

	local thicknesses = { 0.05, 0.1, 0.15, 0.2, 0.25, 0.3 }
	for _, thick in ipairs(thicknesses) do
		G.graphics.set_color("white")
		G.graphics.set_text_outline(0, 0, 0, 255, thick)
		G.graphics.draw_text(FONT, 28, string.format("thickness = %.2f", thick), 40, y)
		G.graphics.clear_text_outline()
		y = y + 40
	end

	-- Different sizes with same outline.
	y = y + 10
	G.graphics.set_color("white")
	G.graphics.draw_text(FONT, 20, "Same outline at different sizes:", 20, y)
	y = y + 30

	local sizes = { 14, 20, 28, 40, 56 }
	for _, sz in ipairs(sizes) do
		G.graphics.set_color("white")
		G.graphics.set_text_outline(60, 0, 120, 255, 0.15)
		G.graphics.draw_text(FONT, sz, "Hello", 40, y)
		G.graphics.clear_text_outline()
		G.graphics.set_color(120, 120, 120, 255)
		local w, _ = G.graphics.text_dimensions(FONT, sz, "Hello")
		G.graphics.draw_text(FONT, 12, "size=" .. sz, 50 + w, y)
		y = y + sz + 10
	end

	-- Right column: outlined colored text.
	local rx = 600
	local ry = 30
	G.graphics.set_color("white")
	G.graphics.draw_text(FONT, 28, "Outlined + Colored Text", rx, ry)
	ry = ry + 50

	G.graphics.set_text_outline(0, 0, 0, 255, 0.2)
	G.graphics.draw_text_colored(FONT, 30, {
		{ 255, 100, 100, 255 },
		"Red ",
		{ 100, 255, 100, 255 },
		"Green ",
		{ 100, 100, 255, 255 },
		"Blue",
	}, rx, ry)
	G.graphics.clear_text_outline()
	ry = ry + 50

	G.graphics.set_text_outline(255, 255, 255, 200, 0.15)
	G.graphics.draw_text_colored(FONT, 30, {
		{ 200, 50, 50, 255 },
		"Fire ",
		{ 255, 160, 0, 255 },
		"and ",
		{ 50, 50, 200, 255 },
		"Ice",
	}, rx, ry)
	G.graphics.clear_text_outline()
end

return Game
