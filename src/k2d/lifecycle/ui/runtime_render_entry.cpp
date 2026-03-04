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
        }
        ImGui::End();
    }

    if (runtime.gui_enabled) {
        ImGui::SetNextWindowPos(ImVec2(12.0f, 120.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Runtime Debug");
        RenderAppDebugUi(runtime);

        ImGui::SeparatorText("Model Hierarchy");
        bridge.RenderModelHierarchyTree();

        ImGui::SeparatorText("Task Category");
        ImGui::Text("Primary: %s", bridge.TaskPrimaryCategoryName());
        ImGui::Text("Secondary: %s", bridge.TaskSecondaryCategoryName());

        if (ImGui::Button("Close Program")) {
            runtime.running = false;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(Esc)");

        RenderReminderPanel(runtime);

        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), runtime.renderer);
    SDL_RenderPresent(runtime.renderer);
}

}  // namespace k2d
