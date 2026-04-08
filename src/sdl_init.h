#pragma once
#ifndef _GAME_SDL_INIT_H
#define _GAME_SDL_INIT_H

#include <SDL3/SDL.h>

#include "vec.h"

namespace G {

struct GameConfig;

// Audio format shared between SDL init and the audio callback in game.cc.
inline constexpr int kAudioChannels = 2;
inline constexpr int kAudioSampleRate = 44100;
// 44100 Hz * 2 channels at ~100ms max = 8820 floats. 16384 is generous.
inline constexpr int kAudioBufFloats = 16384;

// Handles and device IDs for everything InitializeSdl opens. Caller owns
// teardown via ShutdownSdl.
struct SdlContext {
  SDL_Window* window = nullptr;
  SDL_GLContext gl_context = nullptr;
  SDL_AudioStream* audio_stream = nullptr;
  SDL_AudioDeviceID audio_device = 0;
};

// Installs SDL-based log and crash sinks. Call once at startup before
// any logging that should reach SDL_Log.
void InitializeLogging();

// Initializes SDL subsystems, creates the window, loads the GL context,
// and opens the audio device stream. Any failure CHECK-fails. The audio
// stream is paused; the caller must SDL_ResumeAudioDevice when ready.
SdlContext InitializeSdl(const GameConfig& config,
                         SDL_AudioStreamCallback audio_cb,
                         void* audio_cb_userdata);

// Tears down everything InitializeSdl created and quits SDL. Does not
// touch PhysFS (which is initialized separately in game.cc).
void ShutdownSdl(SdlContext* ctx);

// Logs engine, SDL, GL, and vendored-library version information.
// Requires a live GL context.
void PrintSystemInformation();

// Returns the window's framebuffer size in pixels.
IVec2 GetWindowViewport(SDL_Window* window);

}  // namespace G

#endif  // _GAME_SDL_INIT_H
