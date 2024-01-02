Object = require 'classic'

Random = Object:extend()

function Random:new()
    self.rnd = G.random.from_seed(123)
end

function Random:next()
    return G.random.random(self.rnd)
end

return Random
