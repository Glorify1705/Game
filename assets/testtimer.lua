-- Test program for the timer system.
-- Demonstrates: after, every, during, tween, cooldown, cancel.
-- Controls:
--   1: Fire G.timer.after (flash after 1s)
--   2: Fire G.timer.every (spawn dots every 0.5s, 6 times)
--   3: Fire G.timer.during (progress bar over 2s)
--   4: Fire G.timer.tween (move box across screen)
--   5: Fire G.timer.cooldown (click-ready indicator, 1s cooldown)
--   C: Cancel all timers
--   Esc: Quit

local Game = {}

function Game:init()
	G.window.set_title("Timer System Test")

	self.flash_alpha = 0
	self.dots = {}
	self.progress = 0
	self.progress_active = false
	self.box = { x = 50, y = 400 }
	self.cooldown_ready = true
	self.cooldown_fires = 0
	self.log = {}
	self.log_max = 12
end

function Game:add_log(msg)
	table.insert(self.log, 1, string.format("%.1f  %s", G.clock.gametime(), msg))
	if #self.log > self.log_max then
		table.remove(self.log)
	end
end

function Game:update(t, dt)
	if G.input.is_key_pressed("escape") then
		G.system.quit()
	end

	if G.input.is_key_pressed("1") then
		self:add_log("G.timer.after: flash in 1s")
		G.timer.after(1.0, function()
			self.flash_alpha = 255
			self:add_log("FLASH!")
			G.timer.during(0.5, function(dt2, elapsed, frac)
				self.flash_alpha = 255 * (1 - frac)
			end, nil, "flash-fade")
		end, "flash")
	end

	if G.input.is_key_pressed("2") then
		self.dots = {}
		self:add_log("G.timer.every: 6 dots, 0.5s apart")
		G.timer.every(0.5, function()
			local w, h = G.window.dimensions()
			table.insert(self.dots, {
				x = 100 + math.random() * (w - 200),
				y = 200 + math.random() * (h - 300),
			})
			self:add_log("  dot #" .. #self.dots)
		end, 6, "dots")
	end

	if G.input.is_key_pressed("3") then
		self.progress = 0
		self.progress_active = true
		self:add_log("G.timer.during: 2s progress bar")
		G.timer.during(2.0, function(dt2, elapsed, frac)
			self.progress = frac
		end, function()
			self.progress = 1
			self:add_log("  progress complete")
			G.timer.after(0.5, function()
				self.progress_active = false
			end)
		end, "progress")
	end

	if G.input.is_key_pressed("4") then
		self.box.x = 50
		self:add_log("G.timer.tween: box slide (out-cubic)")
		local w = G.window.dimensions()
		G.timer.tween(1.5, self.box, { x = w - 100 }, "out-cubic", function()
			self:add_log("  tween done")
		end, "box-tween")
	end

	if G.input.is_key_pressed("5") then
		self.cooldown_ready = true
		self.cooldown_fires = 0
		self:add_log("G.timer.cooldown: 1s, needs space held")
		G.timer.cooldown(1.0, function()
			return G.input.is_key_down("space")
		end, function()
			self.cooldown_fires = self.cooldown_fires + 1
			self:add_log("  cooldown fired #" .. self.cooldown_fires)
		end, 5, "cd")
	end

	if G.input.is_key_pressed("c") then
		G.timer.cancel_all()
		self:add_log("cancel_all")
	end
end

function Game:draw()
	G.graphics.clear()
	local w, h = G.window.dimensions()

	-- Flash overlay.
	if self.flash_alpha > 1 then
		G.graphics.set_color(255, 255, 100, math.floor(self.flash_alpha))
		G.graphics.draw_rect(0, 0, w, h)
	end

	-- Dots.
	G.graphics.set_color("cyan")
	for _, d in ipairs(self.dots) do
		G.graphics.draw_circle(d.x, d.y, 8)
	end

	-- Progress bar.
	if self.progress_active then
		local bx, by, bw, bh = 50, 350, w - 100, 20
		G.graphics.set_color(80, 80, 80, 255)
		G.graphics.draw_rect(bx, by, bx + bw, by + bh)
		G.graphics.set_color("green")
		G.graphics.draw_rect(bx, by, bx + bw * self.progress, by + bh)
	end

	-- Tween box.
	G.graphics.set_color("yellow")
	G.graphics.draw_rect(self.box.x, self.box.y, self.box.x + 50, self.box.y + 50)

	-- Cooldown indicator.
	G.graphics.set_color("white")
	G.graphics.print(
		string.format("Cooldown fires: %d  (hold space after pressing 5)", self.cooldown_fires),
		50,
		470
	)

	-- HUD.
	G.graphics.set_color("white")
	local y = 20
	local function line(text)
		G.graphics.print(text, 20, y)
		y = y + 20
	end

	line("=== Timer Test ===")
	line("1:after  2:every  3:during  4:tween  5:cooldown  C:cancel  Esc:quit")
	line("")

	-- Log.
	G.graphics.set_color(180, 180, 180, 255)
	for _, msg in ipairs(self.log) do
		line(msg)
		if y > h - 20 then
			break
		end
	end
end

return Game
