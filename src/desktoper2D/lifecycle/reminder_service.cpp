#include "desktoper2D/lifecycle/reminder_service.h"

#include "sqlite/sqlite3.h"

#include <algorithm>

namespace desktoper2D {
namespace {

bool SetError(std::string *out_error, const char *msg) {
    if (out_error) {
        *out_error = msg ? msg : "unknown";
    }
    return false;
}

bool SetSqliteError(sqlite3 *db, std::string *out_error, const char *prefix) {
    if (out_error) {
        *out_error = std::string(prefix ? prefix : "sqlite error") + ": " +
                     (db ? sqlite3_errmsg(db) : "db is null");
    }
    return false;
}

}  // namespace

ReminderService::~ReminderService() {
    Shutdown();
}

bool ReminderService::Init(const std::string &db_path, std::string *out_error) {
    Shutdown();

    if (db_path.empty()) {
        return SetError(out_error, "reminder db path is empty");
    }

    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        return SetSqliteError(db_, out_error, "sqlite3_open failed");
    }

    if (!EnsureSchema(out_error)) {
        Shutdown();
        return false;
    }

    return true;
}

void ReminderService::Shutdown() noexcept {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool ReminderService::EnsureSchema(std::string *out_error) {
    if (!db_) {
        return SetError(out_error, "reminder db not initialized");
    }

    const char *sql =
        "CREATE TABLE IF NOT EXISTS reminders ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  title TEXT NOT NULL,"
        "  due_unix_sec INTEGER NOT NULL,"
        "  completed INTEGER NOT NULL DEFAULT 0,"
        "  notified INTEGER NOT NULL DEFAULT 0,"
        "  created_unix_sec INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_reminders_due ON reminders(due_unix_sec);"
        "CREATE INDEX IF NOT EXISTS idx_reminders_notify ON reminders(completed, notified, due_unix_sec);";

    char *err_msg = nullptr;
    const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (out_error) {
            *out_error = std::string("create schema failed: ") + (err_msg ? err_msg : "unknown");
        }
        if (err_msg) {
            sqlite3_free(err_msg);
        }
        return false;
    }
    return true;
}

bool ReminderService::AddReminder(const std::string &title,
                                  std::int64_t due_unix_sec,
                                  std::string *out_error) {
    if (!db_) {
        return SetError(out_error, "reminder db not initialized");
    }
    if (title.empty()) {
        return SetError(out_error, "title is empty");
    }

    const char *sql = "INSERT INTO reminders(title, due_unix_sec, completed, notified) VALUES(?1, ?2, 0, 0);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return SetSqliteError(db_, out_error, "prepare insert reminder failed");
    }

    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(due_unix_sec));

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return SetSqliteError(db_, out_error, "execute insert reminder failed");
    }
    return true;
}

bool ReminderService::RestoreReminder(const ReminderItem &item,
                                      std::string *out_error) {
    if (!db_) {
        return SetError(out_error, "reminder db not initialized");
    }
    if (item.id <= 0) {
        return SetError(out_error, "reminder id is invalid");
    }
    if (item.title.empty()) {
        return SetError(out_error, "title is empty");
    }

    const char *sql =
        "INSERT INTO reminders(id, title, due_unix_sec, completed, notified) "
        "VALUES(?1, ?2, ?3, ?4, ?5);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return SetSqliteError(db_, out_error, "prepare restore reminder failed");
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(item.id));
    sqlite3_bind_text(stmt, 2, item.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(item.due_unix_sec));
    sqlite3_bind_int(stmt, 4, item.completed ? 1 : 0);
    sqlite3_bind_int(stmt, 5, item.notified ? 1 : 0);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return SetSqliteError(db_, out_error, "execute restore reminder failed");
    }
    return true;
}

