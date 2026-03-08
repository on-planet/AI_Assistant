#include "k2d/lifecycle/legacy_editor_ui_tools.h"

#include "imgui.h"

#include "k2d/core/model.h"
#include "k2d/lifecycle/state/app_runtime_state.h"
#include "k2d/lifecycle/ui/ui_empty_state.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace k2d {

void SetEditorStatus(AppRuntime &runtime, std::string text, float ttl_sec) {
    runtime.editor_status = std::move(text);
    runtime.editor_status_ttl = std::max(0.0f, ttl_sec);
}

float QuantizeToGrid(float v, float grid) {
    const float g = std::max(0.001f, std::abs(grid));
    return std::round(v / g) * g;
}

const char *AxisConstraintName(AxisConstraint c) {
    switch (c) {
        case AxisConstraint::XOnly: return "X";
        case AxisConstraint::YOnly: return "Y";
        default: return "None";
    }
}

const char *EditorPropName(EditorProp p) {
    switch (p) {
        case EditorProp::PosX: return "posX";
        case EditorProp::PosY: return "posY";
        case EditorProp::PivotX: return "pivotX";
        case EditorProp::PivotY: return "pivotY";
        case EditorProp::RotDeg: return "rotDeg";
        case EditorProp::ScaleX: return "scaleX";
        case EditorProp::ScaleY: return "scaleY";
        default: return "unknown";
    }
}

const char *BindingTypeNameUi(BindingType type) {
    switch (type) {
        case BindingType::PosX: return "PosX";
        case BindingType::PosY: return "PosY";
        case BindingType::RotDeg: return "RotDeg";
        case BindingType::ScaleX: return "ScaleX";
        case BindingType::ScaleY: return "ScaleY";
        case BindingType::Opacity: return "Opacity";
        default: return "Unknown";
    }
}

