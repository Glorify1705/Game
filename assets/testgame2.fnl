(local Player (require :player))

(local Game {:score 0 :rectangles []})

(fn Game.init [self]
  (tset self :rectangles [{:x 100 :y 100 :w 100 :h 100}])
  (G.window.set_title "Shoot clicker!"))

(fn Game.update [self t dt]
  (when (G.input.is_key_pressed :q) (G.quit)))

(fn Game.draw [self]
  (G.graphics.draw_text :ponderosa.ttf (.. "Score: " (. self :score)) 0 0)
  (let [(mx my) (G.input.mouse_position)]
    (G.graphics.set_color :poisongreen)
    (each [_ {: x : y : w : h} (pairs (. self :rectangles))]
      (G.graphics.draw_rect x y (+ x w) (+ y h))))
  (G.graphics.set_color :indianred)
  (G.graphics.draw_circle mx my 10))

Game