bool ReminderService::GetReminderById(std::int64_t id,
                                      ReminderItem *out_item,
                                      std::string *out_error) const {
    if (!db_) {
        return SetError(out_error, "reminder db not initialized");
    }
    if (!out_item) {
        return SetError(out_error, "out_item is null");
    }

    const char *sql =
        "SELECT id, title, due_unix_sec, completed, notified "
        "FROM reminders WHERE id = ?1 LIMIT 1;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return SetSqliteError(db_, out_error, "prepare get reminder failed");
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(id));
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        ReminderItem item{};
        item.id = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 0));
        const unsigned char *title_text = sqlite3_column_text(stmt, 1);
        item.title = title_text ? reinterpret_cast<const char *>(title_text) : "";
        item.due_unix_sec = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 2));
        item.completed = sqlite3_column_int(stmt, 3) != 0;
        item.notified = sqlite3_column_int(stmt, 4) != 0;
        *out_item = std::move(item);
        sqlite3_finalize(stmt);
        return true;
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return SetSqliteError(db_, out_error, "execute get reminder failed");
    }
    return SetError(out_error, "reminder not found");
}

std::int64_t ReminderService::LastInsertRowId() const {
    if (!db_) {
        return 0;
    }
    return static_cast<std::int64_t>(sqlite3_last_insert_rowid(db_));
}

std::vector<ReminderItem> ReminderService::ListUpcoming(std::int64_t now_unix_sec,
                                                        int limit,
                                                        std::string *out_error) const {
    std::vector<ReminderItem> rows;
    if (!db_) {
        SetError(out_error, "reminder db not initialized");
        return rows;
    }

    limit = std::clamp(limit, 1, 200);

    const char *sql =
        "SELECT id, title, due_unix_sec, completed, notified "
        "FROM reminders "
        "WHERE completed = 0 AND due_unix_sec >= ?1 "
        "ORDER BY due_unix_sec ASC LIMIT ?2;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        SetSqliteError(db_, out_error, "prepare list upcoming failed");
        return rows;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(now_unix_sec));
    sqlite3_bind_int(stmt, 2, limit);

    for (;;) {
        const int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            ReminderItem item{};
            item.id = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 0));
            const unsigned char *title_text = sqlite3_column_text(stmt, 1);
            item.title = title_text ? reinterpret_cast<const char *>(title_text) : "";
            item.due_unix_sec = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 2));
            item.completed = sqlite3_column_int(stmt, 3) != 0;
            item.notified = sqlite3_column_int(stmt, 4) != 0;
            rows.push_back(std::move(item));
            continue;
        }
        if (rc == SQLITE_DONE) {
            break;
        }
        SetSqliteError(db_, out_error, "step list upcoming failed");
        rows.clear();
        break;
    }

    sqlite3_finalize(stmt);
    return rows;
}

std::vector<ReminderItem> ReminderService::ListActive(int limit,
                                                      std::string *out_error) const {
    std::vector<ReminderItem> rows;
    if (!db_) {
        SetError(out_error, "reminder db not initialized");
        return rows;
    }

    limit = std::clamp(limit, 1, 500);

    const char *sql =
        "SELECT id, title, due_unix_sec, completed, notified "
        "FROM reminders "
        "WHERE completed = 0 "
        "ORDER BY due_unix_sec ASC LIMIT ?1;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        SetSqliteError(db_, out_error, "prepare list active failed");
        return rows;
    }

    sqlite3_bind_int(stmt, 1, limit);

    for (;;) {
        const int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            ReminderItem item{};
            item.id = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 0));
            const unsigned char *title_text = sqlite3_column_text(stmt, 1);
            item.title = title_text ? reinterpret_cast<const char *>(title_text) : "";
            item.due_unix_sec = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 2));
            item.completed = sqlite3_column_int(stmt, 3) != 0;
            item.notified = sqlite3_column_int(stmt, 4) != 0;
            rows.push_back(std::move(item));
            continue;
        }
        if (rc == SQLITE_DONE) {
            break;
        }
        SetSqliteError(db_, out_error, "step list active failed");
        rows.clear();
        break;
    }

    sqlite3_finalize(stmt);
    return rows;
}

