local Entity = require("entity")

local Bullet = Entity:extend()

local FORCE = 500
local LIFETIME = 1.5

local count = 0

function Bullet:new(x, y, angle)
	local id = "bullet" .. count
	count = count + 1
	Bullet.super.new(self, x, y, angle, "laserGreen11", id)
	self.dead = false
	self.lifetime = LIFETIME
	self.travel_angle = angle
	self.dx = math.sin(angle)
	self.dy = -math.cos(angle)
end

function Bullet:update(dt)
	self.lifetime = self.lifetime - dt
	if self.lifetime <= 0 then
		self.dead = true
		return
	end
	self.physics:apply_force(self.dx * FORCE, self.dy * FORCE)
end

function Bullet:draw()
	local v = self.physics:position()
	G.graphics.draw_sprite(self.image, v.x, v.y, self.travel_angle)
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
