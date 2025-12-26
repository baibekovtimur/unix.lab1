#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

struct TextMetrics
{
    int64_t length_chars = 0; // число UTF-8 кодпоинтов (упрощённо, без grapheme clusters)
    int64_t length_bytes = 0; // байты
    int64_t word_count = 0;
    double avg_word_len = 0.0;        // в символах (кодпоинтах)
    double unique_word_pct = 0.0;     // 0..100
    double consecutive_dup_pct = 0.0; // 0..100 (доля повторов подряд)
    int64_t sentences = 0;

    // "плохие" паттерны/символы
    int64_t caps_sequences = 0;
    double upper_ratio = 0.0;    // 0..1
    int64_t exclam_runs = 0;     // последовательности !!! (>=3)
    int64_t quest_runs = 0;      // последовательности ??? (>=3)
    int64_t long_space_runs = 0; // последовательности пробелов (>=3)
    int64_t junk_chars = 0;      // управляющие/нулевой ширины/мусор

    double readability = 0.0; // 0..100 (эвристика)
};

// --- UTF-8 decode (минимально безопасный) ---
inline bool utf8_next(const std::string &s, size_t &i, uint32_t &cp)
{
    if (i >= s.size())
        return false;
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80)
    {
        cp = c;
        i += 1;
        return true;
    }

    int len = 0;
    if ((c & 0xE0) == 0xC0)
        len = 2;
    else if ((c & 0xF0) == 0xE0)
        len = 3;
    else if ((c & 0xF8) == 0xF0)
        len = 4;
    else
    {
        cp = 0xFFFD;
        i += 1;
        return true;
    }

    if (i + len > s.size())
    {
        cp = 0xFFFD;
        i = s.size();
        return true;
    }

    uint32_t out = (c & ((1u << (8 - len - 1)) - 1));
    for (int k = 1; k < len; ++k)
    {
        unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80)
        {
            cp = 0xFFFD;
            i += 1;
            return true;
        }
        out = (out << 6) | (cc & 0x3F);
    }
    cp = out;
    i += static_cast<size_t>(len);
    return true;
}

inline bool is_cyr_upper(uint32_t cp) { return (cp >= 0x0410 && cp <= 0x042F) || cp == 0x0401; }
inline bool is_cyr_lower(uint32_t cp) { return (cp >= 0x0430 && cp <= 0x044F) || cp == 0x0451; }
inline bool is_lat_upper(uint32_t cp) { return (cp >= 'A' && cp <= 'Z'); }
inline bool is_lat_lower(uint32_t cp) { return (cp >= 'a' && cp <= 'z'); }
inline bool is_digit(uint32_t cp) { return (cp >= '0' && cp <= '9'); }

inline bool is_letter(uint32_t cp)
{
    return is_lat_upper(cp) || is_lat_lower(cp) || is_cyr_upper(cp) || is_cyr_lower(cp);
}
inline bool is_upper(uint32_t cp)
{
    return is_lat_upper(cp) || is_cyr_upper(cp);
}

inline uint32_t to_lower_simple(uint32_t cp)
{
    if (is_lat_upper(cp))
        return cp + 32;
    if (cp >= 0x0410 && cp <= 0x042F)
        return cp + 32;
    if (cp == 0x0401)
        return 0x0451; // Ё -> ё
    return cp;
}

inline bool is_word_char(uint32_t cp)
{
    return is_letter(cp) || is_digit(cp) || cp == '_' || cp == '-';
}

inline bool is_space(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r';
}

inline bool is_sentence_end(uint32_t cp)
{
    return cp == '.' || cp == '!' || cp == '?';
}

inline bool is_vowel_ru(uint32_t cp_lower)
{
    // аеёиоуыэюя
    static const uint32_t v[] = {0x0430, 0x0435, 0x0451, 0x0438, 0x043E, 0x0443, 0x044B, 0x044D, 0x044E, 0x044F};
    for (auto x : v)
        if (cp_lower == x)
            return true;
    return false;
}
inline bool is_vowel_en(uint32_t cp_lower)
{
    return cp_lower == 'a' || cp_lower == 'e' || cp_lower == 'i' || cp_lower == 'o' || cp_lower == 'u' || cp_lower == 'y';
}

