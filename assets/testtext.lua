-- Text layout test.
-- Demonstrates word-wrapped text with different alignments and text measurement.

local Game = {}

local FONT = "ponderosa.ttf"
local SIZE = 20
local WRAP_WIDTH = 300

local sample_text =
  "The quick brown fox jumps over the lazy dog. " ..
  "This is a longer sentence to demonstrate word wrapping in the engine. " ..
  "Short words fit. Superlongwordthatcannotpossiblyfitinacolumn gets its own line."

local short_text = "Hello, world!"

function Game:init()
  G.window.set_title("Text Layout Test")
  G.window.set_dimensions(900, 700)
end

function Game:update(t, dt)
end

function Game:draw()
  local y = 20

  -- Regular text with measurement.
  G.graphics.set_color("white")
  G.graphics.draw_text(FONT, SIZE, "Regular text:", 20, y)
  y = y + 30

  G.graphics.draw_text(FONT, SIZE, short_text, 20, y)
  local w, h = G.graphics.text_dimensions(FONT, SIZE, short_text)
  G.graphics.set_color(80, 80, 80, 120)
  G.graphics.draw_rect(20, y, 20 + w, y + h)
  G.graphics.set_color("white")
  G.graphics.draw_text(FONT, 14, "dimensions: " .. w .. "x" .. h, 20 + w + 10, y)
  y = y + h + 20

  -- Left-aligned wrapped text.
  G.graphics.set_color("skyblue")
  G.graphics.draw_text(FONT, SIZE, "Left-aligned (default):", 20, y)
  y = y + 30

  G.graphics.set_color(40, 40, 60, 180)
  local left_h = G.graphics.text_wrapped_height(FONT, SIZE, sample_text, WRAP_WIDTH)
  G.graphics.draw_rect(20, y, 20 + WRAP_WIDTH, y + left_h)
  G.graphics.set_color("white")
  G.graphics.draw_text_wrapped(FONT, SIZE, sample_text, 20, y, WRAP_WIDTH)
  y = y + left_h + 20

  -- Center-aligned wrapped text.
  G.graphics.set_color("lightgreen")
  G.graphics.draw_text(FONT, SIZE, "Center-aligned:", 20, y)
  y = y + 30

  G.graphics.set_color(40, 60, 40, 180)
  local center_h = G.graphics.text_wrapped_height(FONT, SIZE, sample_text, WRAP_WIDTH)
  G.graphics.draw_rect(20, y, 20 + WRAP_WIDTH, y + center_h)
  G.graphics.set_color("white")
  G.graphics.draw_text_wrapped(FONT, SIZE, sample_text, 20, y, WRAP_WIDTH, "center")
  y = y + center_h + 20

  -- Right-aligned wrapped text.
  G.graphics.set_color("salmon")
  G.graphics.draw_text(FONT, SIZE, "Right-aligned:", 20, y)
  y = y + 30

  G.graphics.set_color(60, 40, 40, 180)
  local right_h = G.graphics.text_wrapped_height(FONT, SIZE, sample_text, WRAP_WIDTH)
  G.graphics.draw_rect(20, y, 20 + WRAP_WIDTH, y + right_h)
  G.graphics.set_color("white")
  G.graphics.draw_text_wrapped(FONT, SIZE, sample_text, 20, y, WRAP_WIDTH, "right")
  y = y + right_h + 20

  -- Side-by-side comparison at right side of screen.
  local rx = 500
  G.graphics.set_color("gold")
  G.graphics.draw_text(FONT, SIZE, "Wrapped height measurement:", rx, 20)

  local sizes = {14, 20, 28}
  local sy = 60
  for _, sz in ipairs(sizes) do
    local th = G.graphics.text_wrapped_height(FONT, sz, sample_text, 350)
    G.graphics.set_color(50, 50, 70, 180)
    G.graphics.draw_rect(rx, sy, rx + 350, sy + th)
    G.graphics.set_color("white")
    G.graphics.draw_text_wrapped(FONT, sz, sample_text, rx, sy, 350)
    G.graphics.set_color("yellow")
    G.graphics.draw_text(FONT, 12, "size=" .. sz .. " h=" .. th, rx + 355, sy)
    sy = sy + th + 15
  end
end

return Game
