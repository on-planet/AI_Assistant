#include "desktoper2D/lifecycle/asr/vad_segmenter.h"

#include <algorithm>
#include <cmath>

namespace desktoper2D {

namespace {
float ComputeRms(const std::vector<float> &x) {
    if (x.empty()) return 0.0f;
    double s = 0.0;
    for (float v : x) s += static_cast<double>(v) * static_cast<double>(v);
    return static_cast<float>(std::sqrt(s / static_cast<double>(x.size())));
}
} // namespace

EnergyVadSegmenter::EnergyVadSegmenter(VadConfig config) : config_(config) {}

bool EnergyVadSegmenter::Accept(const VadFrame &frame, VadSegment &out_segment) {
    out_segment = VadSegment{};
    const float rms = ComputeRms(frame.samples);
    const bool speech = rms >= config_.energy_threshold;

    if (speech) {
        ++speech_frames_;
        silence_frames_ = 0;
        in_speech_ = in_speech_ || (speech_frames_ >= std::max(1, config_.min_speech_frames));
        if (in_speech_) {
            current_.insert(current_.end(), frame.samples.begin(), frame.samples.end());
        }
    } else {
        speech_frames_ = 0;
        if (in_speech_) {
            ++silence_frames_;
            current_.insert(current_.end(), frame.samples.begin(), frame.samples.end());
        }
    }

    const bool hit_max_len = in_speech_ && current_.size() >= config_.max_segment_samples;
    const bool hit_tail_silence = in_speech_ && silence_frames_ >= std::max(1, config_.max_silence_frames);
    if (hit_max_len || hit_tail_silence) {
        out_segment.samples = std::move(current_);
        out_segment.sample_rate_hz = frame.sample_rate_hz;
        out_segment.is_final = true;

        current_.clear();
        in_speech_ = false;
        speech_frames_ = 0;
        silence_frames_ = 0;
        return true;
    }

    return false;
}

void EnergyVadSegmenter::Flush(VadSegment &out_segment) {
    out_segment = VadSegment{};
    if (current_.empty()) return;

    out_segment.samples = std::move(current_);
    out_segment.sample_rate_hz = 16000;
    out_segment.is_final = true;

    current_.clear();
    in_speech_ = false;
    speech_frames_ = 0;
    silence_frames_ = 0;
}

} // namespace desktoper2D
