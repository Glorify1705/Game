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
	self.dead = false
	self.lifetime = LIFETIME
	self.visible = true
	self.blink_timer = 0

	local info = G.assets.sprite_info(self.image)
	self.handle = G.physics.add_box(
		x, y, x + info.width, y + info.height, 0, self,
		{
			body_type = "kinematic",
			sensor = true,
			category = "powerup",
			collides_with = { "player" },
		}
	)
	G.physics.set_linear_velocity(self.handle, 0, DRIFT_SPEED)
end

function Powerup:id()
	return self.entity_id
end

function Powerup:update(dt)
	self.lifetime = self.lifetime - dt
	if self.lifetime <= 0 then
		self.dead = true
		return
	end
	self.x, self.y = G.physics.position(self.handle)
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
	local px, py = G.physics.position(self.handle)
	local pv = player.physics:position()
	local dx = px - pv.x
	local dy = py - pv.y
	if dx * dx + dy * dy < PICKUP_RADIUS * PICKUP_RADIUS then
		self.dead = true
		G.sound.play_effect("powerup.wav")
		return true
	end
	return false
end

function Powerup:draw()
	if not self.visible then return end
	local x, y = G.physics.position(self.handle)
	G.graphics.draw_sprite(self.image, x, y)
end

function Powerup:on_collision(other) end

function Powerup:is_powerup()
	return true
end

function Powerup:is_player()
	return false
end

function Powerup:destroy()
	if self.handle and not self.destroyed then
		G.physics.destroy_handle(self.handle)
		self.destroyed = true
	end
end

return Powerup
