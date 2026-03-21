;; Test program for canvas / off-screen rendering API.
;; Exercises: new_canvas, set_canvas, draw_canvas, clear with color,
;; blend modes, pixel-art scaling, and multi-pass rendering.

(local Game {})

(fn Game.init [g]
  (G.window.set_title "Canvas API Test")
  ;; A small pixel-art canvas drawn scaled up with nearest filtering.
  (set g.pixel-canvas (G.graphics.new_canvas 64 64 {:filter :nearest}))
  ;; A regular canvas for off-screen compositing.
  (set g.scene-canvas (G.graphics.new_canvas 400 300))
  ;; A canvas for blend mode demos.
  (set g.blend-canvas (G.graphics.new_canvas 200 200))
  (set g.time 0))

(fn Game.update [g t dt]
  (set g.time t)
  (when (G.input.is_key_pressed :q) (G.system.quit)))

(fn draw-pixel-art [canvas t]
  ;; Draw some pixel art into a tiny 64x64 canvas.
  (G.graphics.set_canvas canvas)
  (G.graphics.clear 0.1 0.1 0.2 1)
  ;; Checkerboard.
  (for [y 0 7]
    (for [x 0 7]
      (when (= (% (+ x y) 2) 0)
        (G.graphics.set_color :green)
        (G.graphics.draw_rect (* x 8) (* y 8)
                              (+ (* x 8) 8) (+ (* y 8) 8)))))
  ;; Animated crosshair.
  (let [cx (+ 32 (* 16 (math.cos t)))
        cy (+ 32 (* 16 (math.sin t)))]
    (G.graphics.set_color :red)
    (G.graphics.draw_rect (- cx 2) (- cy 2) (+ cx 2) (+ cy 2)))
  (G.graphics.set_color :white)
  (G.graphics.set_canvas))

(fn draw-scene [canvas t]
  ;; Draw a scene with shapes into an off-screen canvas.
  (G.graphics.set_canvas canvas)
  (G.graphics.clear 0 0 0 0) ;; transparent background
  ;; Rotating square.
  (G.graphics.set_color :blue)
  (G.graphics.push)
  (G.graphics.translate 200 150)
  (G.graphics.rotate t)
  (G.graphics.draw_rect -40 -40 40 40)
  (G.graphics.pop)
  ;; Orbiting circles.
  (G.graphics.set_color :yellow)
  (let [cx (+ 200 (* 80 (math.cos (* t 2))))
        cy (+ 150 (* 80 (math.sin (* t 2))))]
    (G.graphics.draw_circle cx cy 20))
  (G.graphics.set_color :cyan)
  (let [cx (+ 200 (* 80 (math.cos (+ (* t 2) 3.14))))
        cy (+ 150 (* 80 (math.sin (+ (* t 2) 3.14))))]
    (G.graphics.draw_circle cx cy 20))
  (G.graphics.set_color :white)
  (G.graphics.set_canvas))

(fn draw-blend-demo [canvas t]
  ;; Show additive blending (glow effect).
  (G.graphics.set_canvas canvas)
  (G.graphics.clear 0.05 0.05 0.05 1)
  (G.graphics.set_blend_mode :add)
  ;; Overlapping circles that add together.
  (G.graphics.set_color 150 25 25 255)
  (G.graphics.draw_circle 80 100 50)
  (G.graphics.set_color 25 150 25 255)
  (G.graphics.draw_circle 120 100 50)
  (G.graphics.set_color 25 25 150 255)
  (G.graphics.draw_circle 100 70 50)
  ;; Restore blend mode.
  (G.graphics.set_blend_mode :alpha)
  (G.graphics.set_color :white)
  (G.graphics.set_canvas))

(fn Game.draw [g]
  (let [t g.time]
    ;; Render into each canvas.
    (draw-pixel-art g.pixel-canvas t)
    (draw-scene g.scene-canvas t)
    (draw-blend-demo g.blend-canvas t)
    ;; Now draw everything to the screen.
    (G.graphics.clear 0.15 0.15 0.15 1)
    ;; Labels.
    (G.graphics.draw_text :terminus.ttf 16 "Pixel canvas (64x64 -> 256x256, nearest)" 10 20)
    (G.graphics.draw_canvas g.pixel-canvas 10 40 0 256 256)
    (G.graphics.draw_text :terminus.ttf 16 "Scene canvas (400x300)" 300 20)
    (G.graphics.draw_canvas g.scene-canvas 300 40)
    (G.graphics.draw_text :terminus.ttf 16 "Additive blend (RGB circles)" 10 330)
    (G.graphics.draw_canvas g.blend-canvas 10 350)
    ;; Draw the scene canvas again at half size to test scaling.
    (G.graphics.draw_text :terminus.ttf 16 "Scene canvas (half size)" 300 360)
    (G.graphics.draw_canvas g.scene-canvas 300 380 0 200 150)
    ;; Canvas info.
    (let [(w h) (g.scene-canvas:dimensions)]
      (G.graphics.draw_text :terminus.ttf 16
                            (.. "scene-canvas dimensions: " w "x" h
                                " (width=" (g.scene-canvas:width)
                                " height=" (g.scene-canvas:height) ")")
                            10 560))
    (G.graphics.draw_text :terminus.ttf 16
                          (.. "tostring: " (tostring g.pixel-canvas))
                          10 580)
    (G.graphics.draw_text :terminus.ttf 16 "Press Q to quit" 10 620)))

Game