struct U32Hash
{
    size_t operator()(const std::u32string &s) const noexcept
    {
        size_t h = 1469598103934665603ull;
        for (uint32_t c : s)
        {
            h ^= static_cast<size_t>(c);
            h *= 1099511628211ull;
        }
        return h;
    }
};

inline int count_vowel_groups(const std::u32string &w, const std::string &lang)
{
    bool prev_v = false;
    int groups = 0;
    for (uint32_t cp : w)
    {
        uint32_t cpl = to_lower_simple(cp);
        bool v = (lang == "ru") ? is_vowel_ru(cpl) : is_vowel_en(cpl);
        if (v && !prev_v)
            groups++;
        prev_v = v;
    }
    return groups;
}

inline TextMetrics compute_metrics(const std::string &text, const std::string &lang)
{
    TextMetrics m;
    m.length_bytes = static_cast<int64_t>(text.size());

    std::vector<std::u32string> words;
    words.reserve(128);

    std::u32string cur;
    cur.reserve(32);

    int64_t letters = 0;
    int64_t uppers = 0;
    int64_t max_upper_run = 0;
    int64_t current_upper_run = 0;

    int64_t syllables = 0;
    int64_t sentences = 0;

    int64_t exclam_run = 0;
    int64_t quest_run = 0;
    int64_t space_run = 0;

    size_t i = 0;
    uint32_t cp = 0;
    uint32_t prev_cp = 0;
    bool prev_sentence_end = false;

    while (utf8_next(text, i, cp))
    {
        m.length_chars++;

        // junk chars (controls, zero-width, replacement)
        if ((cp < 32 && cp != '\n' && cp != '\r' && cp != '\t') ||
            cp == 0x7F || cp == 0xFFFD || cp == 0x200B || cp == 0x200C || cp == 0x200D)
        {
            m.junk_chars++;
        }
        // "мусорные" ascii
        if (cp == '`' || cp == '~' || cp == '^')
            m.junk_chars++;

        // sentence count by groups of .!? (не считаем "!!!" как 3 предложения)
        if (is_sentence_end(cp))
        {
            if (!prev_sentence_end)
                sentences++;
            prev_sentence_end = true;
        }
        else
        {
            prev_sentence_end = false;
        }

        // caps stats
        if (is_letter(cp))
        {
            letters++;
            if (is_upper(cp))
            {
                uppers++;
                current_upper_run++;
                max_upper_run = std::max(max_upper_run, current_upper_run);
            }
            else
            {
                current_upper_run = 0;
            }
        }
        else
        {
            current_upper_run = 0;
        }

        // !!!, ??? runs (>=3)
        if (cp == '!')
            exclam_run++;
        else
        {
            if (exclam_run >= 3)
                m.exclam_runs++;
            exclam_run = 0;
        }

        if (cp == '?')
            quest_run++;
        else
        {
            if (quest_run >= 3)
                m.quest_runs++;
            quest_run = 0;
        }

        // long spaces
        if (cp == ' ')
            space_run++;
        else
        {
            if (space_run >= 3)
                m.long_space_runs++;
            space_run = 0;
        }

        // tokenize words
        if (is_word_char(cp))
        {
            cur.push_back(to_lower_simple(cp));
        }
        else
        {
            if (!cur.empty())
            {
                words.push_back(cur);
                cur.clear();
            }
        }

        prev_cp = cp;
    }

    if (exclam_run >= 3)
        m.exclam_runs++;
    if (quest_run >= 3)
        m.quest_runs++;
    if (space_run >= 3)
        m.long_space_runs++;
    if (!cur.empty())
        words.push_back(cur);

    m.word_count = static_cast<int64_t>(words.size());
    m.sentences = std::max<int64_t>(1, sentences);

    // caps_sequences heuristic: если был длинный upper-run
    m.caps_sequences = (max_upper_run >= 5) ? 1 : 0;
    m.upper_ratio = (letters > 0) ? (double)uppers / (double)letters : 0.0;

    // avg word len, unique pct, dup pct, syllables
    if (m.word_count > 0)
    {
        int64_t total_word_len = 0;
        std::unordered_set<std::u32string, U32Hash> uniq;
        uniq.reserve(words.size() * 2);

        int64_t dup = 0;
        for (size_t k = 0; k < words.size(); ++k)
        {
            total_word_len += (int64_t)words[k].size();
            uniq.insert(words[k]);
            syllables += count_vowel_groups(words[k], lang);
            if (k > 0 && words[k] == words[k - 1])
                dup++;
        }

        m.avg_word_len = (double)total_word_len / (double)m.word_count;
        m.unique_word_pct = 100.0 * (double)uniq.size() / (double)m.word_count;
        if (m.word_count > 1)
            m.consecutive_dup_pct = 100.0 * (double)dup / (double)(m.word_count - 1);
        else
            m.consecutive_dup_pct = 0.0;

        // readability (упрощённо)
        double target_wps = (lang == "ru") ? 10.0 : 12.0;
        double target_syl = (lang == "ru") ? 2.0 : 1.5;
        double target_wlen = (lang == "ru") ? 6.0 : 5.0;

        double wps = (double)m.word_count / (double)m.sentences;
        double syl_per_word = (double)syllables / std::max<double>(1.0, (double)m.word_count);

        double r = 100.0;
        r -= std::max(0.0, (wps - target_wps)) * 2.0;
        r -= std::max(0.0, (syl_per_word - target_syl)) * 25.0;
        r -= std::max(0.0, (m.avg_word_len - target_wlen)) * 5.0;
        r -= (m.upper_ratio > 0.35 ? 10.0 : 0.0);
        r -= (m.exclam_runs + m.quest_runs) * 5.0;

        m.readability = std::clamp(r, 0.0, 100.0);
    }
    else
    {
        m.avg_word_len = 0.0;
        m.unique_word_pct = 0.0;
        m.consecutive_dup_pct = 0.0;
        m.readability = 0.0;
    }

    return m;
}

