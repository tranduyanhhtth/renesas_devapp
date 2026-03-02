/*******************************************************************************
 * LpValidate.h – Vietnamese license plate structural validation
 * Tương đương date_chck_module của Q06_expiry_date_detection
 *
 * Sau khi regex match thành công, validator kiểm tra:
 *   1. Province code hợp lệ (mã tỉnh VN)
 *   2. Letter series hợp lệ (không dùng các chữ I, O, Q)
 *   3. Number sequence đủ 4-5 chữ số
 ******************************************************************************/
#ifndef LP_VALIDATE_H
#define LP_VALIDATE_H

#include <string>
#include <unordered_set>
#include "common/lp_regex_module/lp_regex_function.h"

/**
 * @brief Vietnamese license plate validator.
 *        Tương đương class DateChecker trong Q06.
 */
class LpValidator {
public:
    LpValidator();

    /**
     * @brief Check if province code is a known VN province.
     * @param code  2-digit string e.g. "51"
     * @return true if valid
     */
    bool is_valid_province(const std::string& code) const;

    /**
     * @brief Check letter series does not contain forbidden chars (I, O, Q).
     * @param series  e.g. "F", "AA", "B1"
     * @return true if valid
     */
    bool is_valid_series(const std::string& series) const;

    /**
     * @brief Full plate validation: province + series + number length.
     * @param lp  parsed plate from match_vn_plate()
     * @return true if all components are valid
     */
    bool is_valid_plate(const lp_struct& lp) const;

    /**
     * @brief Format plate to standard form "DDX-NNNNN".
     *        If already has dash, return as-is. Otherwise insert dash.
     * @param lp  parsed plate
     * @return std::string  e.g. "51F-12345"
     */
    std::string format_plate(const lp_struct& lp) const;

private:
    std::unordered_set<std::string> m_valid_provinces;
    static constexpr const char* FORBIDDEN_CHARS = "IOQ";
};

#endif // LP_VALIDATE_H
