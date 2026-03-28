---@meta
-- Auto-generated LuaLS stubs from LuaApiFunction metadata.
-- Do not edit manually.

---@class G
---@field data G.data
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
G = {}

---@class G.data
G.data = {}

---Computes a hash of a string or byte buffer
---@param data string a string or byte_buffer to hash
---@return number hash the hash value
function G.data.hash(data) end

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

---Loads a Json file from a string.
---@param name string Filename to read from
---@return string error nil on success, a string if there were any errors
---@return table result Table result of evaluating the Json file, nil if there were any errors
function G.filesystem.load_json(name) end

---Saves a Lua table into a file.
---@param name string Filename to write to from
---@param contents table Table to serialize
---@return string error nil on success, a string if there were any errors
function G.filesystem.save_json(name, contents) end

---List all files in a givne directory
---@param name string Directory to list
---@return table files A list with all the files in the given directory
function G.filesystem.list_directory(name) end

---Returns whether a file exists
---@param name string Path to the potential file to check
---@return boolean exists Whether the file exists or not
function G.filesystem.exists(name) end

---@class G.graphics
G.graphics = {}

---Clear the current render target. With no arguments clears to transparent black. With arguments clears to the given RGBA color.
---@param r? number red component (0-1)
---@param g? number green component (0-1)
---@param b? number blue component (0-1)
---@param a? number alpha component (0-1)
function G.graphics.clear(r?, g?, b?, a?) end

---Saves a screenshot from the contents of the current framebuffer
---@param file? string If provided, a filename where we should write the screenshot.
---@return byte_buffer result If a file was provided, nil if the write suceeded or an error message otherwise. If no file was provided, a byte buffer with the image contents
function G.graphics.take_screenshot(file?) end

---Draws a sprite by name to the screen
---@param sprite string the name of the sprite in any sprite sheet
---@param x number the x position (left-right) in screen coordinates where to draw the sprite
---@param y number the y position (top-bottom) in screen coordinates where to draw the sprite
---@param angle? number if provided, the angle to rotate the sprite
function G.graphics.draw_sprite(sprite, x, y, angle?) end

---Draws an image by name to the screen
---@param sprite string the name of the sprite in any sprite sheet
---@param x number the x position (left-right) in screen coordinates where to draw the sprite
---@param y number the y position (top-bottom) in screen coordinates where to draw the sprite
---@param angle? number if provided, the angle to rotate the sprite
function G.graphics.draw_image(sprite, x, y, angle?) end

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
---@param text string A string or byte buffer with the contents to render to the screen
---@param size integer Size in pixels to use for rendering the text
---@param x number Horizontal position in screen space pixels left-to-right where to render the text
---@param y number Vertical position in screen space pixels top-to-bottom where to render the text
function G.graphics.draw_text(font, text, size, x, y) end

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

---Push a transform to the screen into the transform stack.
---@param transform mat4x4 A 4x4 matrix with the transform to push
function G.graphics.push(transform) end

---Pop the transform at the top of the stack. It will not apply anymore.
function G.graphics.pop() end

---Push a transform to the screen that rotates all objects by a given angle
---@param angle number All objects will be rotated by this angle in radians clockwise
function G.graphics.rotate(angle) end

---Push a transform to the screen that scales all objects by a given angle
---@param xf number Scalar factor to scale up the x coordinate
---@param yf number Scalar factor to scale up the y coordinate
function G.graphics.scale(xf, yf) end

---Translate all objects in the screen by moving the coordinate system center
---@param x number New x position of the coordinate system center
---@param y number New y position of the coordinate system center
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
---@param button number the mouse button number
---@return boolean pressed whether the button was pressed
function G.input.is_mouse_pressed(button) end

---Returns true if the mouse button was released this frame
---@param button number the mouse button number
---@return boolean released whether the button was released
function G.input.is_mouse_released(button) end

---Returns true if the mouse button is currently held down
---@param button number the mouse button number
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

---@class G.math
G.math = {}

---Clamps a value between a minimum and maximum
---@param x number the value to clamp
---@param low number the minimum value
---@param high number the maximum value
---@return number result the clamped value
function G.math.clamp(x, low, high) end

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
---@return physics_handle handle a physics handle for the new body
function G.physics.add_box(tx, ty, bx, by, angle, callback) end

---Adds a dynamic circle body to the physics world
---@param tx number center x
---@param ty number center y
---@param radius number the circle radius
---@param callback function collision callback function
---@return physics_handle handle a physics handle for the new body
function G.physics.add_circle(tx, ty, radius, callback) end

---Destroys a physics body
---@param handle physics_handle the physics handle to destroy
function G.physics.destroy_handle(handle) end

---Creates a static ground body. If walls is false, no screen boundary edges are created.
---@param walls? boolean if true (default), add edge walls around the screen perimeter
function G.physics.create_ground(walls) end

---Sets a global callback invoked when two bodies begin contact
---@param callback function function called with two collision callbacks
function G.physics.set_collision_callback(callback) end

---Returns the position of a physics body
---@param handle physics_handle the physics handle
---@return number x x position
---@return number y y position
function G.physics.position(handle) end

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
local vec2 = {}

---Dot product with another vector
---@param other vec2 the other vector
---@return number result dot product
function vec2:dot(other) end

---Squared length of the vector
---@return number result squared length
function vec2:len2() end

---Returns a normalized copy of the vector
---@return vec2 result normalized vector
function vec2:normalized() end

---Sends this value as a shader uniform. Errors if uniform not found.
---@param name string uniform name
function vec2:send_as_uniform(name) end

---A 3D floating-point vector
---@class vec3
---@operator add(vec3): vec3
---@operator sub(vec3): vec3
---@operator mul(number): vec3
local vec3 = {}

---Dot product with another vector
---@param other vec3 the other vector
---@return number result dot product
function vec3:dot(other) end

---Squared length of the vector
---@return number result squared length
function vec3:len2() end

---Returns a normalized copy of the vector
---@return vec3 result normalized vector
function vec3:normalized() end

---Sends this value as a shader uniform. Errors if uniform not found.
---@param name string uniform name
function vec3:send_as_uniform(name) end

---A 4D floating-point vector
---@class vec4
---@operator add(vec4): vec4
---@operator sub(vec4): vec4
---@operator mul(number): vec4
local vec4 = {}

---Dot product with another vector
---@param other vec4 the other vector
---@return number result dot product
function vec4:dot(other) end

---Squared length of the vector
---@return number result squared length
function vec4:len2() end

---Returns a normalized copy of the vector
---@return vec4 result normalized vector
function vec4:normalized() end

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

---A random number generator
---@class rng
local rng = {}

---A reference to a sprite asset
---@class sprite_asset
local sprite_asset = {}

