// ConversationMemoryStore.hpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

class ConversationMemoryStore {
public:
    struct StoredMessage {
        int64_t id = 0;
        int64_t session_id = 0;
        int64_t message_index = 0;
        std::string role;
        std::string timestamp_iso8601;
        std::string raw_text;
        std::string embedded_text;
    };

    ConversationMemoryStore();
    ~ConversationMemoryStore();

    ConversationMemoryStore(const ConversationMemoryStore&) = delete;
    ConversationMemoryStore& operator=(const ConversationMemoryStore&) = delete;

    bool open(const std::string& db_path, std::string& out_error);

    bool insertMessage(
        int64_t session_id,
        int64_t message_index,
        const std::string& role,
        const std::string& timestamp_iso8601,
        const std::string& text,
        const std::string& embedded_text,
        const std::vector<float>& embedding,
        int64_t& out_id,
        std::string& out_error
    );

    bool getMessageById(
        int64_t id,
        StoredMessage& out_message,
        std::string& out_error
    );

private:
    bool execute(const char* sql, std::string& out_error);
    bool prepareInsert(std::string& out_error);
    bool prepareSelectById(std::string& out_error);

    sqlite3* db_ = nullptr;
    sqlite3_stmt* insert_stmt_ = nullptr;
    sqlite3_stmt* select_by_id_stmt_ = nullptr;
};