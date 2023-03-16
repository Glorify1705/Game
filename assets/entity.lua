Object = require 'classic'
Physics = require 'physics'

Entity = Object:extend()

function Entity:new(x, y, angle, image, id)
    self.x = x
    self.y = y
    self.image = image
    local info = G.assets.subtexture_info(self.image)
    self.physics = Physics(x, y, x + info.width, y + info.height, angle, id)
end

function Entity:render()
    local v = self.physics:position()
    local angle = self.physics:angle()
    G.renderer.draw_sprite(self.image, v.x, v.y, angle)
end

function Entity:update(dt)
end

return Entity
