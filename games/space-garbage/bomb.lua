local Object = require("classic")

local Bomb = Object:extend()

local TRAVEL_SPEED = 200
local COUNTDOWN = 2.0
local BLAST_RADIUS = 150
local BLAST_EXPAND_TIME = 0.5
local BLAST_FADE_TIME = 0.3
local ARRIVAL_DIST = 20

local count = 0

function Bomb:new(x, y, target_x, target_y, get_player)
	self.image = "meteorGrey_tiny1"
	self.entity_id = "bomb" .. count
	count = count + 1
	self.dead = false
	self.category = "bomb"
	self.get_player = get_player

	self.x = x
	self.y = y
	self.target_x = target_x
	self.target_y = target_y
	self.phase = "travel"
	self.countdown = COUNTDOWN
	self.blast_timer = 0
	self.blast_radius = 0
	self.visible = true
	self.blink_timer = 0

	local info = G.assets.sprite_info(self.image)
	self.handle = G.physics.add_box(
		x, y, x + info.width, y + info.height, 0, self,
		{
			body_type = "kinematic",
			sensor = true,
			category = "bomb",
			collides_with = {},
		}
	)

	local dx = target_x - x
	local dy = target_y - y
	local dist = math.sqrt(dx * dx + dy * dy)
	if dist > 0.001 then
		G.physics.set_linear_velocity(self.handle, dx / dist * TRAVEL_SPEED, dy / dist * TRAVEL_SPEED)
	end
end

function Bomb:id()
	return self.entity_id
end

function Bomb:update(dt)
	local x, y = G.physics.position(self.handle)
	self.x = x
	self.y = y

	if self.phase == "travel" then
		local dx = self.target_x - x
		local dy = self.target_y - y
		if dx * dx + dy * dy < ARRIVAL_DIST * ARRIVAL_DIST then
			G.physics.set_linear_velocity(self.handle, 0, 0)
			self.phase = "countdown"
		end
	elseif self.phase == "countdown" then
		self.countdown = self.countdown - dt
		-- Blink faster as countdown progresses.
		local rate = 0.3 * (self.countdown / COUNTDOWN) + 0.05
		self.blink_timer = self.blink_timer + dt
		if self.blink_timer >= rate then
			self.blink_timer = self.blink_timer - rate
			self.visible = not self.visible
		end
		if self.countdown <= 0 then
			self.phase = "blast"
			self.blast_timer = 0
			self.visible = false
			G.sound.play_effect("meteor-explosion.wav")
		end
	elseif self.phase == "blast" then
		self.blast_timer = self.blast_timer + dt
		if self.blast_timer < BLAST_EXPAND_TIME then
			self.blast_radius = BLAST_RADIUS * (self.blast_timer / BLAST_EXPAND_TIME)
		else
			self.blast_radius = BLAST_RADIUS
		end
		-- Check if player is in blast zone.
		local player = self.get_player()
		if player and not player.invincible then
			local pv = player.physics:position()
			local pdx = pv.x - x
			local pdy = pv.y - y
			if pdx * pdx + pdy * pdy < self.blast_radius * self.blast_radius then
				player.health = 0
				if player.on_damage then
					player:on_damage()
				end
			end
		end
		if self.blast_timer >= BLAST_EXPAND_TIME + BLAST_FADE_TIME then
			self.dead = true
		end
	end
end

function Bomb:draw()
	if self.phase == "blast" then
		local alpha = 255
		if self.blast_timer > BLAST_EXPAND_TIME then
			alpha = math.floor(255 * (1 - (self.blast_timer - BLAST_EXPAND_TIME) / BLAST_FADE_TIME))
		end
		G.graphics.set_color(255, 120, 30, alpha)
		G.graphics.draw_circle(self.x, self.y, self.blast_radius)
		G.graphics.set_color(255, 200, 50, math.floor(alpha * 0.6))
		G.graphics.draw_circle(self.x, self.y, self.blast_radius * 0.5)
		G.graphics.set_color("white")
		return
	end
	if not self.visible then return end
	if self.phase == "countdown" then
		G.graphics.set_color(255, 80, 80, 255)
	end
	G.graphics.draw_sprite(self.image, self.x, self.y)
	if self.phase == "countdown" then
		-- Draw warning ring.
		G.graphics.set_color(255, 60, 30, 100)
		G.graphics.draw_circle_outline(self.x, self.y, BLAST_RADIUS)
		G.graphics.set_color("white")
	end
end

function Bomb:on_collision(other) end

function Bomb:is_player()
	return false
end

function Bomb:is_enemy()
	return false
end

function Bomb:is_bomb()
	return true
end

function Bomb:destroy()
	if self.handle and not self.destroyed then
		G.physics.destroy_handle(self.handle)
		self.destroyed = true
	end
end

return Bomb
