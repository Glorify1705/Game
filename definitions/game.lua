---@meta
-- Auto-generated LuaLS stubs from LuaApiFunction metadata.
-- Do not edit manually.

---@class G
---@field data G.data
---@field camera G.camera
---@field filesystem G.filesystem
---@field graphics G.graphics
---@field window G.window
---@field input G.input
---@field math G.math
---@field physics G.physics
---@field random G.random
---@field sound G.sound
---@field system G.system
---@field clock G.clock
---@field assets G.assets
---@field collision G.collision
---@field json G.json
---@field test G.test
---@field timer G.timer
---@field scene G.scene
---@field particles G.particles
---@field save G.save
---@field tilemap G.tilemap
G = {}

---@class G.data
G.data = {}

---Computes a hash of a string or byte buffer
---@param data string a string or byte_buffer to hash
---@return number hash the hash value
function G.data.hash(data) end

---@class G.camera
G.camera = {}

---Sets the camera position in world coordinates.
---@param x number x world coordinate
---@param y number y world coordinate
function G.camera.set(x, y) end

---Returns the current camera position in world coordinates.
---@return number x x world coordinate
---@return number y y world coordinate
function G.camera.get() end

---Moves the camera by a delta in world coordinates.
---@param dx number x offset
---@param dy number y offset
function G.camera.move(dx, dy) end

---Sets the camera zoom level. Values > 1 zoom in, < 1 zoom out.
---@param zoom number zoom factor
function G.camera.set_zoom(zoom) end

---Returns the current zoom level.
---@return number zoom zoom factor
function G.camera.get_zoom() end

---Sets the camera rotation in radians.
---@param angle number rotation angle in radians
function G.camera.set_rotation(angle) end

---Returns the current camera rotation in radians.
---@return number angle rotation angle in radians
function G.camera.get_rotation() end

---Sets the position the camera should follow. Call each frame with the target's position.
---@param x number target x world coordinate
---@param y number target y world coordinate
function G.camera.follow(x, y) end

---Sets the smoothing factor for following. 0 = no movement, 1 = instant. Values around 0.05-0.15 give a smooth feel.
---@param lx number horizontal smoothing factor (0-1)
---@param ly number vertical smoothing factor (0-1)
function G.camera.set_lerp(lx, ly) end

---Stops following the target. Camera stays at its current position.
function G.camera.unfollow() end

---Sets a deadzone rectangle as a fraction of the viewport (0-1). The target can move within this zone without the camera following.
---@param half_w number half-width as fraction of viewport width (0-1)
---@param half_h number half-height as fraction of viewport height (0-1)
function G.camera.set_deadzone(half_w, half_h) end

---Removes the deadzone so the camera tracks the target directly.
function G.camera.clear_deadzone() end

---Sets world bounds that the camera viewport cannot exceed.
---@param x number left edge of bounds
---@param y number top edge of bounds
---@param w number width of bounds
---@param h number height of bounds
function G.camera.set_bounds(x, y, w, h) end

---Removes world bounds so the camera can scroll freely.
function G.camera.clear_bounds() end

---Starts a screen shake effect with the given intensity and duration.
---@param intensity number shake amplitude in pixels
---@param duration number shake duration in seconds
---@param frequency? number oscillation frequency (default 8)
function G.camera.shake(intensity, duration, frequency?) end

---Converts screen coordinates to world coordinates.
---@param screen_x number x position on screen
---@param screen_y number y position on screen
---@return number world_x x position in world
---@return number world_y y position in world
function G.camera.to_world(screen_x, screen_y) end

---Converts world coordinates to screen coordinates.
---@param world_x number x position in world
---@param world_y number y position in world
---@return number screen_x x position on screen
---@return number screen_y y position on screen
function G.camera.to_screen(world_x, world_y) end

---Returns the mouse position in world coordinates.
---@return number world_x x position in world
---@return number world_y y position in world
function G.camera.mouse_world() end

---Pushes the camera transform onto the render stack. Everything drawn after this will be in camera space. Optional parallax factors (0-1) make layers scroll slower for depth effect.
---@param parallax_x? number horizontal parallax factor (default 1.0)
---@param parallax_y? number vertical parallax factor (default 1.0)
function G.camera.attach(parallax_x?, parallax_y?) end

---Pops the camera transform from the render stack. Drawing returns to screen space.
function G.camera.detach() end

---@class G.filesystem
G.filesystem = {}

---Writes a string to a given file, overwriting all contents
---@param name string Filename to write to
---@param str string string to write
---@return string result nil on success, a string if there were any errors
function G.filesystem.spit(name, str) end

---Reads a whole file into a string
---@param name string Filename to read from
---@return string error nil on success, a string if there were any errors
---@return byte_buffer contents File contents on success, nil in case of errors
function G.filesystem.slurp(name) end

---List all files in a given directory
---@param name string Directory to list
---@return table files A list with all the files in the given directory
function G.filesystem.list_directory(name) end

---Returns whether a file exists
---@param name string Path to the potential file to check
---@return boolean exists Whether the file exists or not
function G.filesystem.exists(name) end

---Get file metadata
---@param name string Path to the file or directory
---@return string error nil on success, error message on failure
---@return table info Table with size, type, and modtime fields
function G.filesystem.stat(name) end

---Delete a file from the write directory
---@param name string Path to the file to delete
---@return string error nil on success, error message on failure
function G.filesystem.delete(name) end

---@class G.graphics
G.graphics = {}

---Clear the current render target. With no arguments clears to transparent black. With arguments clears to the given RGBA color (0-255).
---@param r? number red component (0-255)
---@param g? number green component (0-255)
---@param b? number blue component (0-255)
---@param a? number alpha component (0-255)
function G.graphics.clear(r?, g?, b?, a?) end

---Saves a screenshot from the contents of the current framebuffer
---@param file? string If provided, a filename where we should write the screenshot.
---@return byte_buffer result If a file was provided, nil if the write succeeded or an error message otherwise. If no file was provided, a byte buffer with the image contents
function G.graphics.take_screenshot(file?) end

---Draws a sprite by name to the screen
---@param sprite string the name of the sprite in any sprite sheet
---@param x number the x position (left-right) in screen coordinates where to draw the sprite
---@param y number the y position (top-bottom) in screen coordinates where to draw the sprite
---@param angle? number if provided, the angle to rotate the sprite
function G.graphics.draw_sprite(sprite, x, y, angle?) end

---Draws an image by name to the screen
---@param image string the name of the image to draw
---@param x number the x position (left-right) in screen coordinates where to draw the image
---@param y number the y position (top-bottom) in screen coordinates where to draw the image
---@param angle? number if provided, the angle to rotate the image
function G.graphics.draw_image(image, x, y, angle?) end

---Draws a solid rectangle to the screen, with the color provided by the global context
---@param x1 number the x coordinate for the top left of the rectangle
---@param y1 number the y position for the top left of the rectangle
---@param x2 number the x position for the bottom right of the rectangle
---@param y2 number the y position for the bottom right of the rectangle
---@param angle? number if provided, the angle to rotate the rectangle
function G.graphics.draw_rect(x1, y1, x2, y2, angle?) end

---Set the global context color for all subsequent operations
---@param color string a string representing a color name
---@param r number r component of the RGBA for the color
---@param g number g component of the RGBA for the color
---@param b number b component of the RGBA for the color
---@param a number a component of the RGBA for the color
function G.graphics.set_color(color, r, g, b, a) end

---Draws an outlined rectangle with the global context color
---@param x1 number the x coordinate for the top left of the rectangle
---@param y1 number the y position for the top left of the rectangle
---@param x2 number the x position for the bottom right of the rectangle
---@param y2 number the y position for the bottom right of the rectangle
---@param angle? number if provided, the angle to rotate the rectangle
function G.graphics.draw_rect_outline(x1, y1, x2, y2, angle?) end

---Draws a circle with the global context color to the screen
---@param x number the x position (left-right) in screen coordinates of the center of the circle
---@param y number the y position (top-bottom) in screen coordinates of the center of the circle
---@param r number the radius in pixels of the center of the circle
function G.graphics.draw_circle(x, y, r) end

---Draws an outlined circle with the global context color
---@param x number the x position of the center of the circle
---@param y number the y position of the center of the circle
---@param r number the radius in pixels of the circle
function G.graphics.draw_circle_outline(x, y, r) end

