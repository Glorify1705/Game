local Player = require "player"
local player

local positions = {}

function Init()
    local vx, vy = G.renderer.viewport()
    player = Player(100, 100)
    positions = {}
    for i = 0, 10 do
        table.insert(positions, { x = math.random() * vx, y = math.random() * vy })
    end
    -- G.sound.play("music.ogg")
end

function Update(t, dt)
    player:update(t)
end

function Render()
    for _, pos in ipairs(positions) do
        G.renderer.draw_sprite("star1", pos.x, pos.y)
    end
    player:render()
    local mx, my = G.input.mouse_position()
    G.renderer.draw_sprite("numeralX", mx, my)
end
