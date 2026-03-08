// SentenceDetector.hpp
#pragma once

#include <deque>
#include <string>
#include <string_view>
#include <unordered_set>

class SentenceDetector {
public:
    void pushToken(std::string_view token);
    bool hasSentence() const;
    std::string popSentence();

    // Flush remaining text at end of stream if needed
    std::string flushRemainder();

private:
    bool tryExtractSentence();
    bool isSentenceTerminator(std::size_t index) const;
    bool looksLikeAbbreviation(std::size_t period_index) const;
    bool looksLikeDecimal(std::size_t period_index) const;
    void appendFiltered(std::string_view token);

    static std::string trim(std::string s);

private:
    std::string m_buffer;
    std::deque<std::string> m_ready;
    bool m_inside_brackets = false;

    const std::unordered_set<std::string> m_abbreviations = {
        "mr", "mrs", "ms", "dr", "prof", "sr", "jr",
        "st", "vs", "etc", "e.g", "i.e", "u.s", "u.k"
    };
};