(local Game {})

(fn Game.init [self]
  (let [(w h) (G.window.dimensions)]
    (set self.w w)
    (set self.h h)
    (set self.player {:x (/ w 2) :y 100 :dy 0 :r 20})
    (set self.pipes [{:x 800 :y 500} {:x 1000 :y 400}])))

(fn Game.update [self t dt]
  (let [(w h) (G.window.dimensions)]
    (set self.w w)
    (set self.h h))
  (when (G.input.is_key_down :q) (G.system.quit))
  (let [{: x : y} self.player]
    (tset self.player :y (+ self.player.y (* self.player.dy dt)))
    (tset self.player :dy (+ self.player.dy 10))
    (when (or (G.input.is_key_pressed :w) (G.input.is_key_pressed :spacebar))
      (tset self.player :dy -250)))
  (each [_ p (ipairs self.pipes)]
    (tset p :x (- p.x (* 50 dt)))))

(fn Game.draw [self]
  (G.graphics.clear)
  (G.graphics.set_color :blue)
  (let [{: x : y : r} self.player]
    (G.graphics.draw_circle x y r))
  (G.graphics.set_color :green)
  (each [_ {: x : y} (pairs self.pipes)]
    (G.graphics.draw_rect (- x 20) 0 (+ x 20) y)
    (G.graphics.draw_rect (- x 20) (+ y 150) (+ x 20) self.h))
  (G.graphics.set_color :white))

Game
