#pragma once

#include <string>
#include <vector>

#include "desktoper2D/lifecycle/ocr_service.h"
#include "desktoper2D/lifecycle/system_context_service.h"

namespace desktoper2D {

struct PerceptionPipelineState;

class OcrPostprocessService {
public:
    void Apply(const OcrResult &ocr_result,
               const SystemContextSnapshot &context,
               PerceptionPipelineState &state,
               bool has_new_ocr_packet);

private:
    static std::string NormalizeSummary(const std::string &summary,
                                        const std::vector<std::pair<std::string, std::string>> &dict);
    static std::string BuildNormalizedContextText(const SystemContextSnapshot &context,
                                                  const std::string &normalized_summary,
                                                  const std::vector<std::pair<std::string, std::string>> &dict);
    static void InferDomainTags(const std::string &normalized_text,
                                std::vector<std::string> &io_tags);
};

}  // namespace desktoper2D
