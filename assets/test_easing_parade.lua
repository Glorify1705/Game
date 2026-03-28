-- Easing parade: animated shapes showcasing every easing family.
-- Each row shows in / out / in-out variants of one easing type.
-- Press Space to replay, M to toggle between position and scale
-- animation, Esc to quit.

local Game = {}

local FAMILIES = {
	{ name = "quad", color = { 255, 80, 80 } },
	{ name = "cubic", color = { 255, 160, 60 } },
	{ name = "quart", color = { 255, 220, 60 } },
	{ name = "quint", color = { 180, 230, 50 } },
	{ name = "sine", color = { 80, 220, 100 } },
	{ name = "expo", color = { 60, 200, 200 } },
	{ name = "circ", color = { 60, 180, 255 } },
	{ name = "back", color = { 120, 120, 255 } },
	{ name = "elastic", color = { 180, 100, 255 } },
	{ name = "bounce", color = { 255, 100, 200 } },
}

local VARIANTS = { "in-", "out-", "in-out-" }
local DURATION = 2.5
local PAUSE = 0.8

function Game:init()
	G.window.set_dimensions(1280, 800)
	G.window.set_title("Easing Parade")
	self.mode = "position"
	self.items = {}
	self:start_animation()
end

function Game:start_animation()
	G.timer.cancel_all()
	self.items = {}
	local ww = G.window.dimensions()
	local x_start = 180
	local x_end = ww - 40

	for row, fam in ipairs(FAMILIES) do
		for vcol, prefix in ipairs(VARIANTS) do
			local easing = prefix .. fam.name
			local item = { x = x_start, scale = 0.0, alpha = 0 }
			local idx = (row - 1) * 3 + vcol
			self.items[idx] = item

			-- Stagger start slightly per row for a wave effect.
			local delay = (row - 1) * 0.07

			G.timer.after(delay, function()
				item.alpha = 255
				if self.mode == "position" then
					item.scale = 1.0
					G.timer.tween(DURATION, item, { x = x_end }, easing, function()
						G.timer.after(PAUSE, function()
							G.timer.tween(DURATION, item, { x = x_start }, easing)
						end)
					end)
				else
					item.x = x_start + (x_end - x_start) * 0.5
					G.timer.tween(DURATION, item, { scale = 1.0 }, easing, function()
						G.timer.after(PAUSE, function()
							G.timer.tween(DURATION, item, { scale = 0.0 }, easing)
						end)
					end)
				end
			end)
		end
	end
end

function Game:update(t, dt)
	if G.input.is_key_pressed("escape") then
		G.system.quit()
	end
	if G.input.is_key_pressed("space") then
		self:start_animation()
	end
	if G.input.is_key_pressed("m") then
		if self.mode == "position" then
			self.mode = "scale"
		else
			self.mode = "position"
		end
		self:start_animation()
	end
end

function Game:draw()
	G.graphics.clear(0.06, 0.06, 0.10, 1)
	local ww, wh = G.window.dimensions()

	local top_pad = 40
	local bot_pad = 30
	local row_h = (wh - top_pad - bot_pad) / #FAMILIES
	local radius = math.min(row_h * 0.28, 14)

	-- Column headers.
	G.graphics.set_color(100, 100, 120, 255)
	local header_xs = { 210, 560, 920 }
	local header_labels = { "in", "out", "in-out" }
	for i = 1, 3 do
		G.graphics.print(header_labels[i], header_xs[i], 16)
	end

	for row, fam in ipairs(FAMILIES) do
		local cy = top_pad + (row - 0.5) * row_h
		local r, g, b = fam.color[1], fam.color[2], fam.color[3]

		-- Row label.
		G.graphics.set_color(r, g, b, 180)
		G.graphics.print(fam.name, 16, cy - 8)

		-- Horizontal track line.
		G.graphics.set_color(40, 40, 55, 255)
		G.graphics.draw_line(180, cy, ww - 30, cy)

		-- Three variant dots.
		for vcol = 1, 3 do
			local idx = (row - 1) * 3 + vcol
			local item = self.items[idx]
			if item and item.alpha > 0 then
				local s = math.max(item.scale, 0)
				local draw_r = radius * s

				-- Glow.
				if draw_r > 2 then
					G.graphics.set_color(r, g, b, 40)
					G.graphics.draw_circle(item.x, cy, draw_r + 6)
				end

				-- Main dot.
				G.graphics.set_color(r, g, b, item.alpha)
				G.graphics.draw_circle(item.x, cy, math.max(draw_r, 2))

				-- Bright center.
				if draw_r > 4 then
					G.graphics.set_color(255, 255, 255, 120)
					G.graphics.draw_circle(item.x, cy, draw_r * 0.35)
				end
			end
		end

		-- Thin separator.
		if row < #FAMILIES then
			G.graphics.set_color(40, 40, 55, 100)
			G.graphics.draw_line(14, cy + row_h * 0.5, ww - 14, cy + row_h * 0.5)
		end
	end

	-- Footer.
	G.graphics.set_color(100, 100, 120, 255)
	G.graphics.print(
		string.format("Mode: %s   |   Space: replay   M: toggle mode   Esc: quit", self.mode),
		16,
		wh - 24
	)
end

return Game
