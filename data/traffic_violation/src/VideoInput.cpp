/*******************************************************************************
 * traffic_violation/src/VideoInput.cpp
 ******************************************************************************/
#include "VideoInput.h"
#include <iostream>
#include <chrono>

VideoInput::VideoInput(const std::string& source,
                       int width, int height, int fps,
                       const std::string& gst_pipeline)
    : m_source(source)
    , m_width(width), m_height(height), m_fps(fps)
    , m_gst_pipeline(gst_pipeline)
{}

VideoInput::~VideoInput() {
    close();
}

std::string VideoInput::buildGstreamerPipeline() const {
    // Nếu nguồn là số nguyên → camera USB
    bool is_camera = !m_source.empty() &&
                     m_source.find_first_not_of("0123456789") == std::string::npos;
    if (is_camera) {
        return "v4l2src device=/dev/video" + m_source +
               " ! image/jpeg,width=" + std::to_string(m_width) +
               ",height=" + std::to_string(m_height) +
               ",framerate=" + std::to_string(m_fps) + "/1"
               " ! jpegdec ! videoconvert ! appsink";
    }
    // File video
    return "filesrc location=" + m_source +
           " ! decodebin ! videoconvert"
           " ! videoscale ! video/x-raw,width=" + std::to_string(m_width) +
           ",height=" + std::to_string(m_height) +
           " ! appsink";
}

bool VideoInput::open() {
    std::string pipeline = m_gst_pipeline.empty()
                           ? buildGstreamerPipeline()
                           : m_gst_pipeline;

    // Thử GStreamer trước, fallback sang OpenCV native
    m_cap.open(pipeline, cv::CAP_GSTREAMER);
    if (!m_cap.isOpened()) {
        std::cerr << "[VideoInput] GStreamer failed, trying OpenCV native: "
                  << m_source << "\n";
        // Thử parse source là số (camera index) hay chuỗi (file)
        bool is_camera = !m_source.empty() &&
                         m_source.find_first_not_of("0123456789") == std::string::npos;
        if (is_camera)
            m_cap.open(std::stoi(m_source));
        else
            m_cap.open(m_source);

        if (!m_cap.isOpened()) {
            std::cerr << "[VideoInput] Cannot open source: " << m_source << "\n";
            return false;
        }
        m_cap.set(cv::CAP_PROP_FRAME_WIDTH,  m_width);
        m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, m_height);
        m_cap.set(cv::CAP_PROP_FPS,          m_fps);
    }

    m_stop = false;
    m_open = true;
    m_capture_thread = std::thread(&VideoInput::captureLoop, this);
    return true;
}

void VideoInput::close() {
    m_stop = true;
    if (m_capture_thread.joinable())
        m_capture_thread.join();
    m_cap.release();
    m_open = false;
}

bool VideoInput::getFrame(cv::Mat& out) {
    std::lock_guard<std::mutex> lk(m_frame_mutex);
    if (m_latest_frame.empty()) return false;
    m_latest_frame.copyTo(out);
    return true;
}

void VideoInput::captureLoop() {
    cv::Mat frame;
    auto t0 = std::chrono::steady_clock::now();
    int frame_count = 0;

    while (!m_stop) {
        if (!m_cap.read(frame)) {
            // Stream ended
            m_open = false;
            break;
        }
        {
            std::lock_guard<std::mutex> lk(m_frame_mutex);
            frame.copyTo(m_latest_frame);
        }
        ++frame_count;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - t0).count();
        if (elapsed >= 1.0) {
            m_fps_measured = frame_count / elapsed;
            frame_count = 0;
            t0 = now;
        }
    }
}