---Draws a triangle with the global context color to the screen
---@param p1x number The x coordinate in screen coordinates of the first point of the triangle
---@param p1y number The y coordinate in screen coordinates of the first point of the triangle
---@param p2x number The x coordinate in screen coordinates of the second point of the triangle
---@param p2y number The y coordinate in screen coordinates of the second point of the triangle
---@param p3x number The x coordinate in screen coordinates of the third point of the triangle
---@param p3y number The y coordinate in screen coordinates of the third point of the triangle
function G.graphics.draw_triangle(p1x, p1y, p2x, p2y, p3x, p3y) end

---Draws an outlined triangle with the global context color
---@param p1x number x coordinate of the first point
---@param p1y number y coordinate of the first point
---@param p2x number x coordinate of the second point
---@param p2y number y coordinate of the second point
---@param p3x number x coordinate of the third point
---@param p3y number y coordinate of the third point
function G.graphics.draw_triangle_outline(p1x, p1y, p2x, p2y, p3x, p3y) end

---Draws a filled ellipse with the global context color
---@param x number x position of the center
---@param y number y position of the center
---@param rx number horizontal radius in pixels
---@param ry number vertical radius in pixels
function G.graphics.draw_ellipse(x, y, rx, ry) end

---Draws an outlined ellipse with the global context color
---@param x number x position of the center
---@param y number y position of the center
---@param rx number horizontal radius in pixels
---@param ry number vertical radius in pixels
function G.graphics.draw_ellipse_outline(x, y, rx, ry) end

---Draws a filled rounded rectangle with the global context color
---@param x1 number x coordinate of the top left corner
---@param y1 number y coordinate of the top left corner
---@param x2 number x coordinate of the bottom right corner
---@param y2 number y coordinate of the bottom right corner
---@param radius number corner radius in pixels
function G.graphics.draw_rounded_rect(x1, y1, x2, y2, radius) end

---Draws an outlined rounded rectangle with the global context color
---@param x1 number x coordinate of the top left corner
---@param y1 number y coordinate of the top left corner
---@param x2 number x coordinate of the bottom right corner
---@param y2 number y coordinate of the bottom right corner
---@param radius number corner radius in pixels
function G.graphics.draw_rounded_rect_outline(x1, y1, x2, y2, radius) end

---Draws a line with the global context color to the screen
---@param p1x number The x coordinate in screen coordinates of the first point of the line
---@param p1y number The y coordinate in screen coordinates of the first point of the line
---@param p2x number The x coordinate in screen coordinates of the second point of the line
---@param p2y number The y coordinate in screen coordinates of the second point of the line
function G.graphics.draw_line(p1x, p1y, p2x, p2y) end

---Draws a list of lines with the global context color to the screen
---@param ps table A list of points, two consecutive points i and i+1 for a line.
function G.graphics.draw_lines(ps) end

---Writes text to the screen with debug font and fixed size. For quick debug printing.
---@param text string A string or byte buffer with the contents to render to the screen
---@param x number Horizontal position in screen space pixels left-to-right where to render the text
---@param y number Vertical position in screen space pixels top-to-bottom where to render the text
function G.graphics.print(text, x, y) end

---Writes text to the screen.
---@param font string Font name to use for writing text
---@param size integer Size in pixels to use for rendering the text
---@param text string A string or byte buffer with the contents to render to the screen
---@param x number Horizontal position in screen space pixels left-to-right where to render the text
---@param y number Vertical position in screen space pixels top-to-bottom where to render the text
function G.graphics.draw_text(font, size, text, x, y) end

---Returns the dimensions for a text rendered with a given font and size
---@param font string Font name to use for writing text
---@param size integer Size in pixels that the text would be rendered to the screen
---@param text string A string or byte buffer with the contents that would be rendered to the screen
---@return integer width Width in pixels the text would occupy in the screen
---@return integer height Height in pixels the text would occupy in the screen
function G.graphics.text_dimensions(font, size, text) end

---Draws word-wrapped text within a maximum width, with optional alignment.
---@param font string Font name to use for writing text
---@param size integer Size in pixels to use for rendering the text
---@param text string The text content to render
---@param x number Horizontal position in screen space pixels
---@param y number Vertical position in screen space pixels
---@param max_width number Maximum width in pixels before wrapping to the next line
---@param align? string Text alignment: 'left' (default), 'center', or 'right'
function G.graphics.draw_text_wrapped(font, size, text, x, y, max_width, align?) end

---Returns the total height in pixels that word-wrapped text would occupy.
---@param font string Font name to use for writing text
---@param size integer Size in pixels to use for rendering the text
---@param text string The text content to measure
---@param max_width number Maximum width in pixels before wrapping to the next line
---@return integer height Total height in pixels the wrapped text would occupy
function G.graphics.text_wrapped_height(font, size, text, max_width) end

---Draws multi-color text with optional word wrapping and alignment. The segments table alternates between {r,g,b,a} color tables (0-255) and strings.
---@param font string Font name to use for writing text
---@param size integer Size in pixels to use for rendering the text
---@param segments table Table alternating between {r,g,b,a} color tables and strings
---@param x number Horizontal position in screen space pixels
---@param y number Vertical position in screen space pixels
---@param max_width? number Maximum width in pixels before wrapping (0 or omit for no wrap)
---@param align? string Text alignment: 'left' (default), 'center', or 'right'
function G.graphics.draw_text_colored(font, size, segments, x, y, max_width?, align?) end

---Sets the outline color and thickness for subsequent SDF text draws.
---@param r number Red component (0-255)
---@param g number Green component (0-255)
---@param b number Blue component (0-255)
---@param a number Alpha component (0-255)
---@param thickness number Outline thickness in screen pixels (e.g. 2 = 2px outline)
function G.graphics.set_text_outline(r, g, b, a, thickness) end

---Removes the text outline effect for subsequent text draws.
function G.graphics.clear_text_outline() end

---Save the current transform state onto the stack. Pair with pop() to restore it.
function G.graphics.push() end

---Pop the transform at the top of the stack. It will not apply anymore.
function G.graphics.pop() end

---Apply a rotation to the current transform
---@param angle number All objects will be rotated by this angle in radians clockwise
function G.graphics.rotate(angle) end

---Apply a scale to the current transform
---@param xf number Scalar factor to scale up the x coordinate
---@param yf number Scalar factor to scale up the y coordinate
function G.graphics.scale(xf, yf) end

---Apply a translation to the current transform
---@param x number Horizontal offset in pixels
---@param y number Vertical offset in pixels
function G.graphics.translate(x, y) end

---Creates a new shader with a given name and source code, compiling it in the GPU
---@param shader? string Shader to attach, if nothing is passed then pre_pass.frag will be passed
function G.graphics.new_shader(shader?) end

---Attach a shader by name, if no shader is passed resets to the default shader
---@param shader? string Shader to attach, if nothing is passed then pre_pass.frag will be passed
function G.graphics.attach_shader(shader?) end

---Sends a uniform with the given name to the current shader
---@param name string Name of the uniform to send
---@param value number Value to send. Supported values are G.math.v2,v3,v4, G.math.m2x2, G.math.m3x3, G.math.m4x4, and floats
function G.graphics.send_uniform(name, value) end

---Returns true if the current shader has a uniform with the given name
---@param name string Name of the uniform to check
---@return boolean exists Whether the uniform exists
function G.graphics.has_uniform(name) end

---Clips all subsequent drawing to an axis-aligned rectangle in screen pixels. Does not interact with the transform stack.
---@param x number Left edge in pixels
---@param y number Top edge in pixels
---@param w number Width in pixels
---@param h number Height in pixels
function G.graphics.set_scissor(x, y, w, h) end

---Removes the scissor clipping region so drawing is unrestricted.
function G.graphics.clear_scissor() end

---Begin writing geometry into the stencil buffer. Shapes drawn between stencil_begin and stencil_end are invisible but write a value into the stencil buffer for later masking via set_stencil_test.
---@param action string How to modify the stencil: 'replace', 'increment', 'decrement', or 'invert'
---@param value? integer Reference value to write (default 1)
function G.graphics.stencil_begin(action, value?) end

---Stop writing to the stencil buffer and restore normal color output.
function G.graphics.stencil_end() end

