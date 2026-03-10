#pragma once

#include <string>
#include <vector>

#include "desktoper2D/capture/screen_capture.h"

namespace desktoper2D {

struct OcrTextLine {
    std::string text;
    float score = 0.0f;
    float bbox_x = 0.0f;
    float bbox_y = 0.0f;
    float bbox_w = 0.0f;
    float bbox_h = 0.0f;
};

struct OcrSystemContext {
    std::string process_name;
    std::string window_title;
    std::string url;
};

struct OcrResult {
    std::vector<OcrTextLine> lines;
    std::string summary;
    std::vector<std::string> domain_tags;
};

struct OcrPerfBreakdown {
    int preprocess_det_ms = 0;
    int infer_det_ms = 0;
    int preprocess_rec_ms = 0;
    int infer_rec_ms = 0;
};

// PP-OCR(ONNX) det + rec 两模型拆分部署的最小可用服务。
class OcrService {
public:
    bool Init(const std::string &det_model_path,
              const std::string &rec_model_path,
              const std::string &keys_path,
              std::string *out_error);

    void Shutdown() noexcept;

    bool IsReady() const noexcept;

    void SetDetInputSize(int size);
    int GetDetInputSize() const noexcept;

    bool Recognize(const ScreenCaptureFrame &frame,
                   const OcrSystemContext *context,
                   OcrResult &out,
                   std::string *out_error,
                   OcrPerfBreakdown *out_perf = nullptr);

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

}  // namespace desktoper2D
