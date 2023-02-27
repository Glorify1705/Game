Object = require "classic"
Vec2 = require 'vector2d'

Physics = Object:extend()

function Physics:new(tx, ty, bx, by, angle)
    self.handle = G.physics.add_box(tx, ty, bx, by, angle)
end

function Physics:position()
    return Vec2(G.physics.position(self.handle))
end

function Physics:angle()
    return G.physics.angle(self.handle)
end

function Physics:move(x, y)
    G.physics.apply_linear_velocity(self.handle, x, y)
end

function Physics:rotate(angle)
    G.physics.rotate(self.handle, angle)
end

return Physics
