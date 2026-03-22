#include "desktoper2D/lifecycle/state/runtime_audio_state_ops.h"

#include <SDL3/SDL_audio.h>

#include <algorithm>

namespace desktoper2D {

namespace {

constexpr std::size_t kMaxMicQueueSamples = 16000 * 20;

void EnsureMicRingStorage(RuntimeAudioState &audio_state) {
    const std::size_t capacity = std::max<std::size_t>(1, audio_state.mic_pcm_ring_capacity);
    if (audio_state.mic_pcm_ring.size() != capacity) {
        audio_state.mic_pcm_ring.assign(capacity, 0.0f);
        audio_state.mic_pcm_ring_head = 0;
        audio_state.mic_pcm_ring_size = 0;
        audio_state.mic_pcm_ring_capacity = capacity;
    }
}

}  // namespace

void AppendMicPcmSamples(RuntimeAudioState &audio_state, const float *samples, std::size_t sample_count) {
    if (!samples || sample_count == 0) {
        return;
    }

    std::lock_guard<std::mutex> lk(audio_state.mic_mutex);
    audio_state.mic_pcm_ring_capacity = kMaxMicQueueSamples;
    EnsureMicRingStorage(audio_state);

    const std::size_t capacity = audio_state.mic_pcm_ring_capacity;
    if (sample_count >= capacity) {
        const float *tail = samples + (sample_count - capacity);
        std::copy_n(tail, capacity, audio_state.mic_pcm_ring.begin());
        audio_state.mic_pcm_ring_head = 0;
        audio_state.mic_pcm_ring_size = capacity;
        return;
    }

    const std::size_t write_index = (audio_state.mic_pcm_ring_head + audio_state.mic_pcm_ring_size) % capacity;
    const std::size_t first_chunk = std::min(sample_count, capacity - write_index);
    std::copy_n(samples, first_chunk, audio_state.mic_pcm_ring.begin() + static_cast<std::ptrdiff_t>(write_index));
    if (sample_count > first_chunk) {
        std::copy_n(samples + first_chunk,
                    sample_count - first_chunk,
                    audio_state.mic_pcm_ring.begin());
    }

    const std::size_t prior_size = audio_state.mic_pcm_ring_size;
    const std::size_t total_size = prior_size + sample_count;
    if (total_size > capacity) {
        const std::size_t overflow = total_size - capacity;
        audio_state.mic_pcm_ring_head = (audio_state.mic_pcm_ring_head + overflow) % capacity;
        audio_state.mic_pcm_ring_size = capacity;
    } else {
        audio_state.mic_pcm_ring_size = total_size;
    }
}

void ConsumeMicPcmSamples(RuntimeAudioState &audio_state, std::size_t max_samples, std::vector<float> *out_samples) {
    if (!out_samples) {
        return;
    }

    out_samples->clear();
    if (max_samples == 0) {
        return;
    }
    if (out_samples->capacity() < max_samples) {
        out_samples->reserve(max_samples);
    }

    std::lock_guard<std::mutex> lk(audio_state.mic_mutex);
    EnsureMicRingStorage(audio_state);

    const std::size_t available = std::min(max_samples, audio_state.mic_pcm_ring_size);
    out_samples->resize(available);
    if (available == 0) {
        return;
    }

    const std::size_t capacity = audio_state.mic_pcm_ring_capacity;
    const std::size_t first_chunk = std::min(available, capacity - audio_state.mic_pcm_ring_head);
    std::copy_n(audio_state.mic_pcm_ring.begin() + static_cast<std::ptrdiff_t>(audio_state.mic_pcm_ring_head),
                first_chunk,
                out_samples->begin());
    if (available > first_chunk) {
        std::copy_n(audio_state.mic_pcm_ring.begin(),
                    available - first_chunk,
                    out_samples->begin() + static_cast<std::ptrdiff_t>(first_chunk));
    }

    audio_state.mic_pcm_ring_head = (audio_state.mic_pcm_ring_head + available) % capacity;
    audio_state.mic_pcm_ring_size -= available;
}

void CloseMicAudioDevice(RuntimeAudioState &audio_state) {
    if (audio_state.mic_device_id != 0) {
        SDL_CloseAudioDevice(audio_state.mic_device_id);
        audio_state.mic_device_id = 0;
    }
    audio_state.mic_obtained_spec = {};
    std::lock_guard<std::mutex> lk(audio_state.mic_mutex);
    audio_state.mic_pcm_ring.clear();
    audio_state.mic_pcm_ring_head = 0;
    audio_state.mic_pcm_ring_size = 0;
}

}  // namespace desktoper2D
