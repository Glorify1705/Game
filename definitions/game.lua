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

---Clear the screen to black
function G.graphics.clear() end

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

---Draws a circle with the global context color to the screen
---@param x number the x position (left-right) in screen coordinates of the center of the circle
---@param y number the y position (top-bottom) in screen coordinates of the center of the circle
---@param r number the radius in pixels of the center of the circle
function G.graphics.draw_circle(x, y, r) end

---Draws a triangle with the global context color to the screen
---@param p1x number The x coordinate in screen coordinates of the first point of the triangle
---@param p1y number The y coordinate in screen coordinates of the first point of the triangle
---@param p2x number The x coordinate in screen coordinates of the second point of the triangle
---@param p2y number The y coordinate in screen coordinates of the second point of the triangle
---@param p3x number The x coordinate in screen coordinates of the third point of the triangle
---@param p3y number The y coordinate in screen coordinates of the third point of the triangle
function G.graphics.draw_triangle(p1x, p1y, p2x, p2y, p3x, p3y) end

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

---Unimplemented.
function G.graphics.new_canvas() end

---Unimplemented.
function G.graphics.set_canvas() end

---Unimplemented.
function G.graphics.draw_canvas() end

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

---Creates a static ground body
function G.physics.create_ground() end

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

---Sends this value as a shader uniform
---@param name string uniform name
---@return boolean ok whether the uniform was set
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

---Sends this value as a shader uniform
---@param name string uniform name
---@return boolean ok whether the uniform was set
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

---Sends this value as a shader uniform
---@param name string uniform name
---@return boolean ok whether the uniform was set
function vec4:send_as_uniform(name) end

---A 2x2 floating-point matrix
---@class mat2x2
local mat2x2 = {}

---Sends this matrix as a shader uniform
---@param name string uniform name
---@return boolean ok whether the uniform was set
function mat2x2:send_as_uniform(name) end

---A 3x3 floating-point matrix
---@class mat3x3
local mat3x3 = {}

---Sends this matrix as a shader uniform
---@param name string uniform name
---@return boolean ok whether the uniform was set
function mat3x3:send_as_uniform(name) end

---A 4x4 floating-point matrix
---@class mat4x4
local mat4x4 = {}

---Sends this matrix as a shader uniform
---@param name string uniform name
---@return boolean ok whether the uniform was set
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

