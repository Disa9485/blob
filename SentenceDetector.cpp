// SentenceDetector.cpp
#include "SentenceDetector.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

void SentenceDetector::pushToken(std::string_view token) {
    for (char c : token) {
        if (m_inside_action) {
            if (c == ']') {
                std::string action = trim(m_action_buffer);
                m_action_buffer.clear();
                m_inside_action = false;

                if (!action.empty()) {
                    m_ready.push_back({ SegmentType::Action, std::move(action) });
                }
            } else {
                m_action_buffer.push_back(c);
            }
            continue;
        }

        if (c == '[') {
            // Flush any complete dialogue sentences before entering action mode.
            while (tryExtractSentenceFromDialogueBuffer()) {
            }

            // If dialogue remains but is not sentence-terminated yet, emit it as dialogue
            // so the action can appear "below the last thing said".
            std::string pending = trim(m_dialogue_buffer);
            m_dialogue_buffer.clear();
            if (!pending.empty()) {
                m_ready.push_back({ SegmentType::Dialogue, std::move(pending) });
            }

            m_inside_action = true;
            m_action_buffer.clear();
            continue;
        }

        if (c == '"') {
            continue;
        }

        m_dialogue_buffer.push_back(c);

        while (tryExtractSentenceFromDialogueBuffer()) {
        }
    }
}

bool SentenceDetector::hasSegment() const {
    return !m_ready.empty();
}

SentenceDetector::Segment SentenceDetector::popSegment() {
    if (m_ready.empty()) {
        return {};
    }

    Segment out = std::move(m_ready.front());
    m_ready.pop_front();
    return out;
}

std::vector<SentenceDetector::Segment> SentenceDetector::flushAll() {
    std::vector<Segment> out;

    while (hasSegment()) {
        out.push_back(popSegment());
    }

    // If an action was never closed, keep it as an action instead of downgrading to dialogue.
    if (m_inside_action) {
        std::string action = trim(m_action_buffer);
        m_action_buffer.clear();
        m_inside_action = false;

        if (!action.empty()) {
            out.push_back({ SegmentType::Action, std::move(action) });
        }
    }

    std::string tail = trim(m_dialogue_buffer);
    m_dialogue_buffer.clear();

    if (!tail.empty()) {
        out.push_back({ SegmentType::Dialogue, std::move(tail) });
    }

    return out;
}

bool SentenceDetector::tryExtractSentenceFromDialogueBuffer() {
    for (std::size_t i = 0; i < m_dialogue_buffer.size(); ++i) {
        if (!isSentenceTerminator(i)) {
            continue;
        }

        std::size_t end = i + 1;
        while (end < m_dialogue_buffer.size()) {
            const char c = m_dialogue_buffer[end];
            if (c == '"' || c == '\'' || c == ')' || c == ']' || c == '}' ||
                std::isspace(static_cast<unsigned char>(c))) {
                ++end;
            } else {
                break;
            }
        }

        std::string sentence = trim(m_dialogue_buffer.substr(0, end));
        m_dialogue_buffer.erase(0, end);

        if (!sentence.empty()) {
            m_ready.push_back({ SegmentType::Dialogue, std::move(sentence) });
            return true;
        }

        return false;
    }

    return false;
}

bool SentenceDetector::isSentenceTerminator(std::size_t index) const {
    const char c = m_dialogue_buffer[index];
    if (c != '.' && c != '!' && c != '?') {
        return false;
    }

    if (c == '.') {
        if (looksLikeDecimal(index)) {
            return false;
        }

        if (looksLikeAbbreviation(index)) {
            return false;
        }
    }

    return true;
}

bool SentenceDetector::looksLikeDecimal(std::size_t period_index) const {
    if (period_index == 0 || period_index + 1 >= m_dialogue_buffer.size()) {
        return false;
    }

    const unsigned char prev = static_cast<unsigned char>(m_dialogue_buffer[period_index - 1]);
    const unsigned char next = static_cast<unsigned char>(m_dialogue_buffer[period_index + 1]);
    return std::isdigit(prev) && std::isdigit(next);
}

bool SentenceDetector::looksLikeAbbreviation(std::size_t period_index) const {
    if (period_index == 0) {
        return false;
    }

    std::size_t start = period_index;
    while (start > 0) {
        const char c = m_dialogue_buffer[start - 1];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '.') {
            --start;
        } else {
            break;
        }
    }

    std::string token = m_dialogue_buffer.substr(start, period_index - start);
    std::string lowered;
    lowered.reserve(token.size());

    for (char c : token) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    return m_abbreviations.find(lowered) != m_abbreviations.end();
}

std::string SentenceDetector::trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };

    auto begin = std::find_if(s.begin(), s.end(), not_space);
    auto end = std::find_if(s.rbegin(), s.rend(), not_space).base();

    if (begin >= end) {
        return {};
    }

    return std::string(begin, end);
}