/*******************************************************************************
 * LpTextProc.h – License plate raw OCR text processing
 * Tương đương text_proc_module của Q06_expiry_date_detection
 *
 * Xử lý chuỗi raw từ Tesseract trước khi đưa vào regex matching:
 *   1. Xóa whitespace đầu/cuối
 *   2. Xóa space bên trong
 *   3. Thay 'O' → '0' (lỗi OCR phổ biến)
 *   4. Chuyển thành uppercase
 ******************************************************************************/
#ifndef LP_TEXT_PROC_H
#define LP_TEXT_PROC_H

#include <string>
#include <cstring>

/**
 * @brief Normalize raw Tesseract output for license plate matching.
 *        Equivalent to trim_white_spc() in Q06 TextProc.cpp.
 *
 * @param raw_text  C-string returned by tess.GetUTF8Text() — may be nullptr
 * @return std::string  cleaned string, e.g. "51F12345"
 */
std::string lp_trim_normalize(char* raw_text);

#endif // LP_TEXT_PROC_H
