(local Game {})

(fn Game.init [self]
  (tset self :t 0))

(fn Game.update [self t dt]
  (when (G.input.is_key_pressed :q) (G.system.quit))
  (when (G.input.is_key_pressed :r) (G.hotload))
  (tset self :t t))

(fn Game.draw [self]
  (G.graphics.clear)
  (G.graphics.set_color :green)
  (G.graphics.attach_shader :testshader.frag)
  (G.graphics.send_uniform :iTime self.t)
  (let [(w h) (G.window.dimensions)]
    (G.graphics.send_uniform :iResolution (G.math.v2 w h))
    (G.graphics.set_color :white)
    (G.graphics.draw_rect 0 0 w h)))

Game
