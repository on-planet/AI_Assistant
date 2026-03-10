#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_widgets.h"

namespace desktoper2D {

void RenderRuntimeOverviewPanel(AppRuntime &runtime) {
    const OverviewReadModel model = BuildOverviewReadModel(runtime);
    ImGui::SeparatorText("Status Card");
    if (ImGui::BeginTable("overview_status_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.45f);

            char buf[128] = {};
            SDL_snprintf(buf, sizeof(buf), "%.2f ms", model.frame_ms);
            RenderOverviewTableRow("Frame Time", buf);

            SDL_snprintf(buf, sizeof(buf), "%.2f", model.fps);
            RenderOverviewTableRow("FPS", buf);

            SDL_snprintf(buf, sizeof(buf), "%d count", model.model_parts);
            RenderOverviewTableRow("Model Parts", buf);

            RenderOverviewTableRow("Model Loaded", model.model_loaded ? "Yes" : "No");
            RenderOverviewTableRow("Plugin Degraded", model.plugin_degraded ? "Yes" : "No");

            SDL_snprintf(buf, sizeof(buf), "%.2f%%", model.plugin_timeout_rate * 100.0);
            RenderOverviewTableRow("Plugin Timeout Rate", buf);

            RenderOverviewTableRow("ASR Availability", model.asr_available ? "Available" : "Unavailable");
            RenderOverviewTableRow("Chat Availability", model.chat_available ? "Available" : "Unavailable");
            RenderOverviewTableRow("Recent Error Domain", RuntimeErrorDomainName(model.recent_error_domain));

            ImGui::EndTable();
        }

    RenderModuleLatencyPanel(runtime);

    ImGui::SeparatorText("Task Category");
    ImGui::Text("Primary: %s", model.task_primary);
    ImGui::Text("Secondary: %s", model.task_secondary);
}


}  // namespace desktoper2D