bool ReminderService::MarkCompleted(std::int64_t id, bool completed, std::string *out_error) {
    if (!db_) {
        return SetError(out_error, "reminder db not initialized");
    }

    const char *sql = "UPDATE reminders SET completed = ?1 WHERE id = ?2;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return SetSqliteError(db_, out_error, "prepare mark completed failed");
    }

    sqlite3_bind_int(stmt, 1, completed ? 1 : 0);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(id));

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return SetSqliteError(db_, out_error, "execute mark completed failed");
    }

    if (sqlite3_changes(db_) <= 0) {
        return SetError(out_error, "reminder not found");
    }

    return true;
}

bool ReminderService::DeleteReminder(std::int64_t id, std::string *out_error) {
    if (!db_) {
        return SetError(out_error, "reminder db not initialized");
    }

    const char *sql = "DELETE FROM reminders WHERE id = ?1;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return SetSqliteError(db_, out_error, "prepare delete reminder failed");
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(id));

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return SetSqliteError(db_, out_error, "execute delete reminder failed");
    }

    if (sqlite3_changes(db_) <= 0) {
        return SetError(out_error, "reminder not found");
    }

    return true;
}

std::vector<ReminderItem> ReminderService::PollDueAndMarkNotified(std::int64_t now_unix_sec,
                                                                  int limit,
                                                                  std::string *out_error) {
    std::vector<ReminderItem> due_rows;
    if (!db_) {
        SetError(out_error, "reminder db not initialized");
        return due_rows;
    }

    limit = std::clamp(limit, 1, 200);

    const char *select_sql =
        "SELECT id, title, due_unix_sec, completed, notified "
        "FROM reminders "
        "WHERE completed = 0 AND notified = 0 AND due_unix_sec <= ?1 "
        "ORDER BY due_unix_sec ASC LIMIT ?2;";

    sqlite3_stmt *select_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK) {
        SetSqliteError(db_, out_error, "prepare poll due failed");
        return due_rows;
    }

    sqlite3_bind_int64(select_stmt, 1, static_cast<sqlite3_int64>(now_unix_sec));
    sqlite3_bind_int(select_stmt, 2, limit);

    for (;;) {
        const int rc = sqlite3_step(select_stmt);
        if (rc == SQLITE_ROW) {
            ReminderItem item{};
            item.id = static_cast<std::int64_t>(sqlite3_column_int64(select_stmt, 0));
            const unsigned char *title_text = sqlite3_column_text(select_stmt, 1);
            item.title = title_text ? reinterpret_cast<const char *>(title_text) : "";
            item.due_unix_sec = static_cast<std::int64_t>(sqlite3_column_int64(select_stmt, 2));
            item.completed = sqlite3_column_int(select_stmt, 3) != 0;
            item.notified = sqlite3_column_int(select_stmt, 4) != 0;
            due_rows.push_back(std::move(item));
            continue;
        }
        if (rc == SQLITE_DONE) {
            break;
        }
        SetSqliteError(db_, out_error, "step poll due failed");
        due_rows.clear();
        sqlite3_finalize(select_stmt);
        return due_rows;
    }
    sqlite3_finalize(select_stmt);

    if (due_rows.empty()) {
        return due_rows;
    }

    const char *update_sql = "UPDATE reminders SET notified = 1 WHERE id = ?1;";
    sqlite3_stmt *update_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, update_sql, -1, &update_stmt, nullptr) != SQLITE_OK) {
        SetSqliteError(db_, out_error, "prepare update notified failed");
        due_rows.clear();
        return due_rows;
    }

    for (const auto &item : due_rows) {
        sqlite3_reset(update_stmt);
        sqlite3_clear_bindings(update_stmt);
        sqlite3_bind_int64(update_stmt, 1, static_cast<sqlite3_int64>(item.id));
        const int rc = sqlite3_step(update_stmt);
        if (rc != SQLITE_DONE) {
            SetSqliteError(db_, out_error, "update notified failed");
            due_rows.clear();
            break;
        }
    }

    sqlite3_finalize(update_stmt);
    return due_rows;
}

}  // namespace desktoper2D
