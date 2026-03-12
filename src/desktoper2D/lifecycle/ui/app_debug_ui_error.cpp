#include "app_debug_ui_panel_state.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "desktoper2D/core/json.h"
#include "desktoper2D/lifecycle/resource_locator.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_widgets.h"

namespace desktoper2D {

namespace {

struct DefaultPluginSpec {
    std::string name;
    std::string kind;
    std::string onnx;
    std::string labels;
    std::string keys;
    std::string det;
    std::string rec;
    std::string model;
    std::string source;
};

std::string ReadTextFileLocal(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

bool LoadDefaultPluginSpecFromFile(const std::string &path, DefaultPluginSpec *out_spec) {
    if (!out_spec) return false;
    const std::string text = ReadTextFileLocal(path);
    if (text.empty()) return false;

    JsonParseError err{};
    auto root_opt = ParseJson(text, &err);
    if (!root_opt || !root_opt->isObject()) return false;

    const JsonValue &root = *root_opt;
    out_spec->name = root.getString("name").value_or(std::string());
    out_spec->kind = root.getString("kind").value_or(std::string());
    out_spec->onnx = root.getString("onnx").value_or(std::string());
    out_spec->labels = root.getString("labels").value_or(std::string());
    out_spec->keys = root.getString("keys").value_or(std::string());
    out_spec->det = root.getString("det").value_or(std::string());
    out_spec->rec = root.getString("rec").value_or(std::string());
    out_spec->model = root.getString("model").value_or(std::string());
    out_spec->source = path;
    return true;
}

std::unordered_map<std::string, DefaultPluginSpec> ScanDefaultPlugins() {
    std::unordered_map<std::string, DefaultPluginSpec> out;
    const std::string base_dir = ResourceLocator::ResolveFirstExisting("assets/default_plugins");
    if (base_dir.empty()) return out;

    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(base_dir, ec);
         !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        if (it->path().filename() != "plugin.json") continue;

        DefaultPluginSpec spec{};
        const std::string path = it->path().generic_string();
        if (!LoadDefaultPluginSpecFromFile(path, &spec)) {
            continue;
        }
        if (spec.name.empty()) {
            spec.name = it->path().parent_path().filename().string();
        }
        out[spec.name] = std::move(spec);
    }
    return out;
}

std::string TrimCopy(const std::string &s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

std::vector<std::string> SplitExtraOnnxLines(const char *text) {
    std::vector<std::string> out;
    if (!text || text[0] == '\0') {
        return out;
    }
    std::string line;
    std::istringstream iss(text);
    while (std::getline(iss, line)) {
        const std::string trimmed = TrimCopy(line);
        if (!trimmed.empty()) {
            out.push_back(trimmed);
        }
    }
    return out;
}

}

void RenderRuntimePluginQuickControlPanel(AppRuntime &runtime) {
    ImGui::SeparatorText("行为插件");
    ImGui::SameLine();
    if (ImGui::Button("刷新##behavior_plugins")) {
        runtime.plugin_config_refresh_requested = true;
    }
    if (runtime.plugin_config_refresh_requested) {
        RefreshPluginConfigs(runtime);
    }

    const ImVec2 card_padding = ImGui::GetStyle().FramePadding;
    const ImVec2 module_card_padding = ImGui::GetStyle().FramePadding;

    auto open_detail = [&](PluginDetailKind kind,
                           const char *title,
                           const std::string &source,
                           const std::vector<std::string> &assets,
                           const std::string &backend,
                           const std::string &status,
                           const std::string &last_error) {
        runtime.plugin_detail_kind = kind;
        runtime.plugin_detail_title = title ? title : "";
        runtime.plugin_detail_source = source;
        runtime.plugin_detail_assets = assets;
        runtime.plugin_detail_backend = backend;
        runtime.plugin_detail_status = status;
        runtime.plugin_detail_last_error = last_error;
        runtime.plugin_detail_edit_loaded = false;
        runtime.plugin_detail_edit_source = source;
        runtime.show_plugin_detail_window = true;
        runtime.show_plugin_quick_control_window = false;
    };


    ImGui::BeginChild("plugin_quick_control_cards", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    std::string pending_delete_path;
    bool pending_delete_requested = false;
    if (!runtime.plugin_config_entries.empty()) {
        for (std::size_t i = 0; i < runtime.plugin_config_entries.size(); ++i) {
            auto &entry = runtime.plugin_config_entries[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginGroup();
            ImGui::PushID(entry.config_path.c_str());
            ImGui::TextColored(entry.enabled ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f) : ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                               "%s", entry.enabled ? "启用" : "禁用");
            ImGui::SameLine();
            ImGui::Text("%s", entry.name.c_str());
            if (!entry.model_version.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("v%s", entry.model_version.c_str());
            }
            if (!entry.model_id.empty()) {
                ImGui::TextDisabled("model: %s", entry.model_id.c_str());
            }
            ImGui::TextDisabled("%s", entry.config_path.c_str());

            bool enabled_flag = entry.enabled;
            if (ImGui::Checkbox("持久启用", &enabled_flag)) {
                entry.enabled = enabled_flag;
                SetPluginEnabledState(runtime, entry.config_path, entry.enabled);
            }
            ImGui::SameLine();
            const bool entry_enabled_now = entry.enabled;
            if (!entry_enabled_now) {
                ImGui::BeginDisabled();
            }
            if (i < 2) {
                std::string pick_err;
                std::string picked_path;
                if (RenderFilePickerButton("切换", &picked_path, "ONNX Model", "onnx", "", &pick_err)) {
                    PluginAssetOverride override{};
                    override.onnx = picked_path;
                    if (runtime.unified_plugin_entries.empty()) {
                        RefreshUnifiedPlugins(runtime);
                    }
                    const std::string unified_id = std::string("behavior:") + entry.config_path;
                    std::string err;
                    const bool ok = ReplaceUnifiedPluginAssets(runtime, unified_id, override, &err);
                    if (ok) {
                        runtime.plugin_switch_status = "plugin switch queued";
                        runtime.plugin_switch_error.clear();
                    } else {
                        runtime.plugin_switch_status.clear();
                        runtime.plugin_switch_error = err.empty() ? "plugin switch failed" : err;
                    }
                } else if (!pick_err.empty()) {
                    runtime.plugin_switch_status.clear();
                    runtime.plugin_switch_error = pick_err;
                }
            } else {
                if (ImGui::Button("切换")) {
                    SDL_strlcpy(runtime.plugin_name_input, entry.name.c_str(), sizeof(runtime.plugin_name_input));
                    std::string err;
                    const bool ok = SwitchPluginByName(runtime, runtime.plugin_name_input, &err);
                    if (ok) {
                        runtime.plugin_switch_status = "plugin switch queued";
                        runtime.plugin_switch_error.clear();
                    } else {
                        runtime.plugin_switch_status.clear();
                        runtime.plugin_switch_error = err.empty() ? "plugin switch failed" : err;
                    }
                }
            }
            if (!entry_enabled_now) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (ImGui::Button("删除")) {
                pending_delete_requested = true;
                pending_delete_path = entry.config_path;
            }
            if (i >= 2) {
                ImGui::SameLine();
                if (ImGui::Button("禁用")) {
                    entry.enabled = false;
                    SetPluginEnabledState(runtime, entry.config_path, false);
                }
            }
            ImGui::Separator();
            ImGui::EndGroup();
            const ImVec2 behavior_card_min = ImGui::GetItemRectMin();
            const ImVec2 behavior_card_max = ImGui::GetItemRectMax();
            const ImVec2 behavior_cursor = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(behavior_card_min);
            ImGui::InvisibleButton("##behavior_card_button",
                                   ImVec2(behavior_card_max.x - behavior_card_min.x,
                                          behavior_card_max.y - behavior_card_min.y));
            const bool behavior_clicked = ImGui::IsItemClicked();
            const bool behavior_hovered = ImGui::IsItemHovered();
            if (behavior_hovered) {
                ImDrawList *draw_list = ImGui::GetWindowDrawList();
                const ImU32 hover_color = ImGui::GetColorU32(ImVec4(0.2f, 0.45f, 0.95f, 0.18f));
                draw_list->AddRectFilled(behavior_card_min, behavior_card_max, hover_color, 6.0f);
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            ImGui::SetCursorScreenPos(behavior_card_max);
            ImGui::Dummy(ImVec2(1.0f, 1.0f));
            ImGui::SetCursorScreenPos(behavior_cursor);
            ImGui::Dummy(ImVec2(1.0f, 1.0f));
            if (behavior_clicked) {
                std::vector<std::string> assets = {entry.config_path};
                open_detail(PluginDetailKind::Behavior,
                            entry.name.c_str(),
                            entry.config_path,
                            assets,
                            "onnxruntime.cpu",
                            entry.enabled ? "ENABLED" : "DISABLED",
                            runtime.plugin_last_error);
            }
            ImGui::PopID();
            ImGui::PopID();
            if (i + 1 < runtime.plugin_config_entries.size()) {
                ImGui::Dummy(ImVec2(0.0f, card_padding.y));
            }
        }
    }

    if (pending_delete_requested && !pending_delete_path.empty()) {
        std::string err;
        const bool ok = DeletePluginConfig(runtime, pending_delete_path, &err);
        if (ok) {
            runtime.plugin_delete_status = "plugin deleted";
            runtime.plugin_delete_error.clear();
            runtime.plugin_switch_status.clear();
            runtime.plugin_switch_error.clear();
        } else {
            runtime.plugin_delete_status.clear();
            runtime.plugin_delete_error = err.empty() ? "plugin delete failed" : err;
        }
    }

    auto render_plugin_like_card = [&](const char *title,
                                           bool enabled,
                                           const char *detail,
                                           const std::string &last_error,
                                           const std::function<void(bool)> &on_toggle,
                                           const std::function<void()> &inline_actions) {
            ImGui::PushID(title);
            ImGui::BeginGroup();
            ImGui::TextColored(enabled ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f) : ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                               "%s", enabled ? "启用" : "禁用");
            ImGui::SameLine();
            ImGui::Text("%s", title);
            if (detail != nullptr && detail[0] != '\0') {
                ImGui::TextDisabled("%s", detail);
            }
            if (!last_error.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Error: %s", last_error.c_str());
            }
            bool enabled_flag = enabled;
            if (ImGui::Checkbox("持久启用", &enabled_flag)) {
                on_toggle(enabled_flag);
            }
            if (inline_actions) {
                ImGui::SameLine();
                inline_actions();
            }
            ImGui::EndGroup();
            const ImVec2 card_min = ImGui::GetItemRectMin();
            const ImVec2 card_max = ImGui::GetItemRectMax();
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const ImVec2 window_pos = ImGui::GetWindowPos();
            const ImVec2 content_region_min = ImGui::GetWindowContentRegionMin();
            const ImVec2 content_region_max = ImGui::GetWindowContentRegionMax();
            const ImVec2 content_min(window_pos.x + content_region_min.x, window_pos.y + content_region_min.y);
            const ImVec2 content_max(window_pos.x + content_region_max.x, window_pos.y + content_region_max.y);
            const float card_height = card_max.y - card_min.y;
            ImGui::SetCursorScreenPos(ImVec2(content_min.x, card_min.y));
            ImGui::InvisibleButton("##card_button",
                                   ImVec2(content_max.x - content_min.x,
                                          card_height));
            const bool clicked = ImGui::IsItemClicked();
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 hover_min = ImGui::GetItemRectMin();
            const ImVec2 hover_max = ImGui::GetItemRectMax();
            if (hovered) {
                ImDrawList *draw_list = ImGui::GetWindowDrawList();
                const ImU32 hover_color = ImGui::GetColorU32(ImVec4(0.2f, 0.45f, 0.95f, 0.18f));
                draw_list->AddRectFilled(hover_min, hover_max, hover_color, 6.0f);
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            ImGui::SetCursorScreenPos(card_max);
            ImGui::Dummy(ImVec2(1.0f, 1.0f));
            ImGui::SetCursorScreenPos(cursor);
            ImGui::Dummy(ImVec2(1.0f, 1.0f));
            ImGui::PopID();
            return clicked;
        };

        auto build_model_detail = [](const std::string &override_path,
                                     const char *default_path,
                                     const char *prefix) {
            const std::string path = override_path.empty() ? default_path : override_path;
            return std::string(prefix) + ": " + path;
        };

        const auto default_specs = ScanDefaultPlugins();
        auto find_spec = [&](const std::string &name, DefaultPluginSpec *out_spec) -> bool {
            auto it = default_specs.find(name);
            if (it == default_specs.end()) return false;
            if (out_spec) *out_spec = it->second;
            return true;
        };

        DefaultPluginSpec asr_spec{};
        DefaultPluginSpec ocr_spec{};
        DefaultPluginSpec scene_spec{};
        DefaultPluginSpec facemesh_spec{};
        DefaultPluginSpec chat_spec{};
        const bool has_asr_spec = find_spec("asr", &asr_spec);
        const bool has_ocr_spec = find_spec("ocr", &ocr_spec);
        const bool has_scene_spec = find_spec("mobileclip", &scene_spec);
        const bool has_facemesh_spec = find_spec("facemesh", &facemesh_spec);
        const bool has_chat_spec = find_spec("chat", &chat_spec);

        if (runtime.asr_provider_entries.empty()) {
            RefreshAsrProviders(runtime);
        }
        std::string asr_provider_name = runtime.asr_current_provider_name;
        if (asr_provider_name.empty() &&
            runtime.asr_selected_entry_index >= 0 &&
            runtime.asr_selected_entry_index < static_cast<int>(runtime.asr_provider_entries.size())) {
            asr_provider_name = runtime.asr_provider_entries[static_cast<std::size_t>(runtime.asr_selected_entry_index)].name;
        }
        const std::string asr_model_detail = (has_asr_spec && !asr_spec.onnx.empty())
                                                 ? ("Model: " + asr_spec.onnx)
                                                 : "Model: (none)";
        const std::string asr_detail = asr_provider_name.empty()
                                           ? asr_model_detail
                                           : (asr_model_detail + " | Provider: " + asr_provider_name);

        ImGui::PushID("asr_plugin_card");
        if (render_plugin_like_card(
                "ASR",
                runtime.feature_asr_enabled,
                asr_detail.c_str(),
                runtime.asr_last_error,
                [&](bool enabled_flag) { runtime.feature_asr_enabled = enabled_flag; },
                [&]() {
                    std::string asr_pick_err;
                    std::string asr_picked_path;
                    if (RenderFilePickerButton("切换##asr", &asr_picked_path, "ONNX Model", "onnx", "", &asr_pick_err)) {
                        runtime.override_asr_model_path = asr_picked_path;
                        std::string err;
                        if (ApplyOverrideModels(runtime, &err)) {
                            runtime.asr_switch_status = "asr model override ok";
                            runtime.asr_switch_error.clear();
                        } else {
                            runtime.asr_switch_status.clear();
                            runtime.asr_switch_error = err.empty() ? "asr override failed" : err;
                        }
                    } else if (!asr_pick_err.empty()) {
                        runtime.asr_switch_status.clear();
                        runtime.asr_switch_error = asr_pick_err;
                    }
                })) {
            std::vector<std::string> assets;
            if (!runtime.override_asr_model_path.empty()) {
                assets.push_back(runtime.override_asr_model_path);
            } else if (has_asr_spec && !asr_spec.onnx.empty()) {
                assets.push_back(asr_spec.onnx);
            }
            open_detail(PluginDetailKind::Asr,
                        "ASR",
                        has_asr_spec ? asr_spec.source : std::string(),
                        assets,
                        "onnxruntime.cpu",
                        runtime.asr_ready ? "READY" : "NOT_READY",
                        runtime.asr_last_error);
        }
        ImGui::Separator();
        ImGui::PopID();

        ImGui::Dummy(ImVec2(0.0f, module_card_padding.y));

        if (runtime.ocr_model_entries.empty()) {
            RefreshOcrModels(runtime);
        }
        std::string ocr_model_name;
        if (runtime.ocr_selected_entry_index >= 0 &&
            runtime.ocr_selected_entry_index < static_cast<int>(runtime.ocr_model_entries.size())) {
            ocr_model_name = runtime.ocr_model_entries[static_cast<std::size_t>(runtime.ocr_selected_entry_index)].name;
        }
        if (ocr_model_name.empty() && runtime.ocr_model_input[0] != '\0') {
            ocr_model_name = runtime.ocr_model_input;
        }
        std::string ocr_detail = ocr_model_name.empty() ? "Model: (none)" : ("Model: " + ocr_model_name);
        if (has_ocr_spec) {
            if (!ocr_spec.det.empty()) {
                ocr_detail += " | det: " + ocr_spec.det;
            }
            if (!ocr_spec.rec.empty()) {
                ocr_detail += " | rec: " + ocr_spec.rec;
            }
            if (!ocr_spec.keys.empty()) {
                ocr_detail += " | keys: " + ocr_spec.keys;
            }
        }
        const OcrModelEntry *base_ocr = nullptr;
        if (runtime.ocr_selected_entry_index >= 0 &&
            runtime.ocr_selected_entry_index < static_cast<int>(runtime.ocr_model_entries.size())) {
            base_ocr = &runtime.ocr_model_entries[static_cast<std::size_t>(runtime.ocr_selected_entry_index)];
        } else if (!runtime.ocr_model_entries.empty()) {
            base_ocr = &runtime.ocr_model_entries.front();
        }

        ImGui::PushID("ocr_plugin_card");
        if (render_plugin_like_card(
                "OCR",
                runtime.feature_ocr_enabled,
                ocr_detail.c_str(),
                runtime.perception_state.ocr_last_error,
                [&](bool enabled_flag) { runtime.feature_ocr_enabled = enabled_flag; },
                [&]() {
                    std::string ocr_pick_err;
                    std::string ocr_picked_path;
                    if (RenderFilePickerButton("切换##ocr", &ocr_picked_path, "ONNX Model", "onnx", "", &ocr_pick_err)) {
                        if (base_ocr == nullptr) {
                            runtime.ocr_switch_status.clear();
                            runtime.ocr_switch_error = "ocr model base not available";
                        } else {
                            runtime.override_ocr_det_path = ocr_picked_path;
                            runtime.override_ocr_rec_path = base_ocr->rec_path;
                            runtime.override_ocr_keys_path = base_ocr->keys_path;
                            std::string err;
                            if (ApplyOverrideModels(runtime, &err)) {
                                runtime.ocr_switch_status = "ocr model override ok";
                                runtime.ocr_switch_error.clear();
                            } else {
                                runtime.ocr_switch_status.clear();
                                runtime.ocr_switch_error = err.empty() ? "ocr override failed" : err;
                            }
                        }
                    } else if (!ocr_pick_err.empty()) {
                        runtime.ocr_switch_status.clear();
                        runtime.ocr_switch_error = ocr_pick_err;
                    }
                })) {
            std::vector<std::string> assets;
            assets.reserve(3);
            const std::string det_path = !runtime.override_ocr_det_path.empty()
                                             ? runtime.override_ocr_det_path
                                             : (has_ocr_spec && !ocr_spec.det.empty()
                                                    ? ocr_spec.det
                                                    : (base_ocr ? base_ocr->det_path : std::string()));
            const std::string rec_path = !runtime.override_ocr_rec_path.empty()
                                             ? runtime.override_ocr_rec_path
                                             : (has_ocr_spec && !ocr_spec.rec.empty()
                                                    ? ocr_spec.rec
                                                    : (base_ocr ? base_ocr->rec_path : std::string()));
            const std::string keys_path = !runtime.override_ocr_keys_path.empty()
                                              ? runtime.override_ocr_keys_path
                                              : (has_ocr_spec && !ocr_spec.keys.empty()
                                                     ? ocr_spec.keys
                                                     : (base_ocr ? base_ocr->keys_path : std::string()));
            assets.push_back(det_path);
            assets.push_back(rec_path);
            assets.push_back(keys_path);
            open_detail(PluginDetailKind::Ocr,
                        "OCR",
                        has_ocr_spec ? ocr_spec.source : std::string(),
                        assets,
                        "onnxruntime.cpu",
                        runtime.perception_state.ocr_ready ? "READY" : "NOT_READY",
                        runtime.perception_state.ocr_last_error);
        }
        ImGui::Separator();
        ImGui::PopID();

        ImGui::Dummy(ImVec2(0.0f, module_card_padding.y));
 
        const std::string scene_default_model = has_scene_spec && !scene_spec.onnx.empty()
                                                    ? scene_spec.onnx
                                                    : std::string("assets/mobileclip_image.onnx");
        const std::string scene_default_labels = has_scene_spec && !scene_spec.labels.empty()
                                                     ? scene_spec.labels
                                                     : std::string("assets/mobileclip_labels.json");
        const std::string scene_detail = build_model_detail(runtime.override_scene_model_path,
                                                            scene_default_model.c_str(),
                                                            "Model");
        const std::string scene_labels_detail = build_model_detail(runtime.override_scene_labels_path,
                                                                   scene_default_labels.c_str(),
                                                                   "Labels");
        const std::string scene_full_detail = scene_detail + " | " + scene_labels_detail;
        ImGui::PushID("scene_plugin_card");
        if (render_plugin_like_card(
                "MobileCLIP",
                runtime.feature_scene_classifier_enabled,
                scene_full_detail.c_str(),
                runtime.perception_state.scene_classifier_last_error,
                [&](bool enabled_flag) { runtime.feature_scene_classifier_enabled = enabled_flag; },
                [&]() {
                    std::string scene_model_pick_err;
                    std::string scene_model_picked_path;
                    if (RenderFilePickerButton("模型##scene", &scene_model_picked_path, "ONNX Model", "onnx", "", &scene_model_pick_err)) {
                        runtime.override_scene_model_path = scene_model_picked_path;
                        std::string err;
                        if (ApplyOverrideModels(runtime, &err)) {
                            runtime.override_apply_status = "scene model override ok";
                            runtime.override_apply_error.clear();
                        } else {
                            runtime.override_apply_status.clear();
                            runtime.override_apply_error = err.empty() ? "scene override failed" : err;
                        }
                    } else if (!scene_model_pick_err.empty()) {
                        runtime.override_apply_status.clear();
                        runtime.override_apply_error = scene_model_pick_err;
                    }
                    ImGui::SameLine();
                    std::string scene_labels_pick_err;
                    std::string scene_labels_picked_path;
                    if (RenderFilePickerButton("标签##scene", &scene_labels_picked_path, "Labels", "json", "", &scene_labels_pick_err)) {
                        runtime.override_scene_labels_path = scene_labels_picked_path;
                        std::string err;
                        if (ApplyOverrideModels(runtime, &err)) {
                            runtime.override_apply_status = "scene labels override ok";
                            runtime.override_apply_error.clear();
                        } else {
                            runtime.override_apply_status.clear();
                            runtime.override_apply_error = err.empty() ? "scene override failed" : err;
                        }
                    } else if (!scene_labels_pick_err.empty()) {
                        runtime.override_apply_status.clear();
                        runtime.override_apply_error = scene_labels_pick_err;
                    }
                })) {
            std::vector<std::string> assets;
            assets.push_back(!runtime.override_scene_model_path.empty() ? runtime.override_scene_model_path : scene_default_model);
            assets.push_back(!runtime.override_scene_labels_path.empty() ? runtime.override_scene_labels_path : scene_default_labels);
            open_detail(PluginDetailKind::Scene,
                        "MobileCLIP",
                        has_scene_spec ? scene_spec.source : std::string(),
                        assets,
                        "onnxruntime.cpu",
                        runtime.perception_state.scene_classifier_ready ? "READY" : "NOT_READY",
                        runtime.perception_state.scene_classifier_last_error);
        }
        ImGui::Separator();
        ImGui::PopID();

        ImGui::Dummy(ImVec2(0.0f, module_card_padding.y));

        const std::string face_default_model = has_facemesh_spec && !facemesh_spec.onnx.empty()
                                                   ? facemesh_spec.onnx
                                                   : std::string("assets/facemesh.onnx");
        const std::string face_default_labels = has_facemesh_spec && !facemesh_spec.labels.empty()
                                                    ? facemesh_spec.labels
                                                    : std::string("assets/facemesh.labels.json");
        const std::string face_detail = build_model_detail(runtime.override_facemesh_model_path,
                                                           face_default_model.c_str(),
                                                           "Model");
        const std::string face_labels_detail = build_model_detail(runtime.override_facemesh_labels_path,
                                                                  face_default_labels.c_str(),
                                                                  "Labels");
        const std::string face_full_detail = face_detail + " | " + face_labels_detail;
        ImGui::PushID("facemesh_plugin_card");
        if (render_plugin_like_card(
                "FaceMesh",
                runtime.feature_face_emotion_enabled,
                face_full_detail.c_str(),
                runtime.perception_state.camera_facemesh_last_error,
                [&](bool enabled_flag) { runtime.feature_face_emotion_enabled = enabled_flag; },
                [&]() {
                    std::string face_model_pick_err;
                    std::string face_model_picked_path;
                    if (RenderFilePickerButton("模型##facemesh", &face_model_picked_path, "ONNX Model", "onnx", "", &face_model_pick_err)) {
                        runtime.override_facemesh_model_path = face_model_picked_path;
                        std::string err;
                        if (ApplyOverrideModels(runtime, &err)) {
                            runtime.override_apply_status = "facemesh model override ok";
                            runtime.override_apply_error.clear();
                        } else {
                            runtime.override_apply_status.clear();
                            runtime.override_apply_error = err.empty() ? "facemesh override failed" : err;
                        }
                    } else if (!face_model_pick_err.empty()) {
                        runtime.override_apply_status.clear();
                        runtime.override_apply_error = face_model_pick_err;
                    }
                    ImGui::SameLine();
                    std::string face_labels_pick_err;
                    std::string face_labels_picked_path;
                    if (RenderFilePickerButton("标签##facemesh", &face_labels_picked_path, "Labels", "json", "", &face_labels_pick_err)) {
                        runtime.override_facemesh_labels_path = face_labels_picked_path;
                        std::string err;
                        if (ApplyOverrideModels(runtime, &err)) {
                            runtime.override_apply_status = "facemesh labels override ok";
                            runtime.override_apply_error.clear();
                        } else {
                            runtime.override_apply_status.clear();
                            runtime.override_apply_error = err.empty() ? "facemesh override failed" : err;
                        }
                    } else if (!face_labels_pick_err.empty()) {
                        runtime.override_apply_status.clear();
                        runtime.override_apply_error = face_labels_pick_err;
                    }
                })) {
            std::vector<std::string> assets;
            assets.push_back(!runtime.override_facemesh_model_path.empty() ? runtime.override_facemesh_model_path : face_default_model);
            assets.push_back(!runtime.override_facemesh_labels_path.empty() ? runtime.override_facemesh_labels_path : face_default_labels);
            open_detail(PluginDetailKind::Facemesh,
                        "FaceMesh",
                        has_facemesh_spec ? facemesh_spec.source : std::string(),
                        assets,
                        "onnxruntime.cpu",
                        runtime.perception_state.camera_facemesh_ready ? "READY" : "NOT_READY",
                        runtime.perception_state.camera_facemesh_last_error);
        }
        ImGui::Separator();
        ImGui::PopID();

        ImGui::Dummy(ImVec2(0.0f, module_card_padding.y));

        std::string chat_detail = runtime.prefer_cloud_chat ? "Prefer: cloud" : "Prefer: offline";
        if (has_chat_spec) {
            if (chat_spec.model.empty() || chat_spec.model == "none") {
                chat_detail += " | Model: 无模型";
            } else {
                chat_detail += " | Model: " + chat_spec.model;
            }
        }
        ImGui::PushID("chat_plugin_card");
        if (render_plugin_like_card(
                "Chat",
                runtime.feature_chat_enabled,
                chat_detail.c_str(),
                runtime.chat_last_error,
                [&](bool enabled_flag) { runtime.feature_chat_enabled = enabled_flag; },
                [&]() {
                    const char *chat_modes[] = {"Offline Chat", "Cloud Chat"};
                    int chat_mode_index = runtime.prefer_cloud_chat ? 1 : 0;
                    ImGui::SetNextItemWidth(140.0f);
                    if (ImGui::Combo("##chat_mode", &chat_mode_index, chat_modes, IM_ARRAYSIZE(chat_modes))) {
                        runtime.prefer_cloud_chat = (chat_mode_index == 1);
                    }
                })) {
            std::vector<std::string> assets;
            open_detail(PluginDetailKind::Chat,
                        "Chat",
                        has_chat_spec ? chat_spec.source : std::string(),
                        assets,
                        runtime.prefer_cloud_chat ? "cloud+offline" : "offline",
                        runtime.chat_ready ? "READY" : "NOT_READY",
                        runtime.chat_last_error);
        }
        ImGui::Separator();
        ImGui::PopID();

        ImGui::SeparatorText("插件管理");
        if (ImGui::Button("Create")) {
            runtime.show_unified_plugin_create_modal = true;
        }

        ImGui::EndChild();

    if (runtime.show_unified_plugin_create_modal) {
        ImGui::OpenPopup("Create Plugin");
        runtime.show_unified_plugin_create_modal = false;
    }
    if (ImGui::BeginPopupModal("Create Plugin", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("New Plugin");
        ImGui::Separator();
        ImGui::InputTextWithHint("##new_plugin_name", "新建插件名称", runtime.unified_plugin_new_name_input,
                                 sizeof(runtime.unified_plugin_new_name_input));
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (ImGui::Button("Create")) {
            std::string err;
            UserPluginCreateRequest req{};
            req.name = runtime.unified_plugin_new_name_input;
            const bool ok = CreateUserPlugin(runtime, req, &err);
            if (ok) {
                runtime.unified_plugin_create_status = "plugin created";
                runtime.unified_plugin_create_error.clear();
                runtime.unified_plugin_refresh_requested = true;
                runtime.plugin_config_refresh_requested = true;
                RefreshUnifiedPlugins(runtime);
            } else {
                runtime.unified_plugin_create_status.clear();
                runtime.unified_plugin_create_error = err.empty() ? "plugin create failed" : err;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!runtime.override_apply_status.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", runtime.override_apply_status.c_str());
    }
    if (!runtime.override_apply_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", runtime.override_apply_error.c_str());
    }
    if (!runtime.unified_plugin_switch_status.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", runtime.unified_plugin_switch_status.c_str());
    }
    if (!runtime.unified_plugin_switch_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", runtime.unified_plugin_switch_error.c_str());
    }
    if (!runtime.unified_plugin_create_status.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", runtime.unified_plugin_create_status.c_str());
    }
    if (!runtime.unified_plugin_create_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", runtime.unified_plugin_create_error.c_str());
    }
    if (!runtime.unified_plugin_delete_status.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", runtime.unified_plugin_delete_status.c_str());
    }
    if (!runtime.unified_plugin_delete_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", runtime.unified_plugin_delete_error.c_str());
    }


}

void RenderRuntimeErrorPanel(AppRuntime &runtime) {
    static int error_filter_idx = 1; // 默认 Non-OK
    const char *filters[] = {"All", "Non-OK", "Failed", "Degraded"};
    error_filter_idx = std::clamp(error_filter_idx, 0, 3);

    ImGui::BeginChild("health_primary_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Runtime Error Classification");
    ImGui::BeginChild("error_table_child", ImVec2(-1.0f, 240.0f), ImGuiChildFlags_Borders);
    RenderRuntimeErrorClassificationTable(runtime, static_cast<ErrorViewFilter>(error_filter_idx));
    ImGui::EndChild();

    RenderRuntimeOpsActions(runtime);

    ImGui::EndChild();
}

}  // namespace desktoper2D
