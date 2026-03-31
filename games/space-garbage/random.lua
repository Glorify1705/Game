local Object = require("classic")

local Random = Object:extend()

function Random:new(...)
	local n = select("#", ...)
	if n == 0 then
		self.rnd = G.random.non_deterministic()
	else
		local seed = select(1, ...)
		self.rnd = G.random.from_seed(seed)
	end
end

function Random:non_deterministic()
	self.rnd = G.random.non_deterministic()
end

function Random:next()
	return G.random.sample(self.rnd)
end

return Random