---Enable stencil testing. Subsequent draws only appear where the stencil buffer passes the comparison against the reference value.
---@param compare string Comparison function: 'equal', 'notequal', 'less', 'lequal', 'greater', 'gequal', 'always', or 'never'
---@param ref? integer Reference value to compare against (default 1)
function G.graphics.set_stencil_test(compare, ref?) end

---Disable stencil testing so all subsequent draws appear normally.
function G.graphics.clear_stencil_test() end

---Set the blend mode for subsequent drawing operations
---@param mode string Blend mode: 'alpha' (default), 'add' (additive), 'multiply', or 'replace'
function G.graphics.set_blend_mode(mode) end

---Sets the texture filter mode for newly loaded textures. Use 'nearest' for pixel art (crisp pixels) or 'linear' for smooth graphics.
---@param mode string 'nearest' or 'linear'
function G.graphics.set_default_filter(mode) end

---Create an off-screen canvas for rendering. Returns a canvas object.
---@param width integer Canvas width in pixels
---@param height integer Canvas height in pixels
---@param options? table Optional table with 'filter' key: 'nearest' for pixel art or 'linear' (default)
---@return canvas canvas A canvas object
function G.graphics.new_canvas(width, height, options?) end

---Redirect all subsequent drawing to a canvas. Call with no arguments to reset to the screen.
---@param canvas? canvas Canvas to draw to, or nil/nothing to reset to screen
function G.graphics.set_canvas(canvas?) end

---Draw a canvas as a textured quad. Handles Y-flip automatically. Uses the current blend mode (set via set_blend_mode). For canvases with semi-transparent content, use set_blend_mode('premultiplied') before drawing to avoid alpha darkening artifacts.
---@param canvas canvas The canvas to draw
---@param x number X position to draw at
---@param y number Y position to draw at
---@param angle? number Rotation angle in radians
---@param w? number Width to draw at (default: canvas width)
---@param h? number Height to draw at (default: canvas height)
function G.graphics.draw_canvas(canvas, x, y, angle?, w?, h?) end

---@class G.window
G.window = {}

---Returns the window dimensions in pixels
---@return number width the window width
---@return number height the window height
function G.window.dimensions() end

---Sets the window dimensions
---@param width integer the window width
---@param height integer the window height
function G.window.set_dimensions(width, height) end

---Sets the window to exclusive fullscreen mode
function G.window.set_fullscreen() end

---Sets the window to borderless fullscreen mode
function G.window.set_borderless() end

---Sets the window to windowed mode
function G.window.set_windowed() end

---Sets the window title
---@param title string the new window title
function G.window.set_title(title) end

---Returns the current window title
---@return string title the window title
function G.window.get_title() end

---Returns true if the window has keyboard input focus
---@return boolean focused whether the window has input focus
function G.window.has_input_focus() end

---Returns true if the window has mouse focus
---@return boolean focused whether the window has mouse focus
function G.window.has_mouse_focus() end

---@class G.input
G.input = {}

---Returns the current mouse position
---@return number x x coordinate
---@return number y y coordinate
function G.input.mouse_position() end

---Returns true if the key is currently held down
---@param key string the key name
---@return boolean down whether the key is down
function G.input.is_key_down(key) end

---Returns true if the key was released this frame
---@param key string the key name
---@return boolean released whether the key was released
function G.input.is_key_released(key) end

---Returns true if the key was pressed this frame
---@param key string the key name
---@return boolean pressed whether the key was pressed
function G.input.is_key_pressed(key) end

---Returns the mouse wheel scroll delta
---@return number x horizontal scroll
---@return number y vertical scroll
function G.input.mouse_wheel() end

---Returns true if the mouse button was pressed this frame
---@param button number|string Button number or name ("left", "middle"/"mid", "right")
---@return boolean pressed whether the button was pressed
function G.input.is_mouse_pressed(button) end

---Returns true if the mouse button was released this frame
---@param button number|string Button number or name ("left", "middle"/"mid", "right")
---@return boolean released whether the button was released
function G.input.is_mouse_released(button) end

---Returns true if the mouse button is currently held down
---@param button number|string Button number or name ("left", "middle"/"mid", "right")
---@return boolean down whether the button is down
function G.input.is_mouse_down(button) end

---Returns true if the controller button was pressed this frame
---@param button string the controller button name
---@return boolean pressed whether the button was pressed
function G.input.is_controller_button_pressed(button) end

---Returns true if the controller button is currently held down
---@param button string the controller button name
---@return boolean down whether the button is down
function G.input.is_controller_button_down(button) end

---Returns true if the controller button was released this frame
---@param button string the controller button name
---@return boolean released whether the button was released
function G.input.is_controller_button_released(button) end

---Returns the current position of a controller axis or trigger
---@param axis string the axis or trigger name
---@return number position the axis position
function G.input.get_controller_axis(axis) end

---Binds an action name to one or more input sources, replacing any previous bindings. Sources: "key:<name>", "mouse:<left|middle|right|0|1|2>", "gamepad:<button>", and "touch" (any finger)
---@param action string the action name
---@param bindings string[] array of binding strings
function G.input.bind(action, bindings) end

---Returns the binding strings for an action, exactly as passed to bind; an empty table if the action is unbound
---@param action string the action name
---@return table bindings array of binding strings
function G.input.get_bindings(action) end

---Returns true while any binding of the action is down. The action must have been bound
---@param action string the action name
---@return boolean down whether the action is down
function G.input.is_action_down(action) end

---Returns true the frame the action transitions to down. The action must have been bound
---@param action string the action name
---@return boolean pressed whether the action was pressed this frame
function G.input.is_action_pressed(action) end

---Returns true the frame the action transitions to up. The action must have been bound
---@param action string the action name
---@return boolean released whether the action was released this frame
function G.input.is_action_released(action) end

---Returns the seconds the action has been continuously held, or 0 when it is not down. The action must have been bound
---@param action string the action name
---@return number seconds hold duration in seconds
function G.input.action_time(action) end

---Returns the number of fingers currently touching the screen
---@return number count the number of active touches
function G.input.touch_count() end

---Returns the active touches as an array of {id, x, y, pressure} tables, with positions in viewport coordinates
---@return table touches array of active touches
function G.input.touches() end

---Returns true while any finger is touching the screen
---@return boolean down whether any finger is down
function G.input.is_touch_down() end

---Returns true if any finger began touching this frame
---@return boolean pressed whether any finger began touching
function G.input.is_touch_pressed() end

---Returns true if any finger stopped touching this frame
---@return boolean released whether any finger stopped touching
function G.input.is_touch_released() end

---@class G.math
G.math = {}

---Clamps a value between a minimum and maximum
---@param x number the value to clamp
---@param low number the minimum value
---@param high number the maximum value
---@return number result the clamped value
function G.math.clamp(x, low, high) end

---Linearly interpolates between a and b
---@param a number start value
---@param b number end value
---@param t number interpolation factor (0-1)
---@return number result interpolated value
function G.math.lerp(a, b, t) end

---Returns the interpolation factor for x between a and b
---@param a number start value
---@param b number end value
---@param x number value to find factor for
---@return number t interpolation factor
function G.math.inverse_lerp(a, b, x) end

---Remaps x from range [a1,b1] to range [a2,b2]
---@param x number value to remap
---@param a1 number source range start
---@param b1 number source range end
---@param a2 number target range start
---@param b2 number target range end
---@return number result remapped value
function G.math.remap(x, a1, b1, a2, b2) end

---Returns -1, 0, or 1 depending on the sign of x
---@param x number the value
---@return number result -1, 0, or 1
function G.math.sign(x) end

---Rounds x to the nearest integer
---@param x number the value to round
---@return number result rounded value
function G.math.round(x) end

---Euclidean distance between two 2D points
---@param x1 number first point x
---@param y1 number first point y
---@param x2 number second point x
---@param y2 number second point y
---@return number dist distance
function G.math.distance(x1, y1, x2, y2) end

---Squared distance between two 2D points (no sqrt)
---@param x1 number first point x
---@param y1 number first point y
---@param x2 number second point x
---@param y2 number second point y
---@return number dist2 squared distance
function G.math.distance2(x1, y1, x2, y2) end

---Angle in radians from point (x1,y1) to (x2,y2)
---@param x1 number from x
---@param y1 number from y
---@param x2 number to x
---@param y2 number to y
---@return number radians angle in radians
function G.math.angle(x1, y1, x2, y2) end

