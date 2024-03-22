(local Game {})

(fn Game.init []
  (let [exists (G.filesystem.exists :/app/file.txt)]
    (tset Game :exists exists))
  (let [exists2 (G.filesystem.exists :/app/this-file-does-not-exist.txt)]
    (tset Game :exists2 exists2))
  (each [(_ f) (ipairs (G.filesystem.list_directory :/app))]
    (print "File:" f))
  (let [(data err) (G.filesystem.slurp :/app/file.txt)]
    (if data (tset Game :contents data) (print err))))

(fn Game.update [t dt]
  (when (G.input.is_key_pressed :q) (G.system.quit)))

(fn Game.draw [self]
  (when Game.contents
    (G.graphics.draw_text :ponderosa.ttf 24
                          (.. "Contents: " Game.contents "\nExists: "
                              (if Game.exists :Yes! :NO!) "\nThe other: "
                              (if Game.exists2 :Yes! :NO!) "\nHash: "
                              (G.data.hash Game.contents))
                          100 100)))

Game
