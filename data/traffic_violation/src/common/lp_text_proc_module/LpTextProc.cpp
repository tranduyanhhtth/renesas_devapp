/*******************************************************************************
 * LpTextProc.cpp – License plate raw OCR text processing
 * Tương đương TextProc.cpp của Q06_expiry_date_detection
 ******************************************************************************/
#include "LpTextProc.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace {

static char map_char_to_digit(char c)
{
    if (std::isdigit((unsigned char)c)) return c;
    switch ((char)std::toupper((unsigned char)c)) {
        case 'O': case 'Q': case 'D': return '0';
        case 'I': case 'L': return '1';
        case 'Z': return '2';
        case 'A': return '4';
        case 'S': return '5';
        case 'G': return '6';
        case 'T': case 'Y': return '7';
        case 'B': return '8';
        case 'P': return '9';
        default: return '\0';
    }
}

static char map_char_to_letter(char c)
{
    if (std::isalpha((unsigned char)c))
        return (char)std::toupper((unsigned char)c);
    switch (c) {
        case '0': return 'O';
        case '1': return 'I';
        case '2': return 'Z';
        case '3': return 'B';
        case '4': return 'A';
        case '5': return 'S';
        case '6': return 'G';
        case '7': return 'T';
        case '8': return 'B';
        case '9': return 'P';
        default: return '\0';
    }
}

static std::string sanitize_keep_sep(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    bool last_sep = true;

    for (char c : raw) {
        char u = (char)std::toupper((unsigned char)c);
        if (std::isalnum((unsigned char)u)) {
            out.push_back(u);
            last_sep = false;
            continue;
        }

        if ((u == '-' || u == ' ' || u == '_' || u == '.' || u == ':' || u == '/') && !last_sep) {
            out.push_back('-');
            last_sep = true;
        }
    }

    while (!out.empty() && out.back() == '-')
        out.pop_back();
    return out;
}

static std::string alnum_only(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (std::isalnum((unsigned char)c))
            out.push_back((char)std::toupper((unsigned char)c));
    }
    return out;
}

static bool normalize_vn_plate_strict(const std::string& raw, std::string& out)
{
    out.clear();
    std::string filtered = alnum_only(raw);
    if (filtered.size() < 7) return false;

    const size_t prefix_opts[] = {4, 3};
    for (size_t start = 0; start < filtered.size(); ++start) {
        for (size_t k = 0; k < sizeof(prefix_opts) / sizeof(prefix_opts[0]); ++k) {
            size_t pfx_len = prefix_opts[k];
            if (start + pfx_len + 4 > filtered.size()) continue;

            char d0 = map_char_to_digit(filtered[start + 0]);
            char d1 = map_char_to_digit(filtered[start + 1]);
            char s0 = map_char_to_letter(filtered[start + 2]);
            if (!d0 || !d1 || !s0) continue;

            std::string prefix;
            prefix.push_back(d0);
            prefix.push_back(d1);
            prefix.push_back(s0);

            if (pfx_len == 4) {
                char c = filtered[start + 3];
                char md = map_char_to_digit(c);
                char ml = map_char_to_letter(c);
                if (std::isdigit((unsigned char)c) && md) prefix.push_back(md);
                else if (std::isalpha((unsigned char)c) && ml) prefix.push_back(ml);
                else if (md) prefix.push_back(md);
                else if (ml) prefix.push_back(ml);
                else continue;
            }

            std::string suffix;
            suffix.reserve(5);
            for (size_t i = start + pfx_len; i < filtered.size() && suffix.size() < 5; ++i) {
                char md = map_char_to_digit(filtered[i]);
                if (md) suffix.push_back(md);
            }

            if (suffix.size() < 4 || suffix.size() > 5) continue;
            out = prefix + "-" + suffix;
            return true;
        }
    }
    return false;
}

static std::vector<std::string> split_lines(const std::string& s)
{
    std::vector<std::string> lines;
    std::string cur;
    for (char c : s) {
        if (c == '\n' || c == '\r') {
            if (!cur.empty()) {
                lines.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

} // namespace

/**
 * @brief Normalize raw Tesseract output for license plate matching.
 *
 * Steps (giống trim_white_spc() của Q06 + thêm uppercase):
 *   1. Skip leading whitespace
 *   2. Remove trailing whitespace
 *   3. Remove all internal spaces
 *   4. Replace 'O' → '0'  (common OCR misread on license plates)
 *   5. Convert to uppercase
 *
 * @param raw_text  C-string from tess.GetUTF8Text(), may be nullptr
 * @return std::string  e.g. "51F12345"
 */
std::string lp_trim_normalize(char* raw_text)
{
    if (!raw_text) return "";

    /* 1. Remove leading whitespace */
    while (*raw_text && isspace((unsigned char)*raw_text))
        ++raw_text;

    if (*raw_text == '\0') return "";

    /* 2. Remove trailing whitespace */
    char* end = raw_text + strlen(raw_text) - 1;
    while (end > raw_text && isspace((unsigned char)*end))
        --end;
    *(end + 1) = '\0';

    std::string s(raw_text);

    /* 3. Remove all internal spaces */
    s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());

    /* 4. Replace 'O' → '0' (common OCR confusion on license plates) */
    std::replace(s.begin(), s.end(), 'O', '0');

    /* 5. Uppercase */
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);

    return s;
}

std::vector<std::string> lp_build_ocr_candidates(const std::string& raw_text)
{
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto push_unique = [&](const std::string& s) {
        if (s.empty()) return;
        if (seen.insert(s).second) out.push_back(s);
    };

    std::string strict_norm;
    if (normalize_vn_plate_strict(raw_text, strict_norm))
        push_unique(strict_norm);

    {
        std::string copy = raw_text;
        char* cstr = copy.empty() ? nullptr : &copy[0];
        std::string basic = lp_trim_normalize(cstr);
        push_unique(basic);
        if (normalize_vn_plate_strict(basic, strict_norm))
            push_unique(strict_norm);
    }

    std::string sanitized = sanitize_keep_sep(raw_text);
    push_unique(sanitized);
    if (normalize_vn_plate_strict(sanitized, strict_norm))
        push_unique(strict_norm);

    std::vector<std::string> lines = split_lines(raw_text);
    if (lines.size() >= 2) {
        std::string l0 = alnum_only(lines[0]);
        std::string l1 = alnum_only(lines[1]);
        std::string merge01 = l0 + l1;
        std::string merge10 = l1 + l0;
        push_unique(merge01);
        push_unique(merge10);
        if (normalize_vn_plate_strict(merge01, strict_norm))
            push_unique(strict_norm);
        if (normalize_vn_plate_strict(merge10, strict_norm))
            push_unique(strict_norm);
    }

    return out;
}
