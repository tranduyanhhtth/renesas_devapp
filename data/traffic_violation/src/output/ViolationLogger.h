/*******************************************************************************
 * traffic_violation/src/output/ViolationLogger.h
 * Lưu ảnh + metadata JSON khi phát hiện vi phạm
 * Format tên file: {BIEN_SO}_{YYYYMMDD_HHMMSS_mmm}_{LOAI_VI_PHAM}.jpg
 ******************************************************************************/
#pragma once
#include "common/types.h"
#include "common/config.h"
#include <string>
#include <mutex>

class ViolationLogger {
public:
    explicit ViolationLogger(const OutputConfig& cfg);

    // Ghi vi phạm vào disk. Thread-safe.
    void log(const ViolationEvent& ev);

    // Flush JSON (nếu save_json = true)
    void flush();

    size_t totalLogged() const { return m_count; }

private:
    std::string buildFilename(const ViolationEvent& ev) const;
    std::string buildJson(const ViolationEvent& ev,
                          const std::string& img_path) const;
    static std::string formatTimestamp(
        const std::chrono::system_clock::time_point& tp);

    OutputConfig  m_cfg;
    size_t        m_count{0};
    std::mutex    m_mutex;
};
