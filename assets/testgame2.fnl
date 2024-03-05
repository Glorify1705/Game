(local Player (require :player))

(local Game {:score 0 :rectangles []})

(fn Game.init [self]
  (tset self :rectangles [{:x 100 :y 100 :w 100 :h 100}
                          {:x 300 :y 300 :w 50 :h 50}])
  (tset self :random (G.random.non_deterministic))
  (G.sound.set_music_volume 0.2)
  (G.sound.set_sfx_volume 0.1))

(fn contains? [rect px py]
  (let [{: x : y : w : h} rect]
    (and (>= px x) (>= py y) (<= px (+ x w)) (<= py (+ y h)))))

(fn new-rectangle [self sx sy]
  (let [r (. self :random)]
    {:x (G.random.sample r 0 sx)
     :y (G.random.sample r 0 sy)
     :w (G.random.sample r 30 150)
     :h (G.random.sample r 30 150)}))

(fn Game.update [self t dt]
  (when (G.input.is_key_pressed :q) (G.quit))
  (when (G.input.is_mouse_pressed 0)
    (G.sound.play_sfx :gunshot.wav)
    (let [(sx sy) (G.window.dimensions)]
      (let [(mx my) (G.input.mouse_position)]
        (var found false)
        (each [i rect (ipairs (. self :rectangles)) &until found]
          (when (contains? rect mx my)
            (tset self :score (+ (. self :score) 1))
            (table.remove (. self :rectangles) i)
            (table.insert (. self :rectangles) (new-rectangle self sx sy))
            (set found true)))))))

(fn Game.draw [self]
  (let [(screen-width screen-height) (G.window.dimensions)]
    (let [score_text (.. "Score: " (. self :score))]
      (G.graphics.draw_text :ponderosa.ttf 24 score_text (- screen-width 200) 0))
    (let [(mx my) (G.input.mouse_position)]
      (G.graphics.set_color :poisongreen)
      (each [_ {: x : y : w : h} (pairs (. self :rectangles))]
        (G.graphics.draw_rect x y (+ x w) (+ y h)))
      (G.graphics.set_color :neonred)
      (G.graphics.draw_circle mx my 10))))

Game
