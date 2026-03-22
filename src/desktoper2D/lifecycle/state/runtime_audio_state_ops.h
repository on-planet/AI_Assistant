#pragma once

#include "desktoper2D/lifecycle/state/runtime_audio_state.h"

#include <cstddef>
#include <vector>

namespace desktoper2D {

void AppendMicPcmSamples(RuntimeAudioState &audio_state, const float *samples, std::size_t sample_count);
void ConsumeMicPcmSamples(RuntimeAudioState &audio_state, std::size_t max_samples, std::vector<float> *out_samples);
void CloseMicAudioDevice(RuntimeAudioState &audio_state);

}  // namespace desktoper2D
