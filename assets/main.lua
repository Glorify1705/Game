local Player = require "player"
local player, positions

function Init()
    local vx, vy = G.renderer.viewport()
    player = Player(25000, 25000)
    positions = {}
    for i = 0, 50000 do
        table.insert(positions, { x = math.random() * 100000, y = math.random() * 100000 })
    end
    -- G.sound.play("music.ogg")
end

function Update(t, dt)
    player:update(t)
end

function Render()
    G.renderer.push()
    player:center_camera()
    player:render()
    for _, pos in ipairs(positions) do
        G.renderer.draw_sprite("star1", pos.x, pos.y)
    end
    G.renderer.pop()
    local mx, my = G.input.mouse_position()
    G.renderer.draw_sprite("numeralX", mx, my)
end
