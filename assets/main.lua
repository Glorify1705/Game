local Player = require "player"
local Meteor = require "meteor"

local player
local stars = {}
local meteors = {}

function Init()
    local vx, vy = G.renderer.viewport()
    player = Player(100, 100)
    stars = {}
    for i = 0, 10 do
        table.insert(stars, { x = math.random() * vx, y = math.random() * vy })
    end
    table.insert(meteors, Meteor(300, 300))
    table.insert(meteors, Meteor(600, 600))
    G.sound.set_volume(0.1)
    G.sound.play("music.ogg")
end

function Update(t, dt)
    if G.input.is_key_pressed('q') then
        G.quit()
    end
    for _, meteor in ipairs(meteors) do
        meteor:update(dt)
    end
    player:update(dt)
end

function Render()
    for _, pos in ipairs(stars) do
        G.renderer.draw_sprite("star1", pos.x, pos.y)
    end
    for _, meteor in ipairs(meteors) do
        meteor:render()
    end
    player:render()
    local mx, my = G.input.mouse_position()
    G.renderer.draw_sprite("numeralX", mx, my)
end