---Converts an angle and magnitude to x,y components
---@param angle number angle in radians
---@param magnitude number? length (default 1)
---@return number x x component
---@return number y y component
function G.math.direction(angle, magnitude) end

---Hermite smoothstep interpolation between edge0 and edge1
---@param edge0 number lower edge
---@param edge1 number upper edge
---@param x number value to interpolate
---@return number result smoothed value in [0,1]
function G.math.smoothstep(edge0, edge1, x) end

---Converts degrees to radians
---@param degrees number angle in degrees
---@return number radians angle in radians
function G.math.radians(degrees) end

---Converts radians to degrees
---@param radians number angle in radians
---@return number degrees angle in degrees
function G.math.degrees(radians) end

---Creates a 2D vector
---@param x number x component
---@param y number y component
---@return vec2 vec a new vec2
function G.math.v2(x, y) end

---Creates a 3D vector
---@param x number x component
---@param y number y component
---@param z number z component
---@return vec3 vec a new vec3
function G.math.v3(x, y, z) end

---Creates a 4D vector
---@param x number x component
---@param y number y component
---@param z number z component
---@param w number w component
---@return vec4 vec a new vec4
function G.math.v4(x, y, z, w) end

---Creates a 2x2 matrix from 4 values in row-major order
---@param v1 number value 1
---@param v2 number value 2
---@param v3 number value 3
---@param v4 number value 4
---@return mat2x2 mat a new mat2x2
function G.math.m2x2(v1, v2, v3, v4) end

---Creates a 3x3 matrix from 9 values in row-major order
---@param v1 number value 1
---@param v2 number value 2
---@param v3 number value 3
---@param v4 number value 4
---@param v5 number value 5
---@param v6 number value 6
---@param v7 number value 7
---@param v8 number value 8
---@param v9 number value 9
---@return mat3x3 mat a new mat3x3
function G.math.m3x3(v1, v2, v3, v4, v5, v6, v7, v8, v9) end

---Creates a 4x4 matrix from 16 values in row-major order
---@param v1 number value 1
---@param v2 number value 2
---@param v3 number value 3
---@param v4 number value 4
---@param v5 number value 5
---@param v6 number value 6
---@param v7 number value 7
---@param v8 number value 8
---@param v9 number value 9
---@param v10 number value 10
---@param v11 number value 11
---@param v12 number value 12
---@param v13 number value 13
---@param v14 number value 14
---@param v15 number value 15
---@param v16 number value 16
---@return mat4x4 mat a new mat4x4
function G.math.m4x4(v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, v16) end

---@class G.physics
G.physics = {}

---Adds a dynamic box body to the physics world
---@param tx number top-left x
---@param ty number top-left y
---@param bx number bottom-right x
---@param by number bottom-right y
---@param angle number rotation in radians
---@param callback function collision callback function
---@param options table optional table: density, friction, restitution, sensor, category, mask
---@return physics_handle handle a physics handle for the new body
function G.physics.add_box(tx, ty, bx, by, angle, callback, options) end

---Adds a dynamic circle body to the physics world
---@param tx number center x
---@param ty number center y
---@param radius number the circle radius
---@param callback function collision callback function
---@param options table optional table: density, friction, restitution, sensor, category, mask
---@return physics_handle handle a physics handle for the new body
function G.physics.add_circle(tx, ty, radius, callback, options) end

---Destroys a physics body
---@param handle physics_handle the physics handle to destroy
function G.physics.destroy_handle(handle) end

---Registers named collision categories for string-based filtering
---@param categories table array of category name strings (max 16)
function G.physics.set_collision_categories(categories) end

---Creates a static ground body
---@param walls boolean if true, add edge walls around the screen (default true)
function G.physics.create_ground(walls) end

---Sets a global callback invoked when two bodies begin contact
---@param callback function function called with two collision callbacks
function G.physics.on_begin_contact(callback) end

---Sets a global callback invoked when two bodies stop touching. Fires for sensor exits as well as regular contacts.
---@param callback function function called with the two userdata values
function G.physics.on_end_contact(callback) end

---Returns the position of a physics body
---@param handle physics_handle the physics handle
---@return number x x position
---@return number y y position
function G.physics.position(handle) end

---Teleports a physics body to a new position
---@param handle physics_handle the physics handle
---@param x number new x position
---@param y number new y position
function G.physics.set_position(handle, x, y) end

---Returns the rotation angle of a physics body in radians
---@param handle physics_handle the physics handle
---@return number angle the angle in radians
function G.physics.angle(handle) end

---Sets the rotation angle of a physics body
---@param handle physics_handle the physics handle
---@param angle number the angle in radians
function G.physics.rotate(handle, angle) end

---Applies a linear impulse to a physics body
---@param handle physics_handle the physics handle
---@param x number impulse x component
---@param y number impulse y component
function G.physics.apply_linear_impulse(handle, x, y) end

---Applies a continuous force to a physics body
---@param handle physics_handle the physics handle
---@param x number force x component
---@param y number force y component
function G.physics.apply_force(handle, x, y) end

---Applies a torque to a physics body
---@param handle physics_handle the physics handle
---@param torque number the torque value
function G.physics.apply_torque(handle, torque) end

---Returns the linear velocity of a physics body in pixels/s
---@param handle physics_handle the physics handle
---@return number vx x velocity
---@return number vy y velocity
function G.physics.linear_velocity(handle) end

---Sets the linear velocity of a physics body in pixels/s
---@param handle physics_handle the physics handle
---@param vx number x velocity
---@param vy number y velocity
function G.physics.set_linear_velocity(handle, vx, vy) end

---Returns the angular velocity of a physics body in rad/s
---@param handle physics_handle the physics handle
---@return number omega angular velocity
function G.physics.angular_velocity(handle) end

---Sets the angular velocity of a physics body in rad/s
---@param handle physics_handle the physics handle
---@param omega number angular velocity
function G.physics.set_angular_velocity(handle, omega) end

---Sets the linear damping (drag) of a physics body
---@param handle physics_handle the physics handle
---@param damping number damping coefficient (>= 0)
function G.physics.set_linear_damping(handle, damping) end

---Sets the angular damping (rotational drag) of a physics body
---@param handle physics_handle the physics handle
---@param damping number damping coefficient (>= 0)
function G.physics.set_angular_damping(handle, damping) end

---Scales the effect of world gravity on a physics body
---@param handle physics_handle the physics handle
---@param scale number gravity multiplier (0 = none, 1 = normal)
function G.physics.set_gravity_scale(handle, scale) end

---Enables continuous collision detection (CCD) for a fast-moving body
---@param handle physics_handle the physics handle
---@param bullet boolean true to enable CCD
function G.physics.set_bullet(handle, bullet) end

---Prevents or allows a physics body from rotating
---@param handle physics_handle the physics handle
---@param fixed boolean true to lock rotation
function G.physics.set_fixed_rotation(handle, fixed) end

---Returns whether a physics body has fixed rotation
---@param handle physics_handle the physics handle
---@return boolean fixed true if rotation is locked
function G.physics.get_fixed_rotation(handle) end

---Sets the world gravity vector in pixels/s^2
---@param gx number gravity x component
---@param gy number gravity y component
function G.physics.set_gravity(gx, gy) end

---Returns the world gravity vector in pixels/s^2
---@return number gx gravity x component
---@return number gy gravity y component
function G.physics.gravity() end

---Sets the solver iteration counts per time step
---@param velocity number velocity solver iterations (default 6)
---@param position number position solver iterations (default 2)
function G.physics.set_iterations(velocity, position) end

---Returns the pixels-per-meter scale factor
---@return number ppm the scale factor
function G.physics.pixels_per_meter() end

---Casts a ray and returns the closest hit, or nil if nothing was hit
---@param x1 number ray start x
---@param y1 number ray start y
---@param x2 number ray end x
---@param y2 number ray end y
---@param mask number collision mask filter (default 0xFFFF)
---@return table|nil hit table with handle, x, y, nx, ny, fraction fields, or nil
function G.physics.raycast(x1, y1, x2, y2, mask) end

---Casts a ray and returns all hits sorted by distance
---@param x1 number ray start x
---@param y1 number ray start y
---@param x2 number ray end x
---@param y2 number ray end y
---@param mask number collision mask filter (default 0xFFFF)
---@return table hits array of hit tables
function G.physics.raycast_all(x1, y1, x2, y2, mask) end

