#include "k2d/lifecycle/legacy_editor_ui_tools.h"

#include "imgui.h"

#include "k2d/core/model.h"
#include "k2d/lifecycle/state/app_runtime_state.h"

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

void RenderModelHierarchyTree(ModelRuntime &model,
                              int *selected_part_index,
                              const char *filter_text,
                              bool auto_expand_matches,
                              const std::function<void(int)> &on_select) {
    if (model.parts.empty()) {
        ImGui::TextDisabled("(no parts)");
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

    auto matches_filter = [&](const ModelPart &p) {
        if (filter.empty()) return true;
        std::string id = p.id;
        std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return id.find(filter) != std::string::npos;
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
        if (auto_expand_matches && !filter.empty() && !sub.empty()) flags |= ImGuiTreeNodeFlags_DefaultOpen;

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
        ImGui::InputTextWithHint("##resource_tree_filter", "Filter parts by id...", filter_text,
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
        ImGui::TextDisabled("No part selected");
        return;
    }

    ModelPart &part = model.parts[static_cast<std::size_t>(*selected_part_index)];
    ImGui::SeparatorText("Inspector");
    ImGui::Text("Part: %s", part.id.c_str());
    ImGui::TextDisabled("draw=%d, parent=%d", part.draw_order, part.parent_index);

    ImGui::SeparatorText("Deformer Tree");
    const bool is_warp_selected = selected_deformer_type && *selected_deformer_type == 0;
    const bool is_rotation_selected = selected_deformer_type && *selected_deformer_type == 1;

    ImGuiTreeNodeFlags deformer_root_flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
    const bool deformer_root_open = ImGui::TreeNodeEx("Deformer##deformer_root", deformer_root_flags);
    if (deformer_root_open) {
        ImGuiTreeNodeFlags warp_flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (is_warp_selected) warp_flags |= ImGuiTreeNodeFlags_Selected;
        ImGui::TreeNodeEx("Warp##deformer_warp", warp_flags);
        if (ImGui::IsItemClicked() && selected_deformer_type) {
            *selected_deformer_type = 0;
        }

        ImGuiTreeNodeFlags rotation_flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (is_rotation_selected) rotation_flags |= ImGuiTreeNodeFlags_Selected;
        ImGui::TreeNodeEx("Rotation##deformer_rotation", rotation_flags);
        if (ImGui::IsItemClicked() && selected_deformer_type) {
            *selected_deformer_type = 1;
        }

        ImGui::TreePop();
    }

    ImGui::SliderFloat("Opacity", &part.base_opacity, 0.0f, 1.0f, "%.3f");
    ImGui::DragFloat2("Position", &part.base_pos_x, 0.1f, -5000.0f, 5000.0f, "%.3f");
    ImGui::DragFloat2("Pivot", &part.pivot_x, 0.1f, -5000.0f, 5000.0f, "%.3f");
    ImGui::DragFloat("Rotation", &part.base_rot_deg, 0.1f, -3600.0f, 3600.0f, "%.3f");

    ImGui::DragFloat2("Scale", &part.base_scale_x, 0.01f, 0.01f, 100.0f, "%.3f");
    part.base_scale_x = std::max(0.01f, part.base_scale_x);
    part.base_scale_y = std::max(0.01f, part.base_scale_y);

    ImGui::SeparatorText("Deformer");
    int deformer_type_ui = part.deformer_type == DeformerType::Rotation ? 1 : 0;
    if (selected_deformer_type) {
        deformer_type_ui = std::clamp(*selected_deformer_type, 0, 1);
    }
    part.deformer_type = deformer_type_ui == 1 ? DeformerType::Rotation : DeformerType::Warp;

    if (part.deformer_type == DeformerType::Warp) {
        float warp_weight = std::clamp(part.ffd.weight.value(), 0.0f, 1.0f);
        if (ImGui::SliderFloat("Warp Weight", &warp_weight, 0.0f, 1.0f, "%.3f")) {
            part.ffd.weight.SetValueImmediate(warp_weight);
        }
    } else {
        ImGui::SliderFloat("Rotation Weight", &part.rotation_deformer_weight, 0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat("Rotation Speed", &part.rotation_deformer_speed, 0.0f, 10.0f, "%.2f");
        part.rotation_deformer_weight = std::clamp(part.rotation_deformer_weight, 0.0f, 1.0f);
        part.rotation_deformer_speed = std::max(0.0f, part.rotation_deformer_speed);
    }
}

}  // namespace k2d
