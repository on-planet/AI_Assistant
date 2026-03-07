#include "k2d/lifecycle/ui/reminder_panel.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>

#include "imgui.h"

#include "k2d/lifecycle/state/app_runtime_state.h"

namespace k2d {

void RenderReminderPanel(AppRuntime &runtime) {
    constexpr int kPageSize = 8;

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
                runtime.reminder_upcoming = runtime.reminder_service.ListActive(64, nullptr);
                runtime.reminder_page = 0;
            } else {
                runtime.reminder_last_error = add_err;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        if (runtime.reminder_ready) {
            std::string list_err;
            runtime.reminder_upcoming = runtime.reminder_service.ListActive(64, &list_err);
            if (!list_err.empty()) {
                runtime.reminder_last_error = list_err;
            }
        }
    }

    if (!runtime.reminder_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Reminder Error: %s", runtime.reminder_last_error.c_str());
    }

    ImGui::SeparatorText("Search / Filter");
    if (ImGui::InputTextWithHint("##reminder_search", "Search title...", runtime.reminder_search,
                                 static_cast<int>(sizeof(runtime.reminder_search)))) {
        runtime.reminder_page = 0;
    }
    const char *filter_items[] = {"All", "Upcoming", "Overdue"};
    if (ImGui::Combo("Status", &runtime.reminder_filter_mode, filter_items, 3)) {
        runtime.reminder_page = 0;
    }
    const char *sort_items[] = {"Due Asc", "Due Desc", "Overdue First"};
    if (ImGui::Combo("Sort", &runtime.reminder_sort_mode, sort_items, 3)) {
        runtime.reminder_page = 0;
    }

    const std::int64_t now_sec_ui = static_cast<std::int64_t>(std::time(nullptr));
    std::vector<std::size_t> matched_indices;
    matched_indices.reserve(runtime.reminder_upcoming.size());

    std::string needle = runtime.reminder_search;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (std::size_t i = 0; i < runtime.reminder_upcoming.size(); ++i) {
        const auto &item = runtime.reminder_upcoming[i];
        const long long remain_sec = static_cast<long long>(item.due_unix_sec - now_sec_ui);

        if (runtime.reminder_filter_mode == 1 && remain_sec < 0) continue;
        if (runtime.reminder_filter_mode == 2 && remain_sec >= 0) continue;

        if (!needle.empty()) {
            std::string hay = item.title;
            std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (hay.find(needle) == std::string::npos) continue;
        }

        matched_indices.push_back(i);
    }

    std::sort(matched_indices.begin(), matched_indices.end(), [&](std::size_t a, std::size_t b) {
        const auto &ia = runtime.reminder_upcoming[a];
        const auto &ib = runtime.reminder_upcoming[b];
        const long long ra = static_cast<long long>(ia.due_unix_sec - now_sec_ui);
        const long long rb = static_cast<long long>(ib.due_unix_sec - now_sec_ui);

        if (runtime.reminder_sort_mode == 2) {
            const bool a_overdue = ra < 0;
            const bool b_overdue = rb < 0;
            if (a_overdue != b_overdue) {
                return a_overdue > b_overdue;
            }
            if (ia.due_unix_sec != ib.due_unix_sec) {
                return ia.due_unix_sec < ib.due_unix_sec;
            }
            return ia.id < ib.id;
        }

        if (ia.due_unix_sec != ib.due_unix_sec) {
            const bool asc = runtime.reminder_sort_mode == 0;
            return asc ? (ia.due_unix_sec < ib.due_unix_sec) : (ia.due_unix_sec > ib.due_unix_sec);
        }
        return ia.id < ib.id;
    });

    const int total_items = static_cast<int>(matched_indices.size());
    const int total_pages = std::max(1, (total_items + kPageSize - 1) / kPageSize);
    runtime.reminder_page = std::clamp(runtime.reminder_page, 0, total_pages - 1);

    ImGui::SeparatorText("Active Reminders");
    ImGui::Text("Matched: %d", total_items);
    ImGui::SameLine();
    if (ImGui::Button("Prev Page") && runtime.reminder_page > 0) {
        runtime.reminder_page -= 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Next Page") && runtime.reminder_page + 1 < total_pages) {
        runtime.reminder_page += 1;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Page %d / %d", runtime.reminder_page + 1, total_pages);

    const int start = runtime.reminder_page * kPageSize;
    const int end = std::min(total_items, start + kPageSize);

    ImGui::BeginChild("reminder_list_scroll", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    for (int idx = start; idx < end; ++idx) {
        const auto &item = runtime.reminder_upcoming[matched_indices[static_cast<std::size_t>(idx)]];
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
                runtime.reminder_upcoming = runtime.reminder_service.ListActive(64, nullptr);
                runtime.reminder_last_error.clear();
                const bool deleting_last_on_page = (start == end - 1) && (idx == end - 1) && (runtime.reminder_page > 0);
                if (deleting_last_on_page) {
                    runtime.reminder_page -= 1;
                }
                ImGui::PopID();
                break;
            } else {
                runtime.reminder_last_error = op_err;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            std::string op_err;
            if (runtime.reminder_service.DeleteReminder(item.id, &op_err)) {
                runtime.reminder_upcoming = runtime.reminder_service.ListActive(64, nullptr);
                runtime.reminder_last_error.clear();
                const bool deleting_last_on_page = (start == end - 1) && (idx == end - 1) && (runtime.reminder_page > 0);
                if (deleting_last_on_page) {
                    runtime.reminder_page -= 1;
                }
                ImGui::PopID();
                break;
            } else {
                runtime.reminder_last_error = op_err;
            }
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    if (start >= end) {
        ImGui::TextDisabled("(no reminders on this page)");
    }
    ImGui::EndChild();
}

}  // namespace k2d
