local Game = {}

-- Original Flappy Bird logical resolution. The window is 2x this size;
-- we use G.graphics.scale(2, 2) in draw() to scale up.
local SCREEN_W = 288
local SCREEN_H = 512

local GRAVITY = 800
local FLAP_VELOCITY = -280
local BIRD_X = 60
local BIRD_W = 34
local BIRD_H = 24

local PIPE_W = 52
local PIPE_H = 320
local PIPE_GAP = 100
local PIPE_SPEED = 120
local PIPE_SPAWN_INTERVAL = 1.6

local BASE_H = 112
local BASE_Y = SCREEN_H - BASE_H
local GROUND_Y = BASE_Y

local BIRD_FRAMES = {
  "yellowbird-upflap",
  "yellowbird-midflap",
  "yellowbird-downflap",
  "yellowbird-midflap",
}
local BIRD_ANIM_SPEED = 0.1

-- Collision shapes (created once, reused every frame).
local BIRD_MARGIN = 3
local bird_shape = G.collision.aabb(BIRD_W - BIRD_MARGIN * 2,
                                    BIRD_H - BIRD_MARGIN * 2)
local pipe_shape = G.collision.aabb(PIPE_W, PIPE_H)

function Game:init()
  G.input.bind("flap", { "key:space", "key:up", "mouse:left", "touch" })
  self.state = "menu"
  self.bird_y = SCREEN_H / 2 - 20
  self.bird_vy = 0
  self.bird_angle = 0
  self.bird_frame = 1
  self.bird_anim_timer = 0

  self.pipes = {}
  self.pipe_timer = 0
  self.score = 0
  self.best_score = G.save.get("flappybird", "best_score") or 0

  self.base_x = 0

  self.rng = G.random.non_deterministic()

  -- Preload sound effects.
  self.sfx_wing = G.sound.add_effect("wing.qoa")
  self.sfx_point = G.sound.add_effect("point.qoa")
  self.sfx_hit = G.sound.add_effect("hit.qoa")
  self.sfx_die = G.sound.add_effect("die.qoa")
  self.sfx_swoosh = G.sound.add_effect("swoosh.qoa")

  -- Menu bob animation.
  self.menu_bob_timer = 0
end

local function clamp(x, lo, hi)
  if x < lo then return lo end
  if x > hi then return hi end
  return x
end

function Game:flap()
  self.bird_vy = FLAP_VELOCITY
  G.sound.play_effect("wing.qoa")
end

function Game:spawn_pipe()
  -- Random gap position. Keep it away from top/bottom edges.
  local min_top = 50
  local max_top = GROUND_Y - PIPE_GAP - 50
  local gap_top = G.random.sample(self.rng, min_top, max_top)

  table.insert(self.pipes, {
    x = SCREEN_W + PIPE_W,
    gap_top = gap_top,
    scored = false,
  })
end

function Game:start_game()
  self.state = "playing"
  self.bird_y = SCREEN_H / 2 - 20
  self.bird_vy = 0
  self.bird_angle = 0
  self.pipes = {}
  self.pipe_timer = 0
  self.score = 0
  G.sound.play_effect("swoosh.qoa")
  self:flap()
end

function Game:die()
  self.state = "dying"
  self.death_timer = 0
  G.sound.play_effect("hit.qoa")
  G.timer.after(0.3, function()
    G.sound.play_effect("die.qoa")
  end)
  if self.score > self.best_score then
    self.best_score = self.score
    G.save.set("flappybird", "best_score", self.best_score)
  end
end

function Game:input_pressed()
  return G.input.is_action_pressed("flap")
end

