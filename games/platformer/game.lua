-- Test platformer: exercises the tilemap system.
-- Uses Kenney Pixel Platformer tileset (18x18 tiles, packed).
--
-- Controls:
--   Left/Right or A/D: move
--   Space: jump
--   Q: quit

local M = {}
local W, H

-- The TMX file uses Tiled GIDs with firstgid=28 for the tile tileset.
-- gid_offset = 27 converts to our 1-based tile IDs.

-- Player state.
local player = {
  x = 80, y = 0,
  w = 14, h = 16,
  vx = 0, vy = 0,
  on_ground = false,
  facing_right = true,
  walk_timer = 0,
  walk_frame = 1,
}

-- Player animation frames (packed via game atlas from kenney characters).
local ANIM = {
  idle = "player_idle",
  jump = "player_jump",
  fall = "player_fall",
  walk = { "player_walk1", "player_walk2" },
  duck = "player_duck",
}

local WALK_FRAME_TIME = 0.15
local GRAVITY = 600
local JUMP_VEL = -330
local MOVE_SPEED = 120
local FRICTION = 8
local dead = false

local map
local tile_size = 18

function M:init()
  W, H = G.window.dimensions()

  -- Load the level directly from the Tiled TMX file.
  -- The tile tileset has firstgid=28, so gid_offset = 27.
  map = G.tilemap.load_tmx("level.tmx", 27)
  map:set_tileset("tilemap_packed.qoi")
  map:set_collision("Tiles", true)  -- First layer is the terrain.

  -- Set up camera (2.5x zoom for pixel art).
  G.camera.set_zoom(2.5)

  -- Place player near the left side of the level.
  player.x = 4 * tile_size
  player.y = 5 * tile_size
end

function M:update(t, dt)
  if G.input.is_key_pressed("q") then
    G.system.quit()
  end

  if dead then
    if G.input.is_key_pressed("r") then
      -- Respawn.
      player.x = 80
      player.y = 5 * tile_size
      player.vx = 0
      player.vy = 0
      player.on_ground = false
      dead = false
    end
    return
  end

  -- Horizontal input.
  local move = 0
  if G.input.is_key_down("right") or G.input.is_key_down("d") then
    move = 1
    player.facing_right = true
  elseif G.input.is_key_down("left") or G.input.is_key_down("a") then
    move = -1
    player.facing_right = false
  end

  -- Apply horizontal acceleration and friction.
  if move ~= 0 then
    player.vx = move * MOVE_SPEED
  else
    player.vx = player.vx * (1 - FRICTION * dt)
    if math.abs(player.vx) < 1 then player.vx = 0 end
  end

  -- Jump.
  if G.input.is_key_pressed("space") and player.on_ground then
    player.vy = JUMP_VEL
    player.on_ground = false
  end

  -- Gravity.
  player.vy = player.vy + GRAVITY * dt

  -- Move with tilemap collision.
  local nx, ny, hit = map:move(
    player.x, player.y, player.w, player.h,
    player.vx * dt, player.vy * dt
  )

  -- Respond to collision.
  if hit then
    if hit.normal_y < 0 then
      -- Landed on ground.
      player.on_ground = true
      player.vy = 0
    elseif hit.normal_y > 0 then
      -- Hit ceiling.
      player.vy = 0
    end
    if hit.normal_x ~= 0 then
      player.vx = 0
    end
  else
    player.on_ground = false
  end

  player.x = nx
  player.y = ny

  -- Walk animation timer.
  if player.on_ground and math.abs(player.vx) > 5 then
    player.walk_timer = player.walk_timer + dt
    if player.walk_timer >= WALK_FRAME_TIME then
      player.walk_timer = player.walk_timer - WALK_FRAME_TIME
      player.walk_frame = player.walk_frame % #ANIM.walk + 1
    end
  else
    player.walk_timer = 0
    player.walk_frame = 1
  end

  -- Fall off the bottom = death.
  -- The TMX level is 15 tiles tall.
  if player.y > 15 * tile_size + 100 then
    dead = true
  end
end

function M:draw()
  G.graphics.clear(100, 150, 230, 255)

  -- Camera follows player center.
  local cam_x = player.x + player.w / 2
  local cam_y = player.y + player.h / 2
  G.camera.set(cam_x, cam_y)

  G.camera.attach()

  -- Draw tilemap layers.
  map:draw()

  -- Pick the right animation frame.
  local sprite
  if not player.on_ground then
    sprite = player.vy < 0 and ANIM.jump or ANIM.fall
  elseif math.abs(player.vx) > 5 then
    sprite = ANIM.walk[player.walk_frame]
  else
    sprite = ANIM.idle
  end

  -- Draw player sprite scaled to match tile world.
  -- Sprites are 80x110px, but the player collision box is 14x16.
  -- Scale to fit roughly 18px tall (one tile height).
  local sprite_scale = 18 / 110
  local cx = player.x + player.w / 2
  local cy = player.y + player.h / 2
  G.graphics.push()
  G.graphics.translate(cx, cy)
  local sx = player.facing_right and sprite_scale or -sprite_scale
  G.graphics.scale(sx, sprite_scale)
  G.graphics.draw_sprite(sprite, 0, 0)
  G.graphics.pop()

  G.camera.detach()

  -- HUD (drawn in screen space after camera detach).
  G.graphics.set_color(255, 255, 255, 255)
  if dead then
    local msg = "GAME OVER - Press R to restart"
    -- Approximate text width: ~8px per character at default font size.
    G.graphics.print(msg, (W - #msg * 8) / 2, H / 2 - 10)
  else
    G.graphics.print("Arrows/WASD: move   Space: jump   Q: quit", 10, 10)
  end
end

return M
