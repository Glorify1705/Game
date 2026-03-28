-- Minimal sound playback test.

local Game = {}

local sound = "music.ogg"

function Game:init()
	local source = G.sound.add_source(sound)
	G.sound.set_volume(source, 0.21)
	G.sound.play_source(source)
end

function Game:update(t, dt)
	if G.input.is_key_pressed("q") then
		G.system.quit()
	end
end

function Game:draw()
	G.graphics.clear()
	G.graphics.set_color("white")
	G.graphics.draw_text("ponderosa.ttf", 32, "Now playing " .. sound, 100, 100)
end

return Game
