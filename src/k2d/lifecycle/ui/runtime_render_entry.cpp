#include "k2d/lifecycle/ui/runtime_render_entry.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "k2d/lifecycle/state/app_runtime_state.h"
#include "k2d/lifecycle/ui/app_debug_ui.h"
#include "k2d/lifecycle/ui/reminder_panel.h"
#include "k2d/rendering/app_renderer.h"

namespace k2d {

void RunRuntimeRenderEntry(AppRuntime &runtime, const RuntimeRenderBridge &bridge) {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    RenderAppFrame(AppRenderContext{
        .renderer = runtime.renderer,
        .model_loaded = runtime.model_loaded,
        .model = &runtime.model,
        .demo_texture = runtime.demo_texture,
        .demo_texture_w = runtime.demo_texture_w,
        .demo_texture_h = runtime.demo_texture_h,
        .show_debug_stats = runtime.show_debug_stats,
        .manual_param_mode = runtime.manual_param_mode,
        .selected_param_index = runtime.selected_param_index,
        .edit_mode = runtime.edit_mode,
        .selected_part_index = runtime.selected_part_index,
        .gizmo_hover_handle = runtime.gizmo_hover_handle,
        .gizmo_active_handle = runtime.gizmo_active_handle,
        .editor_status = runtime.editor_status.c_str(),
        .editor_status_ttl = runtime.editor_status_ttl,
        .window_h = runtime.window_h,
        .debug_fps = runtime.debug_fps,
        .debug_frame_ms = runtime.debug_frame_ms,
        .has_model_parts = bridge.has_model_parts,
        .has_model_params = bridge.has_model_params,
        .ensure_selected_part_index_valid = bridge.ensure_selected_part_index_valid,
        .ensure_selected_param_index_valid = bridge.ensure_selected_param_index_valid,
        .compute_part_aabb = bridge.compute_part_aabb,
    });

    if (runtime.show_debug_stats) {
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGuiWindowFlags fps_flags = ImGuiWindowFlags_NoDecoration |
                                     ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoFocusOnAppearing |
                                     ImGuiWindowFlags_NoNav;
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
        if (ImGui::Begin("FPS Overlay", nullptr, fps_flags)) {
            ImGui::Text("FPS: %.1f", runtime.debug_fps);
            ImGui::Text("Frame: %.2f ms", runtime.debug_frame_ms);
            ImGui::Text("Parts: %d/%d",
                        runtime.model.debug_stats.drawn_part_count,
                        runtime.model.debug_stats.part_count);
            ImGui::Text("Verts: %d  Tris: %d",
                        runtime.model.debug_stats.vertex_count,
                        runtime.model.debug_stats.triangle_count);
            ImGui::Separator();
            ImGui::Checkbox("Runtime Debug GUI", &runtime.gui_enabled);
        }
        ImGui::End();
    }

    if (runtime.gui_enabled) {
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        const float base_x = vp ? vp->WorkPos.x : 0.0f;
        const float base_y = vp ? vp->WorkPos.y : 0.0f;
        const float work_w = vp ? vp->WorkSize.x : static_cast<float>(runtime.window_w);
        const float work_h = vp ? vp->WorkSize.y : static_cast<float>(runtime.window_h);

        const float margin = 12.0f;
        const float gap = 8.0f;
        const float top_offset = runtime.show_debug_stats ? 108.0f : 48.0f;

        const float usable_w = std::max(720.0f, work_w - margin * 2.0f);
        const float usable_h = std::max(420.0f, work_h - top_offset - margin);

        const bool compact_layout = usable_w < 1200.0f;
        const float debug_w = compact_layout ? usable_w : std::max(460.0f, usable_w * 0.52f);
        const float right_w = compact_layout ? usable_w : std::max(300.0f, usable_w - debug_w - gap);

        ImGui::SetNextWindowPos(ImVec2(base_x + margin, base_y + top_offset), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(debug_w, usable_h), ImGuiCond_FirstUseEver);
        ImGui::Begin("Runtime Debug");
        RenderAppDebugUi(runtime);
        ImGui::End();

        const float right_x = compact_layout ? (base_x + margin) : (base_x + margin + debug_w + gap);
        const float inspector_h = compact_layout ? std::max(240.0f, usable_h * 0.58f) : std::max(300.0f, usable_h * 0.68f);
        const float reminder_h = std::max(180.0f, usable_h - inspector_h - gap);

        ImGui::SetNextWindowPos(ImVec2(right_x, base_y + top_offset), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(right_w, inspector_h), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Model Hierarchy + Inspector")) {
            bridge.RenderResourceTreeInspector();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(right_x, base_y + top_offset + inspector_h + gap), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(right_w, reminder_h), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Reminder")) {
            RenderReminderPanel(runtime);
        }
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), runtime.renderer);
    SDL_RenderPresent(runtime.renderer);
}

}  // namespace k2d
