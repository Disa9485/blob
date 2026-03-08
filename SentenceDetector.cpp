// SentenceDetector.cpp
#include "SentenceDetector.hpp"

#include <algorithm>
#include <cctype>

void SentenceDetector::pushToken(std::string_view token) {
    // Filter out anything inside square brackets, even across token boundaries.
    for (char c : token) {
        if (m_inside_brackets) {
            if (c == ']') {
                m_inside_brackets = false;
            }
            continue;
        }

        if (c == '[') {
            m_inside_brackets = true;
            continue;
        }

        m_buffer.push_back(c);
    }

    while (tryExtractSentence()) {
    }
}

bool SentenceDetector::hasSentence() const {
    return !m_ready.empty();
}

std::string SentenceDetector::popSentence() {
    if (m_ready.empty()) {
        return {};
    }

    std::string out = std::move(m_ready.front());
    m_ready.pop_front();
    return out;
}

std::string SentenceDetector::flushRemainder() {
    std::string out = trim(m_buffer);
    m_buffer.clear();
    return out;
}

bool SentenceDetector::tryExtractSentence() {
    for (std::size_t i = 0; i < m_buffer.size(); ++i) {
        if (!isSentenceTerminator(i)) {
            continue;
        }

        // absorb trailing quotes/parens/spaces after punctuation
        std::size_t end = i + 1;
        while (end < m_buffer.size()) {
            const char c = m_buffer[end];
            if (c == '"' || c == '\'' || c == ')' || c == ']' || c == '}' ||
                std::isspace(static_cast<unsigned char>(c))) {
                ++end;
            } else {
                break;
            }
        }

        std::string sentence = trim(m_buffer.substr(0, end));
        m_buffer.erase(0, end);

        if (!sentence.empty()) {
            m_ready.push_back(std::move(sentence));
            return true;
        }

        return false;
    }

    return false;
}

bool SentenceDetector::isSentenceTerminator(std::size_t index) const {
    const char c = m_buffer[index];
    if (c != '.' && c != '!' && c != '?' && c != ',' && c != ';') {
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
    if (period_index == 0 || period_index + 1 >= m_buffer.size()) {
        return false;
    }

    const unsigned char prev = static_cast<unsigned char>(m_buffer[period_index - 1]);
    const unsigned char next = static_cast<unsigned char>(m_buffer[period_index + 1]);
    return std::isdigit(prev) && std::isdigit(next);
}

bool SentenceDetector::looksLikeAbbreviation(std::size_t period_index) const {
    if (period_index == 0) {
        return false;
    }

    std::size_t start = period_index;
    while (start > 0) {
        const char c = m_buffer[start - 1];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '.') {
            --start;
        } else {
            break;
        }
    }

    std::string token = m_buffer.substr(start, period_index - start);
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