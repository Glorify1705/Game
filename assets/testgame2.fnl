(local Player (require :player))

(local Game {:score 0
             :rectangles []
             :mouse [0 0]
             :reticule {:size 30 :state :normal :st 0 :en 0}})

(fn pick-color [game]
  (G.random.pick (. game :random) [:poisongreen
                                   :desert
                                   :pigpink
                                   :candypink
                                   :boringgreen
                                   :orangepink
                                   :azul]))

(fn Game.init [self]
  (tset self :random (G.random.non_deterministic))
  (tset self :rectangles
        [{:x 100 :y 100 :w 100 :h 100 :color (pick-color self)}
         {:x 300 :y 300 :w 50 :h 50 :color (pick-color self)}])
  (G.sound.set_music_volume 1)
  (G.sound.set_sfx_volume 0.8)
  (G.sound.play_music :weapons_mode.ogg))

(fn contains? [rect px py]
  (let [{: x : y : w : h} rect]
    (and (>= px x) (>= py y) (<= px (+ x w)) (<= py (+ y h)))))

(fn new-rectangle [self sx sy]
  (let [r (. self :random)]
    {:x (G.random.sample r 0 (- sx 90))
     :y (G.random.sample r 0 (- sy 90))
     :w (G.random.sample r 30 90)
     :h (G.random.sample r 30 90)
     :color (pick-color self)}))

(fn clamp [v low high]
  (if (< v low) low
      (> v high) high
      v))

(fn rect-if-any [rects x y]
  (var index nil)
  (each [i rect (ipairs rects) &until index]
    (when (contains? rect x y)
      (set index i)))
  index)

(fn Game.update [self t dt]
  (when (G.input.is_key_pressed :q) (G.quit))
  (let [(mx my) (G.input.mouse_position)]
    (let [[px py] (. self :mouse)]
      (when (and (> t 0.01) (or (not= px mx) (not= py my)))
        (tset self :reticule :state :moving)
        (tset self :reticule :st t)
        (tset self :reticule :en (+ t 0.1)))
      (tset self :mouse [mx my])))
  (let [[mx my] (. self :mouse)
        i (rect-if-any (. self :rectangles) mx my)]
    (when i
      (tset self :reticule :state :inside)
      (tset self :reticule :st t)
      (tset self :reticule :en (+ t 0.3)))
    (when (G.input.is_mouse_pressed 0)
      (G.sound.play_sfx :gunshot.wav)
      (when i
        (let [(sx sy) (G.window.dimensions)]
          (tset self :score (+ (. self :score) 1))
          (table.remove (. self :rectangles) i)
          (table.insert (. self :rectangles) (new-rectangle self sx sy)))))
    (let [{: state : st : en} (. self :reticule)]
      (let [dt (clamp (/ (- t st) (- en st)) 0 1)]
        (case state
          :normal (tset self :reticule :size 10)
          :inside (tset self :reticule :size (+ (* 10 dt) (* 15 (- 1 dt))))
          :moving (tset self :reticule :size (+ (* 10 dt) (* 5 (- 1 dt)))))))))

(fn Game.draw [self]
  (let [(screen-width screen-height) (G.window.dimensions)]
    (let [score_text (.. "Score: " (. self :score))]
      (G.graphics.draw_text :ponderosa.ttf 24 score_text (- screen-width 200) 0))
    (let [(mx my) (G.input.mouse_position)]
      (each [_ {: x : y : w : h : color} (pairs (. self :rectangles))]
        (G.graphics.set_color color)
        (G.graphics.draw_rect x y (+ x w) (+ y h)))
      (G.graphics.set_color :neonred)
      (G.graphics.draw_circle mx my (-> self (. :reticule) (. :size))))))

Game
