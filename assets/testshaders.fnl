(local Game {})

(fn Game.init [self])

(fn Game.update [self t dt]
  (when (G.input.is_key_pressed :q) (G.system.quit))
  (when (G.input.is_key_pressed :r) (G.hotload)))

(fn Game.draw [self]
  (G.graphics.clear)
  (G.graphics.set_color :green)
  (G.graphics.attach_shader :testshader.frag)
  (let [(w h) (G.window.dimensions)]
    (G.graphics.set_color :white)
    (G.graphics.draw_rect 0 0 w h)))

Game
