(local Game {})

(fn Game.init []
  (when (G.input.is_key_pressed :q) (G.system.quit))
  (each [(_ f) (ipairs (G.filesystem.list_directory :/app))]
    (print "File:" f))
  (let [(data err) (G.filesystem.slurp :/app/file.txt)]
    (if data (tset Game :contents data) (print err))))

(fn Game.update [t dt])

(fn Game.draw [self]
  (when Game.contents
    (G.graphics.draw_text :ponderosa.ttf 24 Game.contents 200 200))
  (G.graphics.draw_text :ponderosa.ttf 24
                        (.. "Hash: " (tostring (G.data.hash Game.contents))) 300
                        300))

Game
