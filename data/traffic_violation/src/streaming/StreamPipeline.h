#pragma once
// src/streaming/StreamPipeline.h
// ─────────────────────────────────────────────────────────────────────────────
//  GStreamer appsrc pipeline: pushes rendered BGR frames → H264 → RTSP
//  Designed for Renesas RZV2L / Yocto Linux
//
//  Pipeline:
//    appsrc (BGR) → videoconvert → NV12
//                 → v4l2h264enc (VPU HW) [fallback: x264enc SW]
//                 → h264parse
//                 → rtspclientsink → rtsp://127.0.0.1:8554/cam0
//
//  Usage (from R_Stream_Thread):
//    StreamPipeline sp;
//    sp.init(1280, 720, 15);          // width, height, fps
//    sp.start();
//    while (running) {
//        sp.pushFrame(bgr_mat);       // non-blocking, drops if busy
//    }
//    sp.stop();
// ─────────────────────────────────────────────────────────────────────────────
#include <string>
#include <atomic>
#include <mutex>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <opencv2/core.hpp>

class StreamPipeline {
public:
    StreamPipeline() = default;
    StreamPipeline(const std::string& rtsp_url, int fps, int width, int height, int bitrate);
    ~StreamPipeline();

    // Call once before start()
    // rtsp_url example: "rtsp://127.0.0.1:8554/cam0"
    bool init(int width, int height, int fps,
              const std::string& rtsp_url = "rtsp://127.0.0.1:8554/cam0",
              int bitrate = 1500000);

    // Start GStreamer pipeline
    bool start();

    // Push one BGR frame (cv::Mat CV_8UC3).
    // Thread-safe, non-blocking — drops frame if pipeline is busy.
    // Returns true if frame was accepted.
    bool pushFrame(const cv::Mat& bgr);

    // Graceful shutdown (sends EOS, waits for pipeline to drain)
    void stop();

    bool isRunning() const { return running_.load(); }

private:
    GstElement*        pipeline_  = nullptr;
    GstElement*        appsrc_    = nullptr;
    GstBus*            bus_       = nullptr;

    int    width_  = 1280;
    int    height_ = 720;
    int    fps_    = 15;
    int    bitrate_ = 1500000;
    std::string rtsp_url_;

    std::atomic<bool> running_{false};
    std::atomic<bool> need_data_{false};  // appsrc need-data signal
    std::atomic<guint64> frame_count_{0};

    std::mutex push_mtx_;    // serialise concurrent pushFrame() calls

    // GStreamer signal callbacks
    static void cbNeedData(GstAppSrc*, guint, gpointer);
    static void cbEnoughData(GstAppSrc*, gpointer);
    static gboolean cbBusWatch(GstBus*, GstMessage*, gpointer);
};
