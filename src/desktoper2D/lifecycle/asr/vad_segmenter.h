#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace desktoper2D {

struct VadFrame {
    std::vector<float> samples; // 16k mono
    int sample_rate_hz = 16000;
};

struct VadSegment {
    std::vector<float> samples; // merged speech chunk
    int sample_rate_hz = 16000;
    bool is_final = false;
};

struct VadConfig {
    float energy_threshold = 0.01f;
    int min_speech_frames = 3;
    int max_silence_frames = 8;
    std::size_t max_segment_samples = 16000 * 12;
};

class EnergyVadSegmenter {
public:
    explicit EnergyVadSegmenter(VadConfig config = {});

    bool Accept(const VadFrame &frame, VadSegment &out_segment);
    void Flush(VadSegment &out_segment);

private:
    VadConfig config_{};
    int speech_frames_ = 0;
    int silence_frames_ = 0;
    bool in_speech_ = false;
    std::vector<float> current_;
};

} // namespace desktoper2D
