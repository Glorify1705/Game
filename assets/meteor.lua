Object = require "classic"
Vec2 = require 'vector2d'
Physics = require 'physics'

Meteor = Object:extend()

function Meteor:new(x, y)
    self.image = "meteorBrown_big1"
    self.angle = 0
    local info = G.assets.subtexture_info(self.image)
    self.physics = Physics(x, y, x + info.width, y + info.height, self.angle)
end

function Meteor:update(dt)
    self.physics:rotate(0.2)
end

function Meteor:render()
    local v = self.physics:position()
    local angle = self.physics:angle()
    G.renderer.draw_sprite(self.image, v.x, v.y, angle)
end

return Meteor
