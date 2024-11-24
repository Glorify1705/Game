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
    (tset g :ball {:x (/ w 2) :y (/ h 2) :speed {:x 400 :y 400}})))

(local *font-name* :terminus.ttf)
(local *font-size* 24)

(fn Game.update [g t dt]
  (when (or (G.input.is_key_pressed :q) (G.input.is_key_pressed :esc))
    (G.system.quit))
  (let [(w h) (G.window.dimensions)]
    (let [{:ball-radius b :ball {: x : y :speed {:x dx :y dy}}} g]
      (let [nx (+ x (* dt dx))
            ny (+ y (* dt dy))]
        (if (>= nx b)
            (do
              (tset g :ball :x nx))
            (do
              (tset g :ball :x b)
              (tset g :ball :speed :x (- (-> g (. :ball) (. :speed) (. :x))))))
        (if (<= nx (- w b))
            (do
              (tset g :ball :x nx))
            (do
              (tset g :ball :x (- w b))
              (tset g :ball :speed :x (- (-> g (. :ball) (. :speed) (. :x))))))
        (if (>= ny b)
            (do
              (tset g :ball :y ny))
            (do
              (tset g :ball :y b)
              (tset g :ball :speed :y (- (-> g (. :ball) (. :speed) (. :y))))))
        (if (<= ny (- h b))
            (do
              (tset g :ball :y ny))
            (do
              (tset g :ball :y (- h b))
              (tset g :ball :speed :y (- (-> g (. :ball) (. :speed) (. :y))))))))
    (tset g :width w)
    (tset g :height h)
    (when (G.input.is_key_down :w)
      (tset g :left-player :y (G.math.clamp (- (. g :left-player :y) 5) -10 h)))
    (when (G.input.is_key_down :s)
      (tset g :left-player :y
            (G.math.clamp (+ (. g :left-player :y) 5) -10 (- h 120))))
    (when (G.input.is_key_down :i)
      (tset g :right-player :y
            (G.math.clamp (- (. g :right-player :y) 5) -10 h)))
    (when (G.input.is_key_down :k)
      (tset g :right-player :y
            (G.math.clamp (+ (. g :right-player :y) 5) -10 (- h 120))))))

(fn Game.draw [g]
  (let [{:width w :height h} g]
    (let [{:left-score ls :right-score rs} g]
      (G.graphics.draw_text *font-name* *font-size* (.. "Player 1: " ls) 40 24)
      (G.graphics.draw_text *font-name* *font-size* (.. "Player 2: " rs)
                            (- w 180) 20))
    (for [i 0 30]
      (let [lx (/ w 2) ly (* 30 i)]
        (G.graphics.draw_rect (- lx 1) ly (+ lx 1) (+ ly 20)))))
  (let [{:player-width pw :player-height ph :ball-radius br} g]
    (let [{: x : y} (. g :left-player)]
      (G.graphics.draw_rect x y (+ x pw) (+ y ph)))
    (let [{: x : y} (. g :right-player)]
      (G.graphics.draw_rect x y (+ x pw) (+ y ph)))
    (let [{: x : y} (. g :ball)]
      (G.graphics.draw_circle x y br))))

Game
