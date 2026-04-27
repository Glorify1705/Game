local Game = require("game")
local HS = require("highscores")

G.window.set_title("Space Garbage!")

G.scene.register("menu", Game.Menu)
G.scene.register("playing", Game.Playing)
G.scene.register("name_entry", HS.NameEntry)
G.scene.register("high_scores", HS.HighScoresView)

G.scene.switch("menu")

-- The engine requires _Game to have init/update/draw. Once scenes are active,
-- callbacks route to the active scene instead.
local Stub = {}
function Stub:init() end
function Stub:update() end
function Stub:draw() end
return Stub
