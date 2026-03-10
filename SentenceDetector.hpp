// SentenceDetector.hpp
#pragma once

#include <deque>
#include <string>
#include <string_view>
#include <unordered_set>

class SentenceDetector {
public:
    enum class SegmentType {
        Dialogue,
        Action
    };

    struct Segment {
        SegmentType type = SegmentType::Dialogue;
        std::string text;
    };

    void pushToken(std::string_view token);

    bool hasSegment() const;
    Segment popSegment();

    std::vector<Segment> flushAll();

private:
    bool tryExtractSentenceFromDialogueBuffer();

    bool isSentenceTerminator(std::size_t index) const;
    bool looksLikeAbbreviation(std::size_t period_index) const;
    bool looksLikeDecimal(std::size_t period_index) const;
    bool looksLikeEllipsis(std::size_t period_index) const;
    static bool isOnlyEllipsisLike(std::string_view s);

    static std::string trim(std::string s);

private:
    std::string m_dialogue_buffer;
    std::string m_action_buffer;
    std::deque<Segment> m_ready;
    bool m_inside_action = false;

    const std::unordered_set<std::string> m_abbreviations = {
        "mr", "mrs", "ms", "dr", "prof", "sr", "jr",
        "st", "vs", "etc", "e.g", "i.e", "u.s", "u.k"
    };
};