(local Player (require :player))

(fn init [] 
    (G.window.set_title "My awesome Lua game!"))

(fn update [t dt]
    (when (G.input.is_key_pressed "q") (G.quit)))

(fn draw []
    (G.graphics.draw_text "ponderosa.ttf" "Welcome to my Fennel game" 600 600))

{: init : update : draw}