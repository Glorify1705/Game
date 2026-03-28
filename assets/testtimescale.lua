-- Test program for time scale: slow-motion, pause, and real-time access.
-- Controls:
--   1: Normal speed (1.0x)
--   2: Half speed (0.5x)
--   3: Quarter speed (0.25x)
--   4: Pause (0.0x)
--   5: Double speed (2.0x)
--   Esc: Quit

local Game = {}

function Game:init()
	G.window.set_title("Time Scale Test")

	self.ball_x = 400
	self.ball_y = 300
	self.ball_dx = 200
	self.ball_dy = 150
	self.ball_radius = 15

	self.rotation = 0

	self.hud_blink_timer = 0
	self.hud_visible = true
end

function Game:update(t, dt)
	if G.input.is_key_pressed("escape") then
		G.system.quit()
	end

	-- Time scale controls.
	if G.input.is_key_pressed("1") then
		G.system.set_time_scale(1.0)
	end
	if G.input.is_key_pressed("2") then
		G.system.set_time_scale(0.5)
	end
	if G.input.is_key_pressed("3") then
		G.system.set_time_scale(0.25)
	end
	if G.input.is_key_pressed("4") then
		G.system.set_time_scale(0.0)
	end
	if G.input.is_key_pressed("5") then
		G.system.set_time_scale(2.0)
	end

	-- Move the ball using the scaled dt (slows with time scale).
	local w, h = G.window.dimensions()

	self.ball_x = self.ball_x + self.ball_dx * dt
	self.ball_y = self.ball_y + self.ball_dy * dt

	if self.ball_x - self.ball_radius < 0 then
		self.ball_x = self.ball_radius
		self.ball_dx = -self.ball_dx
	elseif self.ball_x + self.ball_radius > w then
		self.ball_x = w - self.ball_radius
		self.ball_dx = -self.ball_dx
	end

	if self.ball_y - self.ball_radius < 0 then
		self.ball_y = self.ball_radius
		self.ball_dy = -self.ball_dy
	elseif self.ball_y + self.ball_radius > h then
		self.ball_y = h - self.ball_radius
		self.ball_dy = -self.ball_dy
	end

	-- Rotate using scaled dt.
	self.rotation = self.rotation + 2.0 * dt

	-- Blink the HUD indicator using real_dt (unaffected by time scale).
	local real_dt = G.system.get_real_dt()
	self.hud_blink_timer = self.hud_blink_timer + real_dt
	if self.hud_blink_timer >= 0.5 then
		self.hud_blink_timer = self.hud_blink_timer - 0.5
		self.hud_visible = not self.hud_visible
	end
end

function Game:draw()
	G.graphics.clear()

	-- Draw the bouncing ball.
	G.graphics.set_color("cyan")
	G.graphics.draw_circle(self.ball_x, self.ball_y, self.ball_radius)

	-- Draw a rotating square to visualize time scale effect.
	local w, h = G.window.dimensions()
	local cx, cy = w / 2, h / 2
	G.graphics.push()
	G.graphics.translate(cx, cy)
	G.graphics.rotate(self.rotation)
	G.graphics.set_color("yellow")
	G.graphics.draw_rect(-30, -30, 30, 30, 0)
	G.graphics.pop()

	-- HUD (uses real time for blinking, unaffected by time scale).
	local scale = G.system.get_time_scale()
	local game_time = G.clock.gametime()
	local real_time = G.system.get_real_time()

	G.graphics.set_color("white")
	local y = 20
	local function line(text)
		G.graphics.print(text, 20, y)
		y = y + 24
	end

	line("=== Time Scale Test ===")
	line("")
	line(string.format("Time Scale: %.2fx", scale))
	line(string.format("Game Time:  %.2fs", game_time))
	line(string.format("Real Time:  %.2fs", real_time))
	line("")

	-- Blinking indicator (proves real_dt works even when paused).
	if self.hud_visible then
		G.graphics.set_color("green")
		line("[REAL-TIME BLINK] (uses real_dt)")
	else
		G.graphics.set_color(50, 50, 50, 255)
		line("[REAL-TIME BLINK] (uses real_dt)")
	end

	G.graphics.set_color("white")
	y = y + 8
	line("--- Controls ---")
	line("1: Normal (1.0x)    2: Half (0.5x)")
	line("3: Quarter (0.25x)  4: Pause (0.0x)")
	line("5: Double (2.0x)    Esc: Quit")
end

return Game
