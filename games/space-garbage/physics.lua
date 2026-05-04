local Object = require("classic")
local Vec2 = require("vector2d")

-- Localize G.physics functions to avoid 3 table lookups per call.
local _add_box = G.physics.add_box
local _destroy = G.physics.destroy_handle
local _position = G.physics.position
local _angle = G.physics.angle
local _set_angle = G.physics.set_angle
local _rotate = G.physics.rotate
local _apply_force = G.physics.apply_force
local _apply_force_world = G.physics.apply_force_world
local _apply_impulse = G.physics.apply_linear_impulse
local _apply_torque = G.physics.apply_torque
local _set_position = G.physics.set_position
local _linear_velocity = G.physics.linear_velocity
local _set_linear_velocity = G.physics.set_linear_velocity
local _angular_velocity = G.physics.angular_velocity
local _set_angular_velocity = G.physics.set_angular_velocity
local _set_linear_damping = G.physics.set_linear_damping
local _set_fixed_rotation = G.physics.set_fixed_rotation
local _get_fixed_rotation = G.physics.get_fixed_rotation
local _move_toward = G.physics.move_toward
local _look_at = G.physics.look_at

local Physics = Object:extend()

function Physics:new(tx, ty, bx, by, angle, id, options)
	self.handle = _add_box(tx, ty, bx, by, angle, id, options)
end

function Physics:position()
	return Vec2(_position(self.handle))
end

function Physics:destroy()
	if not self.destroyed then
		_destroy(self.handle)
		self.destroyed = true
	end
end

function Physics:__gc()
	self:destroy()
end

function Physics:angle()
	return _angle(self.handle)
end

function Physics:set_angle(a)
	_set_angle(self.handle, a)
end

function Physics:apply_linear_impulse(x, y)
	_apply_impulse(self.handle, x, y)
end

function Physics:apply_force(x, y)
	_apply_force(self.handle, x, y)
end

function Physics:apply_world_force(x, y)
	_apply_force_world(self.handle, x, y)
end

function Physics:rotate(a)
	_rotate(self.handle, a)
end

function Physics:move_toward(tx, ty, speed)
	_move_toward(self.handle, tx, ty, speed)
end

function Physics:look_at(tx, ty)
	_look_at(self.handle, tx, ty)
end

function Physics:apply_torque(t)
	_apply_torque(self.handle, t)
end

function Physics:set_position(x, y)
	_set_position(self.handle, x, y)
end

function Physics:linear_velocity()
	return _linear_velocity(self.handle)
end

function Physics:set_linear_velocity(vx, vy)
	_set_linear_velocity(self.handle, vx, vy)
end

function Physics:angular_velocity()
	return _angular_velocity(self.handle)
end

function Physics:set_angular_velocity(omega)
	_set_angular_velocity(self.handle, omega)
end

function Physics:set_linear_damping(damping)
	_set_linear_damping(self.handle, damping)
end

function Physics:set_fixed_rotation(fixed)
	_set_fixed_rotation(self.handle, fixed)
end

function Physics:get_fixed_rotation()
	return _get_fixed_rotation(self.handle)
end

return Physics
