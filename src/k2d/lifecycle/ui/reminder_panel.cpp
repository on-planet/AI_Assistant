#include "k2d/lifecycle/ui/reminder_panel.h"

#include <algorithm>
#include <ctime>
#include <string>

#include "imgui.h"

#include "k2d/lifecycle/state/app_runtime_state.h"

namespace k2d {

void RenderReminderPanel(AppRuntime &runtime) {
    ImGui::SeparatorText("Schedule Reminder (SQLite)");
    ImGui::InputText("Title", runtime.reminder_title_input, static_cast<int>(sizeof(runtime.reminder_title_input)));
    ImGui::InputInt("After Minutes", &runtime.reminder_after_min, 1, 10);
    runtime.reminder_after_min = std::max(1, runtime.reminder_after_min);

    if (ImGui::Button("Add Reminder")) {
        if (!runtime.reminder_ready) {
            runtime.reminder_last_error = "reminder service not ready";
        } else {
            const std::int64_t now_sec = static_cast<std::int64_t>(std::time(nullptr));
            const std::int64_t due_sec = now_sec + static_cast<std::int64_t>(runtime.reminder_after_min) * 60;
            std::string add_err;
            if (runtime.reminder_service.AddReminder(runtime.reminder_title_input, due_sec, &add_err)) {
                runtime.reminder_last_error.clear();
                runtime.reminder_upcoming = runtime.reminder_service.ListActive(32, nullptr);
            } else {
                runtime.reminder_last_error = add_err;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        if (runtime.reminder_ready) {
            std::string list_err;
            runtime.reminder_upcoming = runtime.reminder_service.ListActive(32, &list_err);
            if (!list_err.empty()) {
                runtime.reminder_last_error = list_err;
            }
        }
    }

    if (!runtime.reminder_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Reminder Error: %s", runtime.reminder_last_error.c_str());
    }

    ImGui::Text("Active Reminders:");
    const std::int64_t now_sec_ui = static_cast<std::int64_t>(std::time(nullptr));
    for (const auto &item : runtime.reminder_upcoming) {
        ImGui::PushID(static_cast<int>(item.id));
        const long long remain_sec = static_cast<long long>(item.due_unix_sec - now_sec_ui);
        ImGui::Text("[%lld] %s", static_cast<long long>(item.id), item.title.c_str());
        ImGui::SameLine();
        if (remain_sec >= 0) {
            ImGui::TextDisabled("in %llds", remain_sec);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "overdue %llds", -remain_sec);
        }

        if (ImGui::Button("Complete")) {
            std::string op_err;
            if (runtime.reminder_service.MarkCompleted(item.id, true, &op_err)) {
                runtime.reminder_upcoming = runtime.reminder_service.ListActive(32, nullptr);
                runtime.reminder_last_error.clear();
            } else {
                runtime.reminder_last_error = op_err;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            std::string op_err;
            if (runtime.reminder_service.DeleteReminder(item.id, &op_err)) {
                runtime.reminder_upcoming = runtime.reminder_service.ListActive(32, nullptr);
                runtime.reminder_last_error.clear();
            } else {
                runtime.reminder_last_error = op_err;
            }
        }
        ImGui::Separator();
        ImGui::PopID();
    }
}

}  // namespace k2d
