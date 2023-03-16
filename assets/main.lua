local Player = require "player"
local Meteor = require "meteor"

local Game = {}

function Game:init()
    self.entities = {}
    self.score = 0
    table.insert(self.entities, Player(100, 100))
    table.insert(self.entities, Meteor(300, 300))
    table.insert(self.entities, Meteor(600, 600))
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
    for _, entity in ipairs(self.entities) do
        entity:update(dt)
    end
end

function Game:render()
    for _, entity in ipairs(self.entities) do
        entity:render()
    end
    local mx, my = G.input.mouse_position()
    G.renderer.draw_sprite("numeralX", mx, my)
    G.renderer.draw_text("Score: " .. self.score, 0, 0)
end

return Game
