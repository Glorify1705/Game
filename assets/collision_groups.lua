-- Collision filter categories for physics bodies.
-- Two bodies collide when (A.category & B.mask) ~= 0 AND (B.category & A.mask) ~= 0.
local C = {
	PLAYER  = 0x0001,
	METEOR  = 0x0002,
	BULLET  = 0x0004,
	POWERUP = 0x0008,
}

return C