---Creates a revolute (hinge) joint between two bodies
---@param body_a physics_handle first body
---@param body_b physics_handle second body
---@param anchor_x number world-space anchor x (pixels)
---@param anchor_y number world-space anchor y (pixels)
---@param options? table optional: enable_limit, lower_angle, upper_angle, enable_motor, motor_speed, max_motor_torque, collide_connected
---@return joint_handle joint a joint handle
function G.physics.create_revolute_joint(body_a, body_b, anchor_x, anchor_y, options?) end

---Creates a distance (spring) joint between two bodies
---@param body_a physics_handle first body
---@param body_b physics_handle second body
---@param ax1 number anchor A x (pixels)
---@param ay1 number anchor A y (pixels)
---@param ax2 number anchor B x (pixels)
---@param ay2 number anchor B y (pixels)
---@param options? table optional: length, frequency, damping_ratio, collide_connected
---@return joint_handle joint a joint handle
function G.physics.create_distance_joint(body_a, body_b, ax1, ay1, ax2, ay2, options?) end

---Creates a weld (rigid) joint between two bodies
---@param body_a physics_handle first body
---@param body_b physics_handle second body
---@param anchor_x number world-space anchor x (pixels)
---@param anchor_y number world-space anchor y (pixels)
---@param options? table optional: frequency, damping_ratio, collide_connected
---@return joint_handle joint a joint handle
function G.physics.create_weld_joint(body_a, body_b, anchor_x, anchor_y, options?) end

---Creates a prismatic (slider) joint between two bodies
---@param body_a physics_handle first body
---@param body_b physics_handle second body
---@param anchor_x number world-space anchor x (pixels)
---@param anchor_y number world-space anchor y (pixels)
---@param axis_x number slide axis x component
---@param axis_y number slide axis y component
---@param options? table optional: enable_limit, lower_translation, upper_translation, enable_motor, motor_speed, max_motor_force, collide_connected
---@return joint_handle joint a joint handle
function G.physics.create_prismatic_joint(body_a, body_b, anchor_x, anchor_y, axis_x, axis_y, options?) end

---Creates a mouse (drag) joint that pulls a body toward a target point
---@param body physics_handle the body to drag
---@param target_x number initial target x (pixels)
---@param target_y number initial target y (pixels)
---@param options? table optional: max_force, frequency, damping_ratio
---@return joint_handle joint a joint handle
function G.physics.create_mouse_joint(body, target_x, target_y, options?) end

---Creates a wheel (vehicle suspension) joint between two bodies
---@param body_a physics_handle chassis body
---@param body_b physics_handle wheel body
---@param anchor_x number world-space anchor x (pixels)
---@param anchor_y number world-space anchor y (pixels)
---@param axis_x number suspension axis x component
---@param axis_y number suspension axis y component
---@param options? table optional: enable_motor, motor_speed, max_motor_torque, frequency, damping_ratio, collide_connected
---@return joint_handle joint a joint handle
function G.physics.create_wheel_joint(body_a, body_b, anchor_x, anchor_y, axis_x, axis_y, options?) end

---Sets the absolute rotation angle of a physics body
---@param handle physics_handle the physics handle
---@param angle number angle in radians (0=right, increases counter-clockwise)
function G.physics.set_angle(handle, angle) end

---Sets velocity to move a body toward a target point at a given speed
---@param handle physics_handle the physics handle
---@param target_x number target x position in pixels
---@param target_y number target y position in pixels
---@param speed number movement speed in pixels/second
function G.physics.move_toward(handle, target_x, target_y, speed) end

---Sets a body's angle to face toward a target point
---@param handle physics_handle the physics handle
---@param target_x number target x position in pixels
---@param target_y number target y position in pixels
function G.physics.look_at(handle, target_x, target_y) end

---Applies a continuous force in world coordinates (not body-local)
---@param handle physics_handle the physics handle
---@param x number force x component (world space)
---@param y number force y component (world space)
function G.physics.apply_force_world(handle, x, y) end

---@class G.random
G.random = {}

---Deterministically creates a random number generator from a seed
---@param seed integer integer with seed number for the rng
---@return rng rng random number generator
function G.random.from_seed(seed) end

---Creates a random number generator from a non deterministic seed
---@return rng rng random number generator
function G.random.non_deterministic() end

---Samples a random number generator in a range. If no range is provided it uses 32 bit integers.
---@param rng rng rng from `from_seed` or `non_deterministic`
---@param start? number start of the range to sample.
---@param end? number end of the range to sample. Must be provided if start is provided.
---@return number result an integer in the range provided
function G.random.sample(rng, start?, end?) end

---Picks an element from a list using a random number generator
---@param rng rng rng from `from_seed` or `non_deterministic`
---@param list table list to pick elements from. Must be non empty.
---@return any result an element from the list
function G.random.pick(rng, list) end

---@class G.sound
G.sound = {}

---Adds an audio source from an asset name
---@param name string name of the asset to play.
---@return integer source a handle for the source
function G.sound.add_source(name) end

---Plays an audio asset on the music channel.
---@param source integer source id of the sound to play
function G.sound.play_source(source) end

---Loads and immediately plays an audio asset on the music channel.
---@param name string name of the sound asset to play.
function G.sound.play(name) end

---Sets the volume a source
---@param source integer source id of the asset to modify
---@param gain number the gain for the channel, must be a number between 0 and 1
function G.sound.set_volume(source, gain) end

---Sets the global volume
---@param gain number the global gain between 0 and 1
function G.sound.set_global_volume(gain) end

---Stops sound source and rewinds it
---@param source integer source id to stop
function G.sound.stop_source(source) end

---Adds a sound effect (fully decoded upfront for low-latency playback).
---@param name string name of the sound asset.
---@return integer source a handle for the source
function G.sound.add_effect(name) end

---Loads, decodes, and immediately plays a sound effect.
---@param name string name of the sound asset.
function G.sound.play_effect(name) end

---Enables or disables looping for a source.
---@param source integer source id to modify
---@param loop boolean true to enable looping, false to disable
function G.sound.set_loop(source, loop) end

---Pauses a source without rewinding it.
---@param source integer source id to pause
function G.sound.pause(source) end

---Resumes a paused source from where it stopped.
---@param source integer source id to resume
function G.sound.resume(source) end

---Returns whether a source is currently playing.
---@param source integer source id to query
---@return boolean playing true if the source is playing
function G.sound.is_playing(source) end

---Sets the playback pitch/speed for a source.
---@param source integer source id to modify
---@param pitch number pitch multiplier (0.25 to 4.0, 1.0 = normal)
function G.sound.set_pitch(source, pitch) end

---Sets the stereo panning for a source.
---@param source integer source id to modify
---@param pan number pan position (-1.0 = left, 0.0 = center, 1.0 = right)
function G.sound.set_pan(source, pan) end

---@class G.system
G.system = {}

---Quits the game
function G.system.quit() end

---Returns the name of the operating system
---@return string os the operating system name
function G.system.operating_system() end

---Returns the number of logical CPU cores
---@return integer count the number of CPUs
function G.system.cpu_count() end

---Sets the system clipboard text
---@param text string the text to copy to the clipboard
function G.system.set_clipboard(text) end

---Opens a URL in the default browser
---@param url string the URL to open
---@return string error nil on success, error message on failure
function G.system.open_url(url) end

---Returns the command-line arguments as a table
---@return table args table of argument strings
function G.system.cli_arguments() end

---Returns the current system clipboard text
---@return string text the clipboard contents
function G.system.get_clipboard() end

---Sets the time scale multiplier (0 = paused, 0.5 = half, 1 = normal, 2 = double)
---@param scale number the time scale multiplier
function G.system.set_time_scale(scale) end

---Returns the current time scale multiplier
---@return number scale the time scale multiplier
function G.system.get_time_scale() end

---Returns the unscaled delta time (useful for UI unaffected by time scale)
---@return number dt unscaled delta time in seconds
function G.system.get_real_dt() end

---Returns the unscaled elapsed time in seconds
---@return number time unscaled elapsed time in seconds
function G.system.get_real_time() end

---@class G.clock
G.clock = {}

---Returns the wall clock time in seconds
---@return number time time in seconds
function G.clock.walltime() end

---Returns the elapsed game time in seconds
---@return number time game time in seconds
function G.clock.gametime() end

---Sleeps for the given number of milliseconds
---@param ms number the number of milliseconds to sleep
function G.clock.sleep_ms(ms) end