inline int compute_score(const TextMetrics &m, const std::string &lang, std::vector<std::string> &errors)
{
    if (m.length_bytes == 0 || m.length_chars == 0)
    {
        errors.push_back("empty_text");
        return 0;
    }

    double score = 100.0;

    // короткий текст
    if (m.length_chars < 20)
        score -= 30.0;
    if (m.word_count < 3)
        score -= 25.0;

    // лексическое разнообразие
    if (m.word_count >= 5 && m.unique_word_pct < 50.0)
    {
        score -= (50.0 - m.unique_word_pct) * 0.5; // до -25
    }

    // повторы подряд
    score -= m.consecutive_dup_pct * 0.7; // 0..-70, но обычно меньше

    // "плохие" паттерны
    double bad = 0.0;
    if (m.upper_ratio > 0.6 && m.length_chars > 40)
        bad += 20.0;
    if (m.caps_sequences)
        bad += 10.0;
    bad += std::min<int64_t>(3, m.exclam_runs) * 6.0;
    bad += std::min<int64_t>(3, m.quest_runs) * 6.0;
    bad += std::min<int64_t>(3, m.long_space_runs) * 5.0;
    bad += std::min<int64_t>(20, m.junk_chars) * 1.5;
    score -= std::min(45.0, bad);

    // читабельность
    if (m.readability < 60.0)
        score -= (60.0 - m.readability) * 0.5;

    // clamp
    if (score < 0.0)
        score = 0.0;
    if (score > 100.0)
        score = 100.0;

    return (int)(score + 0.5);
}

inline std::string status_from_score(int score, const std::vector<std::string> &errors)
{
    if (!errors.empty())
        return "BAD";
    if (score >= 80)
        return "OK";
    if (score >= 50)
        return "WARN";
    return "BAD";
}