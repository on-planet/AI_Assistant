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

void RenderModelHierarchyTree(ModelRuntime &model, int selected_part_index) {
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

    auto draw_node = [&](auto &&self, int idx) -> void {
        auto &part = model.parts[static_cast<std::size_t>(idx)];
        const auto &sub = children[static_cast<std::size_t>(idx)];

        ImGui::PushID(idx);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (sub.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        if (idx == selected_part_index) flags |= ImGuiTreeNodeFlags_Selected;

        const std::string label = part.id + "##part_tree";
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

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

}  // namespace k2d
