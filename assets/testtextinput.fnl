(local Game {})

(fn Game.init [g]
  (tset g :input [])
  (tset g :size 24))

(fn resize-font [g new-size]
  (tset g :size (G.math.clamp new-size 12 40)))

(fn Game.update [g t dt]
  (when (G.input.is_key_pressed :escape) (G.system.quit))
  (when (G.input.is_key_pressed :enter) (table.insert Game.input "\n"))
  (when (G.input.is_key_pressed :backspace) (table.remove Game.input))
  (when (G.input.is_key_pressed "+") (resize-font g (+ 1 (. g :size))))
  (when (G.input.is_key_pressed "-") (resize-font g (- (. g :size) 1))))

(fn Game.textinput [g text]
  (table.insert g.input text))

(fn Game.draw [g]
  (G.graphics.draw_text :ponderosa.ttf (. g :size) (table.concat Game.input "") 100 100))

Game