---Returns the time elapsed since the last frame in seconds
---@return number dt delta time in seconds
function G.clock.gamedelta() end

---Calls a function and records its execution time under a named zone
---@param name string zone name for profiling
---@param fn function function to execute
function G.clock.zone(name, fn) end

---@class G.assets
G.assets = {}

---Returns a sprite object ptr by name. Returns nil if it cannot find.
---@param name string name of the sprite to fetch
---@return sprite_asset result A userdata ptr to a sprite object
function G.assets.sprite(name) end

---Returns a table with width and height in pixels of a sprite.
---@param name string sprite object ptr or sprite name as string
---@return table result A table with two keys, width and height
function G.assets.sprite_info(name) end

---Returns a list with all images
---@return table result A list with name, width, height of all images.
function G.assets.list_images() end

---Returns a list with all sprites
---@return table result A list with width, height, x, y position and spritesheet name of all sprites.
function G.assets.list_sprites() end

---@class G.collision
G.collision = {}

---Creates a new collision world
---@param cell_size number? Spatial hash cell size in pixels (default 64)
---@return collision_world world The collision world
function G.collision.new_world(cell_size) end

---Creates a circle collision shape
---@param radius number Circle radius in pixels
---@return collision_shape shape The collision shape
function G.collision.circle(radius) end

---Creates an axis-aligned bounding box collision shape
---@param width number Box width in pixels
---@param height number Box height in pixels
---@return collision_shape shape The collision shape
function G.collision.aabb(width, height) end

---Tests two shapes for collision without needing a world
---@param shape_a collision_shape First shape
---@param ax number First shape x position
---@param ay number First shape y position
---@param shape_b collision_shape Second shape
---@param bx number Second shape x position
---@param by number Second shape y position
---@return boolean hit Whether the shapes overlap
---@return number? nx Collision normal x (if hit)
---@return number? ny Collision normal y (if hit)
---@return number? depth Penetration depth (if hit)
function G.collision.test(shape_a, ax, ay, shape_b, bx, by) end

---@class G.json
G.json = {}

---Parse a JSON string into a Lua value
---@param str string JSON string to parse
---@return string error nil on success, error message on failure
---@return any result Parsed Lua value on success, nil on failure
function G.json.decode(str) end

---Serialize a Lua value to a JSON string
---@param value any Lua value to serialize
---@return string error nil on success, error message on failure
---@return string result JSON string on success, nil on failure
function G.json.encode(value) end

---@class G.test
G.test = {}

---Injects a key-down event for the named key.
---@param key string the key name
function G.test.key_down(key) end

---Injects a key-up event for the named key.
---@param key string the key name
function G.test.key_up(key) end

---Injects a mouse-button-down event. Buttons: 0=left, 1=middle, 2=right.
---@param button number mouse button index
function G.test.mouse_down(button) end

---Injects a mouse-button-up event.
---@param button number mouse button index
function G.test.mouse_up(button) end

---Sets the synthetic mouse position.
---@param x number x coordinate
---@param y number y coordinate
function G.test.mouse_move(x, y) end

---Adds to the synthetic mouse wheel delta for this frame.
---@param dx number horizontal scroll
---@param dy number vertical scroll
function G.test.mouse_wheel(dx, dy) end

---Synthetically starts a finger contact at viewport coordinates.
---@param id number finger identifier
---@param x number x position in viewport coordinates
---@param y number y position in viewport coordinates
---@param pressure number? normalized pressure (default 1)
function G.test.touch_down(id, x, y, pressure) end

---Moves a synthetic finger contact to viewport coordinates.
---@param id number finger identifier
---@param x number x position in viewport coordinates
---@param y number y position in viewport coordinates
function G.test.touch_move(id, x, y) end

---Ends a synthetic finger contact.
---@param id number finger identifier
function G.test.touch_up(id) end

---Injects a controller button-down event by name.
---@param button string the controller button name
function G.test.controller_down(button) end

---Injects a controller button-up event by name.
---@param button string the controller button name
function G.test.controller_up(button) end

---Sets a synthetic controller axis or trigger value.
---@param axis string the axis or trigger name
---@param value number the axis position
function G.test.controller_axis(axis, value) end

---Yields the test coroutine for the given number of frames.
---@param n number number of frames to wait
function G.test.wait_frames(n) end

---Yields the test coroutine until the given number of seconds has elapsed (rounded up to whole frames).
---@param seconds number duration to wait
function G.test.wait_seconds(seconds) end

---Returns true if the engine is running under --test (a test coroutine is driving input).
---@return boolean active whether test mode is active
function G.test.is_active() end

---Errors out the test coroutine if the condition is false.
---@param cond boolean condition
---@param msg? string optional failure message
function G.test.assert_true(cond, msg?) end

---@class G.timer
G.timer = {}

---Fires a callback once after a delay
---@param delay number seconds to wait
---@param action function function to call
---@param tag? string optional string tag for cancellation
---@return number tag numeric tag for this timer
function G.timer.after(delay, action, tag?) end

---Fires a callback repeatedly at an interval
---@param delay number seconds between fires
---@param action function function to call
---@param times? number number of repetitions (0 or nil = infinite)
---@param tag? string optional string tag for cancellation
---@return number tag numeric tag for this timer
function G.timer.every(delay, action, times?, tag?) end

---Calls a function every frame for a duration
---@param duration number seconds to run
---@param action function function(dt, elapsed, fraction) called each frame
---@param after? function function called when duration ends
---@param tag? string optional string tag for cancellation
---@return number tag numeric tag for this timer
function G.timer.during(duration, action, after?, tag?) end

---Tweens table fields toward target values over a duration
---@param duration number seconds for the tween
---@param subject table table whose fields will be modified
---@param target table table of {key = end_value} pairs
---@param easing? string easing name (default 'linear')
---@param after? function completion callback
---@param tag? string optional string tag
---@return number tag numeric tag for this timer
function G.timer.tween(duration, subject, target, easing?, after?, tag?) end

---Fires when both delay elapsed AND condition is true
---@param delay number seconds between checks
---@param condition function function returning bool
---@param action function function to fire
---@param times? number repetitions (0 or nil = infinite)
---@param tag? string optional string tag
---@return number tag numeric tag for this timer
function G.timer.cooldown(delay, condition, action, times?, tag?) end

---Cancels a timer by tag
---@param tag string string tag of the timer to cancel
function G.timer.cancel(tag) end

---Cancels all active timers
function G.timer.cancel_all() end

---Checks if a timer with the given tag exists
---@param tag string string tag to check
---@return boolean exists true if timer exists
function G.timer.exists(tag) end

---Makes a timer ignore time scale (run in real time)
---@param tag string string tag of the timer
---@param real_time boolean true to ignore time scale
function G.timer.set_real_time(tag, real_time) end

---@class G.scene
G.scene = {}

---Registers a scene by name
---@param name string unique scene name
---@param scene table scene table with lifecycle methods
function G.scene.register(name, scene) end

---Switches to a named scene (deferred to next frame)
---@param name string scene name to switch to
---@param ... any data passed to enter()
function G.scene.switch(name, ...) end

---Pushes a scene onto the stack (overlay)
---@param name string scene name to push
---@param ... any data passed to enter()
function G.scene.push(name, ...) end

---Pops the top scene off the stack
---@param ... any data passed to resume()
function G.scene.pop(...) end

---Returns the name of the currently active scene
---@return string? name current scene name or nil
function G.scene.current() end

---Returns the number of scenes on the stack
---@return integer count stack depth
function G.scene.depth() end

---Draws the scene below the current one on the stack
function G.scene.draw_below() end

---@class G.particles
G.particles = {}

---Creates a new particle emitter from a definition table
---@param def table Emitter definition table
---@return particle_emitter emitter The particle emitter
function G.particles.new_emitter(def) end

---@class G.save
G.save = {}

---Stores a value in the save database
---@param namespace string Key namespace (e.g. "save", "settings")
---@param key string Key name
---@param value any Value to store (nil, bool, number, string, or table)
function G.save.set(namespace, key, value) end

---Retrieves a value from the save database
---@param namespace string Key namespace
---@param key string Key name
---@return any value The stored value, or nil if not found
function G.save.get(namespace, key) end

---Checks if a key exists in the save database
---@param namespace string Key namespace
---@param key string Key name
---@return boolean exists True if the key exists
function G.save.has(namespace, key) end

