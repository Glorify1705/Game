local game_args = {}
local found = false

for index, arg in ipairs(G.system.cli_arguments()) do
	if found then
		table.insert(game_args, arg)
	elseif arg == "--" then
		found = true
	end
end

if #game_args > 0 then
	return require(game_args[1])
else
	return require("pong")
end
