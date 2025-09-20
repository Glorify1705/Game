
# API

```lua

-- Adds an effect, effects are supposed to be short and reused many times.
-- @param sound_name name of the sound effect
-- @return an opaque handle for the effect
function add_effect(sound_name)

-- Adds a music, these are intended for loopable sounds and are streamed.
-- @param sound_name name of the sound music
-- @return an opaque handle for the effect
function add_music(sound_name)

-- Plays the music for a handle. If looped is true, will loop it forever.
-- @param handle the handle from `add_effect` or `add_music`
-- @param loop if true, loops the music
function play_handle(handle, loop)

function 

```

# Hot code reloading