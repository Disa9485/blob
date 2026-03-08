// ConversationMemoryStore.cpp
#include "ConversationMemoryStore.hpp"

#include <sqlite3.h>

ConversationMemoryStore::ConversationMemoryStore() = default;

ConversationMemoryStore::~ConversationMemoryStore() {
    if (insert_stmt_) {
        sqlite3_finalize(insert_stmt_);
        insert_stmt_ = nullptr;
    }

    if (select_by_id_stmt_) {
        sqlite3_finalize(select_by_id_stmt_);
        select_by_id_stmt_ = nullptr;
    }

    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool ConversationMemoryStore::execute(const char* sql, std::string& out_error) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        out_error = err ? err : "sqlite3_exec failed";
        if (err) {
            sqlite3_free(err);
        }
        return false;
    }
    return true;
}

bool ConversationMemoryStore::prepareInsert(std::string& out_error) {
    const char* sql =
        "INSERT INTO conversation_messages ("
        "session_id, message_index, role, timestamp_iso8601, raw_text, embedded_text, embedding, embedding_dim"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

    const int rc = sqlite3_prepare_v2(db_, sql, -1, &insert_stmt_, nullptr);
    if (rc != SQLITE_OK) {
        out_error = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

bool ConversationMemoryStore::open(const std::string& db_path, std::string& out_error) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        out_error = sqlite3_errmsg(db_);
        return false;
    }

    if (!execute("PRAGMA journal_mode=WAL;", out_error)) return false;
    if (!execute("PRAGMA synchronous=FULL;", out_error)) return false;
    if (!execute("PRAGMA temp_store=MEMORY;", out_error)) return false;
    if (!execute("PRAGMA mmap_size=268435456;", out_error)) return false;

    const char* schema =
        "CREATE TABLE IF NOT EXISTS conversation_messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "session_id INTEGER NOT NULL DEFAULT 0,"
        "message_index INTEGER NOT NULL DEFAULT 0,"
        "role TEXT NOT NULL,"
        "timestamp_iso8601 TEXT NOT NULL,"
        "raw_text TEXT NOT NULL,"
        "embedded_text TEXT NOT NULL,"
        "embedding BLOB NOT NULL,"
        "embedding_dim INTEGER NOT NULL,"
        "created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_conversation_messages_role ON conversation_messages(role);"
        "CREATE INDEX IF NOT EXISTS idx_conversation_messages_timestamp ON conversation_messages(timestamp_iso8601);"
        "CREATE INDEX IF NOT EXISTS idx_conversation_messages_session_msg "
        "ON conversation_messages(session_id, message_index);";

    if (!execute(schema, out_error)) {
        return false;
    }

    if (!prepareInsert(out_error)) {
        return false;
    }

    if (!prepareSelectById(out_error)) {
        return false;
    }

    return true;
}

bool ConversationMemoryStore::prepareSelectById(std::string& out_error) {
    const char* sql =
        "SELECT id, session_id, message_index, role, timestamp_iso8601, raw_text, embedded_text "
        "FROM conversation_messages "
        "WHERE id = ?;";

    const int rc = sqlite3_prepare_v2(db_, sql, -1, &select_by_id_stmt_, nullptr);
    if (rc != SQLITE_OK) {
        out_error = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

bool ConversationMemoryStore::insertMessage(
    int64_t session_id,
    int64_t message_index,
    const std::string& role,
    const std::string& timestamp_iso8601,
    const std::string& text,
    const std::string& embedded_text,
    const std::vector<float>& embedding,
    int64_t& out_id,
    std::string& out_error)
{
    if (!db_ || !insert_stmt_) {
        out_error = "ConversationMemoryStore is not initialized.";
        return false;
    }

    sqlite3_reset(insert_stmt_);
    sqlite3_clear_bindings(insert_stmt_);

    const void* blob_ptr = embedding.empty() ? nullptr : embedding.data();
    const int blob_size = static_cast<int>(embedding.size() * sizeof(float));

    if (sqlite3_bind_int64(insert_stmt_, 1, static_cast<sqlite3_int64>(session_id)) != SQLITE_OK ||
        sqlite3_bind_int64(insert_stmt_, 2, static_cast<sqlite3_int64>(message_index)) != SQLITE_OK ||
        sqlite3_bind_text(insert_stmt_, 3, role.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(insert_stmt_, 4, timestamp_iso8601.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(insert_stmt_, 5, text.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(insert_stmt_, 6, embedded_text.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_blob(insert_stmt_, 7, blob_ptr, blob_size, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int(insert_stmt_, 8, static_cast<int>(embedding.size())) != SQLITE_OK) {
        out_error = sqlite3_errmsg(db_);
        return false;
    }

    const int rc = sqlite3_step(insert_stmt_);
    if (rc != SQLITE_DONE) {
        out_error = sqlite3_errmsg(db_);
        return false;
    }

    out_id = static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
    return true;
}

bool ConversationMemoryStore::getMessageById(
    int64_t id,
    StoredMessage& out_message,
    std::string& out_error)
{
    if (!db_ || !select_by_id_stmt_) {
        out_error = "ConversationMemoryStore is not initialized.";
        return false;
    }

    sqlite3_reset(select_by_id_stmt_);
    sqlite3_clear_bindings(select_by_id_stmt_);

    if (sqlite3_bind_int64(select_by_id_stmt_, 1, static_cast<sqlite3_int64>(id)) != SQLITE_OK) {
        out_error = sqlite3_errmsg(db_);
        return false;
    }

    const int rc = sqlite3_step(select_by_id_stmt_);
    if (rc == SQLITE_ROW) {
        out_message.id = static_cast<int64_t>(sqlite3_column_int64(select_by_id_stmt_, 0));
        out_message.session_id = static_cast<int64_t>(sqlite3_column_int64(select_by_id_stmt_, 1));
        out_message.message_index = static_cast<int64_t>(sqlite3_column_int64(select_by_id_stmt_, 2));
        out_message.role = reinterpret_cast<const char*>(sqlite3_column_text(select_by_id_stmt_, 3));
        out_message.timestamp_iso8601 = reinterpret_cast<const char*>(sqlite3_column_text(select_by_id_stmt_, 4));
        out_message.raw_text = reinterpret_cast<const char*>(sqlite3_column_text(select_by_id_stmt_, 5));
        out_message.embedded_text = reinterpret_cast<const char*>(sqlite3_column_text(select_by_id_stmt_, 6));
        return true;
    }

    if (rc == SQLITE_DONE) {
        out_error = "Message id not found.";
        return false;
    }

    out_error = sqlite3_errmsg(db_);
    return false;
}