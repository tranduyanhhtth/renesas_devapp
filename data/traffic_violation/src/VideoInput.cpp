/*******************************************************************************
 * traffic_violation/src/VideoInput.cpp
 ******************************************************************************/
#include "VideoInput.h"
#include <algorithm>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>

VideoInput::VideoInput(const std::string& source,
                       SourceType         source_type,
                       int width, int height, int fps,
                       const std::string& gst_pipeline,
                       bool loop_file,
                       bool realtime_file)
    : m_source(source)
    , m_source_type(source_type)
    , m_width(width), m_height(height), m_fps(fps)
    , m_gst_pipeline(gst_pipeline)
    , m_loop_file(loop_file)
    , m_realtime_file(realtime_file)
{}

VideoInput::~VideoInput() {
    close();
}

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
        if (system(cmd.c_str()) != 0)
            std::cerr << "[VideoInput][WARN] media-ctl command failed (non-fatal)\n";
    }
    return true;
}

std::string VideoInput::buildGstreamerPipeline() const
{
    const std::string W  = std::to_string(m_width);
    const std::string H  = std::to_string(m_height);
    const std::string FPS = std::to_string(m_fps);

    switch (m_source_type) {

    case SourceType::FILE: {
        return "filesrc location=" + m_source +
               " ! qtdemux ! h264parse ! avdec_h264"
               " ! videoscale"
               " ! video/x-raw,width=" + W + ",height=" + H +
               " ! videoconvert"
               " ! queue max-size-buffers=1 max-size-bytes=0 max-size-time=0"
               " ! appsink drop=true sync=false max-buffers=1";
    }

    case SourceType::MIPI: {
        std::string dev = m_source.empty() ? "/dev/video0" : m_source;
        return "v4l2src device=" + dev + " io-mode=dmabuf"
               " ! video/x-raw,width=" + W + ",height=" + H +
               ",framerate=" + FPS + "/1"
               " ! videoconvert ! appsink drop=true sync=false";
    }

    default:
        return m_gst_pipeline;
    }
}

