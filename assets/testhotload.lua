local Game = {}

function Game:init()
	print("Initialization!")
	G.msg = "Dale que onda"
end

function Game:update(t, dt)
	if G.input.is_key_down("q") then
		G.system.quit()
	end
end

function Game:draw()
	G.graphics.clear()
	G.graphics.set_color("green")
	G.graphics.draw_text("ponderosa.ttf", 40, G.msg, 200, 200)
end

return Game
