local Entity = require("entity")

local Meteor = Entity:extend()

local count = 0

local SPRITES = {
	big = { "meteorBrown_big1", "meteorBrown_big2", "meteorBrown_big3", "meteorBrown_big4" },
	med = { "meteorBrown_med1", "meteorBrown_med3" },
	small = { "meteorBrown_small1", "meteorBrown_small2" },
}

local GREY_SPRITES = {
	big = { "meteorGrey_big1", "meteorGrey_big2", "meteorGrey_big3", "meteorGrey_big4" },
	med = { "meteorGrey_med1", "meteorGrey_med2" },
	small = { "meteorGrey_small1", "meteorGrey_small2" },
}

function Meteor:new(x, y, size, grey)
	size = size or "big"
	self.size = size
	self.dead = false
	self.split_pending = false
	self.flash_timer = 0
	local sprites = grey and GREY_SPRITES[size] or SPRITES[size]
	local sprite = sprites[math.random(#sprites)]
	local id = "meteor" .. count
	count = count + 1
	Meteor.super.new(self, x, y, 0, sprite, id)
end

function Meteor:update(dt)
	self.physics:rotate(0.2)
	if self.flash_timer > 0 then
		self.flash_timer = self.flash_timer - dt
	end
end

function Meteor:draw()
	if self.flash_timer > 0 then
		G.graphics.set_color(255, 80, 80, 255)
	end
	local v = self.physics:position()
	local angle = self.physics:angle()
	G.graphics.draw_sprite(self.image, v.x, v.y, angle)
	if self.flash_timer > 0 then
		G.graphics.set_color("white")
	end
end

function Meteor:on_collision(other)
	if other and other.is_bullet and other:is_bullet() then
		self.dead = true
		self.flash_timer = 0.1
		if self.size == "big" or self.size == "med" then
			self.split_pending = true
		end
	end
end

function Meteor:is_meteor()
	return true
end

function Meteor:get_split_children(rng)
	if not self.split_pending then return {} end
	local v = self.physics:position()
	local children = {}
	local child_size = self.size == "big" and "med" or "small"
	local num_children = self.size == "big" and G.random.sample(rng, 2, 3) or 2
	for i = 1, num_children do
		local angle = (i / num_children) * math.pi * 2
		local offset = 30
		local cx = v.x + math.cos(angle) * offset
		local cy = v.y + math.sin(angle) * offset
		children[#children + 1] = { x = cx, y = cy, size = child_size, angle = angle }
	end
	return children
end

return Meteor
