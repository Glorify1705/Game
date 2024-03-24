(local Game {})

(fn Game.init []
  (tset Game :input []))

(fn Game.update [t dt]
  (when (G.input.is_key_pressed :escape) (G.system.quit))
  (when (G.input.is_key_pressed :enter) (table.insert Game.input "\n"))
  (when (G.input.is_key_pressed :backspace) (table.remove Game.input)))

(fn Game.textinput [self text]
  (table.insert self.input text))

(fn Game.draw []
  (G.graphics.draw_text :ponderosa.ttf 24 (table.concat Game.input "") 100 100))

Game
