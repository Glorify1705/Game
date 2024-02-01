local Player = require "player"
local Meteor = require "meteor"
local Timer = require "timer"
local Random = require "random"

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

local G1 = {}

function G1:init()
    G.window.set_title("My awesome Lua game!")
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
    G.sound.set_music_volume(0.2)
    G.sound.set_sfx_volume(0.1)
    G.sound.play_music("music.ogg")
    self.rnd = Random()
end

function G1:update(t, dt)
    if not G.window.has_input_focus() then
        return
    end
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
    if G.input.is_key_pressed('m') then
        G.sound.play_music("music.ogg")
    end
    if G.input.is_key_pressed('c') then
        print("Clipboard says: " .. G.system.get_clipboard())
    end
    self.entities:update(dt)
end

function G1:draw()
    self.entities:draw()
    local mx, my = G.input.mouse_position()
    G.graphics.set_color("neonred")
    G.graphics.draw_rect(300 * self.player:get_health() / 100, 10, 300, 20)
    G.graphics.set_color("freshgreen")
    G.graphics.draw_rect(10, 10, 300, 20)
    G.graphics.set_color("white")
    G.graphics.draw_sprite("numeralX", mx, my)
end

local G2 = {}

function G2:init()
end

function G2:update(t, dt)
end

function G2:draw()
end

local Game = { g = G1 }

function Game:init()
    self.g:init()
end

function Game:update(t, dt)
    if G.input.is_key_pressed('n') then
        print("Using Game G1")
        if self.g ~= G1 then
            self.g = G1
            self.g:init()
        end
    elseif G.input.is_key_pressed('m') then
        print("Using Game G2")
        if self.g ~= G2 then
            self.g = G2
            self.g:init()
        end
    end
    self.g:update(t, dt)
end

function Game:draw()
    self.g:draw()
end

return Game
