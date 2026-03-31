-- Registers named collision categories for the physics system.
-- Call once during init before creating any physics bodies.
local function register()
	G.physics.set_collision_categories({
		"player",
		"meteor",
		"bullet",
		"powerup",
	})
end

return { register = register }
