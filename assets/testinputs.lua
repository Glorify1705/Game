-- Minimal test for the test-input coroutine system. Run with:
--   game run --test -- testinputs

local M = {}

M.frame = 0
M.events = {}
M.test_done = false

function M:init()
  print("testinputs: init")
end

function M:update(t, dt)
  self.frame = self.frame + 1
  if G.input.is_key_pressed("space") then
    table.insert(self.events, "space pressed at frame " .. self.frame)
  end
  if G.input.is_key_released("space") then
    table.insert(self.events, "space released at frame " .. self.frame)
  end
end

function M:draw()
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
    if e:find("pressed") then found_press = true end
    if e:find("released") then found_release = true end
  end

  G.test.assert_true(found_press, "expected to observe a space-press")
  G.test.assert_true(found_release, "expected to observe a space-release")

  print("testinputs: PASS")
end

return M
