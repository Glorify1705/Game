Object = require 'classic'
Physics = require 'physics'

Entity = Object:extend()

function Entity:new(x, y, angle, image, id)
    self.x = x
    self.y = y
    self.image = image
    self.entity_id = id
    local info = G.assets.subtexture_info(self.image)
    self.physics = Physics(x, y, x + info.width, y + info.height, angle, self)
end

function Entity:draw()
    local v = self.physics:position()
    local angle = self.physics:angle()
    G.renderer.draw_sprite(self.image, v.x, v.y, angle)
end

function Entity:id()
    return self.entity_id
end

function Entity:update(dt)
end

return Entity
