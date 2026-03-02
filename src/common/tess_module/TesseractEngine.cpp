/*******************************************************************************
 * TesseractEngine.cpp – Singleton Tesseract OCR engine
 * Tham chiếu từ Q06_expiry_date_detection (Renesas RZ/V AI SDK)
 ******************************************************************************/
#include "TesseractEngine.h"
#include <cstdio>

// PSM_SINGLE_LINE (7): phù hợp cho biển số xe (1 dòng văn bản)
#define PAGE_SEG_MODE   (7)

// Whitelist: chữ hoa + số + dấu gạch (đủ cho biển số VN)
// Biển số VN: 51F-12345 | 29B-1234 | 43AA-123.45
#define CHAR_WHITELIST  "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-."

std::mutex TesseractEngine::s_mutex;

TesseractEngine::TesseractEngine()
{
    // Dùng "eng" (như Q06) – biển số VN dùng Latin + số, không cần "vie"
    if (m_engine.Init(NULL, "eng", tesseract::OEM_DEFAULT)) {
        fprintf(stderr, "[TesseractEngine] ERROR: Failed to initialize Tesseract\n");
        return;
    }

    m_engine.SetPageSegMode(
        static_cast<tesseract::PageSegMode>(PAGE_SEG_MODE));

    // Giới hạn ký tự → giảm lỗi nhầm ký tự
    m_engine.SetVariable("tessedit_char_whitelist", CHAR_WHITELIST);

    printf("[TesseractEngine] Initialized (PSM_SINGLE_LINE, whitelist: %s)\n",
           CHAR_WHITELIST);
}

TesseractEngine& TesseractEngine::getInstance()
{
    static TesseractEngine instance;  // khởi tạo 1 lần, thread-safe (C++11)
    return instance;
}

tesseract::TessBaseAPI& TesseractEngine::getEngine()
{
    return m_engine;
}

void TesseractEngine::clear()
{
    m_engine.Clear();
}

void TesseractEngine::end()
{
    m_engine.End();
}
