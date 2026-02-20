#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace k2d {

struct ReminderItem {
    std::int64_t id = 0;
    std::string title;
    std::int64_t due_unix_sec = 0;
    bool completed = false;
    bool notified = false;
};

class ReminderService {
public:
    ReminderService() = default;
    ~ReminderService();

    ReminderService(const ReminderService &) = delete;
    ReminderService &operator=(const ReminderService &) = delete;

    bool Init(const std::string &db_path, std::string *out_error);
    void Shutdown() noexcept;

    bool AddReminder(const std::string &title,
                     std::int64_t due_unix_sec,
                     std::string *out_error);

    std::vector<ReminderItem> ListUpcoming(std::int64_t now_unix_sec,
                                           int limit,
                                           std::string *out_error) const;

    std::vector<ReminderItem> ListActive(int limit,
                                         std::string *out_error) const;

    bool MarkCompleted(std::int64_t id, bool completed, std::string *out_error);

    bool DeleteReminder(std::int64_t id, std::string *out_error);

    std::vector<ReminderItem> PollDueAndMarkNotified(std::int64_t now_unix_sec,
                                                     int limit,
                                                     std::string *out_error);

private:
    bool EnsureSchema(std::string *out_error);

    sqlite3 *db_ = nullptr;
};

}  // namespace k2d
