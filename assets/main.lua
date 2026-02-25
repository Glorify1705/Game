local game_args = {}
local found = false

for index, arg in ipairs(G.system.cli_arguments()) do
	if found then
		table.insert(game_args, arg)
	elseif arg == "--" then
		found = true
	end
end

local module = game_args[1] or "testgame1"

G.window.set_title("Now running " .. module)

return require(module)
