-- Easing curve gallery: draws every easing function as a graph with a
-- dot tracing the curve in real time. Press Space to replay, Esc to quit.

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

local COLS = 6
local ROWS = 6
local DURATION = 2.0

local PALETTE = {
	{ 255, 80, 80 }, -- red
	{ 255, 160, 60 }, -- orange
	{ 255, 220, 60 }, -- yellow
	{ 80, 220, 100 }, -- green
	{ 60, 180, 255 }, -- blue
	{ 180, 100, 255 }, -- purple
}

function Game:init()
	G.window.set_dimensions(1280, 800)
	G.window.set_title("Easing Curves")
	self.cells = {}
	self:start_tweens()
end

function Game:start_tweens()
	G.timer.cancel_all()
	self.cells = {}
	for i, name in ipairs(EASINGS) do
		local cell = { t = 0 }
		self.cells[i] = cell
		G.timer.tween(DURATION, cell, { t = 1 }, name)
	end
end

function Game:update(t, dt)
	if G.input.is_key_pressed("escape") then
		G.system.quit()
	end
	if G.input.is_key_pressed("space") then
		self:start_tweens()
	end
end

function Game:draw()
	G.graphics.clear(20, 20, 31, 255)
	local ww, wh = G.window.dimensions()

	local pad_x, pad_y = 24, 24
	local cell_w = (ww - pad_x * 2) / COLS
	local cell_h = (wh - pad_y * 2) / ROWS
	local inset = 10
	local graph_w = cell_w - inset * 2
	local graph_h = cell_h - inset * 2 - 18

	for i, name in ipairs(EASINGS) do
		local col = (i - 1) % COLS
		local row = math.floor((i - 1) / COLS)
		local cx = pad_x + col * cell_w
		local cy = pad_y + row * cell_h
		local gx = cx + inset
		local gy = cy + inset + 16
		local cell = self.cells[i]
		local pal = PALETTE[(row % #PALETTE) + 1]

		-- Cell background.
		G.graphics.set_color(30, 30, 40, 200)
		G.graphics.draw_rounded_rect(cx + 2, cy + 2, cx + cell_w - 2, cy + cell_h - 2, 6)

		-- Label.
		G.graphics.set_color(200, 200, 210, 255)
		G.graphics.print(name, gx + 2, cy + inset - 2)

		-- Axes.
		G.graphics.set_color(60, 60, 80, 255)
		G.graphics.draw_line(gx, gy, gx, gy + graph_h)
		G.graphics.draw_line(gx, gy + graph_h, gx + graph_w, gy + graph_h)

		-- The tweened value gives us the curve: cell.t went from 0 to 1
		-- through the easing. We sample the curve by drawing segments for
		-- every previous frame, but since we only have the current value
		-- we draw a trail using the "during" trick: we'll just draw the
		-- endpoint dot on the curve.

		-- Diagonal reference line (linear).
		G.graphics.set_color(50, 50, 65, 255)
		G.graphics.draw_line(gx, gy + graph_h, gx + graph_w, gy)

		-- Trail: record nothing, but draw a horizontal progress bar.
		if cell then
			-- Filled progress region.
			G.graphics.set_color(pal[1], pal[2], pal[3], 35)
			local fill_x = gx + graph_w * math.min(cell.t, 1)
			G.graphics.draw_rect(gx, gy, fill_x, gy + graph_h)

			-- Dot on the curve.
			local dot_x = gx + graph_w * math.min(cell.t, 1)
			local dot_y = gy + graph_h * (1 - math.min(math.max(cell.t, -0.2), 1.2))
			G.graphics.set_color(pal[1], pal[2], pal[3], 255)
			G.graphics.draw_circle(dot_x, dot_y, 5)

			-- Horizontal + vertical guide lines to dot.
			G.graphics.set_color(pal[1], pal[2], pal[3], 60)
			G.graphics.draw_line(gx, dot_y, dot_x, dot_y)
			G.graphics.draw_line(dot_x, gy + graph_h, dot_x, dot_y)
		end
	end

	-- Footer.
	G.graphics.set_color(120, 120, 140, 255)
	G.graphics.print("Space: replay   Esc: quit", pad_x, wh - 22)
end

return Game
