local Player = require "player"
local Meteor = require "meteor"
local Timer = require "timer"

local Game = {}

local Entities = Object:extend()

function Entities:new()
    self.entities = {}
end

function Entities:on_collision(fn)
    G.physics.set_collision_callback(fn)
end

function Entities:add(entity)
    self.entities[entity:id()] = entity
end

function Entities:update(dt)
    for _, entity in pairs(self.entities) do
        entity:update()
    end
end

function Entities:draw()
    for _, entity in pairs(self.entities) do
        entity:draw()
    end
end

function Game:init()
    self.entities = Entities()
    self.score = 0
    self.timer = Timer()
    self.entities:add(Player(100, 100))
    self.entities:add(Meteor(300, 300))
    self.entities:add(Meteor(600, 600))
    Entities:on_collision(function(a, b)
        self.score = self.score + 10
    end)
    --    G.sound.set_music_volume(0.1)
    --    G.sound.set_sfx_volume(0.1)
    --    G.sound.play_music("music.ogg")
end

function Game:update(t, dt)
    self.timer:update(dt)
    if G.input.is_key_pressed('q') then
        G.quit()
    end
    if G.input.is_key_pressed('f') then
        G.graphics.set_fullscreen()
    end
    if G.input.is_mouse_pressed(0) then
        G.sound.play_sfx("laser.wav")
    end
    self.entities:update(dt)
end

function Game:draw()
    self.entities:draw()
    local mx, my = G.input.mouse_position()
    G.graphics.draw_sprite("numeralX", mx, my)
    G.graphics.draw_text("Score: " .. self.score, 10, 10)
end

return Game
