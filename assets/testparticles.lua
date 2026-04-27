-- Particle system test program. Run with:
--   game run -- testparticles
-- Controls:
--   Mouse: move emitter position
--   Left click: burst 50 particles
--   1-5: switch particle effects
--   Space: toggle continuous emission
--   Up/Down: adjust emission rate
--   R: reset all emitters

local M = {}

M.emitters = {}
M.current = 1
M.mouse_x = 400
M.mouse_y = 300

-- Particle effect definitions.
local effects = {
  {
    name = "Fire",
    def = {
      max_particles = 3000,
      emission_rate = 200,
      lifetime = {0.3, 0.8},
      speed = {30, 80},
      direction = -math.pi / 2,
      spread = math.pi / 8,
      size = {6, 12},
      size_over_life = {1.0, 0.5, 0.0},
      color_over_life = {
        {1.0, 1.0, 0.6, 1.0},
        {1.0, 0.5, 0.0, 0.8},
        {0.6, 0.1, 0.0, 0.0},
      },
      gravity = {0, -50},
      damping = 0.95,
      blend_mode = "add",
    },
  },
  {
    name = "Sparks",
    def = {
      max_particles = 2000,
      emission_rate = 100,
      lifetime = {0.5, 1.5},
      speed = {100, 300},
      direction = -math.pi / 2,
      spread = math.pi / 3,
      size = {2, 4},
      size_over_life = {1.0, 0.3, 0.0},
      color_over_life = {
        {1.0, 1.0, 0.8, 1.0},
        {1.0, 0.6, 0.0, 0.8},
        {0.5, 0.0, 0.0, 0.0},
      },
      gravity = {0, 200},
      damping = 0.98,
      blend_mode = "add",
    },
  },
  {
    name = "Snow",
    def = {
      max_particles = 5000,
      emission_rate = 150,
      lifetime = {3.0, 6.0},
      speed = {10, 30},
      direction = math.pi / 2,
      spread = math.pi / 4,
      size = {2, 5},
      size_over_life = {0.5, 1.0, 0.5},
      spin = {-2, 2},
      color_over_life = {
        {0.9, 0.9, 1.0, 0.0},
        {0.9, 0.9, 1.0, 0.8},
        {0.9, 0.9, 1.0, 0.0},
      },
      gravity = {0, 20},
      damping = 0.99,
      blend_mode = "alpha",
      shape = "rect",
      shape_width = 400,
      shape_height = 10,
    },
  },
  {
    name = "Explosion",
    def = {
      max_particles = 5000,
      emission_rate = 0,
      lifetime = {0.3, 1.0},
      speed = {50, 400},
      spread = math.pi,
      size = {3, 8},
      size_over_life = {1.0, 0.8, 0.0},
      color_over_life = {
        {1.0, 1.0, 1.0, 1.0},
        {1.0, 0.8, 0.2, 0.9},
        {1.0, 0.3, 0.0, 0.5},
        {0.3, 0.0, 0.0, 0.0},
      },
      gravity = {0, 100},
      damping = 0.92,
      blend_mode = "add",
    },
  },
  {
    name = "Smoke",
    def = {
      max_particles = 2000,
      emission_rate = 80,
      lifetime = {1.0, 3.0},
      speed = {10, 40},
      direction = -math.pi / 2,
      spread = math.pi / 6,
      size = {8, 20},
      size_over_life = {0.5, 1.0, 1.5},
      spin = {-1, 1},
      color_over_life = {
        {0.4, 0.4, 0.4, 0.0},
        {0.3, 0.3, 0.3, 0.4},
        {0.2, 0.2, 0.2, 0.0},
      },
      gravity = {0, -30},
      damping = 0.96,
      blend_mode = "alpha",
    },
  },
}

function M:create_emitters()
  self.emitters = {}
  for i, effect in ipairs(effects) do
    self.emitters[i] = G.particles.new_emitter(effect.def)
    self.emitters[i]:set_position(self.mouse_x, self.mouse_y)
  end
  -- Start the current emitter.
  self.emitters[self.current]:start()
end

function M:init()
  self:create_emitters()
end

function M:update(t, dt)
  -- Mouse position.
  self.mouse_x, self.mouse_y = G.input.mouse_position()

  -- Switch effects with number keys.
  for i = 1, #effects do
    if G.input.is_key_pressed(tostring(i)) then
      self.emitters[self.current]:stop()
      self.current = i
      self.emitters[self.current]:start()
    end
  end

  -- Toggle emission with space.
  if G.input.is_key_pressed("space") then
    local e = self.emitters[self.current]
    if e:is_active() then
      e:stop()
    else
      e:start()
    end
  end

  -- Burst on left click (button 1).
  if G.input.is_mouse_pressed(1) then
    self.emitters[self.current]:burst(50, self.mouse_x, self.mouse_y)
  end

  -- Adjust emission rate.
  if G.input.is_key_down("up") then
    local e = self.emitters[self.current]
    e:set_emission_rate(effects[self.current].def.emission_rate * 2)
  end
  if G.input.is_key_down("down") then
    local e = self.emitters[self.current]
    e:set_emission_rate(effects[self.current].def.emission_rate)
  end

  -- Reset.
  if G.input.is_key_pressed("r") then
    for _, e in ipairs(self.emitters) do
      e:stop()
    end
    self:create_emitters()
  end

  -- Update all emitters (even inactive ones, so particles finish dying).
  for _, e in ipairs(self.emitters) do
    e:set_position(self.mouse_x, self.mouse_y)
    e:update(dt)
  end
end

function M:draw()
  G.graphics.clear(0.05, 0.05, 0.08)

  -- Draw all emitters (particles from inactive emitters still render
  -- until they expire).
  for _, e in ipairs(self.emitters) do
    e:draw()
  end

  -- HUD.
  G.graphics.set_color(255, 255, 255, 200)
  G.graphics.print("Particle System Test", 10, 10)

  local y = 30
  for i, effect in ipairs(effects) do
    local prefix = (i == self.current) and "> " or "  "
    local count = self.emitters[i]:particle_count()
    local active = self.emitters[i]:is_active() and " [ON]" or ""
    G.graphics.print(
      string.format("%s%d: %s (%d particles)%s", prefix, i, effect.name, count, active),
      10, y)
    y = y + 16
  end

  y = y + 10
  G.graphics.print("Space: toggle emission | Click: burst | R: reset", 10, y)
  y = y + 16
  G.graphics.print("Up/Down: emission rate | 1-5: switch effect", 10, y)
end

return M
