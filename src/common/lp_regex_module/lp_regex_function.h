/*******************************************************************************
 * lp_regex_function.h – Vietnamese license plate regex matching
 * Tương đương regex_module của Q06_expiry_date_detection
 *
 * Biển số VN được chia làm 3 thành phần:
 *   province_code  : 2 chữ số  (11-99)   e.g. "51", "29"
 *   letter_series  : 1-2 chữ cái          e.g. "F", "AA", "B1"
 *   number_seq     : 4-5 chữ số           e.g. "12345", "1234"
 *
 * Hỗ trợ các format:
 *   LP_FMT_DASH      : DD[X]{1,2}-NNNNN    e.g. 51F-12345
 *   LP_FMT_SERIES_NUM: DDX[0-9]-NNNN       e.g. 29B1-1234
 *   LP_FMT_NO_DASH   : DD[X]{1,2}NNNNN     e.g. 51F12345
 ******************************************************************************/
#ifndef LP_REGEX_FUNCTION_H
#define LP_REGEX_FUNCTION_H

#include <string>
#include <vector>
#include <regex>

/* Tesseract OCR image constants (tương đương Q06 MIN_CROP_HEIGHT, TESS_IMG_RESOLUTION) */
#define LP_MIN_CROP_HEIGHT    (32)
#define LP_TESS_RESOLUTION    (70)

/**
 * @brief Result structure for license plate regex matching.
 *        Tương đương ymd_struct trong Q06.
 */
struct lp_struct {
    bool        matched       = false;
    std::string format;          // "LP_FMT_DASH" | "LP_FMT_SERIES_NUM" | "LP_FMT_NO_DASH"
    std::string province_code;   // "51"
    std::string letter_series;   // "F" | "AA" | "B1"
    std::string number_seq;      // "12345"
    std::string full_plate;      // "51F-12345" (normalized with dash)
};

/**
 * @brief Build and return compiled regex list for VN plate matching.
 *        Tương đương create_regex_dict() trong Q06.
 *        Gọi một lần lúc khởi tạo để tránh recompile mỗi frame.
 *
 * @return std::vector<std::pair<std::regex, std::string>>
 *         pair: (compiled pattern, format name)
 */
std::vector<std::pair<std::regex, std::string>> create_lp_regex_list();

/**
 * @brief Match a normalized string against VN plate patterns.
 *        Tương đương get_yymmddd() trong Q06.
 *
 * @param patterns  Precompiled list from create_lp_regex_list()
 * @param inp_str   Normalized string from lp_trim_normalize()
 * @return lp_struct  parsed result; .matched=false if no pattern fits
 */
lp_struct match_vn_plate(
    const std::vector<std::pair<std::regex, std::string>>& patterns,
    const std::string& inp_str);

#endif // LP_REGEX_FUNCTION_H
