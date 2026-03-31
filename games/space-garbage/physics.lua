local Object = require("classic")
local Vec2 = require("vector2d")

local Physics = Object:extend()

function Physics:new(tx, ty, bx, by, angle, id, options)
	self.handle = G.physics.add_box(tx, ty, bx, by, angle, id, options)
end

function Physics:position()
	return Vec2(G.physics.position(self.handle))
end

function Physics:destroy()
	if not self.destroyed then
		G.physics.destroy_handle(self.handle)
		self.destroyed = true
	end
end

function Physics:__gc()
	self:destroy()
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

function Physics:set_position(x, y)
	G.physics.set_position(self.handle, x, y)
end

function Physics:linear_velocity()
	return G.physics.linear_velocity(self.handle)
end

function Physics:set_linear_velocity(vx, vy)
	G.physics.set_linear_velocity(self.handle, vx, vy)
end

function Physics:angular_velocity()
	return G.physics.angular_velocity(self.handle)
end

function Physics:set_angular_velocity(omega)
	G.physics.set_angular_velocity(self.handle, omega)
end

function Physics:set_fixed_rotation(fixed)
	G.physics.set_fixed_rotation(self.handle, fixed)
end

function Physics:get_fixed_rotation()
	return G.physics.get_fixed_rotation(self.handle)
end

return Physics