void RenderModelHierarchyTree(ModelRuntime &model,
                              int *selected_part_index,
                              const char *filter_text,
                              bool auto_expand_matches,
                              const std::function<void(int)> &on_select) {
    if (model.parts.empty()) {
        RenderUnifiedEmptyState("model_hierarchy_no_parts_empty_state",
                                "无部件",
                                "当前模型没有可显示的部件层级，请先检查模型资源或导入结果。",
                                ImVec4(0.70f, 0.78f, 1.0f, 1.0f));
        return;
    }

    if (ImGui::Button("Hide All")) {
        for (auto &p : model.parts) {
            p.base_opacity = 0.0f;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Show All")) {
        for (auto &p : model.parts) {
            p.base_opacity = 1.0f;
        }
    }

    std::string filter = filter_text ? filter_text : "";
    std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    auto to_lower_copy = [](const std::string &s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return out;
    };

    auto contains_filter = [&](const std::string &text) {
        if (filter.empty()) return true;
        return to_lower_copy(text).find(filter) != std::string::npos;
    };

    auto matches_filter = [&](const ModelPart &p) {
        if (filter.empty()) return true;
        if (contains_filter(p.id)) {
            return true;
        }
        if (!p.texture_cache_key.empty() && contains_filter(p.texture_cache_key)) {
            return true;
        }
        for (const auto &binding : p.bindings) {
            if (binding.param_index >= 0 && binding.param_index < static_cast<int>(model.parameters.size())) {
                const auto &param = model.parameters[static_cast<std::size_t>(binding.param_index)];
                if (contains_filter(param.id)) {
                    return true;
                }
            }
        }
        return false;
    };

    std::vector<std::vector<int>> children(model.parts.size());
    std::vector<int> roots;
    roots.reserve(model.parts.size());

    for (int i = 0; i < static_cast<int>(model.parts.size()); ++i) {
        const int parent = model.parts[static_cast<std::size_t>(i)].parent_index;
        if (parent >= 0 && parent < static_cast<int>(model.parts.size())) {
            children[static_cast<std::size_t>(parent)].push_back(i);
        } else {
            roots.push_back(i);
        }
    }

    for (auto &v : children) {
        std::sort(v.begin(), v.end(), [&](int a, int b) {
            const auto &pa = model.parts[static_cast<std::size_t>(a)];
            const auto &pb = model.parts[static_cast<std::size_t>(b)];
            if (pa.draw_order != pb.draw_order) return pa.draw_order < pb.draw_order;
            return pa.id < pb.id;
        });
    }
    std::sort(roots.begin(), roots.end(), [&](int a, int b) {
        const auto &pa = model.parts[static_cast<std::size_t>(a)];
        const auto &pb = model.parts[static_cast<std::size_t>(b)];
        if (pa.draw_order != pb.draw_order) return pa.draw_order < pb.draw_order;
        return pa.id < pb.id;
    });

    auto has_visible_descendant = [&](auto &&self, int idx) -> bool {
        const auto &p = model.parts[static_cast<std::size_t>(idx)];
        if (matches_filter(p)) return true;
        for (int c : children[static_cast<std::size_t>(idx)]) {
            if (self(self, c)) return true;
        }
        return false;
    };

    std::vector<std::uint8_t> selected_path_flags(model.parts.size(), 0);
    if (selected_part_index && *selected_part_index >= 0 && *selected_part_index < static_cast<int>(model.parts.size())) {
        int walk = *selected_part_index;
        while (walk >= 0 && walk < static_cast<int>(model.parts.size())) {
            selected_path_flags[static_cast<std::size_t>(walk)] = 1;
            walk = model.parts[static_cast<std::size_t>(walk)].parent_index;
        }
    }

    auto draw_node = [&](auto &&self, int idx) -> void {
        auto &part = model.parts[static_cast<std::size_t>(idx)];
        const auto &sub = children[static_cast<std::size_t>(idx)];

        const bool self_match = matches_filter(part);
        const bool visible = self_match || has_visible_descendant(has_visible_descendant, idx);
        if (!visible) {
            return;
        }

        ImGui::PushID(idx);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (sub.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        if (selected_part_index && idx == *selected_part_index) flags |= ImGuiTreeNodeFlags_Selected;
        if (((auto_expand_matches && !filter.empty()) || selected_path_flags[static_cast<std::size_t>(idx)] != 0) && !sub.empty()) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }

        const std::string label = part.id + "##part_tree";
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
        if (ImGui::IsItemClicked() && selected_part_index) {
            *selected_part_index = idx;
            if (on_select) on_select(idx);
        }

        ImGui::SameLine();
        const bool hidden = part.base_opacity <= 0.001f;
        if (ImGui::SmallButton(hidden ? "Show" : "Hide")) {
            part.base_opacity = hidden ? 1.0f : 0.0f;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("(draw=%d, parent=%d)", part.draw_order, part.parent_index);

        if (open) {
            for (int c : sub) {
                self(self, c);
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    };

    for (int r : roots) {
        draw_node(draw_node, r);
    }
}

void RenderResourceTreeInspector(ModelRuntime &model,
                                 int *selected_part_index,
                                 int *selected_deformer_type,
                                 char *filter_text,
                                 int filter_text_capacity,
                                 bool *auto_expand_matches,
                                 const std::function<void(int)> &on_select) {
    if (filter_text && filter_text_capacity > 0) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##resource_tree_filter", "搜索节点/参数/部件名...", filter_text,
                                 static_cast<std::size_t>(filter_text_capacity));
    }

    if (auto_expand_matches) {
        ImGui::Checkbox("Auto Expand Matches", auto_expand_matches);
    }

    RenderModelHierarchyTree(model,
                             selected_part_index,
                             filter_text,
                             auto_expand_matches ? *auto_expand_matches : true,
                             on_select);

    if (!selected_part_index || *selected_part_index < 0 ||
        *selected_part_index >= static_cast<int>(model.parts.size())) {
        ImGui::SeparatorText("Inspector");
        RenderUnifiedEmptyState("inspector_no_part_selected_empty_state",
                                "未选择部件",
                                "请先在左侧层级树中选择一个部件，再查看其检查器与引用信息。",
                                ImVec4(1.0f, 0.82f, 0.25f, 1.0f));
        return;
    }

    ModelPart &part = model.parts[static_cast<std::size_t>(*selected_part_index)];
    ImGui::SeparatorText("Inspector");
    ImGui::Text("Part: %s", part.id.c_str());
    const bool part_dirty = part.transform_dirty || part.deformer_dirty || part.ffd_delta_dirty || part.render_cache_dirty;
    ImGui::TextDisabled("draw=%d, parent=%d", part.draw_order, part.parent_index);
    ImGui::SameLine();
    ImGui::TextColored(part_dirty ? ImVec4(1.0f, 0.82f, 0.25f, 1.0f) : ImVec4(0.45f, 0.85f, 0.45f, 1.0f),
                       "%s",
                       part_dirty ? "未保存修改" : "已保存");

    auto push_inspector_command_if_changed = [&](const EditCommand &cmd) {
        if (cmd.before_pos_x != cmd.after_pos_x ||
            cmd.before_pos_y != cmd.after_pos_y ||
            cmd.before_pivot_x != cmd.after_pivot_x ||
            cmd.before_pivot_y != cmd.after_pivot_y ||
            cmd.before_rot_deg != cmd.after_rot_deg ||
            cmd.before_scale_x != cmd.after_scale_x ||
            cmd.before_scale_y != cmd.after_scale_y ||
            cmd.before_opacity != cmd.after_opacity ||
            cmd.before_deformer_type != cmd.after_deformer_type ||
            cmd.before_rotation_weight != cmd.after_rotation_weight ||
            cmd.before_rotation_speed != cmd.after_rotation_speed ||
            cmd.before_warp_weight != cmd.after_warp_weight) {
            PushEditCommand(g_runtime.undo_stack, g_runtime.redo_stack, cmd);
            g_runtime.editor_project_dirty = true;
        }
    };

    ImGui::SeparatorText("References");
    ImGui::TextWrapped("Texture: %s",
                       part.texture_cache_key.empty() ? "(none)" : part.texture_cache_key.c_str());
    ImGui::Text("Bindings: %d", static_cast<int>(part.bindings.size()));
    if (part.bindings.empty()) {
        RenderUnifiedEmptyState("inspector_no_binding_refs_empty_state",
                                "无参数引用",
                                "当前部件还没有绑定任何参数或驱动映射，可在编辑面板中创建绑定。",
                                ImVec4(0.70f, 0.78f, 1.0f, 1.0f));
    } else {
        if (ImGui::BeginTable("inspector_binding_refs", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Param", ImGuiTableColumnFlags_WidthStretch, 0.22f);
            ImGui::TableSetupColumn("Driver", ImGuiTableColumnFlags_WidthStretch, 0.14f);
            ImGui::TableSetupColumn("InMin", ImGuiTableColumnFlags_WidthStretch, 0.12f);
            ImGui::TableSetupColumn("InMax", ImGuiTableColumnFlags_WidthStretch, 0.12f);
            ImGui::TableSetupColumn("OutMin", ImGuiTableColumnFlags_WidthStretch, 0.12f);
            ImGui::TableSetupColumn("OutMax", ImGuiTableColumnFlags_WidthStretch, 0.12f);
            ImGui::TableHeadersRow();

            for (const auto &binding : part.bindings) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (binding.param_index >= 0 && binding.param_index < static_cast<int>(model.parameters.size())) {
                    ImGui::TextUnformatted(model.parameters[static_cast<std::size_t>(binding.param_index)].id.c_str());
                } else {
                    ImGui::TextDisabled("(invalid param)");
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(BindingTypeNameUi(binding.type));
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.2f", binding.in_min);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.2f", binding.in_max);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.2f", binding.out_min);
                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%.2f", binding.out_max);
            }
            ImGui::EndTable();
        }
    }

    int channel_ref_count = 0;
    for (const auto &channel : model.animation_channels) {
        if (channel.param_index < 0 || channel.param_index >= static_cast<int>(model.parameters.size())) {
            continue;
        }
        const std::string &param_id = model.parameters[static_cast<std::size_t>(channel.param_index)].id;
        for (const auto &binding : part.bindings) {
            if (binding.param_index >= 0 && binding.param_index < static_cast<int>(model.parameters.size()) &&
                model.parameters[static_cast<std::size_t>(binding.param_index)].id == param_id) {
                channel_ref_count += 1;
                break;
            }
        }
    }
    ImGui::Text("Animation Channel References: %d", channel_ref_count);
    if (channel_ref_count > 0) {
        if (ImGui::BeginTable("inspector_channel_refs", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthStretch, 0.24f);
            ImGui::TableSetupColumn("Param", ImGuiTableColumnFlags_WidthStretch, 0.22f);
            ImGui::TableSetupColumn("Interp", ImGuiTableColumnFlags_WidthStretch, 0.14f);
            ImGui::TableSetupColumn("Wrap", ImGuiTableColumnFlags_WidthStretch, 0.14f);
            ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthStretch, 0.10f);
            ImGui::TableHeadersRow();

            for (const auto &channel : model.animation_channels) {
                if (channel.param_index < 0 || channel.param_index >= static_cast<int>(model.parameters.size())) {
                    continue;
                }
                const std::string &param_id = model.parameters[static_cast<std::size_t>(channel.param_index)].id;
                bool referenced = false;
                for (const auto &binding : part.bindings) {
                    if (binding.param_index >= 0 && binding.param_index < static_cast<int>(model.parameters.size()) &&
                        model.parameters[static_cast<std::size_t>(binding.param_index)].id == param_id) {
                        referenced = true;
                        break;
                    }
                }
                if (!referenced) {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(channel.id.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(param_id.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(channel.timeline_interp == TimelineInterpolation::Step
                                           ? "Step"
                                           : (channel.timeline_interp == TimelineInterpolation::Linear ? "Linear" : "Hermite"));
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(channel.timeline_wrap == TimelineWrapMode::Clamp
                                           ? "Clamp"
                                           : (channel.timeline_wrap == TimelineWrapMode::Loop ? "Loop" : "PingPong"));
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(channel.enabled ? "Yes" : "No");
            }
            ImGui::EndTable();
        }
    } else {
        RenderUnifiedEmptyState("inspector_no_track_refs_empty_state",
                                "无动画轨道引用",
                                "当前部件没有关联的动画通道，可先创建时间轴通道并绑定到相关参数。",
                                ImVec4(1.0f, 0.82f, 0.25f, 1.0f));
    }

    ImGui::Text("Deformer Driver: %s", part.deformer_type == DeformerType::Warp ? "Warp" : "Rotation");

    ImGui::SeparatorText("Deformer Tree");
    const bool is_warp_selected = selected_deformer_type && *selected_deformer_type == 0;
    const bool is_rotation_selected = selected_deformer_type && *selected_deformer_type == 1;

    ImGuiTreeNodeFlags deformer_root_flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
    const bool deformer_root_open = ImGui::TreeNodeEx("Deformer##deformer_root", deformer_root_flags);
    if (deformer_root_open) {
        ImGuiTreeNodeFlags warp_flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth |
                                        ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (is_warp_selected) warp_flags |= ImGuiTreeNodeFlags_Selected;
        ImGui::TreeNodeEx("Warp##deformer_warp", warp_flags);
        if (ImGui::IsItemClicked() && selected_deformer_type) {
            *selected_deformer_type = 0;
        }

        ImGuiTreeNodeFlags rotation_flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth |
                                            ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (is_rotation_selected) rotation_flags |= ImGuiTreeNodeFlags_Selected;
        ImGui::TreeNodeEx("Rotation##deformer_rotation", rotation_flags);
        if (ImGui::IsItemClicked() && selected_deformer_type) {
            *selected_deformer_type = 1;
        }

        ImGui::TreePop();
    }

    EditCommand inspector_cmd{};
    inspector_cmd.type = EditCommand::Type::Inspector;
    inspector_cmd.part_id = part.id;
    inspector_cmd.before_pos_x = part.base_pos_x;
    inspector_cmd.before_pos_y = part.base_pos_y;
    inspector_cmd.before_pivot_x = part.pivot_x;
    inspector_cmd.before_pivot_y = part.pivot_y;
    inspector_cmd.before_rot_deg = part.base_rot_deg;
    inspector_cmd.before_scale_x = part.base_scale_x;
    inspector_cmd.before_scale_y = part.base_scale_y;
    inspector_cmd.before_opacity = part.base_opacity;
    inspector_cmd.before_deformer_type = part.deformer_type == DeformerType::Rotation ? 1 : 0;
    inspector_cmd.before_rotation_weight = part.rotation_deformer_weight;
    inspector_cmd.before_rotation_speed = part.rotation_deformer_speed;
    inspector_cmd.before_warp_weight = part.ffd.weight.value();

    bool inspector_changed = false;
    inspector_changed |= ImGui::SliderFloat("Opacity", &part.base_opacity, 0.0f, 1.0f, "%.3f");
    inspector_changed |= ImGui::DragFloat2("Position", &part.base_pos_x, 0.1f, -5000.0f, 5000.0f, "%.3f");
    inspector_changed |= ImGui::DragFloat2("Pivot", &part.pivot_x, 0.1f, -5000.0f, 5000.0f, "%.3f");
    inspector_changed |= ImGui::DragFloat("Rotation", &part.base_rot_deg, 0.1f, -3600.0f, 3600.0f, "%.3f");

    inspector_changed |= ImGui::DragFloat2("Scale", &part.base_scale_x, 0.01f, 0.01f, 100.0f, "%.3f");
    part.base_scale_x = std::max(0.01f, part.base_scale_x);
    part.base_scale_y = std::max(0.01f, part.base_scale_y);

    ImGui::SeparatorText("Deformer");
    int deformer_type_ui = part.deformer_type == DeformerType::Rotation ? 1 : 0;
    if (selected_deformer_type) {
        deformer_type_ui = std::clamp(*selected_deformer_type, 0, 1);
    }
    const int before_deformer_type_ui = deformer_type_ui;
    part.deformer_type = deformer_type_ui == 1 ? DeformerType::Rotation : DeformerType::Warp;

    if (part.deformer_type == DeformerType::Warp) {
        float warp_weight = std::clamp(part.ffd.weight.value(), 0.0f, 1.0f);
        if (ImGui::SliderFloat("Warp Weight", &warp_weight, 0.0f, 1.0f, "%.3f")) {
            part.ffd.weight.SetValueImmediate(warp_weight);
            inspector_changed = true;
        }
    } else {
        inspector_changed |= ImGui::SliderFloat("Rotation Weight", &part.rotation_deformer_weight, 0.0f, 1.0f, "%.3f");
        inspector_changed |= ImGui::SliderFloat("Rotation Speed", &part.rotation_deformer_speed, 0.0f, 10.0f, "%.2f");
        part.rotation_deformer_weight = std::clamp(part.rotation_deformer_weight, 0.0f, 1.0f);
        part.rotation_deformer_speed = std::max(0.0f, part.rotation_deformer_speed);
    }

    inspector_cmd.after_pos_x = part.base_pos_x;
    inspector_cmd.after_pos_y = part.base_pos_y;
    inspector_cmd.after_pivot_x = part.pivot_x;
    inspector_cmd.after_pivot_y = part.pivot_y;
    inspector_cmd.after_rot_deg = part.base_rot_deg;
    inspector_cmd.after_scale_x = part.base_scale_x;
    inspector_cmd.after_scale_y = part.base_scale_y;
    inspector_cmd.after_opacity = part.base_opacity;
    inspector_cmd.after_deformer_type = part.deformer_type == DeformerType::Rotation ? 1 : 0;
    inspector_cmd.after_rotation_weight = part.rotation_deformer_weight;
    inspector_cmd.after_rotation_speed = part.rotation_deformer_speed;
    inspector_cmd.after_warp_weight = part.ffd.weight.value();

    if (inspector_changed || inspector_cmd.before_deformer_type != inspector_cmd.after_deformer_type ||
        before_deformer_type_ui != inspector_cmd.after_deformer_type) {
        push_inspector_command_if_changed(inspector_cmd);
    }
}

}  // namespace k2d
