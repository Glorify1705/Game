local Player = require "player"
local Meteor = require "meteor"

local Game = {}

local Entities = Object:extend()

function Entities:new()
    self.entities = {}
end

function Entities:on_collision(fn)
    G.physics.set_collision_callback(function(a, b)
        print("Collision between", a:id(), "and", b:id())
        fn(a, b)
    end)
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
    self.entities:add(Player(100, 100))
    self.entities:add(Meteor(300, 300))
    self.entities:add(Meteor(600, 600))
    Entities:on_collision(function(a, b)
        self.score = self.score + 10
    end)
    G.sound.set_music_volume(0.1)
    G.sound.set_sfx_volume(0.1)
    G.sound.play_music("music.ogg")
end

function Game:update(t, dt)
    if G.input.is_key_pressed('q') then
        G.quit()
    end
    if G.input.is_mouse_pressed(0) then
        G.sound.play_sfx("laser.wav")
    end
    self.entities:update(dt)
end

function Game:draw()
    G.renderer.set_color(1, 1, 1, 1)
    self.entities:draw()
    local mx, my = G.input.mouse_position()
    G.renderer.draw_sprite("numeralX", mx, my)
    G.renderer.draw_text("Score: " .. self.score, 10, 10)
    G.renderer.set_color(1, 0, 1, 1)
    G.renderer.draw_circle(100, 100, 50)
end

return Game
