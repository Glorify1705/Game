(local Player (require :player))

(local Game {:score 0 :rectangles []})

(fn Game.init [self]
  (tset self :rectangles [{:x 100 :y 100 :w 100 :h 100}
                          {:x 300 :y 300 :w 50 :h 50}])
  (G.sound.set_music_volume 0.2)
  (G.sound.set_sfx_volume 0.1)
  (G.window.set_title "Shoot clicker!"))

(fn contains? [rect px py]
  (let [{: x : y : w : h} rect]
    (and (>= px x) (>= py y) (<= px (+ x w)) (<= py (+ y h)))))

(fn on-rectangle? [rects x y]
  (var found false)
  (each [_ rect (ipairs rects) &until found]
    (when (contains? rect x y) (set found true)))
  found)

(fn Game.update [self t dt]
  (when (G.input.is_key_pressed :q) (G.quit))
  (when (G.input.is_mouse_pressed 0)
    (G.sound.play_sfx :gunshot.wav)
    (let [(mx my) (G.input.mouse_position)]
      (when (on-rectangle? (. self :rectangles) mx my)
        (tset self :score (+ (. self :score) 1))))))

(fn Game.draw [self]
  (let [score_text (.. "Score: " (. self :score))]
    (G.graphics.draw_text :ponderosa.ttf 24 score_text 1000 0))
  (let [(mx my) (G.input.mouse_position)]
    (G.graphics.set_color :poisongreen)
    (each [_ {: x : y : w : h} (pairs (. self :rectangles))]
      (G.graphics.draw_rect x y (+ x w) (+ y h)))
    (G.graphics.set_color :neonred)
    (G.graphics.draw_circle mx my 10)))

Game
