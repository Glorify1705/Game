local Entity = require("entity")
local FSM = require("fsm")
local steer = require("steer")

local Enemy = Entity:extend()

local CHASE_FORCE = 40
local SEPARATE_FORCE = 30
local SEPARATE_DIST = 80
local MAX_HEALTH = 3
local SPAWN_TIME = 1.5
local FLASH_DURATION = 0.1
local BLINK_RATE = 0.08

local SPRITES = { "enemyRed1", "enemyRed2", "enemyRed3", "enemyRed4", "enemyRed5" }

local count = 0

function Enemy:new(x, y, world_w, world_h, get_player, get_enemy_positions)
	local sprite = SPRITES[math.random(#SPRITES)]
	local id = "enemy" .. count
	count = count + 1
	Enemy.super.new(self, x, y, 0, sprite, id, {
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
	self.invincible = true
	self.spawn_timer = SPAWN_TIME
	self.visible = true
	self.blink_timer = 0
	self.fsm = FSM.new(self:make_states(), "spawn")
end

function Enemy:make_states()
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
				local player = s.get_player()
				if player then
					local pv = player.physics:position()
					local ev = s.physics:position()
					local fx, fy = steer.seek_toroidal(
						ev.x, ev.y, pv.x, pv.y,
						CHASE_FORCE * 0.3, s.world_w, s.world_h
					)
					s.physics:apply_world_force(fx, fy)
				end
				if s.spawn_timer <= 0 then
					s.fsm:transition(s, "chase")
				end
			end,
			exit = function()
				s.invincible = false
				s.visible = true
			end,
		},

		chase = {
			update = function(_, dt)
				local player = s.get_player()
				if not player then return end
				local pv = player.physics:position()
				local pvx, pvy = player.physics:linear_velocity()
				local ev = s.physics:position()
				local pfx, pfy = steer.pursue_toroidal(
					ev.x, ev.y, pv.x, pv.y, pvx, pvy,
					CHASE_FORCE, s.world_w, s.world_h
				)
				local neighbors = s.get_enemy_positions(s)
				local sfx, sfy = steer.separate_toroidal(
					ev.x, ev.y, neighbors, SEPARATE_DIST, SEPARATE_FORCE,
					s.world_w, s.world_h
				)
				s.physics:apply_world_force(pfx + sfx, pfy + sfy)
			end,
		},

		dead = {
			enter = function()
				s.dead = true
			end,
		},
	}
end

function Enemy:update(dt)
	self.fsm:update(self, dt)
	if self.flash_timer > 0 then
		self.flash_timer = self.flash_timer - dt
	end
	-- Rotate sprite to face velocity direction.
	local vx, vy = self.physics:linear_velocity()
	if vx * vx + vy * vy > 1 then
		local target_angle = math.atan2(vx, -vy)
		local current = self.physics:angle()
		local delta = target_angle - current
		if delta > math.pi then delta = delta - 2 * math.pi end
		if delta < -math.pi then delta = delta + 2 * math.pi end
		self.physics:rotate(delta * 0.3)
	end
end

function Enemy:draw()
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

function Enemy:on_collision(other)
	if not other then return end
	if self.invincible then return end
	if other.category ~= "bullet" then return end
	self.health = self.health - 1
	self.flash_timer = FLASH_DURATION
	if self.health <= 0 then
		self.fsm:transition(self, "dead")
	end
end

function Enemy:is_enemy()
	return true
end

return Enemy
