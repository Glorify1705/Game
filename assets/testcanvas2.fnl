;; Test program for canvas multi-pass rendering and blend modes.
;; Renders to canvas A, then draws canvas A into canvas B, then draws B
;; to screen — verifying that canvas-to-canvas compositing works.

(local Game {})

(fn Game.init [g]
  (G.window.set_title "Canvas Multi-Pass Test")
  ;; First pass: draw shapes.
  (set g.pass1 (G.graphics.new_canvas 300 300))
  ;; Second pass: composite pass1 with tinting.
  (set g.pass2 (G.graphics.new_canvas 300 300))
  ;; Test semi-transparent canvas for alpha correctness.
  (set g.alpha-canvas (G.graphics.new_canvas 200 200))
  (set g.time 0))

(fn Game.update [g t dt]
  (set g.time t)
  (when (G.input.is_key_pressed :q) (G.system.quit))
  ;; Cycle blend modes with keys.
  (when (G.input.is_key_pressed :1) (set g.blend :alpha))
  (when (G.input.is_key_pressed :2) (set g.blend :add))
  (when (G.input.is_key_pressed :3) (set g.blend :multiply))
  (when (G.input.is_key_pressed :4) (set g.blend :replace)))

(fn Game.draw [g]
  (let [t g.time]
    ;; Pass 1: draw animated shapes into pass1 canvas.
    (G.graphics.set_canvas g.pass1)
    (G.graphics.clear 0 0 0 1)
    (G.graphics.set_color :red)
    (G.graphics.push)
    (G.graphics.translate 150 150)
    (G.graphics.rotate t)
    (G.graphics.draw_rect -60 -60 60 60)
    (G.graphics.pop)
    (G.graphics.set_color :green)
    (let [cx (+ 150 (* 80 (math.cos (* t 1.5))))
          cy (+ 150 (* 80 (math.sin (* t 1.5))))]
      (G.graphics.draw_circle cx cy 30))
    (G.graphics.set_color :white)
    (G.graphics.set_canvas)
    ;; Pass 2: draw pass1 into pass2 with a color tint.
    (G.graphics.set_canvas g.pass2)
    (G.graphics.clear 0 0 0 0)
    (G.graphics.set_color 180 200 255 255)
    (G.graphics.draw_canvas g.pass1 0 0)
    (G.graphics.set_color :white)
    ;; Draw some overlay text into pass2.
    (G.graphics.draw_text :terminus.ttf 16 :Multi-pass! 10 10)
    (G.graphics.set_canvas)
    ;; Alpha canvas: test semi-transparent content.
    (G.graphics.set_canvas g.alpha-canvas)
    (G.graphics.clear 0 0 0 0)
    ;; Draw semi-transparent overlapping rects.
    (G.graphics.set_color 255 0 0 128)
    (G.graphics.draw_rect 20 20 120 120)
    (G.graphics.set_color 0 0 255 128)
    (G.graphics.draw_rect 60 60 160 160)
    (G.graphics.set_color :white)
    (G.graphics.set_canvas)
    ;; Draw to screen.
    (G.graphics.clear 0.2 0.2 0.25 1)
    ;; Pass 1 result.
    (G.graphics.draw_text :terminus.ttf 16 "Pass 1 (shapes)" 10 20)
    (G.graphics.draw_canvas g.pass1 10 40)
    ;; Pass 2 result (pass1 composited with tint).
    (G.graphics.draw_text :terminus.ttf 16 "Pass 2 (tinted + text overlay)" 320
                          20)
    (G.graphics.draw_canvas g.pass2 320 40)
    ;; Alpha canvas.
    (G.graphics.draw_text :terminus.ttf 16 "Semi-transparent rects" 10 360)
    (G.graphics.draw_canvas g.alpha-canvas 10 380)
    ;; Blend mode info.
    (let [mode (or g.blend :alpha)]
      (G.graphics.draw_text :terminus.ttf 16
                            (.. "Blend mode: " mode " (press 1-4 to change)") 10
                            590))
    (G.graphics.draw_text :terminus.ttf 16
                          "1=alpha  2=add  3=multiply  4=replace  Q=quit" 10 620)))

Game
