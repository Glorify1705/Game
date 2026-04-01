local Object = require("classic")

local Powerup = Object:extend()

local count = 0
local DRIFT_SPEED = 20
local LIFETIME = 8.0
local BLINK_START = 6.0
local PICKUP_RADIUS = 40

local TYPE_SPRITES = {
	shield = "powerupBlue_shield",
	rapid_fire = "powerupGreen_bolt",
	heal = "powerupRed_star",
	score_bonus = "powerupYellow_star",
}

function Powerup:new(x, y, ptype)
	self.ptype = ptype
	self.image = TYPE_SPRITES[ptype]
	self.entity_id = "powerup" .. count
	count = count + 1
	self.x = x
	self.y = y
	self.vy = DRIFT_SPEED
	self.dead = false
	self.lifetime = LIFETIME
	self.visible = true
	self.blink_timer = 0
end

function Powerup:id()
	return self.entity_id
end

function Powerup:update(dt)
	self.lifetime = self.lifetime - dt
	self.y = self.y + self.vy * dt
	if self.lifetime <= 0 then
		self.dead = true
		return
	end
	if self.lifetime < BLINK_START then
		self.blink_timer = self.blink_timer + dt
		if self.blink_timer > 0.15 then
			self.blink_timer = self.blink_timer - 0.15
			self.visible = not self.visible
		end
	end
end

function Powerup:check_pickup(player)
	if self.dead then return false end
	if player.dead then return false end
	local pv = player.physics:position()
	local dx = self.x - pv.x
	local dy = self.y - pv.y
	if dx * dx + dy * dy < PICKUP_RADIUS * PICKUP_RADIUS then
		self.dead = true
		G.sound.play_effect("powerup.wav")
		return true
	end
	return false
end

function Powerup:draw()
	if not self.visible then return end
	G.graphics.draw_sprite(self.image, self.x, self.y)
end

function Powerup:on_collision(other) end

function Powerup:is_powerup()
	return true
end

function Powerup:is_player()
	return false
end

function Powerup:destroy() end

return Powerup
