local Player = require "player"
local player

local stars = {}

function Init()
    local vx, vy = G.renderer.viewport()
    player = Player(100, 100)
    stars = {}
    for i = 0, 10 do
        table.insert(stars, { x = math.random() * vx, y = math.random() * vy })
    end
    G.sound.set_volume(0.1)
    G.sound.play("music.ogg")
    print("Playing music")
end

function Update(t, dt)
    if G.input.is_key_pressed('q') then
        G.quit()
    end
    player:update(t)
end

function Render()
    for _, pos in ipairs(stars) do
        G.renderer.draw_sprite("star1", pos.x, pos.y)
    end
    player:render()
    local mx, my = G.input.mouse_position()
    G.renderer.draw_sprite("numeralX", mx, my)
    G.renderer.draw_text("Hello world!", 200, 200)
end
