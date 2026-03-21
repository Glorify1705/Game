;; Canvas post-processing demo.
;; Renders a game scene (sprites, shapes, particles) to a canvas, then
;; draws that canvas to screen through different post-processing shaders
;; (CRT scanlines, pixelation, vignette). Press 1-4 to switch effects.

(local Game {})

(local NUM-STARS 80)
(local NUM-PARTICLES 40)

(fn make-star [w h]
  {:x (math.random 0 w)
   :y (math.random 0 h)
   :speed (+ 20 (math.random 0 80))
   :brightness (+ 100 (math.random 0 155))})

(fn make-particle []
  {:x 0 :y 0 :dx 0 :dy 0 :life 0 :max-life 0 :active false})

(fn spawn-particle [p x y]
  (let [angle (* (math.random) math.pi 2)
        speed (+ 30 (math.random 0 120))]
    (tset p :x x)
    (tset p :y y)
    (tset p :dx (* speed (math.cos angle)))
    (tset p :dy (* speed (math.sin angle)))
    (tset p :life 1.0)
    (tset p :max-life 1.0)
    (tset p :active true)))

(fn Game.init [self]
  (let [(w h) (G.window.dimensions)]
    (G.window.set_title "Canvas Post-Processing Demo")
    ;; Scene canvas — everything gets drawn here first.
    (set self.scene (G.graphics.new_canvas w h))
    ;; Starfield.
    (set self.stars [])
    (for [i 1 NUM-STARS]
      (table.insert self.stars (make-star w h)))
    ;; Ship state.
    (set self.ship {:x (/ w 2) :y (/ h 2) :angle 0 :thrust false})
    ;; Particle pool.
    (set self.particles [])
    (for [i 1 NUM-PARTICLES]
      (table.insert self.particles (make-particle)))
    (set self.particle-timer 0)
    ;; Effect state.
    (set self.effect :none)
    (set self.time 0)
    (set self.w w)
    (set self.h h)))

(fn update-stars [stars dt w]
  (each [_ star (ipairs stars)]
    (tset star :x (- star.x (* star.speed dt)))
    (when (< star.x 0)
      (tset star :x w)
      (tset star :y (math.random 0 600))
      (tset star :speed (+ 20 (math.random 0 80))))))

(fn update-ship [ship dt]
  (let [turn-speed 3.0]
    (when (G.input.is_key_down :a)
      (tset ship :angle (- ship.angle (* turn-speed dt))))
    (when (G.input.is_key_down :d)
      (tset ship :angle (+ ship.angle (* turn-speed dt))))
    (tset ship :thrust (G.input.is_key_down :w))
    (when ship.thrust
      (let [speed 120
            dx (* speed (math.cos ship.angle) dt)
            dy (* speed (math.sin ship.angle) dt)]
        (tset ship :x (+ ship.x dx))
        (tset ship :y (+ ship.y dy))))))

(fn update-particles [particles dt]
  (each [_ p (ipairs particles)]
    (when p.active
      (tset p :x (+ p.x (* p.dx dt)))
      (tset p :y (+ p.y (* p.dy dt)))
      (tset p :dx (* p.dx 0.98))
      (tset p :dy (* p.dy 0.98))
      (tset p :life (- p.life (* dt 1.5)))
      (when (<= p.life 0)
        (tset p :active false)))))

(fn spawn-engine-particles [particles ship timer dt]
  (when (not ship.thrust)
    (values timer))
  (var t (+ timer dt))
  (while (>= t 0.03)
    (set t (- t 0.03))
    ;; Find an inactive particle.
    (var spawned false)
    (each [_ p (ipairs particles) &until spawned]
      (when (not p.active)
        ;; Spawn behind the ship.
        (let [ex (- ship.x (* 15 (math.cos ship.angle)))
              ey (- ship.y (* 15 (math.sin ship.angle)))]
          (spawn-particle p ex ey)
          ;; Override velocity to go opposite of ship direction.
          (let [spread (- (* (math.random) 0.8) 0.4)
                back-angle (+ ship.angle math.pi spread)
                speed (+ 60 (math.random 0 80))]
            (tset p :dx (* speed (math.cos back-angle)))
            (tset p :dy (* speed (math.sin back-angle)))))
        (set spawned true))))
  t)

(fn Game.update [self t dt]
  (set self.time t)
  (when (G.input.is_key_pressed :q) (G.system.quit))
  ;; Switch effects.
  (when (G.input.is_key_pressed :1) (set self.effect :none))
  (when (G.input.is_key_pressed :2) (set self.effect :crt))
  (when (G.input.is_key_pressed :3) (set self.effect :pixelate))
  (when (G.input.is_key_pressed :4) (set self.effect :vignette))
  ;; Update game.
  (update-stars self.stars dt self.w)
  (update-ship self.ship dt)
  (update-particles self.particles dt)
  (set self.particle-timer
       (spawn-engine-particles self.particles self.ship
                               self.particle-timer dt)))

