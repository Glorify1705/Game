Object = require("classic")
Physics = require("physics")

Entity = Object:extend()

function Entity:new(x, y, angle, image, id)
	self.x = x
	self.y = y
	self.image = image
	self.entity_id = id
	local info = G.assets.sprite_info(self.image)
	self.physics = Physics(x, y, x + info.width, y + info.height, angle, self)
end

function Entity:draw()
	local v = self.physics:position()
	local angle = self.physics:angle()
	G.graphics.draw_sprite(self.image, v.x, v.y, angle)
end

function Entity:id()
	return self.entity_id
end

function Entity:update(dt) end

function Entity:is_player()
	return false
end

function Entity:on_collision(other) end

return Entity
