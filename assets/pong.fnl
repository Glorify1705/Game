(local Game {:left-score 0
             :right-score 0
             :timer 0
             :width 0
             :height 0
             :player-width 20
             :player-height 150
             :ball-radius 10})

(fn Game.init [g]
  (G.window.set_title :Pong!)
  (tset g :timer 60)
  (let [(w h) (G.window.dimensions)]
    (tset g :width w)
    (tset g :height h)
    (tset g :left-player {:x 10 :y (/ h 2)})
    (tset g :right-player {:x (- w 30) :y (/ h 2)})
    (tset g :ball {:x (/ w 2) :y (/ h 2)})))

(local *font-name* :terminus.ttf)
(local *font-size* 24)

(fn Game.update [g t dt]
  (when (or (G.input.is_key_pressed :q) (G.input.is_key_pressed :esc))
    (G.system.quit))
  (let [(w h) (G.window.dimensions)]
    (tset g :width w)
    (tset g :height h)))

(fn Game.draw [g]
  (let [{:width w :height h} g]
    (let [{:left-score ls :right-score rs} g]
      (G.graphics.draw_text *font-name* *font-size* (.. "Player 1: " ls) 20 24)
      (G.graphics.draw_text *font-name* *font-size* (.. "Player 2: " rs)
                            (- w 160) 20)))
  (let [{:player-width pw :player-height ph :ball-radius br} g]
    (let [{: x : y} (. g :left-player)]
      (G.graphics.draw_rect x y (+ x pw) (+ y ph)))
    (let [{: x : y} (. g :right-player)]
      (G.graphics.draw_rect x y (+ x pw) (+ y ph)))
    (let [{: x : y} (. g :ball)]
      (G.graphics.draw_circle x y br))))

Game
