local Entity = require("entity")

local Powerup = Entity:extend()

local count = 0
local DRIFT_SPEED = 30
local LIFETIME = 8.0
local BLINK_START = 6.0

local TYPE_SPRITES = {
	shield = "powerupBlue_shield",
	rapid_fire = "powerupGreen_bolt",
	heal = "powerupRed_star",
	score_bonus = "powerupYellow_star",
}

function Powerup:new(x, y, ptype)
	self.ptype = ptype
	local sprite = TYPE_SPRITES[ptype]
	local id = "powerup" .. count
	count = count + 1
	Powerup.super.new(self, x, y, 0, sprite, id)
	self.dead = false
	self.picked_up = false
	self.lifetime = LIFETIME
	self.visible = true
	self.blink_timer = 0
	self.physics:apply_linear_impulse(0, DRIFT_SPEED)
end

function Powerup:update(dt)
	self.lifetime = self.lifetime - dt
	if self.lifetime <= 0 then
		self.dead = true
		return
	end
	if self.lifetime < (LIFETIME - BLINK_START) then
		self.blink_timer = self.blink_timer + dt
		if self.blink_timer > 0.15 then
			self.blink_timer = self.blink_timer - 0.15
			self.visible = not self.visible
		end
	end
end

function Powerup:draw()
	if not self.visible then return end
	local v = self.physics:position()
	local angle = self.physics:angle()
	G.graphics.draw_sprite(self.image, v.x, v.y, angle)
end

function Powerup:on_collision(other)
	if other and other.is_player and other:is_player() then
		self.dead = true
		self.picked_up = true
		G.sound.play_effect("pong-blip1.ogg")
	end
end

function Powerup:is_powerup()
	return true
end

return Powerup
