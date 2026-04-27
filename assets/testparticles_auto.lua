-- Automated particle system test. Run with:
--   game run --test -- testparticles_auto
-- Exercises particle API with synthetic inputs.

local M = {}

M.emitters = {}
M.frame = 0
M.events = {}

function M:init()
  -- Create a simple emitter for testing.
  self.emitters.sparks = G.particles.new_emitter({
    max_particles = 500,
    emission_rate = 100,
    lifetime = {0.3, 0.8},
    speed = {50, 150},
    direction = -math.pi / 2,
    spread = math.pi / 4,
    size = {2, 6},
    size_over_life = {1.0, 0.0},
    color_over_life = {
      {1.0, 1.0, 0.5, 1.0},
      {1.0, 0.3, 0.0, 0.0},
    },
    gravity = {0, 100},
    damping = 0.97,
    blend_mode = "add",
  })

  -- Create a second emitter with different config.
  self.emitters.smoke = G.particles.new_emitter({
    max_particles = 200,
    emission_rate = 40,
    lifetime = {1.0, 2.0},
    speed = {10, 30},
    direction = -math.pi / 2,
    spread = math.pi / 6,
    size = 12,
    size_over_life = {0.5, 1.0, 1.5},
    color_over_life = {
      {0.3, 0.3, 0.3, 0.0},
      {0.3, 0.3, 0.3, 0.4},
      {0.2, 0.2, 0.2, 0.0},
    },
    gravity = {0, -20},
    damping = 0.96,
    blend_mode = "alpha",
  })

  if not G.test.is_active() then
    self.message = "Not running under --test. Run with: game run --test -- testparticles_auto"
    self.quit_timer = 3.0
  end
end

function M:update(t, dt)
  self.frame = self.frame + 1

  for _, e in pairs(self.emitters) do
    e:update(dt)
  end

  if self.quit_timer then
    self.quit_timer = self.quit_timer - dt
    if self.quit_timer <= 0 then G.system.quit() end
  end
end

function M:draw()
  G.graphics.clear(0.05, 0.05, 0.08)
  for _, e in pairs(self.emitters) do
    e:draw()
  end

  G.graphics.set_color(255, 255, 255, 200)
  if self.message then
    G.graphics.print(self.message, 10, 10)
  end

  local y = 30
  for name, e in pairs(self.emitters) do
    G.graphics.print(
      string.format("%s: %d particles, active=%s", name, e:particle_count(), tostring(e:is_active())),
      10, y)
    y = y + 16
  end

  for i, ev in ipairs(self.events) do
    G.graphics.print(ev, 10, y)
    y = y + 16
  end
end

function M:test_inputs()
  local function log(msg)
    table.insert(self.events, msg)
    print("testparticles_auto: " .. msg)
  end

  -- Test 1: Emitter starts inactive.
  log("Test 1: emitters start inactive")
  assert(not self.emitters.sparks:is_active(), "sparks should start inactive")
  assert(not self.emitters.smoke:is_active(), "smoke should start inactive")
  assert(self.emitters.sparks:particle_count() == 0, "sparks should have 0 particles")
  log("  PASS")
  G.test.wait_frames(1)

  -- Test 2: Start emitter and verify particles spawn.
  log("Test 2: start emission")
  self.emitters.sparks:set_position(400, 300)
  self.emitters.sparks:start()
  -- Wait a few frames for particles to spawn.
  G.test.wait_frames(10)
  local count = self.emitters.sparks:particle_count()
  log("  particle count after 10 frames: " .. count)
  assert(count > 0, "expected particles to spawn, got " .. count)
  log("  PASS")

  -- Test 3: Stop emission, particles should decay.
  log("Test 3: stop emission, particles decay")
  self.emitters.sparks:stop()
  local count_at_stop = self.emitters.sparks:particle_count()
  -- Wait for particles to die (lifetime is 0.3-0.8s at 60fps = 18-48 frames).
  G.test.wait_frames(60)
  local count_after = self.emitters.sparks:particle_count()
  log("  count at stop: " .. count_at_stop .. ", after 60 frames: " .. count_after)
  assert(count_after < count_at_stop, "particles should have died")
  log("  PASS")

  -- Test 4: Burst spawns exact count (up to capacity).
  log("Test 4: burst spawning")
  -- Wait for all to die first.
  G.test.wait_frames(60)
  self.emitters.sparks:burst(25, 300, 200)
  G.test.wait_frames(1)
  count = self.emitters.sparks:particle_count()
  log("  burst 25, count = " .. count)
  assert(count == 25, "expected 25 particles from burst, got " .. count)
  log("  PASS")

  -- Test 5: Smoke emitter works independently.
  log("Test 5: second emitter")
  self.emitters.smoke:set_position(500, 300)
  self.emitters.smoke:start()
  G.test.wait_frames(10)
  local smoke_count = self.emitters.smoke:particle_count()
  log("  smoke particles: " .. smoke_count)
  assert(smoke_count > 0, "smoke should have particles")
  self.emitters.smoke:stop()
  log("  PASS")

  -- Test 6: set_direction, set_gravity, set_emission_rate.
  log("Test 6: runtime parameter changes")
  self.emitters.sparks:set_direction(0)
  self.emitters.sparks:set_gravity(0, 0)
  self.emitters.sparks:set_emission_rate(500)
  self.emitters.sparks:start()
  G.test.wait_frames(5)
  count = self.emitters.sparks:particle_count()
  log("  count with high emission rate: " .. count)
  assert(count > 25, "expected more particles with 500/sec rate")
  self.emitters.sparks:stop()
  log("  PASS")

  log("All tests passed!")
  G.test.wait_frames(30)
  G.system.quit()
end

return M
