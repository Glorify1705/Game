-- Test program for audio features: looping, pause/resume, pitch, panning.
-- Controls:
--   1: Play music (looping)         2: Stop music
--   3: Pause music                  4: Resume music
--   5: Play laser effect            6: Play gunshot effect
--   Q/W: Pitch down/up             A/S: Pan left/right
--   R: Reset pitch and pan          Esc: Quit

local Game = {}

function Game:init()
    G.window.set_title("Audio Features Test")

    -- Set up a looping music source.
    self.music = G.sound.add_source("music.ogg")
    G.sound.set_volume(self.music, 0.3)
    G.sound.set_loop(self.music, true)

    -- Set up a reusable effect source.
    self.laser = G.sound.add_effect("laser.ogg")
    G.sound.set_volume(self.laser, 0.5)

    self.pitch = 1.0
    self.pan = 0.0
    self.status = "Press 1 to start music"
end

function Game:update(t, dt)
    if G.input.is_key_pressed("escape") then
        G.system.quit()
    end

    -- Music controls.
    if G.input.is_key_pressed("1") then
        G.sound.play_source(self.music)
        self.status = "Playing music (looping)"
    end
    if G.input.is_key_pressed("2") then
        G.sound.stop_source(self.music)
        self.status = "Music stopped"
    end
    if G.input.is_key_pressed("3") then
        G.sound.pause(self.music)
        self.status = "Music paused"
    end
    if G.input.is_key_pressed("4") then
        G.sound.resume(self.music)
        self.status = "Music resumed"
    end

    -- Effect playback.
    if G.input.is_key_pressed("5") then
        G.sound.play_effect("laser.ogg")
        self.status = "Played laser (fire-and-forget)"
    end
    if G.input.is_key_pressed("6") then
        G.sound.play_effect("gunshot.ogg")
        self.status = "Played gunshot (fire-and-forget)"
    end

    -- Pitch control (applied to music).
    if G.input.is_key_down("q") then
        self.pitch = math.max(0.25, self.pitch - 0.5 * dt)
        G.sound.set_pitch(self.music, self.pitch)
    end
    if G.input.is_key_down("w") then
        self.pitch = math.min(4.0, self.pitch + 0.5 * dt)
        G.sound.set_pitch(self.music, self.pitch)
    end

    -- Pan control (applied to music).
    if G.input.is_key_down("a") then
        self.pan = math.max(-1.0, self.pan - 1.0 * dt)
        G.sound.set_pan(self.music, self.pan)
    end
    if G.input.is_key_down("s") then
        self.pan = math.min(1.0, self.pan + 1.0 * dt)
        G.sound.set_pan(self.music, self.pan)
    end

    -- Reset.
    if G.input.is_key_pressed("r") then
        self.pitch = 1.0
        self.pan = 0.0
        G.sound.set_pitch(self.music, self.pitch)
        G.sound.set_pan(self.music, self.pan)
        self.status = "Reset pitch and pan"
    end
end

function Game:draw()
    G.graphics.clear()
    G.graphics.set_color("white")

    local y = 40
    local function line(text)
        G.graphics.print(text, 40, y)
        y = y + 28
    end

    line("=== Audio Features Test ===")
    line("")
    line("Status: " .. self.status)
    line("Playing: " .. tostring(G.sound.is_playing(self.music)))
    line(string.format("Pitch: %.2f", self.pitch))
    line(string.format("Pan:   %.2f", self.pan))
    line("")
    line("--- Controls ---")
    line("1: Play music (loop)    2: Stop music")
    line("3: Pause                4: Resume")
    line("5: Laser effect         6: Gunshot effect")
    line("Q/W: Pitch down/up      A/S: Pan left/right")
    line("R: Reset pitch & pan    Esc: Quit")

    -- Draw a visual pan indicator.
    local w, h = G.window.dimensions()
    local cx = w / 2
    local bar_y = y + 30
    local bar_w = 300

    G.graphics.set_color("grey")
    G.graphics.draw_rect(cx - bar_w / 2, bar_y, cx + bar_w / 2, bar_y + 10, 0)

    G.graphics.set_color("green")
    local dot_x = cx + self.pan * (bar_w / 2)
    G.graphics.draw_circle(dot_x, bar_y + 5, 8)

    -- Draw pitch bar.
    local pitch_y = bar_y + 40
    G.graphics.set_color("grey")
    G.graphics.draw_rect(cx - bar_w / 2, pitch_y, cx + bar_w / 2, pitch_y + 10, 0)

    G.graphics.set_color("yellow")
    local pitch_x = cx + ((self.pitch - 1.0) / 3.0) * (bar_w / 2)
    G.graphics.draw_circle(pitch_x, pitch_y + 5, 8)

    G.graphics.set_color("white")
    G.graphics.print("Pan", cx - bar_w / 2 - 50, bar_y - 3)
    G.graphics.print("Pitch", cx - bar_w / 2 - 60, pitch_y - 3)
end

return Game
