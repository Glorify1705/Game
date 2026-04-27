local Entity = require("entity")
local FSM = require("fsm")
local steer = require("steer")

local Turret = Entity:extend()

local ORBIT_FORCE = 120
local DAMPING = 1.5
local SEPARATE_FORCE = 50
local SEPARATE_DIST = 80
local MAX_HEALTH = 4
local SPAWN_TIME = 1.5
local FLASH_DURATION = 0.1
local BLINK_RATE = 0.08
local FIRE_COOLDOWN = 2.5
local FIRE_PAUSE = 0.3
local PREFERRED_DIST = 500
local DIST_TOLERANCE = 100

local SPRITES = { "enemyBlue1", "enemyBlue2", "enemyBlue3", "enemyBlue4", "enemyBlue5" }

local count = 0

function Turret:new(x, y, world_w, world_h, get_player, get_enemy_positions, spawn_bullet_fn)
	local sprite = SPRITES[math.random(#SPRITES)]
	local id = "turret" .. count
	count = count + 1
	Turret.super.new(self, x, y, 0, sprite, id, {
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
	self.spawn_bullet_fn = spawn_bullet_fn
	self.invincible = true
	self.spawn_timer = SPAWN_TIME
	self.visible = true
	self.blink_timer = 0
	self.fire_timer = FIRE_COOLDOWN
	self.physics:set_fixed_rotation(true)
	self.physics:set_linear_damping(DAMPING)
	self.fsm = FSM.new(self:make_states(), "spawn")
end

function Turret:make_states()
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
					s.fsm:transition(s, "orbit")
				end
			end,
			exit = function()
				s.invincible = false
				s.visible = true
			end,
		},

		orbit = {
			update = function(_, dt)
				local player = s.get_player()
				if player then
					local pv = player.physics:position()
					local ev = s.physics:position()
					-- Compute toroidal delta to player.
					local dx = pv.x - ev.x
					local dy = pv.y - ev.y
					if dx > s.world_w / 2 then dx = dx - s.world_w end
					if dx < -s.world_w / 2 then dx = dx + s.world_w end
					if dy > s.world_h / 2 then dy = dy - s.world_h end
					if dy < -s.world_h / 2 then dy = dy + s.world_h end
					local dist = math.sqrt(dx * dx + dy * dy)
					-- Orbit: seek a point at PREFERRED_DIST from player, perpendicular.
					local fx, fy = 0, 0
					if dist < PREFERRED_DIST - DIST_TOLERANCE then
						-- Too close, flee.
						fx, fy = steer.flee(ev.x, ev.y, ev.x + dx, ev.y + dy, ORBIT_FORCE)
					elseif dist > PREFERRED_DIST + DIST_TOLERANCE then
						-- Too far, approach.
						fx, fy = steer.seek(ev.x, ev.y, ev.x + dx, ev.y + dy, ORBIT_FORCE)
					else
						-- At good range, orbit (perpendicular drift).
						if dist > 0.001 then
							fx = -dy / dist * ORBIT_FORCE * 0.5
							fy = dx / dist * ORBIT_FORCE * 0.5
						end
					end
					local neighbors = s.get_enemy_positions(s)
					local sfx, sfy = steer.separate_toroidal(
						ev.x, ev.y, neighbors, SEPARATE_DIST, SEPARATE_FORCE,
						s.world_w, s.world_h
					)
					s.physics:apply_world_force(fx + sfx, fy + sfy)
					-- Rotate to face player.
					local target_angle = math.atan2(dx, -dy)
					local current = s.physics:angle()
					local rd = target_angle - current
					if rd > math.pi then rd = rd - 2 * math.pi end
					if rd < -math.pi then rd = rd + 2 * math.pi end
					s.physics:rotate(rd * 0.3)
				end
				-- Fire cooldown.
				s.fire_timer = s.fire_timer - dt
				if s.fire_timer <= 0 and s.get_player() then
					s.fsm:transition(s, "fire")
				end
			end,
		},

		fire = {
			enter = function()
				s.fire_pause = FIRE_PAUSE
			end,
			update = function(_, dt)
				s.fire_pause = s.fire_pause - dt
				if s.fire_pause <= 0 then
					local player = s.get_player()
					if player then
						local ev = s.physics:position()
						local pv = player.physics:position()
						local pvx, pvy = player.physics:linear_velocity()
						-- Predict where player will be.
						local dx = pv.x - ev.x
						local dy = pv.y - ev.y
						if dx > s.world_w / 2 then dx = dx - s.world_w end
						if dx < -s.world_w / 2 then dx = dx + s.world_w end
						if dy > s.world_h / 2 then dy = dy - s.world_h end
						if dy < -s.world_h / 2 then dy = dy + s.world_h end
						local dist = math.sqrt(dx * dx + dy * dy)
						local predict = dist / 400
						local tx = ev.x + dx + pvx * predict
						local ty = ev.y + dy + pvy * predict
						local angle = math.atan2(tx - ev.x, -(ty - ev.y))
						local nose_x = ev.x + math.sin(angle) * 40
						local nose_y = ev.y - math.cos(angle) * 40
						local fire_angle = math.atan2(tx - nose_x, -(ty - nose_y))
						s.spawn_bullet_fn(nose_x, nose_y, fire_angle)
						G.sound.play_effect("laser.wav")
					end
					s.fire_timer = FIRE_COOLDOWN
					s.fsm:transition(s, "orbit")
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

function Turret:update(dt)
	self.fsm:update(self, dt)
	if self.flash_timer > 0 then
		self.flash_timer = self.flash_timer - dt
	end
end

function Turret:draw()
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

function Turret:on_collision(other)
	if not other then return end
	if self.invincible then return end
	if other.category ~= "bullet" then return end
	self.health = self.health - 1
	self.flash_timer = FLASH_DURATION
	if self.health <= 0 then
		self.fsm:transition(self, "dead")
	end
end

function Turret:is_enemy()
	return true
end

function Turret:is_turret()
	return true
end

return Turret