---Deletes a key from the save database
---@param namespace string Key namespace
---@param key string Key name
function G.save.delete(namespace, key) end

---Returns all key-value pairs in a namespace as a table
---@param namespace string Key namespace
---@return table entries Table of {key = value, ...}
function G.save.list(namespace) end

---Returns all keys in a namespace as an array
---@param namespace string Key namespace
---@return table keys Array of key names
function G.save.keys(namespace) end

---Deletes all keys in a namespace
---@param namespace string Key namespace
function G.save.clear(namespace) end

---Returns all namespace names as an array
---@return table names Array of namespace names
function G.save.namespaces() end

---Checkpoints the WAL to the main database file
function G.save.flush() end

---@class G.tilemap
G.tilemap = {}

---Creates a new tilemap
---@param config table Config table with tile_width, tile_height, and optional tileset fields
---@return tilemap tilemap The new tilemap
function G.tilemap.new(config) end

---Loads a tilemap from a Tiled TMX file
---@param filename string TMX asset filename
---@param gid_offset integer Tiled GID offset for the tile tileset (firstgid value minus 1)
---@return tilemap tilemap The loaded tilemap
function G.tilemap.load_tmx(filename, gid_offset) end

---A binary data buffer
---@class byte_buffer
---@operator len: integer
---@operator concat(any): string
local byte_buffer = {}

---A 2D floating-point vector
---@class vec2
---@operator add(vec2): vec2
---@operator sub(vec2): vec2
---@operator mul(number): vec2
---@operator unm(): vec2
local vec2 = {}

---Distance to another vector
---@param other vec2 the other vector
---@return number result distance
function vec2:distance(other) end

---Squared distance to another vector
---@param other vec2 the other vector
---@return number result squared distance
function vec2:distance2(other) end

---Angle of this vector in radians (atan2(y, x))
---@return number radians angle
function vec2:angle() end

---Angle from this vector to another
---@param other vec2 the other vector
---@return number radians angle
function vec2:angle_between(other) end

---Returns a copy rotated by the given angle
---@param angle number rotation in radians
---@return vec2 result rotated vector
function vec2:rotate(angle) end

---Returns the perpendicular vector (-y, x)
---@return vec2 result perpendicular vector
function vec2:perpendicular() end

---Reflects this vector off a surface normal
---@param normal vec2 surface normal
---@return vec2 result reflected vector
function vec2:reflect(normal) end

---Projects this vector onto another
---@param onto vec2 vector to project onto
---@return vec2 result projected vector
function vec2:project(onto) end

---A 3D floating-point vector
---@class vec3
---@operator add(vec3): vec3
---@operator sub(vec3): vec3
---@operator mul(number): vec3
---@operator unm(): vec3
local vec3 = {}

---Dot product with another vector
---@param other vec3 the other vector
---@return number result dot product
function vec3:dot(other) end

---Squared length of the vector
---@return number result squared length
function vec3:len2() end

---Length (magnitude) of the vector
---@return number result length
function vec3:length() end

---Returns a normalized copy of the vector
---@return vec3 result normalized vector
function vec3:normalized() end

---Linearly interpolates between this vector and another
---@param other vec3 target vector
---@param t number interpolation factor (0-1)
---@return vec3 result interpolated vector
function vec3:lerp(other, t) end

---Returns the vector components as separate numbers
---@return number x x component
---@return number y y component
function vec3:unpack() end

---Sends this value as a shader uniform. Errors if uniform not found.
---@param name string uniform name
function vec3:send_as_uniform(name) end

---A 4D floating-point vector
---@class vec4
---@operator add(vec4): vec4
---@operator sub(vec4): vec4
---@operator mul(number): vec4
---@operator unm(): vec4
local vec4 = {}

---Dot product with another vector
---@param other vec4 the other vector
---@return number result dot product
function vec4:dot(other) end

---Squared length of the vector
---@return number result squared length
function vec4:len2() end

---Length (magnitude) of the vector
---@return number result length
function vec4:length() end

---Returns a normalized copy of the vector
---@return vec4 result normalized vector
function vec4:normalized() end

---Linearly interpolates between this vector and another
---@param other vec4 target vector
---@param t number interpolation factor (0-1)
---@return vec4 result interpolated vector
function vec4:lerp(other, t) end

---Returns the vector components as separate numbers
---@return number x x component
---@return number y y component
function vec4:unpack() end

---Sends this value as a shader uniform. Errors if uniform not found.
---@param name string uniform name
function vec4:send_as_uniform(name) end

---A 2x2 floating-point matrix
---@class mat2x2
local mat2x2 = {}

---Sends this matrix as a shader uniform. Errors if uniform not found.
---@param name string uniform name
function mat2x2:send_as_uniform(name) end

---A 3x3 floating-point matrix
---@class mat3x3
local mat3x3 = {}

---Sends this matrix as a shader uniform. Errors if uniform not found.
---@param name string uniform name
function mat3x3:send_as_uniform(name) end

---A 4x4 floating-point matrix
---@class mat4x4
local mat4x4 = {}

---Sends this matrix as a shader uniform. Errors if uniform not found.
---@param name string uniform name
function mat4x4:send_as_uniform(name) end

---An opaque handle to a physics body
---@class physics_handle
local physics_handle = {}

---An opaque handle to a physics joint
---@class joint_handle
local joint_handle = {}

---Destroys this joint
function joint_handle:destroy() end

---Returns true if this joint handle is still valid
---@return boolean valid whether the joint exists
function joint_handle:is_valid() end

---Returns the joint type name
---@return string type joint type string
function joint_handle:get_type() end

---Returns the revolute joint angle in radians
---@return number angle angle in radians
function joint_handle:get_joint_angle() end

---Returns the joint speed (revolute: rad/s, prismatic: pixels/s)
---@return number speed joint speed
function joint_handle:get_joint_speed() end

---Returns the prismatic joint translation in pixels
---@return number translation translation in pixels
function joint_handle:get_joint_translation() end

---Returns the current distance joint length in pixels
---@return number length current length in pixels
function joint_handle:get_current_length() end

---Sets the motor speed
---@param speed number motor speed
function joint_handle:set_motor_speed(speed) end

---Enables or disables the joint motor
---@param enabled boolean whether to enable
function joint_handle:enable_motor(enabled) end

---Enables or disables joint limits
---@param enabled boolean whether to enable
function joint_handle:enable_limit(enabled) end

---Sets joint limits (revolute: radians, prismatic: pixels)
---@param lower number lower limit
---@param upper number upper limit
function joint_handle:set_limits(lower, upper) end

---Sets max motor torque (revolute, wheel)
---@param torque number max torque
function joint_handle:set_max_motor_torque(torque) end

---Sets max motor force (prismatic)
---@param force number max force
function joint_handle:set_max_motor_force(force) end

---Sets the rest length (distance joint, pixels)
---@param length number rest length in pixels
function joint_handle:set_length(length) end

---Sets the mouse joint target position
---@param x number target x (pixels)
---@param y number target y (pixels)
function joint_handle:set_target(x, y) end

---Sets the max force (mouse joint)
---@param force number max force
function joint_handle:set_max_force(force) end

---Sets the spring frequency in Hz
---@param hz number frequency in Hz
function joint_handle:set_frequency(hz) end

---Sets the damping ratio (0-1)
---@param ratio number damping ratio
function joint_handle:set_damping_ratio(ratio) end

---A random number generator
---@class rng
local rng = {}

---A reference to a sprite asset
---@class sprite_asset
local sprite_asset = {}

---A collision detection world with spatial hashing
---@class collision_world
local collision_world = {}

---Adds a collider to the world
---@param shape collision_shape Collision shape
---@param x number X position
---@param y number Y position
---@param opts table? Options table: category, mask, trigger, userdata
---@return collision_handle handle Handle to the new collider
function collision_world:add(shape, x, y, opts) end

---Removes a collider from the world
---@param handle collision_handle Handle to remove
function collision_world:remove(handle) end

---Sets the position of a collider
---@param handle collision_handle Collider handle
---@param x number X position
---@param y number Y position
function collision_world:set_position(handle, x, y) end

---Gets the position of a collider
---@param handle collision_handle Collider handle
---@return number x X position
---@return number y Y position
function collision_world:get_position(handle) end

