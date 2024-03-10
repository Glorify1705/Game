(local Object (require :classic))

(local Styles
       {:none {:points 500 :duration 0 :text :Yaawn}
        :dull {:points 1000 :duration 10 :text :Dull}
        :clumsy {:points 1100 :duration 8 :text :Clumsy}
        :basic {:points 1250 :duration 7 :text :Basic}
        :acceptable {:points 1600 :duration 5 :text :Acceptable}
        :superb {:points 1800 :duration 4 :text :Superb}
        :ssick {:points 1800 :duration 3 :text :SSick}
        :sssavage {:points 1800 :duration 2 :text :SSSavage}
        :sssassy {:points 1800 :duration 1 :text :SSSSASSY}})

(local Colors [:poisongreen
               :desert
               :pigpink
               :candypink
               :boringgreen
               :orangepink
               :oliveyellow
               :rawsienna
               :camo
               :oldrose
               :hazel
               :darkishpurple
               :azul])

(local Game {:score 0
             :timer 0
             :rectangles []
             :mouse [0 0]
             :dimensions [0 0]
             :reticule {:size 30 :state :normal :st 0 :en 0}
             :style {:style :none :st 0 :en 0}})

(fn style-spelling [g]
  (let [current (-> g (. :style) (. :style))]
    (-> Styles (. current) (. :text))))

(fn update-score! [g]
  (tset g :score (+ g.score (-> Styles (. g.style.style) (. :points)))))

(fn pick-color [g]
  (G.random.pick (. g.random) Colors))

(fn clamp [v low high]
  (if (< v low) low
      (> v high) high
      v))

(fn add-rectangle! [g t]
  (let [[sx sy] g.dimensions
        rand g.random
        rect {:x (G.random.sample rand 200 (- sx 200))
              :y (G.random.sample rand 200 (- sy 200))
              :w (G.random.sample rand 30 90)
              :h (G.random.sample rand 30 90)
              :start t
              :end (+ t 0.5)
              :color (pick-color g)}]
    (table.insert g.rectangles rect)))

(fn linear-interpolate [dt st en]
  (+ (* en dt) (* st (- 1 dt))))

(fn rectangle-position [g rect]
  (let [{: x : y : w : h :start st :end en} rect
        dt (clamp (/ (- g.t st) (- en st)) 0 1)
        dx (linear-interpolate dt 0 rect.w)
        dy (linear-interpolate dt 0 rect.h)]
    [(- x (/ dx 2)) (- y (/ dy 2)) (+ x (/ dx 2)) (+ y (/ dy 2))]))

(fn draw-rectangle [g rect]
  (let [[sx sy ex ey] (rectangle-position g rect)]
    (G.graphics.draw_rect sx sy ex ey)))

(fn rectangle-contains? [g rect px py]
  (let [[sx sy ex ey] (rectangle-position g rect)]
    (and (>= px sx) (>= py sy) (<= px ex) (<= py ey))))

(fn find-containing-rectangle [g x y]
  (var index nil)
  (each [i rect (ipairs g.rectangles) &until index]
    (when (rectangle-contains? g rect x y)
      (set index i)))
  index)

(fn Game.init [g]
  (tset g :random (G.random.non_deterministic))
  (tset g :timer 60)
  (let [(sx sy) (G.window.dimensions)]
    (tset g :dimensions [sx sy])
    (for [_ 1 10] (add-rectangle! g 0)))
  (G.sound.set_music_volume 1)
  (G.sound.set_sfx_volume 0.8)
  (G.sound.play_music :weapons_mode.ogg))

(fn Game.quit [g]
  (print "Thanks for playing!"))

(fn Game.update [g t dt]
  (when (G.input.is_key_pressed :q) (G.system.quit))
  (tset g :t t)
  (let [(sx sy) (G.window.dimensions)]
    (tset g :dimensions [sx sy]))
  (when (< g.timer 0) (G.sound.stop))
  (when (> g.timer 0)
    (tset g :timer (- g.timer dt))
    (let [(mx my) (G.input.mouse_position)]
      (let [[px py] (. g :mouse)]
        (when (and (> t 0.01) (or (not= px mx) (not= py my)))
          (tset g :reticule :state :moving)
          (tset g :reticule :st t)
          (tset g :reticule :en (+ t 0.1)))
        (tset g :mouse [mx my])))
    (let [[mx my] (. g :mouse)
          i (find-containing-rectangle g mx my)]
      (when i
        (tset g :reticule :state :inside)
        (tset g :reticule :st t)
        (tset g :reticule :en (+ t 0.3)))
      (when (G.input.is_mouse_pressed 0)
        (G.sound.play_sfx :gunshot.wav)
        (tset g :reticule :state :moving)
        (tset g :reticule :st t)
        (tset g :reticule :en (+ t 0.1))
        (when i
          (let [(sx sy) (G.window.dimensions)]
            (update-score! g)
            (table.remove (. g :rectangles) i)
            (add-rectangle! g t))))
      (let [{: state : st : en} (. g :reticule)]
        (let [dt (clamp (/ (- t st) (- en st)) 0 1)]
          (case state
            :normal (tset g :reticule :size 10)
            :inside (tset g :reticule :size (linear-interpolate dt 10 15))
            :moving (tset g :reticule :size (linear-interpolate dt 10 5))))))))

(local *font-name* :ponderosa.ttf)
(local *font-size* 16)

(fn text-dimensions [msg]
  (G.graphics.text_dimensions *font-name* *font-size* msg))

(fn draw-text [msg x y]
  (G.graphics.draw_text *font-name* *font-size* msg x y))

(fn Game.draw [g]
  (if (<= g.timer 0)
      (let [msg (.. "Game Over. Score: " g.score)
            [sx sy] g.dimensions
            (tx ty) (text-dimensions msg)]
        (draw-text msg (- (/ sx 2) (/ tx 2)) (- (/ sy 2) (/ ty 2))))
      (let [(screen-width screen-height) (G.window.dimensions)]
        (let [hud (.. "Timer: " (math.floor g.timer) " / Score: " g.score " / "
                      (style-spelling g))]
          (let [(tx ty) (text-dimensions hud)]
            (draw-text hud (- screen-width tx) 20)))
        (let [(mx my) (G.input.mouse_position)]
          (each [_ rect (pairs g.rectangles)]
            (G.graphics.set_color rect.color)
            (draw-rectangle g rect))
          (G.graphics.set_color :neonred)
          (G.graphics.draw_circle mx my (-> g (. :reticule) (. :size)))))))

Game
