(local fennel (require :fennel))

(G.graphics.set_color :white)
(G.graphics.print (.. "Fennel version: " (fennel.view {:x 100 :y 200})) 100 100)
