#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include <SDL3/SDL.h>

namespace desktoper2D {

struct RuntimeAudioState {
    SDL_AudioDeviceID mic_device_id = 0;
    SDL_AudioSpec mic_obtained_spec{};
    std::mutex mic_mutex;
    std::vector<float> mic_pcm_ring;
    std::size_t mic_pcm_ring_head = 0;
    std::size_t mic_pcm_ring_size = 0;
    std::size_t mic_pcm_ring_capacity = 16000 * 20;
};

}  // namespace desktoper2D
