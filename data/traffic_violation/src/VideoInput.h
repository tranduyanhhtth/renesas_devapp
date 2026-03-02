/*******************************************************************************
 * traffic_violation/src/VideoInput.h
 ******************************************************************************/
#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>

class VideoInput {
public:
    explicit VideoInput(const std::string& source,
                        int width, int height, int fps,
                        const std::string& gst_pipeline = "");
    ~VideoInput();

    bool open();
    void close();

    // Lấy frame mới nhất (thread-safe). Trả false nếu stream kết thúc.
    bool getFrame(cv::Mat& out);

    bool isOpen() const { return m_open.load(); }
    double fpsMeasured() const { return m_fps_measured; }

private:
    void captureLoop();
    std::string buildGstreamerPipeline() const;

    std::string        m_source;
    int                m_width, m_height, m_fps;
    std::string        m_gst_pipeline;

    cv::VideoCapture   m_cap;
    cv::Mat            m_latest_frame;
    std::mutex         m_frame_mutex;
    std::thread        m_capture_thread;
    std::atomic<bool>  m_open{false};
    std::atomic<bool>  m_stop{false};
    double             m_fps_measured{0.0};
};
