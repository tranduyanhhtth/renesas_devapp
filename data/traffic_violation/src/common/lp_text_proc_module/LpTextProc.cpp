/*******************************************************************************
 * LpTextProc.cpp – License plate raw OCR text processing
 * Tương đương TextProc.cpp của Q06_expiry_date_detection
 ******************************************************************************/
#include "LpTextProc.h"
#include <algorithm>
#include <cctype>

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
