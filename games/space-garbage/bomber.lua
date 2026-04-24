local Entity = require("entity")
local FSM = require("fsm")
local steer = require("steer")

local Bomber = Entity:extend()

local APPROACH_FORCE = 30
local FLEE_FORCE = 35
local SEPARATE_FORCE = 25
local SEPARATE_DIST = 80
local MAX_HEALTH = 3
local SPAWN_TIME = 1.5
local FLASH_DURATION = 0.1
local BLINK_RATE = 0.08
local THROW_RANGE = 600
local THROW_PAUSE = 0.5
local RETREAT_TIME = 2.0
local THROW_COOLDOWN = 4.0
local BOMB_SPEED = 200

local SPRITES = { "enemyBlack1", "enemyBlack2", "enemyBlack3", "enemyBlack4", "enemyBlack5" }

local count = 0

function Bomber:new(x, y, world_w, world_h, get_player, get_enemy_positions, spawn_bomb_fn)
	local sprite = SPRITES[math.random(#SPRITES)]
	local id = "bomber" .. count
	count = count + 1
	Bomber.super.new(self, x, y, 0, sprite, id, {
		category = "enemy",
		collides_with = { "player", "bullet" },
	})
	self.dead = false
	self.health = MAX_HEALTH
	self.flash_timer = 0
	self.world_w = world_w
	self.world_h = world_h
	self.get_player = get_player
	self.get_enemy_positions = get_enemy_positions
	self.spawn_bomb_fn = spawn_bomb_fn
	self.invincible = true
	self.spawn_timer = SPAWN_TIME
	self.visible = true
	self.blink_timer = 0
	self.throw_cooldown = THROW_COOLDOWN * 0.5
	self.physics:set_fixed_rotation(true)
	self.fsm = FSM.new(self:make_states(), "spawn")
end

function Bomber:make_states()
	local s = self
	return {
		spawn = {
			update = function(_, dt)
				s.spawn_timer = s.spawn_timer - dt
				s.blink_timer = s.blink_timer + dt
				if s.blink_timer >= BLINK_RATE then
					s.blink_timer = s.blink_timer - BLINK_RATE
					s.visible = not s.visible
				end
				if s.spawn_timer <= 0 then
					s.fsm:transition(s, "approach")
				end
			end,
			exit = function()
				s.invincible = false
				s.visible = true
			end,
		},

		approach = {
			update = function(_, dt)
				local player = s.get_player()
				if player then
					local pv = player.physics:position()
					local ev = s.physics:position()
					local fx, fy = steer.seek_toroidal(
						ev.x, ev.y, pv.x, pv.y,
						APPROACH_FORCE, s.world_w, s.world_h
					)
					local neighbors = s.get_enemy_positions(s)
					local sfx, sfy = steer.separate_toroidal(
						ev.x, ev.y, neighbors, SEPARATE_DIST, SEPARATE_FORCE,
						s.world_w, s.world_h
					)
					s.physics:apply_world_force(fx + sfx, fy + sfy)
					-- Check if in throw range.
					local dx = pv.x - ev.x
					local dy = pv.y - ev.y
					if dx > s.world_w / 2 then dx = dx - s.world_w end
					if dx < -s.world_w / 2 then dx = dx + s.world_w end
					if dy > s.world_h / 2 then dy = dy - s.world_h end
					if dy < -s.world_h / 2 then dy = dy + s.world_h end
					local dist = math.sqrt(dx * dx + dy * dy)
					s.throw_cooldown = s.throw_cooldown - dt
					if dist < THROW_RANGE and s.throw_cooldown <= 0 then
						s.fsm:transition(s, "throw")
					end
				end
			end,
		},

		throw = {
			enter = function()
				s.throw_pause = THROW_PAUSE
			end,
			update = function(_, dt)
				s.throw_pause = s.throw_pause - dt
				if s.throw_pause <= 0 then
					local player = s.get_player()
					if player then
						local ev = s.physics:position()
						local pv = player.physics:position()
						local pvx, pvy = player.physics:linear_velocity()
						-- Predict player position accounting for bomb travel time.
						local dx = pv.x - ev.x
						local dy = pv.y - ev.y
						if dx > s.world_w / 2 then dx = dx - s.world_w end
						if dx < -s.world_w / 2 then dx = dx + s.world_w end
						if dy > s.world_h / 2 then dy = dy - s.world_h end
						if dy < -s.world_h / 2 then dy = dy + s.world_h end
						local dist = math.sqrt(dx * dx + dy * dy)
						local travel_time = dist / BOMB_SPEED
						local tx = pv.x + pvx * travel_time
						local ty = pv.y + pvy * travel_time
						s.spawn_bomb_fn(ev.x, ev.y, tx, ty)
					end
					s.throw_cooldown = THROW_COOLDOWN
					s.fsm:transition(s, "retreat")
				end
			end,
		},

		retreat = {
			enter = function()
				s.retreat_timer = RETREAT_TIME
			end,
			update = function(_, dt)
				local player = s.get_player()
				if player then
					local pv = player.physics:position()
					local ev = s.physics:position()
					-- Flee from player.
					local dx = pv.x - ev.x
					local dy = pv.y - ev.y
					if dx > s.world_w / 2 then dx = dx - s.world_w end
					if dx < -s.world_w / 2 then dx = dx + s.world_w end
					if dy > s.world_h / 2 then dy = dy - s.world_h end
					if dy < -s.world_h / 2 then dy = dy + s.world_h end
					local fx, fy = steer.flee(ev.x, ev.y, ev.x + dx, ev.y + dy, FLEE_FORCE)
					s.physics:apply_world_force(fx, fy)
				end
				s.retreat_timer = s.retreat_timer - dt
				if s.retreat_timer <= 0 then
					s.fsm:transition(s, "approach")
				end
			end,
		},

		dead = {
			enter = function()
				s.dead = true
			end,
		},
	}
end

function Bomber:update(dt)
	self.fsm:update(self, dt)
	if self.flash_timer > 0 then
		self.flash_timer = self.flash_timer - dt
	end
	-- Rotate to face velocity direction.
	local vx, vy = self.physics:linear_velocity()
	if vx * vx + vy * vy > 1 then
		local target_angle = math.atan2(vx, -vy)
		local current = self.physics:angle()
		self.physics:rotate(target_angle - current)
	end
end

function Bomber:draw()
	if not self.visible then return end
	if self.flash_timer > 0 then
		G.graphics.set_color(255, 80, 80, 255)
	end
	local v = self.physics:position()
	local angle = self.physics:angle()
	G.graphics.draw_sprite(self.image, v.x, v.y, angle)
	if self.flash_timer > 0 then
		G.graphics.set_color("white")
	end
end

function Bomber:on_collision(other)
	if not other then return end
	if self.invincible then return end
	if other.category ~= "bullet" then return end
	self.health = self.health - 1
	self.flash_timer = FLASH_DURATION
	if self.health <= 0 then
		self.fsm:transition(self, "dead")
	end
end

function Bomber:is_enemy()
	return true
end

function Bomber:is_bomber()
	return true
end

return Bomber
