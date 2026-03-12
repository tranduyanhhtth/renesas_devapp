/*******************************************************************************
 * traffic_violation/src/VideoInput.h
 *
 * Flexible video source – switch bằng source_type trong config.yaml:
 *
 *   "file"   video file (.mp4/.avi/...)
 *   "usb"    USB camera qua v4l2src
 *   "mipi"   MIPI CSI-2 camera trên RZ/V2L (chạy media-ctl trước)
 *   "rtsp"   RTSP network stream
 *   "custom" dùng nguyên gstreamer_pipeline từ config
 ******************************************************************************/
#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>

/* Source type – xác định GStreamer pipeline sẽ được build */
enum class SourceType {
    FILE,    /* video file                               */
    USB,     /* USB camera qua v4l2src                   */
    MIPI,    /* MIPI CSI-2 – media-ctl init + v4l2src    */
    RTSP,    /* RTSP network stream                      */
    CUSTOM,  /* dùng nguyên gstreamer_pipeline từ config  */
};

/* Convert string → SourceType */
inline SourceType sourceTypeFromString(const std::string& s) {
    if (s == "mipi")   return SourceType::MIPI;
    if (s == "usb")    return SourceType::USB;
    if (s == "rtsp")   return SourceType::RTSP;
    if (s == "custom") return SourceType::CUSTOM;
    return SourceType::FILE;   /* default */
}

class VideoInput {
public:
    /**
     * @param source         File path / device path / RTSP URL / camera index
     * @param source_type    SourceType enum value
     * @param width, height  Desired resolution
     * @param fps            Desired frame rate
     * @param gst_pipeline   Used verbatim when source_type==CUSTOM (or to override)
     * @param loop_file      When source_type==FILE, loop the video continuously
     */
    explicit VideoInput(const std::string& source,
                        SourceType         source_type,
                        int  width, int height, int fps,
                        const std::string& gst_pipeline = "",
                        bool loop_file = false);
    ~VideoInput();

    bool open();
    void close();

    /** Lấy frame mới nhất (thread-safe). Trả false nếu stream kết thúc. */
    bool getFrame(cv::Mat& out);

    bool   isOpen()       const { return m_open.load(); }
    double fpsMeasured()  const { return m_fps_measured; }
    SourceType sourceType() const { return m_source_type; }

private:
    /** Build GStreamer pipeline string cho từng source_type */
    std::string buildGstreamerPipeline() const;

    /** Chạy media-ctl commands để init MIPI camera trên RZ/V2L */
    bool mipiInit() const;

    void captureLoop();

    std::string   m_source;
    SourceType    m_source_type;
    int           m_width, m_height, m_fps;
    std::string   m_gst_pipeline;   /* custom override */
    bool          m_loop_file;

    cv::VideoCapture  m_cap;
    cv::Mat           m_latest_frame;
    std::mutex        m_frame_mutex;
    std::thread       m_capture_thread;
    std::atomic<bool> m_open{false};
    std::atomic<bool> m_stop{false};
    double            m_fps_measured{0.0};
};
