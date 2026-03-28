local Player = require("player")
local Meteor = require("meteor")
local Bullet = require("bullet")
local Timer = require("timer")
local Random = require("random")
local Object = require("classic")
local Starfield = require("starfield")
local Powerup = require("powerup")

local Entities = Object:extend()

function Entities:new()
	self.entities = {}
end

function Entities:on_collision(fn)
	G.physics.set_collision_callback(fn)
end

function Entities:add(entity)
	self.entities[entity:id()] = entity
end

function Entities:remove(id)
	local entity = self.entities[id]
	if entity then
		entity:destroy()
		self.entities[id] = nil
	end
end

function Entities:update(dt)
	for _, entity in pairs(self.entities) do
		entity:update(dt)
	end
end

function Entities:draw()
	for _, entity in pairs(self.entities) do
		entity:draw()
	end
end

function Entities:collect_dead()
	local dead = {}
	for id, entity in pairs(self.entities) do
		if entity.dead then
			dead[#dead + 1] = entity
		end
	end
	return dead
end

function Entities:remove_dead()
	local to_remove = {}
	for id, entity in pairs(self.entities) do
		if entity.dead then
			to_remove[#to_remove + 1] = id
		end
	end
	for _, id in ipairs(to_remove) do
		self:remove(id)
	end
end

function Entities:count_meteors()
	local count = 0
	for _, entity in pairs(self.entities) do
		if entity.is_meteor and entity:is_meteor() then
			count = count + 1
		end
	end
	return count
end

local SCREEN_W = 1024
local SCREEN_H = 800
local WORLD_W = 4000
local WORLD_H = 3000

local function draw_number(n, x, y)
	local s = tostring(n)
	local digit_w = 19
	local start_x = x - #s * digit_w
	for i = 1, #s do
		local ch = s:sub(i, i)
		G.graphics.draw_sprite("numeral" .. ch, start_x + (i - 1) * digit_w, y)
	end
end

local G1 = {}

local SCORE_VALUES = { big = 100, med = 50, small = 25 }
local INVINCIBLE_TIME = 3.0
local POWERUP_TYPES = { "shield", "rapid_fire", "heal", "score_bonus" }
local POWERUP_HUD_SPRITES = {
	shield = "powerupBlue_shield",
	rapid_fire = "powerupGreen_bolt",
}
local POWERUP_NAMES = {
	shield = "SHIELD",
	rapid_fire = "RAPID FIRE",
	heal = "HEAL +25",
	score_bonus = "SCORE +500",
}

function G1:init()
	G.window.set_title("My awesome Lua game 1!")
	G.physics.create_ground(false)
	self.entities = Entities()
	self.timer = Timer()
	self.fullscreen = false
	self.rnd = Random()
	self.score = 0
	self.lives = 3
	self.wave = 0
	self.wave_active = false
	self.shake = { x = 0, y = 0 }
	self.state = "playing"
	self.respawning = false
	self.particles = {}
	self.messages = {}
	self.intense_music = false
	self.cam_x = WORLD_W / 2
	self.cam_y = WORLD_H / 2
	self.zoom = 1.0
	self.target_zoom = 1.0
	self.starfield = Starfield(SCREEN_W, SCREEN_H, self.rnd.rnd)

	self.game_canvas = G.graphics.new_canvas(SCREEN_W, SCREEN_H)
	self.vignette_canvas = G.graphics.new_canvas(SCREEN_W, SCREEN_H)
	self:bake_vignette()

	self:spawn_player()

	Entities:on_collision(function(a, b)
		if a then
			a:on_collision(b)
		end
		if b then
			b:on_collision(a)
		end
	end)

	local source = G.sound.add_source("music.ogg")
	G.sound.set_volume(source, 0.4)
	G.sound.set_loop(source, true)
	G.sound.play_source(source)
	self.music_source = source

	self:start_next_wave()
end

function G1:spawn_player()
	self.player = Player(WORLD_W / 2, WORLD_H / 2)
	self.entities:add(self.player)
	self.respawning = false

	self.player:set_spawn_callback(function(x, y, angle)
		local b = Bullet(x, y, angle)
		self.entities:add(b)
	end)

	self.player:set_death_callback(function()
		self:on_player_death()
	end)

	self.player:set_damage_callback(function()
		self:screen_shake(25, 2.0)
	end)
end

function G1:on_player_death()
	self.lives = self.lives - 1
	self:screen_shake(12)
	if self.lives <= 0 then
		self.state = "game_over"
		G.sound.play_effect("game-over.ogg")
	else
		self.respawning = true
		self.timer:after(2, function()
			self:spawn_player()
			self.player:make_invincible(INVINCIBLE_TIME)
		end)
	end
end

function G1:screen_shake(intensity, duration)
	duration = duration or 0.3
	local scaled = intensity / math.max(self.zoom, 0.3)
	self.shake_intensity = scaled
	self.shake_duration = duration
	self.shake_elapsed = 0
end

function G1:update_shake(dt)
	if not self.shake_duration or self.shake_elapsed >= self.shake_duration then
		self.shake.x = 0
		self.shake.y = 0
		return
	end
	self.shake_elapsed = self.shake_elapsed + dt
	local t = self.shake_elapsed / self.shake_duration
	local strength = self.shake_intensity * (1 - t * t)
	local angle = math.random() * math.pi * 2
	self.shake.x = math.cos(angle) * strength
	self.shake.y = math.sin(angle) * strength
end

function G1:bake_vignette()
	G.graphics.set_canvas(self.vignette_canvas)
	G.graphics.clear()
	local cx = SCREEN_W / 2
	local cy = SCREEN_H / 2
	local max_r = math.sqrt(cx * cx + cy * cy)
	local steps = 40
	for i = steps, 1, -1 do
		local t = i / steps
		local radius = max_r * t
		local alpha = math.floor(180 * (1 - t) * (1 - t))
		G.graphics.set_color(0, 0, 0, alpha)
		G.graphics.draw_circle(cx, cy, radius)
	end
	G.graphics.set_color("white")
	G.graphics.set_canvas()
end

function G1:screen_wrap_entity(entity)
	if not entity.physics or entity.physics.destroyed then return end
	local v = entity.physics:position()
	local nx, ny = v.x, v.y
	local wrapped = false
	if v.x < 0 then
		nx = WORLD_W + v.x
		wrapped = true
	elseif v.x > WORLD_W then
		nx = v.x - WORLD_W
		wrapped = true
	end
	if v.y < 0 then
		ny = WORLD_H + v.y
		wrapped = true
	elseif v.y > WORLD_H then
		ny = v.y - WORLD_H
		wrapped = true
	end
	if wrapped then
		entity.physics:set_position(nx, ny)
	end
end

function G1:spawn_particles(x, y, count)
	for i = 1, count do
		local angle = math.random() * math.pi * 2
		local speed = 50 + math.random() * 100
		local p = {
			x = x,
			y = y,
			vx = math.cos(angle) * speed,
			vy = math.sin(angle) * speed,
			life = 0.3,
			max_life = 0.3,
			r = math.random(200, 255),
			g = math.random(100, 200),
			b = 0,
		}
		self.particles[#self.particles + 1] = p
	end
end

function G1:update_particles(dt)
	local alive = {}
	for _, p in ipairs(self.particles) do
		p.x = p.x + p.vx * dt
		p.y = p.y + p.vy * dt
		p.life = p.life - dt
		if p.life > 0 then
			alive[#alive + 1] = p
		end
	end
	self.particles = alive
end

function G1:draw_particles()
	for _, p in ipairs(self.particles) do
		local t = p.life / p.max_life
		local radius = 3 * t
		G.graphics.set_color(p.r, p.g, p.b, math.floor(255 * t))
		G.graphics.draw_circle(p.x, p.y, radius)
	end
	G.graphics.set_color("white")
end

function G1:show_message(text)
	self.messages[#self.messages + 1] = {
		text = text,
		life = 2.0,
		max_life = 2.0,
		y_offset = 0,
	}
end

function G1:update_messages(dt)
	local alive = {}
	for _, m in ipairs(self.messages) do
		m.life = m.life - dt
		m.y_offset = m.y_offset + dt * 40
		if m.life > 0 then
			alive[#alive + 1] = m
		end
	end
	self.messages = alive
end

function G1:draw_messages()
	for _, m in ipairs(self.messages) do
		local t = m.life / m.max_life
		local alpha = math.floor(255 * math.min(1, t * 3))
		G.graphics.set_color(255, 255, 100, alpha)
		G.graphics.print(m.text, SCREEN_W / 2 - 40, SCREEN_H / 2 - 60 - m.y_offset)
	end
	G.graphics.set_color("white")
end

function G1:update_music()
	local meteor_count = self.entities:count_meteors()
	if meteor_count > 0 and not self.intense_music then
		self.intense_music = true
		G.sound.stop_source(self.music_source)
		self.music_source = G.sound.add_source("weapons_mode.ogg")
		G.sound.set_volume(self.music_source, 0.4)
		G.sound.set_loop(self.music_source, true)
		G.sound.play_source(self.music_source)
	elseif meteor_count == 0 and self.intense_music then
		self.intense_music = false
		G.sound.stop_source(self.music_source)
		self.music_source = G.sound.add_source("music.ogg")
		G.sound.set_volume(self.music_source, 0.4)
		G.sound.set_loop(self.music_source, true)
		G.sound.play_source(self.music_source)
	end
end

function G1:spawn_meteor(size, grey)
	local rng = self.rnd.rnd
	local x = G.random.sample(rng, 100, WORLD_W - 100)
	local y = G.random.sample(rng, 100, WORLD_H - 100)
	-- avoid spawning right on top of the player
	if self.player and not self.player.dead then
		local pv = self.player.physics:position()
		local dx = x - pv.x
		local dy = y - pv.y
		if dx * dx + dy * dy < 300 * 300 then
			x = x + (dx >= 0 and 400 or -400)
			y = y + (dy >= 0 and 400 or -400)
		end
	end

	local m = Meteor(x, y, size, grey)
	-- random gentle drift direction
	local angle = G.random.sample(rng, 0, 628) / 100.0
	local base_force = 20 + self.wave * 3
	m:set_drift(math.cos(angle) * base_force, math.sin(angle) * base_force)
	self.entities:add(m)
end

function G1:start_next_wave()
	self.wave = self.wave + 1
	self.wave_active = true
	local num_big = self.wave * 3 + 6
	local num_med = self.wave * 2 + 2
	local num_small = self.wave
	local use_grey = self.wave > 3

	for _ = 1, num_big do
		local grey = use_grey and G.random.sample(self.rnd.rnd, 1, 100) > 50
		self:spawn_meteor("big", grey)
	end
	for _ = 1, num_med do
		local grey = use_grey and G.random.sample(self.rnd.rnd, 1, 100) > 50
		self:spawn_meteor("med", grey)
	end
	for _ = 1, num_small do
		local grey = use_grey and G.random.sample(self.rnd.rnd, 1, 100) > 50
		self:spawn_meteor("small", grey)
	end
end

function G1:update(t, dt)
	if not G.window.has_input_focus() then
		return
	end
	self.timer:update(dt)
	if G.input.is_key_pressed("q") then
		G.system.quit()
	end
	if G.input.is_key_pressed("f") then
		if not self.fullscreen then
			G.window.set_fullscreen()
			self.fullscreen = true
		else
			G.window.set_windowed()
			self.fullscreen = false
		end
	end
	if G.input.is_key_pressed("r") then
		G.hotload()
	end

	self.starfield:update(dt)

	local _, wy = G.input.mouse_wheel()
	if wy ~= 0 then
		self.target_zoom = G.math.clamp(self.target_zoom + wy * 0.1, 0.3, 2.0)
	end
	self.zoom = self.zoom + (self.target_zoom - self.zoom) * math.min(1, dt * 8)

	if self.state == "game_over" then
		if G.input.is_key_pressed("return") then
			self:init()
		end
		return
	end

	self:update_shake(dt)
	self:update_messages(dt)
	self.entities:update(dt)
	self:update_particles(dt)

	for _, entity in pairs(self.entities.entities) do
		if entity:is_player() or (entity.is_meteor and entity:is_meteor()) then
			self:screen_wrap_entity(entity)
		end
	end

	if self.player and not self.player.dead then
		local pv = self.player.physics:position()
		-- Toroidal lerp: take the shortest path around the world.
		local dx = pv.x - self.cam_x
		local dy = pv.y - self.cam_y
		if dx > WORLD_W / 2 then dx = dx - WORLD_W end
		if dx < -WORLD_W / 2 then dx = dx + WORLD_W end
		if dy > WORLD_H / 2 then dy = dy - WORLD_H end
		if dy < -WORLD_H / 2 then dy = dy + WORLD_H end
		local speed = math.min(1, dt * 5)
		self.cam_x = self.cam_x + dx * speed
		self.cam_y = self.cam_y + dy * speed
		-- Keep camera coordinates within world bounds.
		self.cam_x = self.cam_x % WORLD_W
		self.cam_y = self.cam_y % WORLD_H
	end

	if self.player and not self.player.dead then
		for _, entity in pairs(self.entities.entities) do
			if entity.is_powerup and entity:is_powerup() and entity.check_pickup then
				if entity:check_pickup(self.player) then
					if entity.ptype == "score_bonus" then
						self.score = self.score + 500
					else
						self.player:apply_powerup(entity.ptype)
					end
					self:show_message(POWERUP_NAMES[entity.ptype] or entity.ptype)
				end
			end
		end
	end

	local dead = self.entities:collect_dead()
	local to_spawn = {}
	local powerup_spawns = {}
	for _, entity in ipairs(dead) do
		if entity.is_meteor and entity:is_meteor() then
			self.score = self.score + (SCORE_VALUES[entity.size] or 0)
			local v = entity.physics:position()
			self:spawn_particles(v.x, v.y, G.random.sample(self.rnd.rnd, 5, 8))
			local children = entity:get_split_children(self.rnd.rnd)
			for _, c in ipairs(children) do
				to_spawn[#to_spawn + 1] = c
			end
			if entity.size == "big" then
				self:screen_shake(12)
			elseif entity.size == "med" then
				self:screen_shake(6)
			else
				self:screen_shake(3)
			end
			if (entity.size == "big" or entity.size == "med") and G.random.sample(self.rnd.rnd, 1, 100) <= 20 then
				local v = entity.physics:position()
				local ptype = G.random.pick(self.rnd.rnd, POWERUP_TYPES)
				powerup_spawns[#powerup_spawns + 1] = { x = v.x, y = v.y, ptype = ptype }
			end
		end
	end

	self.entities:remove_dead()

	for _, c in ipairs(to_spawn) do
		local grey = self.wave > 3 and G.random.sample(self.rnd.rnd, 1, 100) > 50
		local m = Meteor(c.x, c.y, c.size, grey)
		local drift = 25
		m:set_drift(math.cos(c.angle) * drift, math.sin(c.angle) * drift)
		self.entities:add(m)
	end

	for _, p in ipairs(powerup_spawns) do
		local pu = Powerup(p.x, p.y, p.ptype)
		self.entities:add(pu)
	end

	if self.wave_active and self.entities:count_meteors() == 0 then
		self.wave_active = false
		self.timer:after(2, function()
			self:start_next_wave()
		end)
	end

	self:update_music()
end

function G1:draw_hud()
	G.graphics.set_color("white")
	draw_number(self.score, SCREEN_W - 20, 15)

	for i = 1, self.lives do
		G.graphics.draw_sprite("playerLife1_green", 350 + (i - 1) * 35, 15)
	end

	G.graphics.set_color("freshgreen")
	G.graphics.draw_rect(10, 10, 300, 20)
	G.graphics.set_color("neonred")
	local health = self.player and self.player:get_health() or 0
	G.graphics.draw_rect(300 * health / 100, 10, 300, 20)

	G.graphics.set_color("white")
	G.graphics.print("WAVE " .. self.wave, SCREEN_W / 2 - 30, 5)

	if self.player and not self.player.dead then
		local pname = self.player:active_powerup_name()
		if pname and POWERUP_HUD_SPRITES[pname] then
			G.graphics.draw_sprite(POWERUP_HUD_SPRITES[pname], SCREEN_W - 50, 50)
		end
	end
end

local MINIMAP_W = 160
local MINIMAP_H = MINIMAP_W * (WORLD_H / WORLD_W)
local MINIMAP_X = SCREEN_W - MINIMAP_W - 10
local MINIMAP_Y = SCREEN_H - MINIMAP_H - 10

function G1:draw_minimap()
	local mx = MINIMAP_X
	local my = MINIMAP_Y
	local sx = MINIMAP_W / WORLD_W
	local sy = MINIMAP_H / WORLD_H

	G.graphics.set_color(0, 0, 0, 150)
	G.graphics.draw_rect(mx, my, mx + MINIMAP_W, my + MINIMAP_H)
	G.graphics.set_color(80, 80, 80, 200)
	G.graphics.draw_rect_outline(mx, my, mx + MINIMAP_W, my + MINIMAP_H)

	for _, entity in pairs(self.entities.entities) do
		if entity.is_meteor and entity:is_meteor() then
			local v = entity.physics:position()
			local px = mx + v.x * sx
			local py = my + v.y * sy
			G.graphics.set_color(180, 120, 60, 220)
			G.graphics.draw_circle(px, py, 2)
		end
		if entity.is_powerup and entity:is_powerup() then
			local px = mx + entity.x * sx
			local py = my + entity.y * sy
			G.graphics.set_color(255, 255, 100, 220)
			G.graphics.draw_circle(px, py, 2)
		end
	end

	if self.player and not self.player.dead then
		local pv = self.player.physics:position()
		local px = mx + pv.x * sx
		local py = my + pv.y * sy
		G.graphics.set_color(100, 255, 100, 255)
		G.graphics.draw_circle(px, py, 3)
	end

	G.graphics.set_color("white")
end

function G1:get_wrap_offsets(x, y, margin)
	local offsets = { { 0, 0 } }
	local near_left = x < margin
	local near_right = x > WORLD_W - margin
	local near_top = y < margin
	local near_bottom = y > WORLD_H - margin
	if near_left then offsets[#offsets + 1] = { WORLD_W, 0 } end
	if near_right then offsets[#offsets + 1] = { -WORLD_W, 0 } end
	if near_top then offsets[#offsets + 1] = { 0, WORLD_H } end
	if near_bottom then offsets[#offsets + 1] = { 0, -WORLD_H } end
	if near_left and near_top then offsets[#offsets + 1] = { WORLD_W, WORLD_H } end
	if near_left and near_bottom then offsets[#offsets + 1] = { WORLD_W, -WORLD_H } end
	if near_right and near_top then offsets[#offsets + 1] = { -WORLD_W, WORLD_H } end
	if near_right and near_bottom then offsets[#offsets + 1] = { -WORLD_W, -WORLD_H } end
	return offsets
end

function G1:draw_entities_wrapped()
	local margin = SCREEN_W / self.zoom
	for _, entity in pairs(self.entities.entities) do
		local ex, ey
		if entity.physics and not entity.physics.destroyed then
			local v = entity.physics:position()
			ex, ey = v.x, v.y
		elseif entity.x then
			ex, ey = entity.x, entity.y
		end
		if ex then
			local offsets = self:get_wrap_offsets(ex, ey, margin)
			for _, off in ipairs(offsets) do
				G.graphics.push()
				G.graphics.translate(off[1], off[2])
				entity:draw()
				G.graphics.pop()
			end
		else
			entity:draw()
		end
	end
end

function G1:draw_aim_line()
	if not self.player or self.player.dead then return end
	local v = self.player.physics:position()
	local angle = self.player.physics:angle()
	local dx = math.sin(angle)
	local dy = -math.cos(angle)
	local start_dist = 50
	local dot_spacing = 14
	local num_dots = 25
	for i = 0, num_dots - 1 do
		local dist = start_dist + i * dot_spacing
		local alpha = math.floor(180 * (1 - i / num_dots))
		G.graphics.set_color(100, 255, 100, alpha)
		G.graphics.draw_circle(v.x + dx * dist, v.y + dy * dist, 2)
	end
	G.graphics.set_color("white")
end

function G1:draw()
	-- Render everything to the game canvas with CRT shader.
	G.graphics.set_canvas(self.game_canvas)
	G.graphics.clear()

	G.graphics.attach_shader("crt.frag")

	self.starfield:draw(self.cam_x, self.cam_y)

	G.graphics.push()
	G.graphics.translate(SCREEN_W / 2 + self.shake.x, SCREEN_H / 2 + self.shake.y)
	G.graphics.scale(self.zoom, self.zoom)
	G.graphics.translate(-self.cam_x, -self.cam_y)

	self:draw_entities_wrapped()
	self:draw_particles()

	if self.player and not self.player.dead then
		local pv = self.player.physics:position()
		local margin = SCREEN_W / self.zoom
		local offsets = self:get_wrap_offsets(pv.x, pv.y, margin)
		for _, off in ipairs(offsets) do
			G.graphics.push()
			G.graphics.translate(off[1], off[2])
			self:draw_aim_line()
			G.graphics.pop()
		end
	end

	G.graphics.pop()

	self:draw_hud()
	self:draw_minimap()
	self:draw_messages()

	if self.state == "game_over" then
		G.graphics.set_color(200, 0, 0, 255)
		G.graphics.print("GAME OVER", SCREEN_W / 2 - 50, SCREEN_H / 2 - 40)
		G.graphics.set_color("white")
		G.graphics.print("SCORE: " .. self.score, SCREEN_W / 2 - 50, SCREEN_H / 2)
		G.graphics.print("Press ENTER to restart", SCREEN_W / 2 - 80, SCREEN_H / 2 + 30)
	end

	G.graphics.attach_shader()

	-- Draw game canvas to screen, then overlay vignette.
	G.graphics.set_canvas()
	G.graphics.clear()
	G.graphics.set_color("white")
	G.graphics.set_blend_mode("premultiplied")
	G.graphics.draw_canvas(self.game_canvas, 0, 0)
	G.graphics.set_blend_mode("alpha")
	G.graphics.draw_canvas(self.vignette_canvas, 0, 0)
	G.graphics.set_color("white")
end

local G2 = {}

function G2:init() end

function G2:update(t, dt) end

function G2:draw() end

local Game = { g = G1 }

function Game:init()
	self.g:init()
end

function Game:update(t, dt)
	if G.input.is_key_pressed("n") then
		print("Using Game G1")
		if self.g ~= G1 then
			self.g = G1
			self.g:init()
		end
	elseif G.input.is_key_pressed("m") then
		print("Using Game G2")
		if self.g ~= G2 then
			self.g = G2
			self.g:init()
		end
	end
	self.g:update(t, dt)
end

function Game:draw()
	self.g:draw()
end

return Game
