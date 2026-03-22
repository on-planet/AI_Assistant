#include "desktoper2D/lifecycle/ui/runtime_imgui_backend.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

namespace desktoper2D {

bool InitRuntimeImGuiBackends(const RuntimeWindowState &window_state) {
    if (!window_state.window || !window_state.renderer) {
        return false;
    }

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window_state.window, window_state.renderer)) {
        return false;
    }

    if (!ImGui_ImplSDLRenderer3_Init(window_state.renderer)) {
        ImGui_ImplSDL3_Shutdown();
        return false;
    }

    return true;
}

void ShutdownRuntimeImGuiBackends() {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
}

void BeginRuntimeImGuiFrame() {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
}

void RenderRuntimeImGuiDrawData(const RuntimeWindowState &window_state) {
    if (!window_state.renderer) {
        return;
    }

    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), window_state.renderer);
}

}  // namespace desktoper2D
