-- Test program for the G.docs() introspection API.

local Game = {}

function Game:init()
end

function Game:update(t, dt)
	if G.input.is_key_pressed("q") then
		G.system.quit()
	end
end

function Game:draw()
	G.graphics.set_color("white")
	G.graphics.print(G.docs(G.graphics.draw_text), 100, 100)
end

return Game
