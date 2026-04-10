local Random = require("random")
local Starfield = require("starfield")
local Scores = require("scores")

local FONT = "ponderosa.ttf"
local FONT_SM = 18
local FONT_MD = 24
local FONT_LG = 36

local function text_w(size, text)
	return G.graphics.text_dimensions(FONT, size, text)
end

local function draw_text_centered(text, size, cx, y)
	local w = text_w(size, text)
	G.graphics.draw_text(FONT, size, text, cx - w / 2, y)
end

-- Arcade-style name entry after game over.
local NameEntry = {}
local NAME_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "
local NAME_LENGTH = 3

function NameEntry:init(score)
	self.screen_w, self.screen_h = G.window.dimensions()
	self.score = score
	self.chars = { 1, 1, 1 }  -- indices into NAME_CHARS
	self.pos = 1               -- which character we're editing (1..3)
	self.done = false
	self.blink = 0
	self.rnd = Random()
	self.starfield = Starfield(self.screen_w, self.screen_h, self.rnd.rnd)
end

function NameEntry:update(t, dt)
	self.starfield:update(dt)
	self.blink = self.blink + dt

	if G.input.is_key_pressed("up") or G.input.is_key_pressed("w") then
		self.chars[self.pos] = self.chars[self.pos] - 1
		if self.chars[self.pos] < 1 then self.chars[self.pos] = #NAME_CHARS end
	end
	if G.input.is_key_pressed("down") or G.input.is_key_pressed("s") then
		self.chars[self.pos] = self.chars[self.pos] + 1
		if self.chars[self.pos] > #NAME_CHARS then self.chars[self.pos] = 1 end
	end
	if G.input.is_key_pressed("right") or G.input.is_key_pressed("d") then
		if self.pos < NAME_LENGTH then self.pos = self.pos + 1 end
	end
	if G.input.is_key_pressed("left") or G.input.is_key_pressed("a") then
		if self.pos > 1 then self.pos = self.pos - 1 end
	end
	if G.input.is_key_pressed("return") or G.input.is_key_pressed("space") then
		self.done = true
	end
end

function NameEntry:get_name()
	local parts = {}
	for i = 1, NAME_LENGTH do
		parts[i] = NAME_CHARS:sub(self.chars[i], self.chars[i])
	end
	return table.concat(parts)
end

function NameEntry:draw()
	G.graphics.clear()
	self.starfield:draw(0, 0)
	local cx = self.screen_w / 2
	local cy = self.screen_h / 2

	G.graphics.set_color(200, 0, 0, 255)
	draw_text_centered("GAME OVER", FONT_LG, cx, cy - 120)
	G.graphics.set_color("white")
	draw_text_centered("SCORE: " .. self.score, FONT_MD, cx, cy - 80)

	G.graphics.set_color(100, 200, 255, 255)
	draw_text_centered("ENTER YOUR NAME", FONT_MD, cx, cy - 30)

	-- Draw the three character slots.
	local slot_w = 40
	local total_w = NAME_LENGTH * slot_w
	local start_x = cx - total_w / 2

	for i = 1, NAME_LENGTH do
		local ch = NAME_CHARS:sub(self.chars[i], self.chars[i])
		local sx = start_x + (i - 1) * slot_w + slot_w / 2
		local sy = cy + 20

		if i == self.pos then
			-- Blinking underscore for active slot.
			local show = math.floor(self.blink * 3) % 2 == 0
			G.graphics.set_color(255, 255, 100, 255)
			draw_text_centered(ch, FONT_LG, sx, sy)
			if show then
				G.graphics.set_color(255, 255, 100, 200)
				local uw = text_w(FONT_LG, "_")
				G.graphics.draw_text(FONT, FONT_LG, "_", sx - uw / 2, sy + 5)
			end
			-- Up/down arrows.
			G.graphics.set_color(255, 255, 100, 150)
			draw_text_centered("^", FONT_SM, sx, sy - 30)
			draw_text_centered("v", FONT_SM, sx, sy + 40)
		else
			G.graphics.set_color(200, 200, 200, 255)
			draw_text_centered(ch, FONT_LG, sx, sy)
		end
	end

	G.graphics.set_color(150, 150, 150, 255)
	draw_text_centered("UP/DOWN to change, LEFT/RIGHT to move, ENTER to confirm", FONT_SM, cx, cy + 90)
	G.graphics.set_color("white")
end

-- High scores display screen.
local HighScoresView = {}

function HighScoresView:init()
	self.screen_w, self.screen_h = G.window.dimensions()
	self.scores = Scores.load()
	self.rnd = Random()
	self.starfield = Starfield(self.screen_w, self.screen_h, self.rnd.rnd)
end

function HighScoresView:update(t, dt)
	self.starfield:update(dt)
	if G.input.is_key_pressed("return") or G.input.is_key_pressed("space")
		or G.input.is_key_pressed("escape") then
		return "back"
	end
end

function HighScoresView:draw()
	G.graphics.clear()
	self.starfield:draw(0, 0)
	local cx = self.screen_w / 2
	local cy = self.screen_h / 2

	G.graphics.set_color(100, 200, 255, 255)
	draw_text_centered("HIGH SCORES", FONT_LG, cx, 60)

	if #self.scores == 0 then
		G.graphics.set_color(150, 150, 150, 255)
		draw_text_centered("No scores yet!", FONT_MD, cx, cy)
	else
		local start_y = 120
		for i, entry in ipairs(self.scores) do
			local y = start_y + (i - 1) * 35
			local rank = tostring(i) .. "."
			local name = entry.name or "???"
			local score = tostring(entry.score or 0)

			if i == 1 then
				G.graphics.set_color(255, 215, 0, 255)
			elseif i == 2 then
				G.graphics.set_color(192, 192, 192, 255)
			elseif i == 3 then
				G.graphics.set_color(205, 127, 50, 255)
			else
				G.graphics.set_color(200, 200, 200, 255)
			end

			local line = rank .. " " .. name .. "  " .. score
			draw_text_centered(line, FONT_MD, cx, y)
		end
	end

	G.graphics.set_color(150, 150, 150, 255)
	draw_text_centered("Press ENTER to return", FONT_SM, cx, self.screen_h - 50)
	G.graphics.set_color("white")
end

return {
	NameEntry = NameEntry,
	HighScoresView = HighScoresView,
}
