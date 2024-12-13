(local Game {})

(fn Game.init [])

(fn Game.update [self t dt]
  (when (G.input.is_key_pressed :q) (G.system.quit))
  (tset self t t))

(fn Game.draw1 [self]
  (G.graphics.attach_shader :testshader.frag)
  (G.graphics.send_uniform :iTime self.t)
  (let [(w h) (G.window.dimensions)]
    (G.graphics.send_uniform :iResolution (G.math.v2 w h))))

(fn Game.draw [self] (G.graphics.attach_shader :crt.frag))

Game
