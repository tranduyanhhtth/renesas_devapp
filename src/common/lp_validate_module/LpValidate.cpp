/*******************************************************************************
 * LpValidate.cpp – Vietnamese license plate structural validation
 * Tương đương date_check.cpp của Q06_expiry_date_detection
 ******************************************************************************/
#include "LpValidate.h"
#include <algorithm>
#include <cctype>
#include <iostream>

/**
 * @brief Constructor – nạp danh sách mã tỉnh hợp lệ của Việt Nam.
 *        Tương đương DateChecker::DateChecker() nạp monthMap trong Q06.
 *
 * Nguồn: Nghị định 65/2012/NĐ-CP và các sửa đổi bổ sung
 */
LpValidator::LpValidator()
{
    /* Biển số xe ô tô và xe máy theo tỉnh/thành phố */
    m_valid_provinces = {
        /* Hà Nội */         "29","30","31","32","33","40",
        /* TP.HCM */         "41","50","51","52","53","54","55","56","57","58","59",
        /* Hải Phòng */      "15","16",
        /* Đà Nẵng */        "43",
        /* Cần Thơ */        "65",
        /* Bắc Giang */      "98",
        /* Bắc Kạn */        "97",
        /* Bắc Ninh */       "99",
        /* Bến Tre */        "71",
        /* Bình Dương */     "61",
        /* Bình Định */      "77",
        /* Bình Phước */     "93",
        /* Bình Thuận */     "86",
        /* Cà Mau */         "69",
        /* Cao Bằng */       "11",
        /* Đắk Lắk */        "47",
        /* Đắk Nông */       "48",
        /* Điện Biên */      "27",
        /* Đồng Nai */       "39",
        /* Đồng Tháp */      "66",
        /* Gia Lai */        "81",
        /* Hà Giang */       "23",
        /* Hà Nam */         "90",
        /* Hà Tĩnh */        "38",
        /* Hải Dương */      "34",
        /* Hậu Giang */      "95",
        /* Hòa Bình */       "28",
        /* Hưng Yên */       "89",
        /* Khánh Hòa */      "79",
        /* Kiên Giang */     "68",
        /* Kon Tum */        "82",
        /* Lai Châu */       "25",
        /* Lâm Đồng */       "49",
        /* Lạng Sơn */       "12",
        /* Lào Cai */        "24",
        /* Long An */        "62",
        /* Nam Định */       "18",
        /* Nghệ An */        "37",
        /* Ninh Bình */      "35",
        /* Ninh Thuận */     "85",
        /* Phú Thọ */        "19",
        /* Phú Yên */        "78",
        /* Quảng Bình */     "73",
        /* Quảng Nam */      "92",
        /* Quảng Ngãi */     "76",
        /* Quảng Ninh */     "14",
        /* Quảng Trị */      "74",
        /* Sóc Trăng */      "83",
        /* Sơn La */         "26",
        /* Tây Ninh */       "70",
        /* Thái Bình */      "17",
        /* Thái Nguyên */    "20",
        /* Thanh Hóa */      "36",
        /* Thừa Thiên–Huế */ "75",
        /* Tiền Giang */     "63",
        /* Trà Vinh */       "84",
        /* Tuyên Quang */    "22",
        /* Vĩnh Long */      "64",
        /* Vĩnh Phúc */      "88",
        /* Yên Bái */        "21",
    };
}

/**
 * @brief Check if 2-digit province code is a known VN province.
 */
bool LpValidator::is_valid_province(const std::string& code) const
{
    return m_valid_provinces.count(code) > 0;
}

/**
 * @brief Check letter series does not contain forbidden OCR-error chars.
 *        Biển số VN không dùng I, O, Q để tránh nhầm với 1, 0, 0.
 */
bool LpValidator::is_valid_series(const std::string& series) const
{
    for (char c : series) {
        if (!std::isalpha((unsigned char)c) && !std::isdigit((unsigned char)c))
            return false;
        if (c == 'I' || c == 'O' || c == 'Q')
            return false;
    }
    return !series.empty();
}

/**
 * @brief Full structural validation of a parsed VN plate.
 *        Tương đương is_expired() / calculate_days_left() trong Q06.
 *
 * Checks:
 *   - matched flag must be true
 *   - province code in known list
 *   - letter series valid (no I/O/Q)
 *   - number sequence 4-5 digits
 *
 * @param lp  result from match_vn_plate()
 * @return true if plate is structurally valid
 */
bool LpValidator::is_valid_plate(const lp_struct& lp) const
{
    if (!lp.matched) return false;

    if (!is_valid_province(lp.province_code)) {
        std::cout << "[LpValidator] Unknown province code: "
                  << lp.province_code << "\n";
        /* Non-fatal: vẫn accept nếu format match — tỉnh mới có thể chưa có */
    }

    if (!is_valid_series(lp.letter_series)) {
        std::cout << "[LpValidator] Invalid series: " << lp.letter_series << "\n";
        return false;
    }

    size_t num_digits = lp.number_seq.size();
    if (num_digits < 4 || num_digits > 5) {
        std::cout << "[LpValidator] Invalid number length: "
                  << num_digits << " digits\n";
        return false;
    }

    /* All digits in number_seq must be numeric */
    for (char c : lp.number_seq) {
        if (!std::isdigit((unsigned char)c)) return false;
    }

    return true;
}

/**
 * @brief Format plate to canonical "DDX-NNNNN" with dash.
 */
std::string LpValidator::format_plate(const lp_struct& lp) const
{
    if (!lp.matched) return "";
    return lp.province_code + lp.letter_series + "-" + lp.number_seq;
}
