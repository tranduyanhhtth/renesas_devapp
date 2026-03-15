/*******************************************************************************
 * traffic_violation/src/VideoInput.h
 ******************************************************************************/
#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>

/* Source type */
enum class SourceType {
    FILE,
    USB,
    MIPI,
    RTSP,
    CUSTOM,
};

inline SourceType sourceTypeFromString(const std::string& s) {
    if (s == "mipi")   return SourceType::MIPI;
    return SourceType::FILE;
}

class VideoInput {
public:
    explicit VideoInput(const std::string& source,
                        SourceType         source_type,
                        int  width, int height, int fps,
                        const std::string& gst_pipeline = "",
                        bool loop_file = false,
                        bool realtime_file = true);
    ~VideoInput();

    bool open();
    void close();

    /** Lấy frame mới nhất (thread-safe). Trả false nếu stream kết thúc. */
    bool getFrame(cv::Mat& out);


    uint64_t frameSeq() const { return m_frame_seq.load(std::memory_order_acquire); }

    bool   isOpen()       const { return m_open.load(); }
    double fpsMeasured()  const { return m_fps_measured.load(); }
    SourceType sourceType() const { return m_source_type; }

private:
    std::string buildGstreamerPipeline() const;
    bool mipiInit() const;
    void captureLoop();

    std::string   m_source;
    SourceType    m_source_type;
    int           m_width, m_height, m_fps;
    std::string   m_gst_pipeline;   /* custom override */
    bool          m_loop_file;
    bool          m_realtime_file;

    cv::VideoCapture  m_cap;
    cv::Mat           m_latest_frame;
    std::mutex        m_frame_mutex;
    std::thread       m_capture_thread;
    std::atomic<bool>     m_open{false};
    std::atomic<bool>     m_stop{false};
    std::atomic<double>   m_fps_measured{0.0};
    std::atomic<uint64_t> m_frame_seq{0};
    bool              m_is_gstreamer{false};
    std::string       m_active_pipeline;   /* stored at open() for GStreamer reopen-on-loop */
};
