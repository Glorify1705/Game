Object = require("classic")
Vec2 = require("vector2d")

Physics = Object:extend()

function Physics:new(tx, ty, bx, by, angle, id)
	self.handle = G.physics.add_box(tx, ty, bx, by, angle, id)
end

function Physics:position()
	return Vec2(G.physics.position(self.handle))
end

function Physics:__gc()
	G.physics.destroy_handle(self.handle)
end

function Physics:angle()
	return G.physics.angle(self.handle)
end

function Physics:apply_linear_impulse(x, y)
	G.physics.apply_linear_impulse(self.handle, x, y)
end

function Physics:apply_force(x, y)
	G.physics.apply_force(self.handle, x, y)
end

function Physics:rotate(angle)
	G.physics.rotate(self.handle, angle)
end

function Physics:apply_torque(angle)
	G.physics.apply_torque(self.handle, angle)
end

return Physics
