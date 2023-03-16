Entity = require 'entity'

Meteor = Entity:extend()

local count = 0

function Meteor:new(x, y)
    Meteor.super.new(self, x, y, 0, "meteorBrown_big1", "meteor" .. count)
    count = count + 1
end

function Meteor:update(dt)
    self.physics:rotate(0.2)
end

return Meteor
