local sound = "music.ogg"
local source = G.sound.add_source(sound)
G.sound.set_volume(source, 0.21)
G.sound.play_source(source)
G.graphics.clear()
G.graphics.set_color("white")
G.graphics.draw_text("ponderosa.ttf", 32, "Now playing " .. sound, 100, 100)