bool VideoInput::open()
{
    std::cout << "[VideoInput] Type    : ";
    switch (m_source_type) {
        case SourceType::FILE:   std::cout << "file\n";   break;
        case SourceType::MIPI:   std::cout << "mipi\n";   break;
        default:                 std::cout << "custom\n"; break;
    }
    std::cout << "[VideoInput] Source  : " << m_source << "\n";

    if (m_source_type == SourceType::FILE) {
        const std::string W  = std::to_string(m_width);
        const std::string H  = std::to_string(m_height);
        /* Realtime file playback: keep wall-clock speed, drop frames if lagging. */
        const std::string sink_mjpeg = m_realtime_file
                          ? " ! appsink max-buffers=1 drop=true sync=true"
                          : " ! appsink max-buffers=4 drop=false sync=true";
        const std::string sink_h264  = m_realtime_file
                          ? " ! appsink max-buffers=1 drop=true sync=true"
                          : " ! appsink max-buffers=4 drop=false sync=false";

        /* Check for MJPEG AVI by extension */
        bool is_avi = (m_source.size() >= 4 &&
                       m_source.substr(m_source.size() - 4) == ".avi");

        if (is_avi) {
            std::string pipe_mjpeg =
                "filesrc location=" + m_source +
                " ! avidemux ! jpegdec"
                " ! videoscale ! video/x-raw,width=" + W + ",height=" + H +
                " ! videoconvert" + sink_mjpeg;
            std::cout << "[VideoInput] MJPEG pipeline: " << pipe_mjpeg << "\n";
            m_cap.open(pipe_mjpeg, cv::CAP_GSTREAMER);
            m_active_pipeline = pipe_mjpeg;
        } else {
            /* Tier 1: omxh264dec hardware */
            std::string pipe_omx =
                "filesrc location=" + m_source +
                " ! qtdemux ! h264parse ! omxh264dec"
                " ! videoscale ! video/x-raw,width=" + W + ",height=" + H +
                " ! videoconvert" + sink_h264;
            std::cout << "[VideoInput] Trying omxh264dec: " << pipe_omx << "\n";
            m_cap.open(pipe_omx, cv::CAP_GSTREAMER);
            if (m_cap.isOpened()) {
                m_active_pipeline = pipe_omx;
            }

            if (!m_cap.isOpened()) {
                /* Tier 2: avdec_h264 software */
                std::string pipe_sw =
                    "filesrc location=" + m_source +
                    " ! qtdemux ! h264parse ! avdec_h264"
                    " ! videoscale ! video/x-raw,width=" + W + ",height=" + H +
                    " ! videoconvert" + sink_h264;
                std::cout << "[VideoInput] omxh264dec failed, avdec_h264: " << pipe_sw << "\n";
                m_cap.open(pipe_sw, cv::CAP_GSTREAMER);
                if (m_cap.isOpened()) {
                    m_active_pipeline = pipe_sw;
                }
            }
        }

        if (!m_cap.isOpened()) {
            std::cerr << "[VideoInput] All decoders failed for: " << m_source << "\n";
            return false;
        }
        m_is_gstreamer = true;
        /* m_active_pipeline is set to the exact pipeline that opened successfully. */
        std::cout << "[VideoInput] Backend : GStreamer\n";
    } else {
        if (m_source_type == SourceType::MIPI) {
            if (!mipiInit()) {
                std::cerr << "[VideoInput] MIPI init failed\n";
                return false;
            }
        }
        std::string pipeline = m_gst_pipeline.empty()
                               ? buildGstreamerPipeline()
                               : m_gst_pipeline;
        std::cout << "[VideoInput] Pipeline: " << pipeline << "\n";
        m_cap.open(pipeline, cv::CAP_GSTREAMER);
        if (!m_cap.isOpened()) {
            std::cerr << "[VideoInput] GStreamer failed for live source\n";
            return false;
        }
        std::cout << "[VideoInput] Backend : GStreamer\n";
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
    auto t0           = std::chrono::steady_clock::now();
    auto t_last_frame = std::chrono::steady_clock::now();
    int  frame_count  = 0;
    int  jitter_count = 0;
    double max_gap_ms  = 0.0;
    double max_read_ms = 0.0;

    /* GStreamer with sync=true paces at PTS rate; FFmpeg needs manual sleep. */
    const bool do_pace = (m_source_type == SourceType::FILE && m_fps > 0 && !m_is_gstreamer);
    const auto frame_interval = std::chrono::microseconds(
        do_pace ? (1000000 / m_fps) : 0);
    auto next_tp = std::chrono::steady_clock::now();

    while (!m_stop) {
        auto t_read_start = std::chrono::steady_clock::now();
        if (!m_cap.read(frame) || frame.empty()) {
            if (m_source_type == SourceType::FILE && m_loop_file) {
                if (m_is_gstreamer && !m_active_pipeline.empty()) {
                    /* GStreamer: CAP_PROP_POS_FRAMES seek does not work with
                     * filesrc pipelines — must release and reopen the pipeline. */
                    m_cap.release();
                    m_cap.open(m_active_pipeline, cv::CAP_GSTREAMER);
                    if (!m_cap.isOpened()) {
                        std::cerr << "[VideoInput] Loop reopen failed\n";
                        m_open = false; break;
                    }
                } else {
                    m_cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                }
                std::cout << "[VideoInput] File looped\n";
                continue;
            }
            std::cerr << "[VideoInput] Stream ended or read error\n";
            m_open = false;
            break;
        }
        /* ── Resize to configured resolution (FFmpeg gives native size) ─── */
        if (m_width > 0 && m_height > 0 &&
            (frame.cols != m_width || frame.rows != m_height)) {
            cv::resize(frame, frame, cv::Size(m_width, m_height), 0, 0, cv::INTER_LINEAR);
        }

        /* ── Inter-frame jitter measurement ─────────────────────────────── */
        auto t_now_cap = std::chrono::steady_clock::now();
        double read_ms = std::chrono::duration<double>(t_now_cap - t_read_start).count() * 1000.0;
        double gap_ms  = std::chrono::duration<double>(t_now_cap - t_last_frame).count() * 1000.0;
        t_last_frame   = t_now_cap;
        if (gap_ms > max_gap_ms)   max_gap_ms   = gap_ms;
        if (read_ms > max_read_ms) max_read_ms  = read_ms;
        if (gap_ms > 60.0) {   /* > 1.5× nominal 40 ms → visible freeze */
            // fprintf(stderr, "[CAP][JITTER] gap=%.0fms  read=%.0fms\n", gap_ms, read_ms);
            ++jitter_count;
        }

        {
            std::lock_guard<std::mutex> lk(m_frame_mutex);
            m_latest_frame = std::move(frame);
        }
        m_frame_seq.fetch_add(1, std::memory_order_release);

        ++frame_count;
        auto now_fps = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now_fps - t0).count();
        if (elapsed >= 1.0) {
            m_fps_measured.store(frame_count / elapsed);
            // printf("[CAP] fps=%.1f  max_gap=%.0fms  read_max=%.0fms  jitter=%d\n", frame_count / elapsed, max_gap_ms, max_read_ms, jitter_count);
            fflush(stdout);
            frame_count   = 0;
            jitter_count  = 0;
            max_gap_ms    = 0.0;
            max_read_ms   = 0.0;
            t0            = now_fps;
        }

        if (do_pace) {
            next_tp += frame_interval;
            auto now_pace = std::chrono::steady_clock::now();
            if (next_tp > now_pace)
                std::this_thread::sleep_until(next_tp);
            else
                next_tp = now_pace;  /* reset on late frame — no catch-up burst */
        }
    }
}
