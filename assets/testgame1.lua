local Player = require("player")
local Meteor = require("meteor")
local Bullet = require("bullet")
local Timer = require("timer")
local Random = require("random")
local Object = require("classic")

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

function G1:init()
	G.window.set_title("My awesome Lua game 1!")
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
	self.player = Player(SCREEN_W / 2, SCREEN_H / 2)
	self.entities:add(self.player)
	self.respawning = false

	self.player:set_spawn_callback(function(x, y, angle)
		local b = Bullet(x, y, angle)
		self.entities:add(b)
	end)

	self.player:set_death_callback(function()
		self:on_player_death()
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

function G1:screen_shake(intensity)
	self.shake.x = intensity
	self.shake.y = intensity
	self.timer:tween(0.3, self.shake, { x = 0, y = 0 }, "out-quad")
end

function G1:spawn_meteor_offscreen(size, grey)
	local rng = self.rnd.rnd
	local side = G.random.sample(rng, 1, 4)
	local x, y
	local margin = 80
	if side == 1 then
		x = G.random.sample(rng, 0, SCREEN_W)
		y = -margin
	elseif side == 2 then
		x = SCREEN_W + margin
		y = G.random.sample(rng, 0, SCREEN_H)
	elseif side == 3 then
		x = G.random.sample(rng, 0, SCREEN_W)
		y = SCREEN_H + margin
	else
		x = -margin
		y = G.random.sample(rng, 0, SCREEN_H)
	end

	local m = Meteor(x, y, size, grey)
	local cx = SCREEN_W / 2 + G.random.sample(rng, -200, 200)
	local cy = SCREEN_H / 2 + G.random.sample(rng, -200, 200)
	local dx = cx - x
	local dy = cy - y
	local dist = math.sqrt(dx * dx + dy * dy)
	local base_speed = 10 + self.wave * 2
	if dist > 0 then
		m.physics:apply_linear_impulse(dx / dist * base_speed, dy / dist * base_speed)
	end
	self.entities:add(m)
end

function G1:start_next_wave()
	self.wave = self.wave + 1
	self.wave_active = true
	local num_big = self.wave + 2
	local num_med = math.floor(self.wave / 2)
	local use_grey = self.wave > 3

	for i = 1, num_big do
		local grey = use_grey and G.random.sample(self.rnd.rnd) > 0.5
		self:spawn_meteor_offscreen("big", grey)
	end
	for i = 1, num_med do
		local grey = use_grey and G.random.sample(self.rnd.rnd) > 0.5
		self:spawn_meteor_offscreen("med", grey)
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

	if self.state == "game_over" then
		if G.input.is_key_pressed("return") then
			self:init()
		end
		return
	end

	self.entities:update(dt)

	local dead = self.entities:collect_dead()
	local to_spawn = {}
	for _, entity in ipairs(dead) do
		if entity.is_meteor and entity:is_meteor() then
			self.score = self.score + (SCORE_VALUES[entity.size] or 0)
			local children = entity:get_split_children(self.rnd.rnd)
			for _, c in ipairs(children) do
				to_spawn[#to_spawn + 1] = c
			end
			if entity.size == "big" then
				self:screen_shake(8)
			elseif entity.size == "med" then
				self:screen_shake(4)
			end
		end
	end

	self.entities:remove_dead()

	for _, c in ipairs(to_spawn) do
		local grey = self.wave > 3 and G.random.sample(self.rnd.rnd) > 0.5
		local m = Meteor(c.x, c.y, c.size, grey)
		local impulse = 20
		m.physics:apply_linear_impulse(math.cos(c.angle) * impulse, math.sin(c.angle) * impulse)
		self.entities:add(m)
	end

	if self.wave_active and self.entities:count_meteors() == 0 then
		self.wave_active = false
		self.timer:after(2, function()
			self:start_next_wave()
		end)
	end
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
end

function G1:draw()
	G.graphics.clear()
	G.graphics.push()
	G.graphics.translate(self.shake.x, self.shake.y)

	self.entities:draw()

	G.graphics.pop()

	self:draw_hud()

	if self.state == "game_over" then
		G.graphics.set_color(200, 0, 0, 255)
		G.graphics.print("GAME OVER", SCREEN_W / 2 - 50, SCREEN_H / 2 - 40)
		G.graphics.set_color("white")
		G.graphics.print("SCORE: " .. self.score, SCREEN_W / 2 - 50, SCREEN_H / 2)
		G.graphics.print("Press ENTER to restart", SCREEN_W / 2 - 80, SCREEN_H / 2 + 30)
	end

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
