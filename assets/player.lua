local Entity = require("entity")
local Timer = require("timer")

local FORCE = 50.000
local ANGLE_DELTA = 20
local FIRE_COOLDOWN = 0.2

local Player = Entity:extend()

function Player:new(x, y)
	Player.super.new(self, x, y, 0, "playerShip1_green", "player")
	self.health = 100
	self.timer = Timer()
	self.cooldown = { v = 0, color = { 255, 255, 255, 255 } }
	self.fire_timer = 0
	self.spawn_bullet = nil
end

function Player:set_spawn_callback(fn)
	self.spawn_bullet = fn
end

function Player:update(dt)
	self.timer:update(dt)
	self.fire_timer = math.max(0, self.fire_timer - dt)

	if G.input.is_key_down("w") then
		self.physics:apply_force(0, -FORCE)
	elseif G.input.is_key_down("s") then
		self.physics:apply_force(0, FORCE)
	end

	if G.input.is_key_down("d") then
		self.physics:apply_torque(ANGLE_DELTA)
	elseif G.input.is_key_down("a") then
		self.physics:apply_torque(-ANGLE_DELTA)
	end

	if (G.input.is_key_pressed("space") or G.input.is_mouse_pressed(0)) and self.fire_timer <= 0 then
		self:shoot()
		self.fire_timer = FIRE_COOLDOWN
	end
end

function Player:shoot()
	if not self.spawn_bullet then return end
	local v = self.physics:position()
	local angle = self.physics:angle()
	local nose_dist = 40
	local bx = v.x + math.sin(angle) * nose_dist
	local by = v.y - math.cos(angle) * nose_dist
	G.sound.play_effect("laser.wav")
	self.spawn_bullet(bx, by, angle)
end

function Player:on_collision(other)
	if self.cooldown.v < 1e-8 then
		self.health = self.health - 10
		self.cooldown.v = 1
		self.cooldown.color = { 255, 0, 0, 255 }
		self.timer:tween(5, self.cooldown, { v = 0, color = { 255, 255, 255, 255 } }, "in-out-quad")
	end
end

function Player:draw()
	local v = self.physics:position()
	local angle = self.physics:angle()
	if self.cooldown.v > 0 then
		G.graphics.set_color(unpack(self.cooldown.color))
	end
	G.graphics.draw_sprite(self.image, v.x, v.y, angle)
	G.graphics.set_color(255, 255, 255, 255)
end

function Player:is_player()
	return true
end

function Player:get_health()
	return self.health
end

function Player:center_camera()
	local vx, vy = G.window.dimensions()
	local v = self.physics:position()
	local angle = self.physics:angle()
	G.graphics.translate(-v.x, -v.y)
	local mx, my = G.input.mouse_wheel()
	local factor = 0.4 + my * 0.9
	G.graphics.scale(factor, factor)
	G.graphics.rotate(-angle)
	G.graphics.translate(v.x, v.y)
	G.graphics.translate(vx / 2 - v.x, vy / 2 - v.y)
end

return Player
