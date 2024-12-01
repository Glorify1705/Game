(local Game {:left-score 0
             :right-score 0
             :timer 0
             :width 0
             :height 0
             :player-width 20
             :state :serve
             :player-height 150
             :max-score 3
             :ball-radius 10})

(lambda Game.init [g]
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

(fn dist2 [ax ay bx by]
  (let [dx (- bx ax)
        dy (- by ay)]
    (+ (* dx dx) (* dy dy))))

(fn collides-with-pad? [sx sy ex ey bx by br]
  (let [cx (G.math.clamp bx sx ex)
        cy (G.math.clamp by sy ey)]
    (<= (dist2 cx cy bx by) (* br br))))

(fn Game.update [g t dt]
  (when (or (G.input.is_key_pressed :q) (G.input.is_key_pressed :esc))
    (G.system.quit))
  (let [(w h) (G.window.dimensions)]
    (tset g :width w)
    (tset g :height h))
  (when (= (. g :state) :serve)
    (let [{: max-score} g]
      (if (or (>= (. g :left-score) max-score)
              (>= (. g :right-score) max-score))
          (tset g :state :finished)
          (when (G.input.is_key_pressed :space)
            (tset g :ball :speed {:x 400 :y 400})
            (tset g :state :running)))))
  (when (= (. g :state) :running)
    (let [{:width w
           :height h
           :ball-radius br
           :ball {: x : y :speed {:x dx :y dy}}} g]
      (let [nx (+ x (* dt dx))
            ny (+ y (* dt dy))]
        (let [{:player-width pw :player-height ph} g]
          (let [{:x px :y py} (. g :left-player)]
            (when (collides-with-pad? px py (+ px pw) (+ py ph) nx ny br)
              (tset g :ball :speed :x (- (-> g (. :ball) (. :speed) (. :x))))))
          (let [{:x px :y py} (. g :right-player)]
            (when (collides-with-pad? px py (+ px pw) (+ py ph) nx ny br)
              (tset g :ball :speed :x (- (-> g (. :ball) (. :speed) (. :x)))))))
        (when (= (. g :state) :running)
          (if (>= nx (/ br 2))
              (do
                (tset g :ball :x nx))
              (do
                (tset g :right-score (+ 1 (. g :right-score)))
                (tset g :ball {:x (/ w 2) :y (/ h 2) :speed {:x 0 :y 0}})
                (tset g :state :serve))))
        (when (= (. g :state) :running)
          (if (<= nx (- w (/ br 2)))
              (do
                (tset g :ball :x nx))
              (do
                (tset g :left-score (+ 1 (. g :left-score)))
                (tset g :ball {:x (/ w 2) :y (/ h 2) :speed {:x 0 :y 0}})
                (tset g :state :serve))))
        (when (= (. g :state) :running)
          (if (>= ny br)
              (do
                (tset g :ball :y ny))
              (do
                (tset g :ball :y br)
                (tset g :ball :speed :y (- (-> g (. :ball) (. :speed) (. :y)))))))
        (when (= (. g :state) :running)
          (if (<= ny (- h br))
              (do
                (tset g :ball :y ny))
              (do
                (tset g :ball :y (- h br))
                (tset g :ball :speed :y (- (-> g (. :ball) (. :speed) (. :y))))))))))
  (when (let [s (. g :state)] (or (= s :running) (= s :serve)))
    (let [{:width w :height h} g]
      (when (G.input.is_key_down :w)
        (tset g :left-player :y
              (G.math.clamp (- (. g :left-player :y) 5) -10 h)))
      (when (G.input.is_key_down :s)
        (tset g :left-player :y
              (G.math.clamp (+ (. g :left-player :y) 5) -10 (- h 120))))
      (when (G.input.is_key_down :i)
        (tset g :right-player :y
              (G.math.clamp (- (. g :right-player :y) 5) -10 h)))
      (when (G.input.is_key_down :k)
        (tset g :right-player :y
              (G.math.clamp (+ (. g :right-player :y) 5) -10 (- h 120)))))))

(fn draw-text! [text x y]
  (G.graphics.draw_text *font-name* *font-size* text x y))

(fn Game.draw [g]
  (let [{:width w :height h} g]
    (if (= (. g :state) :finished)
        (let [winner-text (if (> (. g :left-score) (. g :right-score))
                              "Player 1 wins"
                              "Player 2 wins")]
          (let [(tx ty) (G.graphics.text_dimensions *font-name* *font-size*
                                                    winner-text)]
            (G.graphics.draw_text *font-name* *font-size* winner-text
                                  (- (/ w 2) (/ tx 2)) (/ h 2))))
        (do
          (let [{:left-score ls :right-score rs} g]
            (G.graphics.draw_text *font-name* *font-size* (.. "Player 1: " ls)
                                  40 24)
            (G.graphics.draw_text *font-name* *font-size* (.. "Player 2: " rs)
                                  (- w 180) 20))
          (for [i 0 30]
            (let [lx (/ w 2)
                  ly (* 30 i)]
              (G.graphics.draw_rect (- lx 1) ly (+ lx 1) (+ ly 20))))
          (let [{:player-width pw :player-height ph :ball-radius br} g]
            (let [{: x : y} (. g :left-player)]
              (G.graphics.draw_rect x y (+ x pw) (+ y ph)))
            (let [{: x : y} (. g :right-player)]
              (G.graphics.draw_rect x y (+ x pw) (+ y ph)))
            (let [{: x : y} (. g :ball)]
              (G.graphics.draw_circle x y br)))))))

Game