(fn draw-stars [stars]
  (each [_ star (ipairs stars)]
    (let [b star.brightness]
      (G.graphics.set_color b b b 255)
      (G.graphics.draw_rect star.x star.y
                            (+ star.x 2) (+ star.y 2)))))

(fn draw-ship [ship]
  ;; Draw ship as a triangle pointing in its direction.
  (let [a ship.angle
        ;; Nose.
        nx (+ ship.x (* 18 (math.cos a)))
        ny (+ ship.y (* 18 (math.sin a)))
        ;; Left wing.
        la (+ a 2.5)
        lx (+ ship.x (* 14 (math.cos la)))
        ly (+ ship.y (* 14 (math.sin la)))
        ;; Right wing.
        ra (- a 2.5)
        rx (+ ship.x (* 14 (math.cos ra)))
        ry (+ ship.y (* 14 (math.sin ra)))]
    ;; Ship body.
    (G.graphics.set_color :white)
    (G.graphics.draw_triangle nx ny lx ly rx ry)
    ;; Cockpit.
    (G.graphics.set_color :cyan)
    (G.graphics.draw_circle (+ ship.x (* 5 (math.cos a)))
                            (+ ship.y (* 5 (math.sin a))) 3))
  ;; Draw sprites around the ship.
  (G.graphics.set_color :white)
  (G.graphics.draw_sprite :playerShip1_green
                          (+ ship.x 50) (- ship.y 30) 0)
  (G.graphics.draw_sprite :playerShip1_green
                          (- ship.x 80) (+ ship.y 40) 1.5))

(fn draw-particles [particles]
  (G.graphics.set_blend_mode :add)
  (each [_ p (ipairs particles)]
    (when p.active
      (let [alpha (math.floor (* p.life 255))
            r 255
            g (math.floor (* p.life 200))
            b (math.floor (* p.life p.life 80))
            size (+ 1 (* p.life 3))]
        (G.graphics.set_color r g b alpha)
        (G.graphics.draw_rect (- p.x size) (- p.y size)
                              (+ p.x size) (+ p.y size)))))
  (G.graphics.set_blend_mode :alpha))

(fn draw-scene [self]
  (G.graphics.set_canvas self.scene)
  (G.graphics.clear 0.02 0.02 0.08 1)
  (draw-stars self.stars)
  (draw-particles self.particles)
  (draw-ship self.ship)
  ;; Draw some floating text in the scene.
  (G.graphics.set_color :white)
  (G.graphics.draw_text :terminus.ttf 20 "STARFIELD" 10 20)
  (G.graphics.set_canvas))

(fn draw-with-effect [self]
  (if (= self.effect :crt)
      (do
        (G.graphics.attach_shader :crt.frag)
        (G.graphics.send_uniform :iResolution
                                 (G.math.v2 self.w self.h))
        (G.graphics.draw_canvas self.scene 0 0)
        (G.graphics.attach_shader))

      (= self.effect :pixelate)
      (do
        (G.graphics.attach_shader :pixelate.frag)
        (G.graphics.send_uniform :pixels 300.0)
        (G.graphics.draw_canvas self.scene 0 0)
        (G.graphics.attach_shader))

      (= self.effect :vignette)
      (do
        (G.graphics.attach_shader :vignette.frag)
        (G.graphics.send_uniform :iResolution
                                 (G.math.v2 self.w self.h))
        (G.graphics.send_uniform :intensity 0.6)
        (G.graphics.send_uniform :warmth
                                 (* 0.5 (math.sin self.time)))
        (G.graphics.draw_canvas self.scene 0 0)
        (G.graphics.attach_shader))

      ;; No effect — draw directly.
      (G.graphics.draw_canvas self.scene 0 0)))

(fn Game.draw [self]
  ;; Render the game scene into the canvas.
  (draw-scene self)
  ;; Draw to screen, optionally through a post-process shader.
  (G.graphics.clear 0 0 0 1)
  (draw-with-effect self)
  ;; HUD (drawn directly to screen, not affected by post-processing).
  (G.graphics.set_color :white)
  (G.graphics.draw_text :terminus.ttf 16
                        (.. "Effect: " self.effect
                            "  |  1=none  2=CRT  3=pixelate  4=vignette")
                        10 (- self.h 40))
  (G.graphics.draw_text :terminus.ttf 16
                        "WASD to fly  |  Q to quit" 10 (- self.h 20)))

Game
