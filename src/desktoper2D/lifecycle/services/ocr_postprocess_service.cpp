#include "desktoper2D/lifecycle/services/ocr_postprocess_service.h"

#include <algorithm>
#include <cctype>

#include "desktoper2D/lifecycle/perception_pipeline.h"

namespace desktoper2D {

namespace {

void ToLowerInplace(std::string &text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
}

void ReplaceAll(std::string &text, const std::string &from, const std::string &to) {
    if (from.empty()) {
        return;
    }
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

}  // namespace

void OcrPostprocessService::Apply(const OcrResult &ocr_result,
                                 const SystemContextSnapshot &context,
                                 PerceptionPipelineState &state,
                                 bool has_new_ocr_packet) {
    if (!has_new_ocr_packet) {
        return;
    }

    const std::vector<std::pair<std::string, std::string>> dict = {
        {"visual studio code", "vscode"}, {"vs code", "vscode"}, {"clion", "clion"},
        {"read me", "readme"}, {"help", "help"}, {"settings", "settings"},
        {"file", "file"}, {"edit", "edit"}, {"view", "view"}, {"run", "run"},
        {"debug", "debug"}, {"terminal", "terminal"}, {"browser", "browser"}, {"docs", "docs"}
    };

    std::string norm_summary = NormalizeSummary(ocr_result.summary, dict);
    std::string norm_text = BuildNormalizedContextText(context, norm_summary, dict);

    if (state.ocr_summary_candidate == norm_summary) {
        state.ocr_summary_consistent_count += 1;
    } else {
        state.ocr_summary_candidate = norm_summary;
        state.ocr_summary_consistent_count = 1;
    }
    if (state.ocr_summary_consistent_count >= std::max(2, state.ocr_summary_debounce_frames)) {
        state.ocr_summary_stable = state.ocr_summary_candidate;
    }

    state.blackboard.ocr.lines = ocr_result.lines;
    state.blackboard.ocr.summary = state.ocr_summary_stable.empty() ? ocr_result.summary : state.ocr_summary_stable;
    state.blackboard.ocr.domain_tags = ocr_result.domain_tags;

    InferDomainTags(norm_text, state.blackboard.ocr.domain_tags);
}

std::string OcrPostprocessService::NormalizeSummary(const std::string &summary,
                                                    const std::vector<std::pair<std::string, std::string>> &dict) {
    std::string normalized = summary;
    ToLowerInplace(normalized);
    for (const auto &kv : dict) {
        ReplaceAll(normalized, kv.first, kv.second);
    }
    return normalized;
}

std::string OcrPostprocessService::BuildNormalizedContextText(const SystemContextSnapshot &context,
                                                              const std::string &normalized_summary,
                                                              const std::vector<std::pair<std::string, std::string>> &dict) {
    std::string normalized = context.process_name + "\n" +
                             context.window_title + "\n" +
                             context.url_hint + "\n" +
                             normalized_summary;
    ToLowerInplace(normalized);
    for (const auto &kv : dict) {
        ReplaceAll(normalized, kv.first, kv.second);
    }
    return normalized;
}

void OcrPostprocessService::InferDomainTags(const std::string &normalized_text,
                                           std::vector<std::string> &io_tags) {
    std::vector<std::string> inferred_tags;
    auto push_unique = [&](const std::string &tag) {
        if (std::find(inferred_tags.begin(), inferred_tags.end(), tag) == inferred_tags.end()) {
            inferred_tags.push_back(tag);
        }
    };

    if (normalized_text.find("http") != std::string::npos || normalized_text.find("www.") != std::string::npos ||
        normalized_text.find("chrome") != std::string::npos || normalized_text.find("edge") != std::string::npos ||
        normalized_text.find("firefox") != std::string::npos || normalized_text.find("browser") != std::string::npos) {
        push_unique("browser");
    }
    if (normalized_text.find("chat") != std::string::npos || normalized_text.find("discord") != std::string::npos ||
        normalized_text.find("slack") != std::string::npos || normalized_text.find("wechat") != std::string::npos ||
        normalized_text.find("qq") != std::string::npos) {
        push_unique("chat");
    }
    if (normalized_text.find("readme") != std::string::npos || normalized_text.find("docs") != std::string::npos ||
        normalized_text.find("manual") != std::string::npos || normalized_text.find("wiki") != std::string::npos ||
        normalized_text.find("help") != std::string::npos) {
        push_unique("doc");
    }
    if (normalized_text.find("cpp") != std::string::npos || normalized_text.find("cmake") != std::string::npos ||
        normalized_text.find("visual studio") != std::string::npos || normalized_text.find("clion") != std::string::npos ||
        normalized_text.find("vscode") != std::string::npos || normalized_text.find("github") != std::string::npos ||
        normalized_text.find("debug") != std::string::npos || normalized_text.find("terminal") != std::string::npos) {
        push_unique("code");
    }

    for (const auto &tag : inferred_tags) {
        if (std::find(io_tags.begin(), io_tags.end(), tag) == io_tags.end()) {
            io_tags.push_back(tag);
        }
    }
}

}  // namespace desktoper2D
