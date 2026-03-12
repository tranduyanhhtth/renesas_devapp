/*******************************************************************************
 * traffic_violation/src/VideoInput.cpp
 ******************************************************************************/
#include "VideoInput.h"
#include <iostream>
#include <chrono>
#include <cstdlib>   /* system() for media-ctl */

/* ── Constructor ────────────────────────────────────────────────────────── */
VideoInput::VideoInput(const std::string& source,
                       SourceType         source_type,
                       int width, int height, int fps,
                       const std::string& gst_pipeline,
                       bool loop_file)
    : m_source(source)
    , m_source_type(source_type)
    , m_width(width), m_height(height), m_fps(fps)
    , m_gst_pipeline(gst_pipeline)
    , m_loop_file(loop_file)
{}

VideoInput::~VideoInput() {
    close();
}

/* ── mipiInit ───────────────────────────────────────────────────────────────
 * Chạy 4 lệnh media-ctl để cấu hình MIPI CSI-2 camera (OV5645) trên RZ/V2L.
 * Giống hàm mipi_cam_init() trong R01_object_detection.
 * Trả false nếu bất kỳ lệnh nào thất bại.                                   */
bool VideoInput::mipiInit() const
{
    const std::string fmt = std::to_string(m_width) + "x" + std::to_string(m_height);
    const std::string commands[4] = {
        "media-ctl -d /dev/media0 -r",
        "media-ctl -d /dev/media0 -V \"'ov5645 0-003c':0 [fmt:UYVY8_2X8/" + fmt + " field:none]\"",
        "media-ctl -d /dev/media0 -l \"'rzg2l_csi2 10830400.csi2':1 -> 'CRU output':0 [1]\"",
        "media-ctl -d /dev/media0 -V \"'rzg2l_csi2 10830400.csi2':1 [fmt:UYVY8_2X8/" + fmt + " field:none]\""
    };
    for (const auto& cmd : commands) {
        std::cout << "[VideoInput] MIPI init: " << cmd << "\n";
        if (system(cmd.c_str()) != 0) {
            std::cerr << "[VideoInput][WARN] media-ctl command failed (non-fatal)\n";
            /* Non-fatal: camera may already be initialised */
        }
    }
    return true;
}

/* ── buildGstreamerPipeline ─────────────────────────────────────────────────
 * Build pipeline string cho từng SourceType.                                 */
std::string VideoInput::buildGstreamerPipeline() const
{
    const std::string W  = std::to_string(m_width);
    const std::string H  = std::to_string(m_height);
    const std::string FPS = std::to_string(m_fps);

    switch (m_source_type) {

    case SourceType::FILE: {
        /* filesrc → decodebin → scale → appsink
         * Thêm videorate để đảm bảo đúng fps (tránh burst khi decode nhanh).
         * loop_file: videorepeat không khả dụng rộng rãi; dùng appsink +
         * seek trong captureLoop thay vào đó.                                */
        return "filesrc location=" + m_source +
               " ! decodebin ! videoconvert ! videoscale"
               " ! video/x-raw,width=" + W + ",height=" + H +
               " ! videorate ! video/x-raw,framerate=" + FPS + "/1"
               " ! appsink drop=true sync=false";
    }

    case SourceType::MIPI: {
        /* Sau khi mipiInit() đã cấu hình media-ctl, dùng v4l2src như USB.
         * source = "/dev/video0" (thiết bị CRU output).                       */
        std::string dev = m_source.empty() ? "/dev/video0" : m_source;
        return "v4l2src device=" + dev + " io-mode=dmabuf"
               " ! video/x-raw,width=" + W + ",height=" + H +
               ",framerate=" + FPS + "/1"
               " ! videoconvert ! appsink drop=true sync=false";
    }

    default:
        /* gstreamer_pipeline được set từ bên ngoài, không build ở đây */
        return m_gst_pipeline;
    }
}

/* ── open ───────────────────────────────────────────────────────────────── */
bool VideoInput::open()
{
    /* MIPI cần init media-ctl trước khi mở capture */
    if (m_source_type == SourceType::MIPI) {
        if (!mipiInit()) {
            std::cerr << "[VideoInput] MIPI init failed\n";
            return false;
        }
    }

    /* Determine pipeline */
    std::string pipeline = (!m_gst_pipeline.empty() &&
                            m_source_type != SourceType::CUSTOM)
                           ? m_gst_pipeline           /* explicit override */
                           : buildGstreamerPipeline();

    std::cout << "[VideoInput] Type    : ";
    switch (m_source_type) {
        case SourceType::FILE:   std::cout << "file\n";   break;
        case SourceType::USB:    std::cout << "usb\n";    break;
        case SourceType::MIPI:   std::cout << "mipi\n";   break;
        case SourceType::RTSP:   std::cout << "rtsp\n";   break;
        case SourceType::CUSTOM: std::cout << "custom\n"; break;
    }
    std::cout << "[VideoInput] Source  : " << m_source  << "\n";
    std::cout << "[VideoInput] Pipeline: " << pipeline  << "\n";

    /* Try GStreamer first */
    m_cap.open(pipeline, cv::CAP_GSTREAMER);
    if (!m_cap.isOpened()) {
        std::cerr << "[VideoInput] GStreamer failed, trying OpenCV native\n";

        /* OpenCV native fallback (file or device index) */
        bool is_idx = !m_source.empty() &&
                      m_source.find_first_not_of("0123456789") == std::string::npos;
        if (is_idx)
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

/* ── close ──────────────────────────────────────────────────────────────── */
void VideoInput::close() {
    m_stop = true;
    if (m_capture_thread.joinable())
        m_capture_thread.join();
    m_cap.release();
    m_open = false;
}

/* ── getFrame ───────────────────────────────────────────────────────────── */
bool VideoInput::getFrame(cv::Mat& out) {
    std::lock_guard<std::mutex> lk(m_frame_mutex);
    if (m_latest_frame.empty()) return false;
    m_latest_frame.copyTo(out);
    return true;
}

/* ── captureLoop ────────────────────────────────────────────────────────── */
void VideoInput::captureLoop() {
    cv::Mat frame;
    auto t0 = std::chrono::steady_clock::now();
    int frame_count = 0;

    while (!m_stop) {
        if (!m_cap.read(frame) || frame.empty()) {
            /* File video ended */
            if (m_source_type == SourceType::FILE && m_loop_file) {
                /* Seek to beginning and continue */
                m_cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                std::cout << "[VideoInput] File looped\n";
                continue;
            }
            /* Stream ended or camera error */
            std::cerr << "[VideoInput] Stream ended or read error\n";
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