---Sets the shape of a collider
---@param handle collision_handle Collider handle
---@param shape collision_shape New shape
function collision_world:set_shape(handle, shape) end

---Sets collision filter for a collider
---@param handle collision_handle Collider handle
---@param category integer Category bitmask
---@param mask integer Detection mask
function collision_world:set_filter(handle, category, mask) end

---Gets the userdata associated with a collider
---@param handle collision_handle Collider handle
---@return any userdata The stored userdata value
function collision_world:get_userdata(handle) end

---Moves a collider with sliding collision resolution
---@param handle collision_handle Collider handle
---@param vx number X velocity
---@param vy number Y velocity
---@return number x Final x position
---@return number y Final y position
---@return table contacts Array of contact info
function collision_world:move_and_slide(handle, vx, vy) end

---Moves a collider until first collision
---@param handle collision_handle Collider handle
---@param vx number X velocity
---@param vy number Y velocity
---@return number x Final x position
---@return number y Final y position
---@return table? contact Contact info or nil
function collision_world:move_and_collide(handle, vx, vy) end

---Moves a collider toward a target position with collision
---@param handle collision_handle Collider handle
---@param target_x number Target x position
---@param target_y number Target y position
---@param speed number Movement speed in pixels per second
---@param dt number Delta time in seconds
---@return number x Final x position
---@return number y Final y position
---@return table? contact Contact info or nil
function collision_world:move_toward(handle, target_x, target_y, speed, dt) end

---Gets all shapes overlapping this collider
---@param handle collision_handle Collider handle
---@return table overlaps Array of overlap info
function collision_world:get_overlaps(handle) end

---Casts a ray and returns the closest hit
---@param ox number Origin x
---@param oy number Origin y
---@param dx number Direction x
---@param dy number Direction y
---@param max_dist number Maximum distance
---@param mask integer? Filter mask (default 0xFFFF)
---@return table? hit Hit info or nil
function collision_world:raycast(ox, oy, dx, dy, max_dist, mask) end

---Casts a ray and returns all hits sorted by distance
---@param ox number Origin x
---@param oy number Origin y
---@param dx number Direction x
---@param dy number Direction y
---@param max_dist number Maximum distance
---@param mask integer? Filter mask (default 0xFFFF)
---@return table hits Array of hit info sorted by t
function collision_world:raycast_all(ox, oy, dx, dy, max_dist, mask) end

---Finds all colliders containing a point
---@param x number Point x
---@param y number Point y
---@param mask integer? Filter mask (default 0xFFFF)
---@return table handles Array of collider handles
function collision_world:query_point(x, y, mask) end

---Finds all colliders overlapping a rectangle
---@param x1 number Min x
---@param y1 number Min y
---@param x2 number Max x
---@param y2 number Max y
---@param mask integer? Filter mask (default 0xFFFF)
---@return table handles Array of collider handles
function collision_world:query_rect(x1, y1, x2, y2, mask) end

---Finds all colliders overlapping a circle
---@param cx number Center x
---@param cy number Center y
---@param radius number Circle radius
---@param mask integer? Filter mask (default 0xFFFF)
---@return table handles Array of collider handles
function collision_world:query_circle(cx, cy, radius, mask) end

---Sets callback for trigger enter events
---@param fn function Callback function(handle_a, handle_b)
function collision_world:on_trigger_enter(fn) end

---Sets callback for trigger exit events
---@param fn function Callback function(handle_a, handle_b)
function collision_world:on_trigger_exit(fn) end

---Updates the collision world (rebuilds broad phase, fires triggers)
function collision_world:update() end

---An opaque handle to a collider in a collision world
---@class collision_handle
local collision_handle = {}

---A collision shape (circle or AABB)
---@class collision_shape
local collision_shape = {}

---A particle emitter that manages a pool of particles
---@class particle_emitter
local particle_emitter = {}

---Sets the emitter position
---@param x number X position
---@param y number Y position
function particle_emitter:set_position(x, y) end

---Gets the emitter position
---@return number x X position
---@return number y Y position
function particle_emitter:get_position() end

---Starts continuous particle emission
function particle_emitter:start() end

---Stops continuous particle emission
function particle_emitter:stop() end

---Advances the particle simulation by dt seconds
---@param dt number Time step in seconds
function particle_emitter:update(dt) end

---Spawns particles immediately
---@param count integer Number of particles
---@param x number? Optional x position
---@param y number? Optional y position
function particle_emitter:burst(count, x, y) end

---Draws all live particles
function particle_emitter:draw() end

---Returns the number of live particles
---@return integer count Live particle count
function particle_emitter:particle_count() end

---Returns whether the emitter is actively spawning
---@return boolean active True if active
function particle_emitter:is_active() end

---Changes the emission rate
---@param rate number Particles per second
function particle_emitter:set_emission_rate(rate) end

---Changes the emission direction
---@param angle number Direction in radians
function particle_emitter:set_direction(angle) end

---Changes the emission spread angle
---@param spread number Half-angle in radians
function particle_emitter:set_spread(spread) end

---Changes the gravity force
---@param gx number Gravity X
---@param gy number Gravity Y
function particle_emitter:set_gravity(gx, gy) end

---A 2D tilemap with layers and tile collision
---@class tilemap
local tilemap = {}

---Adds a new layer to the tilemap
---@param name string Layer name
---@param width integer Width in tiles
---@param height integer Height in tiles
---@param opts table? Options table with optional collision=true
---@return integer index Layer index (1-based)
function tilemap:add_layer(name, width, height, opts) end

---Sets a tile ID at the given coordinates in a layer
---@param layer string Layer name
---@param x integer Tile x coordinate
---@param y integer Tile y coordinate
---@param tile_id integer Tile ID (0 = empty)
function tilemap:set_tile(layer, x, y, tile_id) end

---Gets the tile ID at the given coordinates in a layer
---@param layer string Layer name
---@param x integer Tile x coordinate
---@param y integer Tile y coordinate
---@return integer tile_id Tile ID (0 = empty)
function tilemap:get_tile(layer, x, y) end

---Draws all visible layers with camera-based viewport culling
function tilemap:draw() end

---Draws a single layer by name
---@param name string Layer name
function tilemap:draw_layer(name) end

---Draws a single tile from the tileset at an arbitrary world position
---@param tile_id integer Tile ID (1-based)
---@param x number World x position
---@param y number World y position
function tilemap:draw_tile(tile_id, x, y) end

---Returns the tile ID at a world position from the collision layer
---@param x number World x position
---@param y number World y position
---@return integer tile_id Tile ID or 0
function tilemap:tile_at(x, y) end

---Returns true if the world position overlaps a solid tile
---@param x number World x position
---@param y number World y position
---@return boolean solid Whether the tile is solid
function tilemap:is_solid(x, y) end

---Converts world coordinates to tile coordinates
---@param x number World x position
---@param y number World y position
---@return integer tx Tile x coordinate
---@return integer ty Tile y coordinate
function tilemap:world_to_tile(x, y) end

---Converts tile coordinates to world coordinates (top-left corner)
---@param tx integer Tile x coordinate
---@param ty integer Tile y coordinate
---@return number x World x position
---@return number y World y position
function tilemap:tile_to_world(tx, ty) end

---Moves an AABB through the tilemap with collision resolution
---@param x number Current x position
---@param y number Current y position
---@param w number AABB width
---@param h number AABB height
---@param vx number X velocity
---@param vy number Y velocity
---@return number nx Final x position
---@return number ny Final y position
---@return table? hit Collision info or nil
function tilemap:move(x, y, w, h, vx, vy) end

---Sets the parallax scroll factor for a layer
---@param name string Layer name
---@param px number Horizontal parallax (1.0 = normal)
---@param py number Vertical parallax (1.0 = normal)
function tilemap:set_parallax(name, px, py) end

---Sets whether a layer is visible
---@param name string Layer name
---@param visible boolean Whether to draw this layer
function tilemap:set_visible(name, visible) end

---Returns all objects from a named object layer as a table
---@param layer_name string Object group name
---@return table objects Array of {id, name, type, x, y, width, height, properties?}
function tilemap:get_objects(layer_name) end

---Returns tile dimensions and layer count
---@return integer tile_width Tile width in pixels
---@return integer tile_height Tile height in pixels
---@return integer layer_count Number of layers
function tilemap:dimensions() end

---Returns the number of layers
---@return integer count Number of layers
function tilemap:layer_count() end

