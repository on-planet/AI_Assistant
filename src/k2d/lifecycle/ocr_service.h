#pragma once

#include <string>
#include <vector>

#include "k2d/capture/screen_capture.h"

namespace k2d {

struct OcrTextLine {
    std::string text;
    float score = 0.0f;
};

struct OcrSystemContext {
    std::string process_name;
    std::string window_title;
    std::string url;
};

struct OcrResult {
    std::vector<OcrTextLine> lines;
    std::string summary;
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

    bool Recognize(const ScreenCaptureFrame &frame,
                   const OcrSystemContext *context,
                   OcrResult &out,
                   std::string *out_error);

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

}  // namespace k2d
