/*******************************************************************************
 * traffic_violation/src/output/ViolationLogger.cpp
 ******************************************************************************/
#include "ViolationLogger.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>

ViolationLogger::ViolationLogger(const OutputConfig& cfg) : m_cfg(cfg) {
    // Tạo thư mục output nếu chưa có
    struct stat st{};
    if (stat(m_cfg.save_dir.c_str(), &st) != 0)
        ::mkdir(m_cfg.save_dir.c_str(), 0755);
}

std::string ViolationLogger::formatTimestamp(
    const std::chrono::system_clock::time_point& tp)
{
    auto tt  = std::chrono::system_clock::to_time_t(tp);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   tp.time_since_epoch()) % 1000;
    struct tm ltm{};
    localtime_r(&tt, &ltm);
    std::ostringstream ss;
    ss << std::put_time(&ltm, "%Y%m%d_%H%M%S")
       << '_' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string ViolationLogger::buildFilename(const ViolationEvent& ev) const {
    std::string plate = ev.plate.empty() ? "NOPLATE" : ev.plate;
    // Loại bỏ ký tự không hợp lệ trong tên file
    for (char& c : plate)
        if (c == '/' || c == '\\' || c == ' ') c = '_';

    std::string ts   = formatTimestamp(ev.timestamp);
    std::string viol = violationName(ev.type);

    std::string pattern = m_cfg.filename_pattern;
    auto replace = [&](std::string& s, const std::string& key, const std::string& val) {
        auto pos = s.find(key);
        while (pos != std::string::npos) {
            s.replace(pos, key.size(), val);
            pos = s.find(key);
        }
    };
    replace(pattern, "{plate}",     plate);
    replace(pattern, "{datetime}",  ts);
    replace(pattern, "{violation}", viol);

    return m_cfg.save_dir + "/" + pattern + ".jpg";
}

std::string ViolationLogger::buildJson(const ViolationEvent& ev,
                                        const std::string& img_path) const {
    std::ostringstream js;
    js << "{\n"
       << "  \"plate\": \""     << ev.plate                    << "\",\n"
       << "  \"timestamp\": \"" << formatTimestamp(ev.timestamp)<< "\",\n"
       << "  \"violation\": \"" << violationName(ev.type)      << "\",\n"
       << "  \"track_id\": "    << ev.track_id                  << ",\n"
       << "  \"bbox\": ["
           << ev.vehicle_rect.x   << ", "
           << ev.vehicle_rect.y   << ", "
           << ev.vehicle_rect.width  << ", "
           << ev.vehicle_rect.height << "],\n"
       << "  \"image\": \""     << img_path                    << "\"\n"
       << "}\n";
    return js.str();
}

void ViolationLogger::log(const ViolationEvent& ev) {
    std::lock_guard<std::mutex> lk(m_mutex);

    std::string img_path = buildFilename(ev);

    // ── Chọn frame cần lưu ────────────────────────────────────────────────────
    cv::Mat save_frame;
    if (m_cfg.save_full_frame || ev.frame.empty()) {
        save_frame = ev.frame.clone();
    } else {
        cv::Rect safe = ev.vehicle_rect &
                        cv::Rect(0, 0, ev.frame.cols, ev.frame.rows);
        if (safe.width > 0 && safe.height > 0)
            save_frame = ev.frame(safe).clone();
        else
            save_frame = ev.frame.clone();
    }

    // Vẽ annotation lên frame lưu
    if (!save_frame.empty()) {
        // Bounding box đỏ
        cv::Rect draw_rect = m_cfg.save_full_frame
                           ? ev.vehicle_rect
                           : cv::Rect(0, 0, save_frame.cols, save_frame.rows);
        cv::rectangle(save_frame, draw_rect, cv::Scalar(0, 0, 255), 3);

        // Label vi phạm
        std::string label = violationName(ev.type) + " | " +
                            (ev.plate.empty() ? "?" : ev.plate);
        int font = cv::FONT_HERSHEY_DUPLEX;
        double scale = 0.8;
        int thick = 2;
        cv::Size ts = cv::getTextSize(label, font, scale, thick, nullptr);
        cv::Point tp = {draw_rect.x, draw_rect.y - 8};
        if (tp.y < 20) tp.y = draw_rect.y + ts.height + 8;
        cv::rectangle(save_frame,
                      {tp.x - 2, tp.y - ts.height - 4},
                      {tp.x + ts.width + 2, tp.y + 4},
                      cv::Scalar(0, 0, 200), cv::FILLED);
        cv::putText(save_frame, label, tp, font, scale,
                    cv::Scalar(255, 255, 255), thick);
    }

    // ── Ghi ảnh ───────────────────────────────────────────────────────────────
    if (!save_frame.empty()) {
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 92};
        if (!cv::imwrite(img_path, save_frame, params))
            std::cerr << "[Logger] imwrite failed: " << img_path << "\n";
        else
            std::cout << "[Logger] Saved: " << img_path << "\n";
    }

    // ── Ghi JSON metadata ─────────────────────────────────────────────────────
    if (m_cfg.save_json) {
        std::string json_path = img_path.substr(0, img_path.rfind('.')) + ".json";
        std::ofstream jf(json_path);
        if (jf.is_open())
            jf << buildJson(ev, img_path);
    }

    ++m_count;
}

void ViolationLogger::flush() {
    // Nothing to flush for now (no buffered writes)
}
