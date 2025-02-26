(local Game {:left-score 0
             :right-score 0
             :width 0
             :height 0
             :player-width 20
             :player-height 150
             :max-score 10
             :colors [:red
                      :green
                      :blue
                      :cyan
                      :yellow
                      :candypink
                      :desert
                      :white]
             :simulation-steps 3
             :random (G.random.non_deterministic)
             :ball-speed 1
             :ball-color :white
             :ball-speed-update 0
             :ball-radius 10})

(lambda Game.init [g]
  (G.window.set_title :Pong!)
  (let [(w h) (G.window.dimensions)]
    (set g.state (G.random.pick (G.random.non_deterministic)
                                [:serve-left :serve-right]))
    (set g.width w)
    (set g.height h)
    (set g.left-player {:x 10 :y (- (/ h 2) (/ g.player-height 2))})
    (set g.right-player {:x (- w 30) :y (- (/ h 2) (/ g.player-height 2))})
    (set g.ball {:x (/ w 2) :y (/ h 2) :speed {:x 400 :y 400}})))

(local *font-name* :terminus.ttf)
(local *font-size* 24)

(fn change-ball-color! [g]
  (set g.ball-color (G.random.pick g.random g.colors)))

(fn Game.update [g t dt]
  (let [bt g.ball-speed-update]
    (set g.ball-speed-update (+ bt dt))
    (when (> g.ball-speed-update 5)
      (set g.ball-speed (+ g.ball-speed 0.1))
      (set g.ball-speed-update 0)))
  (when (or (G.input.is_key_pressed :q) (G.input.is_key_pressed :esc))
    (G.system.quit))
  (let [(w h) (G.window.dimensions)]
    (set g.width w)
    (set g.height h))
  (when (= g.state :serve-left)
    (let [{: max-score} g]
      (if (or (>= g.left-score max-score) (>= g.right-score max-score))
          (do
            (tset g :state :finished)
            (G.sound.play_music :game-over.ogg 0))
          (do
            (set g.ball.x (+ g.left-player.x g.player-width g.ball-radius))
            (set g.ball.y (+ g.left-player.y (/ g.player-height 2)))
            (when (G.input.is_key_pressed :space)
              (set g.ball.speed {:x 400 :y 300})
              (set g.state :running))))))
  (when (= g.state :serve-right)
    (let [{: max-score} g]
      (if (or (>= g.left-score max-score) (>= g.right-score max-score))
          (do
            (tset g :state :finished)
            (G.sound.play_music :game-over.ogg 0))
          (do
            (set g.ball.x (- g.right-player.x g.ball-radius))
            (set g.ball.y (+ g.right-player.y (/ g.player-height 2)))
            (when (G.input.is_key_pressed :space)
              (set g.ball.speed {:x -400 :y 300})
              (set g.state :running))))))
  (var collided false)
  (when (= g.state :running)
    (let [{:width w
           :height h
           :player-width pw
           :player-height ph
           :ball-radius br
           :ball-speed bs
           :simulation-steps steps
           :ball {: x : y :speed {:x dx :y dy}}} g]
      (for [i 0 steps &until collided]
        (let [step (* i (/ dt steps))
              nx (+ x (* step dx bs))
              ny (+ y (* step dy bs))]
          (when (and (not collided) (= g.state :running))
            (let [{:x px :y py} g.left-player]
              (when (and (> (+ ny br) py) (< ny (+ py ph))
                         (< (- nx br) (+ px pw)))
                (G.sound.play_sound :pong-blip1.ogg)
                (change-ball-color! g)
                (set g.ball.speed.x (- 0 g.ball.speed.x))
                (set g.ball.x (+ px pw br 1))
                (set collided true))))
          (when (and (not collided) (= g.state :running))
            (let [{:x px :y py} g.right-player]
              (when (and (> (+ ny br) py) (< ny (+ py ph)) (> (+ nx br) px))
                (G.sound.play_sound :pong-blip1.ogg)
                (change-ball-color! g)
                (set g.ball.x (- px (+ br 1)))
                (set g.ball.speed.x (- g.ball.speed.x))
                (set collided true))))
          (when (and (not collided) (= g.state :running))
            (if (>= nx 0)
                (do
                  (set g.ball.x nx))
                (do
                  (G.sound.play_sound :pong-score.ogg)
                  (change-ball-color! g)
                  (set g.right-score (+ 1 g.right-score))
                  (set g.left-player {:x 10 :y (- (/ h 2) (/ ph 2))})
                  (set g.right-player {:x (- w 30) :y (- (/ h 2) (/ ph 2))})
                  (set g.ball {:x (/ w 2) :y (/ h 2) :speed {:x 0 :y 0}})
                  (set g.state :serve-left))))
          (when (and (not collided) (= g.state :running))
            (if (<= nx w)
                (do
                  (set g.ball.x nx))
                (do
                  (G.sound.play_sound :pong-score.ogg)
                  (change-ball-color! g)
                  (set g.left-score (+ 1 g.left-score))
                  (set g.left-player {:x 10 :y (- (/ h 2) (/ ph 2))})
                  (set g.right-player {:x (- w 30) :y (- (/ h 2) (/ ph 2))})
                  (set g.ball {:x (/ w 2) :y (/ h 2) :speed {:x 0 :y 0}})
                  (set g.state :serve-right))))
          (when (and (not collided) (= g.state :running))
            (if (>= ny br)
                (do
                  (set g.ball.y ny))
                (do
                  (G.sound.play_sound :pong-blip2.ogg)
                  (change-ball-color! g)
                  (set g.ball.y br)
                  (set g.ball.speed.y (- g.ball.speed.y))
                  (set collided true))))
          (when (and (not collided) (= g.state :running))
            (if (<= ny (- h br))
                (do
                  (set g.ball.y ny))
                (do
                  (G.sound.play_sound :pong-blip2.ogg)
                  (change-ball-color! g)
                  (set g.ball.y (- h br))
                  (set g.ball.speed.y (- g.ball.speed.y))
                  (set collided true))))))))
  (when (let [s g.state]
          (or (= s :running) (= s :serve-left) (= s :serve-right)))
    (let [{:width w :height h} g]
      (when (G.input.is_key_down :w)
        (set g.left-player.y (G.math.clamp (- g.left-player.y 5) -10 h)))
      (when (G.input.is_key_down :s)
        (set g.left-player.y (G.math.clamp (+ g.left-player.y 5) -10 (- h 120))))
      (when (G.input.is_key_down :i)
        (set g.right-player.y (G.math.clamp (- g.right-player.y 5) -10 h)))
      (when (G.input.is_key_down :k)
        (set g.right-player.y
             (G.math.clamp (+ g.right-player.y 5) -10 (- h 120)))))))

(fn Game.draw [g]
  (G.graphics.clear)
  (G.graphics.attach_shader :crt.frag)
  (let [{:width w :height h} g]
    (G.graphics.send_uniform :iResolution (G.math.v2 w h))
    (if (= g.state :finished)
        (let [winner-text (if (> g.left-score g.right-score) "Player 1 wins"
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
              (G.graphics.set_color g.ball-color)
              (G.graphics.draw_circle x y br)))))
    (G.graphics.set_color :white)))

Game
