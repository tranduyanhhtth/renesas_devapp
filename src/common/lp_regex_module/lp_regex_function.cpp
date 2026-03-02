/*******************************************************************************
 * lp_regex_function.cpp – Vietnamese license plate regex matching
 * Tương đương regex_function.cpp của Q06_expiry_date_detection
 *
 * VN plate formats supported:
 *   LP_FMT_DASH       : DD[A-Z]{1,2}-\d{4,5}   e.g. 51F-12345, 43AA-12345
 *   LP_FMT_SERIES_NUM : DD[A-Z]\d-\d{4}         e.g. 29B1-1234
 *   LP_FMT_NO_DASH    : DD[A-Z]{1,2}\d{4,5}     e.g. 51F12345, 43AA12345
 *   LP_FMT_SERIES_NODASH: DD[A-Z]\d\d{4}        e.g. 29B11234
 ******************************************************************************/
#include "lp_regex_function.h"
#include <iostream>

/**
 * @brief Build precompiled regex list for VN plate patterns.
 *        Gọi một lần khi khởi tạo — tương đương create_regex_dict() Q06.
 *
 *  Capture groups chuẩn cho mọi pattern:
 *    Group 1 (province) : 2 chữ số
 *    Group 2 (series)   : 1-2 chữ cái (+ có thể thêm 1 chữ số cho series_num)
 *    Group 3 (number)   : 4-5 chữ số
 *
 * @return vector of (compiled_regex, format_name)
 */
std::vector<std::pair<std::regex, std::string>> create_lp_regex_list()
{
    /* Pattern definitions — thứ tự quan trọng: dài/đặc biệt hơn trước */
    std::vector<std::pair<std::string, std::string>> raw_patterns = {
        /* LP_FMT_DASH: 51F-12345 | 43AA-12345 */
        { R"(^(\d{2})([A-Z]{1,2})-(\d{4,5})$)",       "LP_FMT_DASH"        },
        /* LP_FMT_SERIES_NUM_DASH: 29B1-1234 */
        { R"(^(\d{2})([A-Z]\d)-(\d{4})$)",             "LP_FMT_SERIES_NUM"  },
        /* LP_FMT_NO_DASH: 51F12345 | 43AA12345 */
        { R"(^(\d{2})([A-Z]{1,2})(\d{4,5})$)",         "LP_FMT_NO_DASH"     },
        /* LP_FMT_SERIES_NUM_NODASH: 29B11234 */
        { R"(^(\d{2})([A-Z]\d)(\d{4})$)",              "LP_FMT_SERIES_NODASH" },
    };

    std::vector<std::pair<std::regex, std::string>> patterns;
    patterns.reserve(raw_patterns.size());

    for (auto& [pattern_str, fmt_name] : raw_patterns) {
        patterns.emplace_back(std::regex(pattern_str), fmt_name);
    }

    printf("[INFO] VN plate regex dictionary ready (%zu patterns)\n",
           patterns.size());
    return patterns;
}

/**
 * @brief Match normalized string against VN plate patterns.
 *        Tương đương get_yymmddd() của Q06.
 *
 * @param patterns  từ create_lp_regex_list()
 * @param inp_str   từ lp_trim_normalize()
 * @return lp_struct  .matched=false nếu không khớp format nào
 */
lp_struct match_vn_plate(
    const std::vector<std::pair<std::regex, std::string>>& patterns,
    const std::string& inp_str)
{
    lp_struct result;
    result.matched = false;

    for (const auto& [rgx, fmt_name] : patterns) {
        std::smatch match;
        if (std::regex_match(inp_str, match, rgx)) {
            result.matched       = true;
            result.format        = fmt_name;
            result.province_code = match[1].str();   // e.g. "51"
            result.letter_series = match[2].str();   // e.g. "F" | "AA" | "B1"
            result.number_seq    = match[3].str();   // e.g. "12345"

            /* Normalize full plate to DD[X]-NNNNN format */
            result.full_plate = result.province_code + result.letter_series
                                + "-" + result.number_seq;

            std::cout << "[lp_regex] Matched '" << inp_str
                      << "' -> format=" << fmt_name
                      << " plate=" << result.full_plate << "\n";
            break;
        }
    }

    if (!result.matched) {
        std::cout << "[lp_regex] '" << inp_str
                  << "' does not match any VN plate format\n";
    }

    return result;
}