function Game:update(t, dt)
  if G.input.is_key_pressed("escape") or G.input.is_key_pressed("q") then
    G.system.quit()
  end

  -- Animate bird wings in all states except dead on ground.
  if self.state ~= "dead" then
    self.bird_anim_timer = self.bird_anim_timer + dt
    if self.bird_anim_timer >= BIRD_ANIM_SPEED then
      self.bird_anim_timer = self.bird_anim_timer - BIRD_ANIM_SPEED
      self.bird_frame = self.bird_frame % #BIRD_FRAMES + 1
    end
  end

  -- Scroll ground in menu and playing states.
  if self.state == "menu" or self.state == "playing" then
    self.base_x = self.base_x - PIPE_SPEED * dt
    if self.base_x <= -24 then
      self.base_x = self.base_x + 24
    end
  end

  if self.state == "menu" then
    self.menu_bob_timer = self.menu_bob_timer + dt
    self.bird_y = SCREEN_H / 2 - 20 + math.sin(self.menu_bob_timer * 3) * 8
    if self:input_pressed() then
      self:start_game()
    end
    return
  end

  if self.state == "playing" then
    -- Bird physics.
    if self:input_pressed() then
      self:flap()
    end

    self.bird_vy = self.bird_vy + GRAVITY * dt
    self.bird_y = self.bird_y + self.bird_vy * dt

    -- Bird tilt: angle follows velocity.
    if self.bird_vy < 0 then
      self.bird_angle = -0.4
    else
      self.bird_angle = clamp(self.bird_vy / 400, -0.4, 1.2)
    end

    -- Spawn pipes.
    self.pipe_timer = self.pipe_timer + dt
    if self.pipe_timer >= PIPE_SPAWN_INTERVAL then
      self.pipe_timer = self.pipe_timer - PIPE_SPAWN_INTERVAL
      self:spawn_pipe()
    end

    -- Move pipes and check scoring.
    for i = #self.pipes, 1, -1 do
      local pipe = self.pipes[i]
      pipe.x = pipe.x - PIPE_SPEED * dt

      -- Score when bird passes pipe center.
      if not pipe.scored and pipe.x + PIPE_W / 2 < BIRD_X then
        pipe.scored = true
        self.score = self.score + 1
        G.sound.play_effect("point.qoa")
      end

      -- Remove off-screen pipes.
      if pipe.x < -PIPE_W then
        table.remove(self.pipes, i)
      end
    end

    -- Collision detection using engine collision shapes.
    local bird_cx, bird_cy = BIRD_X, self.bird_y
    for _, pipe in ipairs(self.pipes) do
      local pipe_cx = pipe.x + PIPE_W / 2
      -- Top pipe.
      local top_cy = pipe.gap_top - PIPE_H / 2
      if G.collision.test(bird_shape, bird_cx, bird_cy,
                          pipe_shape, pipe_cx, top_cy) then
        self:die()
        return
      end
      -- Bottom pipe.
      local bot_cy = pipe.gap_top + PIPE_GAP + PIPE_H / 2
      if G.collision.test(bird_shape, bird_cx, bird_cy,
                          pipe_shape, pipe_cx, bot_cy) then
        self:die()
        return
      end
    end

    -- Hit ground or ceiling.
    if self.bird_y + BIRD_H / 2 >= GROUND_Y then
      self.bird_y = GROUND_Y - BIRD_H / 2
      self:die()
      return
    end
    if self.bird_y - BIRD_H / 2 <= 0 then
      self.bird_y = BIRD_H / 2
      self.bird_vy = 0
    end
    return
  end

  if self.state == "dying" then
    -- Bird falls to ground.
    self.bird_vy = self.bird_vy + GRAVITY * dt
    self.bird_y = self.bird_y + self.bird_vy * dt
    self.bird_angle = clamp(self.bird_angle + dt * 4, -0.4, 1.57)

    if self.bird_y + BIRD_H / 2 >= GROUND_Y then
      self.bird_y = GROUND_Y - BIRD_H / 2
      self.state = "dead"
      self.dead_timer = 0
    end
    return
  end

  if self.state == "dead" then
    self.dead_timer = self.dead_timer + dt
    if self.dead_timer > 0.5 and self:input_pressed() then
      self:start_game()
    end
    return
  end
end

function Game:draw_score(score, y)
  -- Draw score as individual digit sprites, centered.
  local digits = tostring(score)
  local total_w = 0
  local digit_widths = {}
  for i = 1, #digits do
    local ch = digits:sub(i, i)
    local info = G.assets.sprite_info(ch)
    digit_widths[i] = info.width
    total_w = total_w + info.width + (i > 1 and 2 or 0)
  end
  local x = (SCREEN_W - total_w) / 2
  for i = 1, #digits do
    local ch = digits:sub(i, i)
    G.graphics.draw_sprite(ch, x + digit_widths[i] / 2, y)
    x = x + digit_widths[i] + 2
  end
end

function Game:draw()
  G.graphics.clear()
  G.graphics.push()
  G.graphics.scale(2, 2)

  -- Background.
  G.graphics.set_color("white")
  G.graphics.draw_sprite("background-day", SCREEN_W / 2, SCREEN_H / 2)

  -- Pipes (draw before base so base covers pipe bottoms).
  for _, pipe in ipairs(self.pipes) do
    local cx = pipe.x + PIPE_W / 2
    -- Top pipe (flipped vertically): draw upside down.
    local top_cy = pipe.gap_top - PIPE_H / 2
    G.graphics.draw_sprite("pipe-green", cx, top_cy, 3.14159)
    -- Bottom pipe.
    local bot_cy = pipe.gap_top + PIPE_GAP + PIPE_H / 2
    G.graphics.draw_sprite("pipe-green", cx, bot_cy)
  end

  -- Ground (tile the base sprite to fill width).
  local base_w = 336
  local x = self.base_x
  while x < SCREEN_W do
    G.graphics.draw_sprite("base", x + base_w / 2, BASE_Y + BASE_H / 2)
    x = x + base_w
  end

  -- Bird.
  local frame = BIRD_FRAMES[self.bird_frame]
  G.graphics.draw_sprite(frame, BIRD_X, self.bird_y, self.bird_angle)

  -- Score.
  if self.state == "playing" or self.state == "dying" or self.state == "dead" then
    self:draw_score(self.score, 40)
  end

  -- Menu overlay.
  if self.state == "menu" then
    G.graphics.draw_sprite("message", SCREEN_W / 2, SCREEN_H / 2 - 40)
  end

  -- Game over overlay.
  if self.state == "dead" then
    G.graphics.draw_sprite("gameover", SCREEN_W / 2, SCREEN_H / 2 - 60)

    -- Show best score.
    if self.best_score > 0 then
      local size = 16
      local text = "BEST: " .. tostring(self.best_score)
      local tw = G.graphics.text_dimensions("debug_font.ttf", size, text)
      G.graphics.set_text_outline(0, 0, 0, 255, 2)
      G.graphics.set_color(255, 255, 255, 255)
      G.graphics.draw_text("debug_font.ttf", size, text,
                           (SCREEN_W - tw) / 2, SCREEN_H / 2 - 20)
      G.graphics.clear_text_outline()
      G.graphics.set_color("white")
    end
  end

  G.graphics.pop()
end

return Game
