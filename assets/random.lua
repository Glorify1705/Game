Object = require 'classic'

Random = Object:extend()

function Random:new(...)
    if #arg == 0 then
        self.rnd = G.random.non_deterministic()
    else
        self.rnd = G.random.from_seed(arg[1])
    end
end

function Random.non_deterministic()
    self.rnd = G.random.non_deterministic()
end

function Random:next()
    return G.random.sample(self.rnd)
end

return Random
