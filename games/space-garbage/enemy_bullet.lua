local Object = require("classic")

local EnemyBullet = Object:extend()

local FORCE = 400
local LIFETIME = 2.5

local count = 0

function EnemyBullet:new(x, y, angle)
	self.image = "laserBlue01"
	self.entity_id = "enemy_bullet" .. count
	count = count + 1
	self.dead = false
	self.lifetime = LIFETIME
	self.travel_angle = angle
	self.dx = math.sin(angle)
	self.dy = -math.cos(angle)
	self.category = "enemy_bullet"

	local info = G.assets.sprite_info(self.image)
	self.handle = G.physics.add_box(
		x, y, x + info.width, y + info.height, angle, self,
		{
			body_type = "dynamic",
			category = "enemy_bullet",
			collides_with = { "player" },
		}
	)
	G.physics.set_fixed_rotation(self.handle, true)
end

function EnemyBullet:id()
	return self.entity_id
end

function EnemyBullet:update(dt)
	self.lifetime = self.lifetime - dt
	if self.lifetime <= 0 then
		self.dead = true
		return
	end
	G.physics.apply_force(self.handle, self.dx * FORCE, self.dy * FORCE)
end

function EnemyBullet:draw()
	local x, y = G.physics.position(self.handle)
	G.graphics.draw_sprite(self.image, x, y, self.travel_angle)
end

function EnemyBullet:on_collision(other)
	if not other then return end
	self.dead = true
end

function EnemyBullet:is_player()
	return false
end

function EnemyBullet:is_enemy()
	return false
end

function EnemyBullet:destroy()
	if self.handle and not self.destroyed then
		G.physics.destroy_handle(self.handle)
		self.destroyed = true
	end
end

return EnemyBullet
