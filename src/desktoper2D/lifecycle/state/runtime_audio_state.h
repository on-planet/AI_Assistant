#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include <SDL3/SDL.h>

namespace desktoper2D {

struct RuntimeAudioState {
    SDL_AudioDeviceID mic_device_id = 0;
    SDL_AudioSpec mic_obtained_spec{};
    std::mutex mic_mutex;
    std::deque<float> mic_pcm_queue;
};

}  // namespace desktoper2D
