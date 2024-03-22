(local Game {})

(fn Game.init [])

(fn Game.update [t dt])

(fn Game.draw []
  (G.graphics.new_canvas)
  (G.graphics.draw_text :ponderosa.ttf 24 "Hello there!" 200 200)
  (G.graphics.set_color :neonred)
  (G.graphics.draw_text :ponderosa.ttf 24 "Hello there but in red!" 400 400)
  (G.graphics.set_color :white)
  (G.graphics.draw_circle 60 60 30)
  (G.graphics.set_color :darkblue)
  (G.graphics.draw_circle 600 600 30)
  (G.graphics.set_color :camo)
  (G.graphics.draw_rect 100 100 150 150)
  (G.graphics.set_color :white)
  (G.graphics.draw_sprite :playerShip1_green 500 500 10)
  (G.graphics.draw_lines [[0 0]
                          [300 300]
                          [300 600]
                          [600 600]
                          [800 700]
                          [900 750]]))

Game
