local Game = {}

function Game:init()
	print("Initialization!")
	G.msg = "Tu vieja en tanga"
end

function Game:update()
	if G.input.is_key_down("q") then
		G.system.quit()
	end
end

function Game:draw()
	G.graphics.clear()
	G.graphics.set_color("red")
	G.graphics.draw_text("ponderosa.ttf", 32, G.msg, 100, 100)
end

return Game
