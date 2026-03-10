#include "desktoper2D/lifecycle/asr/offline_asr_provider.h"

#include <chrono>
#include <filesystem>

namespace desktoper2D {
namespace {

std::string ResolveExistingPath(const std::string &path) {
    static const char *kPrefixes[] = {"", "../", "../../", "../../../", "../../../../"};
    std::error_code ec;
    for (const char *prefix : kPrefixes) {
        const std::filesystem::path candidate = std::filesystem::path(prefix) / path;
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate.lexically_normal().generic_string();
        }
    }
    return {};
}

}  // namespace

OfflineAsrProvider::OfflineAsrProvider(std::string model_path)
    : model_path_(std::move(model_path)) {}

bool OfflineAsrProvider::Init(std::string *out_error) {
    std::error_code ec;
    const std::string resolved_model = ResolveExistingPath(model_path_);
    const std::string resolved_tokenizer = ResolveExistingPath(tokenizer_path_);
    const std::string resolved_vad_model = ResolveExistingPath(vad_model_path_);
    const std::string resolved_vad_cfg = ResolveExistingPath(vad_config_path_);
    const std::string resolved_am_mvn = ResolveExistingPath(am_mvn_path_);
    const std::string resolved_fsmn_am_mvn = ResolveExistingPath(fsmn_am_mvn_path_);

    const bool has_model = !resolved_model.empty();
    const bool has_tokenizer = !resolved_tokenizer.empty();
    const bool has_vad_model = !resolved_vad_model.empty();
    const bool has_vad_cfg = !resolved_vad_cfg.empty();
    const bool has_am_mvn = !resolved_am_mvn.empty();
    const bool has_fsmn_am_mvn = !resolved_fsmn_am_mvn.empty();

    ready_ = has_model && has_tokenizer && has_vad_model && has_vad_cfg && has_am_mvn && has_fsmn_am_mvn;
    if (ready_) {
        model_path_ = resolved_model;
        tokenizer_path_ = resolved_tokenizer;
        vad_model_path_ = resolved_vad_model;
        vad_config_path_ = resolved_vad_cfg;
        am_mvn_path_ = resolved_am_mvn;
        fsmn_am_mvn_path_ = resolved_fsmn_am_mvn;
    }
    if (!ready_) {
        if (out_error) {
            *out_error = "offline asr assets missing: model=" + model_path_ +
                         ", tokenizer=" + tokenizer_path_ +
                         ", vad_model=" + vad_model_path_ +
                         ", vad_cfg=" + vad_config_path_ +
                         ", am_mvn=" + am_mvn_path_ +
                         ", fsmn_am_mvn=" + fsmn_am_mvn_path_;
        }
        return false;
    }
    if (out_error) {
        out_error->clear();
    }
    return true;
}

void OfflineAsrProvider::Shutdown() noexcept {
    ready_ = false;
}

bool OfflineAsrProvider::Recognize(const AsrAudioChunk &chunk,
                                   const AsrRecognitionOptions &options,
                                   AsrRecognitionResult &out_result,
                                   std::string *out_error) {
    const auto t0 = std::chrono::steady_clock::now();

    if (!ready_) {
        const std::string err = "offline provider not initialized";
        if (out_error) {
            *out_error = err;
        }
        out_result = AsrRecognitionResult{};
        out_result.ok = false;
        out_result.error = err;
        out_result.provider = AsrProviderKind::Offline;
        return false;
    }

    out_result = AsrRecognitionResult{};
    out_result.ok = true;
    out_result.provider = AsrProviderKind::Offline;
    out_result.is_final = chunk.is_final;
    out_result.confidence = chunk.samples.empty() ? 0.2f : 0.65f;
    out_result.text = chunk.samples.empty()
                          ? ""
                          : (options.language == "zh" ? "[离线识别占位结果]" : "[offline recognition placeholder]");

    const auto t1 = std::chrono::steady_clock::now();
    out_result.latency_ms =
        static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    if (out_error) {
        out_error->clear();
    }
    return true;
}

}  // namespace desktoper2D
