(local Game {})

(fn Game.init [])

(fn Game.update [t dt]
  (when (G.input.is_key_pressed :q) (G.system.quit)))

(fn Game.draw []
  (G.graphics.clear)
  (let [(mx my) (G.input.mouse_position)]
    (G.graphics.draw_text :ponderosa.ttf 24
                          (.. "Hello there! Your mouse is at x = " mx " y = "
                              my) 50 200)
    (G.graphics.set_color :blue)
    (G.graphics.draw_circle mx my 5))
  (G.graphics.set_color :green)
  (G.graphics.draw_text :ponderosa.ttf 24 "Hello there but in green" 400 400)
  (G.graphics.set_color :white)
  (G.graphics.draw_circle 60 60 30)
  (G.graphics.set_color :darkblue)
  (G.graphics.draw_circle 600 600 30)
  (G.graphics.set_color :camo)
  (G.graphics.draw_rect 100 100 150 150)
  (G.graphics.set_color :white)
  (G.graphics.draw_text :ponderosa.ttf 16
                        "The quick brown fox jumps over the lazy dog" 100 300)
  (G.graphics.draw_text :terminus.ttf 16
                        "The quick brown fox jumps over the lazy dog" 100 400)
  (G.graphics.draw_text :terminus.ttf 20
                        "The quick brown fox jumps over the lazy dog" 100 500)
  (G.graphics.draw_text :terminus.ttf 24
                        "The quick brown fox jumps over the lazy dog" 100 600)
  (G.graphics.draw_text :terminus.ttf 32
                        "The quick brown fox jumps over the lazy dog" 100 800)
  (G.graphics.draw_sprite :playerShip1_green 500 500 10)
  (G.graphics.draw_sprite :playerShip1_green 500 500 10)
  (G.graphics.set_color :darkcream)
  (G.graphics.draw_triangle 200 600 250 650 500 700)
  (G.graphics.set_color :white)
  (G.graphics.draw_image :jill.qoi 850 550)
  (G.graphics.draw_image :jill.png 850 650)
  (G.graphics.draw_lines [[0 0]
                          [300 300]
                          [300 600]
                          [600 600]
                          [800 700]
                          [900 750]]))

Game
