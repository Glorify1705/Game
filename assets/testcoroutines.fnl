(local Object (require :classic))
(local Queue (require :queue))

(local *text* "Strange game, the only winning move is not to play!")

(local *font-name* :ponderosa.ttf)
(local *font-size* 16)

(local GameObject (Object:extend))

(fn GameObject.update [t dt])

(fn GameObject.draw [])

(local Text (GameObject:extend))

(fn Text.new [t text x y]
  (tset t :text text)
  (tset t :x x)
  (tset t :y y)
  (tset t :scale 1))

(fn Text.scale! [t s] (tset t :scale (+ s t.scale)))

(fn Text.draw [t]
  (let [{: x : y : text} t
        (w h) (G.graphics.text_dimensions *font-name* *font-size* t.text)
        dx (/ w 2)
        dy (/ h 2)]
    (G.graphics.push)
    (G.graphics.translate (- x) (- y))
    (let [s t.scale] (G.graphics.scale s s))
    (G.graphics.translate x y)
    (G.graphics.draw_text *font-name* *font-size* t.text (- x dx) (- y dy))))

(local Action (Object:extend))

(fn Action.new [a] (tset a :finished false))

(fn Action.finished? [a] a.finished)

(fn Action.finished! [a] (tset a :finished true))

(fn Action.update [a t dt])

(local DelayAction (Action:extend))

(fn DelayAction.new [a delay]
  (Action.super.new a)
  (tset a :delay delay))

(fn DelayAction.update [a t dt]
  (let [r (- a.delay dt)]
    (tset a :delay r)
    (when (<= r 0) (a:finished!))))

(local RepeatAction (Action:extend))

(fn RepeatAction.new [a n f]
  (Action.super.new a)
  (tset a :times n)
  (tset a :callback f))

(fn RepeatAction.update [a t dt]
  (let [times a.times]
    (if (<= times 0) (a:finished!) (a.callback))
    (tset a :times (- times 1))))

(local Room (Object:extend))

(fn Room.new [r name]
  (tset r :name name)
  (tset r :objects [])
  (tset r :actions (Queue)))

(fn Room.init [])

(fn Room.add-object! [r o]
  (table.insert r.objects o))

(fn Room.add-action! [r a]
  (r.actions:push! a))

(fn Room.update [r t dt]
  ;; Update objects
  (each [_ o (ipairs r.objects)]
    (o:update t dt))
  ;; Update actions
  (when (not (r.actions:empty?))
    (let [a (r.actions:peek)]
      (a:update t dt)
      (when (a:finished?) (r.actions:pop!)))))

(fn Room.draw [r]
  (each [_ o (ipairs r.objects)]
    (o:draw)))

(local Game {})

(fn switch-room! [g name]
  (let [room (. g name)]
    (room:init g)
    (tset g :current room)))

(local SwitchRoomAction (Action:extend))

(fn SwitchRoomAction.new [a g room]
  (Action.super.new a)
  (tset a :game g)
  (tset a :room room))

(fn SwitchRoomAction.update [a t dt]
  (switch-room! a.game a.room)
  (a:finished!))

(local Menu (Room:extend))

(fn centered-text [msg]
  (let [(w h) (G.window.dimensions)] (Text msg (/ w 2) (/ h 2))))

(fn Menu.init [r g]
  (let [menu-text (centered-text "This is the menu")]
    (r:add-object! menu-text)
    (r:add-action! (DelayAction 1))
    (r:add-action! (RepeatAction 1 (fn [] (menu-text:scale! 1))))
    (r:add-action! (DelayAction 1))
    (r:add-action! (RepeatAction 1 (fn [] (menu-text:scale! 1))))
    (r:add-action! (DelayAction 1))
    (r:add-action! (SwitchRoomAction g :main))))

(local Main (Room:extend))

(fn Main.init [r g]
  (r:add-object! (centered-text "This is the main code")))

(fn Game.init [g]
  (let [menu (Menu g)] (tset g :menu menu))
  (let [main (Main g)] (tset g :main main))
  (switch-room! g :menu))

(fn Game.update [g t dt]
  (when g.current (g.current:update t dt)))

(fn Game.draw [g]
  (when g.current (g.current:draw)))

Game
