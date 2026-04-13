-- Minimal test for the test-input coroutine system. Run with:
--   game run --test -- testinputs

local M = {}

M.frame = 0
M.events = {}
M.test_done = false
M.quit_at = nil
M.message = nil

function M:init()
	print("testinputs: init")
	if not G.test.is_active() then
		self.message = "Not running under --test, quitting in 5 seconds..."
		print("testinputs: " .. self.message)
		self.quit_at = 5.0
	end
end

function M:update(t, dt)
	self.frame = self.frame + 1
	if G.input.is_key_pressed("space") then
		table.insert(self.events, "space pressed at frame " .. self.frame)
	end
	if G.input.is_key_released("space") then
		table.insert(self.events, "space released at frame " .. self.frame)
	end
	if self.quit_at then
		self.quit_at = self.quit_at - dt
		if self.quit_at <= 0 then
			G.system.quit()
		end
	end
end

function M:draw()
	if self.message then
		G.graphics.set_color("white")
		G.graphics.print(self.message, 20, 20)
		G.graphics.print(string.format("Quitting in %.1fs", math.max(0, self.quit_at or 0)), 20, 40)
	end
end

function M:test_inputs()
	print("testinputs: test coroutine started")

	-- Press space, run a frame, release.
	G.test.key_down("space")
	G.test.wait_frames(1)
	G.test.key_up("space")
	G.test.wait_frames(1)

	-- Verify the game observed the press and release.
	local found_press = false
	local found_release = false
	for _, e in ipairs(self.events) do
		print("testinputs: observed " .. e)
		if e:find("pressed") then
			found_press = true
		end
		if e:find("released") then
			found_release = true
		end
	end

	G.test.assert_true(found_press, "expected to observe a space-press")
	G.test.assert_true(found_release, "expected to observe a space-release")

	print("testinputs: PASS")
end

return M
