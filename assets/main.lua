local game_args = G.system.cli_arguments()
local module = game_args[1] or "testgame1"

G.window.set_title("Now running " .. module)

return require(module)
