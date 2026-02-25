
# API

```lua

-- Adds an effect, effects are supposed to be short and reused many times.
-- @param sound_name name of the sound effect
-- @return an opaque handle for the effect
function add_effect(sound_name)
end

-- Adds a music, these are intended for loopable sounds and are streamed.
-- @param sound_name name of the sound music
-- @return an opaque handle for the effect
function add_music(sound_name)
end

-- Plays the music for a handle. If looped is true, will loop it forever.
-- @param handle the handle from `add_effect` or `add_music`
-- @param loop if true, loops the music
function play_handle(handle, loop)
end

-- Stops the provided handle.
-- @param handle the handle from `add_effect` or `add_music`
function stop_handle(handle)
end

-- Plays the music for a handle. If looped is true, will loop it forever.
-- @param handle the handle from `add_effect` or `add_music`
-- @return True if playing, false otherwise.
function is_playing(handle)
end


```

## Asset support

* WAV (with drwav).
* Vorbis (with stb_vorbis).


# Hot reloading

How do we handle hot code reloading of the sound system?
* Delegate it to a on_reload callback provided by the user that makes all the sounds stop?
* Stop them ourselves?
* The problem is the asset itself reloading, e.g. we now reloaded the shotgun effect.

# Code

Effects are fully loaded to memory using interleaved floating point samples. This way we can reuse them easily.

Music is streamed from the asset itself. We cannot reuse them unless they are stopped.

A voice can load an effect or a music through a callback system. We produce callbacks with a userdata and then the voice uses that to get samples and play.

When adding an effect, we check if we have it loaded. If we do not, we create a new loader for it and set the position at 0.

When adding a music, we find if we have a stream for it that is not playing. If we have it, we reset the stream and play that. Otherwise we get a new stream and play it.

A voice links to an effect or a music.

* We have several source types.
	* WAV 
	* VORBIS.
	* Raw PPM for effects loading?


# TODO

* Effects?
* Multiple busses?