local Entity = require("entity")

local Bullet = Entity:extend()

local SPEED = 80
local LIFETIME = 2.0

local count = 0

function Bullet:new(x, y, angle)
	local id = "bullet" .. count
	count = count + 1
	Bullet.super.new(self, x, y, angle, "laserGreen11", id)
	self.dead = false
	self.lifetime = LIFETIME
	self.physics:apply_linear_impulse(math.sin(angle) * SPEED, -math.cos(angle) * SPEED)
end

function Bullet:update(dt)
	self.lifetime = self.lifetime - dt
	if self.lifetime <= 0 then
		self.dead = true
	end
end

function Bullet:on_collision(other)
	if other and not other:is_player() then
		self.dead = true
	end
end

function Bullet:is_bullet()
	return true
end

return Bullet
