local Entity = require("entity")

local Bullet = Entity:extend()

local FORCE = 500
local LIFETIME = 1.5

local count = 0

function Bullet:new(x, y, angle, world_w, world_h)
	local id = "bullet" .. count
	count = count + 1
	Bullet.super.new(self, x, y, angle, "laserGreen11", id, {
		category = "bullet",
		collides_with = { "meteor", "enemy" },
	})
	self.physics:set_fixed_rotation(true)
	self.dead = false
	self.lifetime = LIFETIME
	self.travel_angle = angle
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
	-- Body-local (0, -1) is "forward" (the direction the sprite faces).
	self.physics:apply_force(0, -FORCE)
end

function Bullet:draw()
	local v = self.physics:position()
	G.graphics.draw_sprite(self.image, v.x, v.y, self.travel_angle)
end

function Bullet:on_collision(other)
	if not other then return end
	self.dead = true
end

return Bullet
