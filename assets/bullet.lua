local Entity = require("entity")
local C = require("collision_groups")

local Bullet = Entity:extend()

local FORCE = 500
local LIFETIME = 1.5

local count = 0

function Bullet:new(x, y, angle, world_w, world_h)
	local id = "bullet" .. count
	count = count + 1
	Bullet.super.new(self, x, y, angle, "laserGreen11", id, {
		category = C.BULLET,
		mask = C.METEOR,
	})
	self.dead = false
	self.lifetime = LIFETIME
	self.travel_angle = angle
	self.dx = math.sin(angle)
	self.dy = -math.cos(angle)
	self.world_w = world_w
	self.world_h = world_h
end

function Bullet:update(dt)
	self.lifetime = self.lifetime - dt
	if self.lifetime <= 0 then
		self.dead = true
		return
	end
	local v = self.physics:position()
	if v.x < 0 or v.x > self.world_w or v.y < 0 or v.y > self.world_h then
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
	if not other then return end
	self.dead = true
end

function Bullet:is_bullet()
	return true
end

return Bullet
