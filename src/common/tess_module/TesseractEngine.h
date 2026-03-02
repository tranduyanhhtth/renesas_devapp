/*******************************************************************************
 * TesseractEngine.h – Singleton Tesseract OCR engine
 * Tham chiếu từ Q06_expiry_date_detection (Renesas RZ/V AI SDK)
 * Adapted for License Plate OCR in traffic_violation project
 ******************************************************************************/
#pragma once
#include <tesseract/baseapi.h>
#include <mutex>

/**
 * @brief Singleton – chỉ 1 engine Tesseract cho toàn app.
 *        Khởi tạo 1 lần, dùng lại mỗi frame (tránh overhead Init).
 */
class TesseractEngine {
    tesseract::TessBaseAPI m_engine;
    static std::mutex      s_mutex;

    TesseractEngine();

public:
    static TesseractEngine& getInstance();
    tesseract::TessBaseAPI& getEngine();
    void clear();
    void end();

    // Non-copyable
    TesseractEngine(const TesseractEngine&)            = delete;
    TesseractEngine& operator=(const TesseractEngine&) = delete;
};
