(local Game {})

(fn Game.init [])

(fn Game.update [t dt]
  (when (G.input.is_key_pressed :q) (G.system.quit)))

(fn Game.draw [self]
  (G.graphics.draw_text :ponderosa.ttf 24 (tostring (G.math.v2 1 2)) 100 100)
  (let [v (G.math.v2 2 3)
        n (v:normalized)]
    (G.graphics.draw_text :ponderosa.ttf 24 (tostring n) 100 200))
  (let [v (G.math.v2 2 3)
        n (v:len2)]
    (G.graphics.draw_text :ponderosa.ttf 24 (tostring n) 100 300))
  (let [v (G.math.v2 2 3)
        w (G.math.v2 2 3)]
    (G.graphics.draw_text :ponderosa.ttf 24 (tostring (v:dot w)) 100 400))
  (let [v (G.math.v2 2 3)
        w (G.math.v2 2 3)]
    (G.graphics.draw_text :ponderosa.ttf 24 (tostring (- v w)) 100 500))
  (let [v (G.math.v2 2 3)
        w (G.math.v2 2 3)]
    (G.graphics.draw_text :ponderosa.ttf 24 (tostring (+ v w)) 100 600))
  (let [v (G.math.v2 2 3)]
    (G.graphics.draw_text :ponderosa.ttf 24 (tostring (* 3 v)) 100 700))
  (let [v (G.math.v2 2 3)]
    (G.graphics.draw_text :ponderosa.ttf 24 (tostring (* v 3)) 100 800)))

Game
