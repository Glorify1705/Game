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
        entity:update(dt)
    end
end

function Entities:draw()
    for _, entity in pairs(self.entities) do
        entity:draw()
    end
end

function Game:init()
    self.entities = Entities()
    self.timer = Timer()
    self.player = Player(100, 100)
    self.entities:add(self.player)
    self.entities:add(Meteor(300, 300))
    self.entities:add(Meteor(600, 600))
    self.fullscreen = false
    Entities:on_collision(function(a, b)
        if a then
            a:on_collision(b)
        end
        if b then
            b:on_collision(a)
        end
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
        if not self.fullscreen then
            G.window.set_fullscreen()
            self.fullscreen = true
        else
            G.window.set_windowed()
            self.fullscreen = false
        end
    end
    if G.input.is_mouse_pressed(0) then
        G.sound.play_sfx("laser.wav")
    end
    self.entities:update(dt)
end

function Game:draw()
    self.entities:draw()
    local mx, my = G.input.mouse_position()
    G.graphics.attach_shader("pixelate.frag")
    G.graphics.send_uniform("pixels", 4096)
    G.graphics.set_color(255, 0, 0, 255)
    G.graphics.draw_rect(300 * self.player:get_health() / 100, 10, 300, 20)
    G.graphics.set_color(0, 255, 0, 255)
    G.graphics.draw_rect(10, 10, 300, 20)
    G.graphics.set_color(255, 255, 255, 255)
    G.graphics.draw_sprite("numeralX", mx, my)
end

return Game
