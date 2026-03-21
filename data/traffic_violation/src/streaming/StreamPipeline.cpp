// src/streaming/StreamPipeline.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "StreamPipeline.h"
#include <cstdio>
#include <cstring>
#include <gst/app/gstappsrc.h>

// ── Helpers ──────────────────────────────────────────────────────────────────

// Build pipeline description string.
// Tries v4l2h264enc (VPU HW) first; falls back to x264enc (SW).
// Caller can override via env STREAM_USE_SW_ENC=1 to force software.
static std::string buildPipelineDesc(int w, int h, int fps,
                                     const std::string& rtsp_url,
                                     int bitrate)
{
    const bool force_sw = (std::getenv("STREAM_USE_SW_ENC") != nullptr);

    char desc[1024];

    if (!force_sw) {
        // ── HW path: omxh264enc (RZV2L) ──────────────────────────────────
        std::snprintf(desc, sizeof(desc),
            "appsrc name=src is-live=true format=time "
            "    block=true max-bytes=10485760 ! "
            "video/x-raw,format=BGR,width=%d,height=%d,framerate=%d/1 ! "
            "videoconvert ! video/x-raw,format=NV12 ! "
            "omxh264enc target-bitrate=%d interval-intraframes=30 ! "
            "video/x-h264 ! "
            "h264parse config-interval=1 ! "
            "rtspclientsink name=sink location=\"%s\" protocols=tcp",
            w, h, fps, bitrate, rtsp_url.c_str());
    } else {
        // ── SW path: avenc_mpeg4 fallback ─────────────────────────────────
        std::snprintf(desc, sizeof(desc),
            "appsrc name=src is-live=true format=time "
            "    block=true max-bytes=10485760 ! "
            "video/x-raw,format=BGR,width=%d,height=%d,framerate=%d/1 ! "
            "videoconvert ! video/x-raw,format=I420 ! "
            "avenc_mpeg4 bitrate=%d ! "
            "rtpmp4vpay config-interval=1 ! "
            "rtspclientsink name=sink location=\"%s\" protocols=tcp",
            w, h, fps, bitrate, rtsp_url.c_str());
    }

    return std::string(desc);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

StreamPipeline::StreamPipeline(const std::string& rtsp_url, int fps,
                                int width, int height, int bitrate)
{
    init(width, height, fps, rtsp_url, bitrate);
}

bool StreamPipeline::init(int width, int height, int fps,
                           const std::string& rtsp_url,
                           int bitrate)
{
    width_    = width;
    height_   = height;
    fps_      = fps;
    bitrate_  = bitrate;
    rtsp_url_ = rtsp_url;

    // Init GStreamer once per process
    if (!gst_is_initialized()) {
        int    argc = 0;
        char** argv = nullptr;
        gst_init(&argc, &argv);
    }

    printf("[Stream] Pipeline URL  : %s\n", rtsp_url_.c_str());
    printf("[Stream] Resolution    : %dx%d @ %dfps\n", width_, height_, fps_);
    printf("[Stream] Bitrate       : %d\n", bitrate_);

    const bool force_sw = (std::getenv("STREAM_USE_SW_ENC") != nullptr);
    printf("[Stream] Encoder       : %s\n", force_sw ? "x264enc (SW)" : "v4l2h264enc (HW)");

    return true;
}

bool StreamPipeline::start()
{
    if (running_.load()) return true;

    std::string desc = buildPipelineDesc(width_, height_, fps_, rtsp_url_, bitrate_);
    printf("[Stream] GStreamer desc: %s\n", desc.c_str());

    GError* err = nullptr;
    pipeline_ = gst_parse_launch(desc.c_str(), &err);
    if (!pipeline_ || err) {
        fprintf(stderr, "[Stream][ERROR] gst_parse_launch: %s\n",
                err ? err->message : "unknown");
        if (err) g_error_free(err);
        return false;
    }

    appsrc_ = gst_bin_get_by_name(GST_BIN(pipeline_), "src");
    if (!appsrc_) {
        fprintf(stderr, "[Stream][ERROR] Cannot find appsrc element 'src'\n");
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    // Set appsrc caps explicitly
    {
        GstCaps* caps = gst_caps_new_simple("video/x-raw",
            "format",    G_TYPE_STRING,   "BGR",
            "width",     G_TYPE_INT,       width_,
            "height",    G_TYPE_INT,       height_,
            "framerate", GST_TYPE_FRACTION, fps_, 1,
            nullptr);
        gst_app_src_set_caps(GST_APP_SRC(appsrc_), caps);
        gst_caps_unref(caps);
    }

    // need-data / enough-data callbacks control back-pressure
    GstAppSrcCallbacks cbs = {};
    cbs.need_data   = cbNeedData;
    cbs.enough_data = cbEnoughData;
    gst_app_src_set_callbacks(GST_APP_SRC(appsrc_), &cbs, this, nullptr);

    // Stream type: live
    g_object_set(appsrc_,
        "stream-type", GST_APP_STREAM_TYPE_STREAM,
        "is-live",     TRUE,
        "format",      GST_FORMAT_TIME,
        nullptr);

    // Bus watch for errors
    bus_ = gst_element_get_bus(pipeline_);
    gst_bus_add_watch(bus_, cbBusWatch, this);

    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        fprintf(stderr, "[Stream][ERROR] Failed to set pipeline to PLAYING\n");
        gst_object_unref(appsrc_);  appsrc_  = nullptr;
        gst_object_unref(bus_);     bus_     = nullptr;
        gst_object_unref(pipeline_); pipeline_ = nullptr;
        return false;
    }

    running_.store(true);
    need_data_.store(true);
    printf("[Stream] Pipeline PLAYING → %s\n", rtsp_url_.c_str());
    return true;
}

void StreamPipeline::stop()
{
    if (!running_.load()) return;
    running_.store(false);

    if (appsrc_) {
        // Send EOS so encoder/muxer flush properly
        gst_app_src_end_of_stream(GST_APP_SRC(appsrc_));
    }

    if (pipeline_) {
        // Wait for EOS with 5-second timeout
        if (bus_) {
            GstMessage* msg = gst_bus_timed_pop_filtered(
                bus_, 5 * GST_SECOND,
                (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
            if (msg) gst_message_unref(msg);
        }
        gst_element_set_state(pipeline_, GST_STATE_NULL);
    }

    if (appsrc_)  { gst_object_unref(appsrc_);   appsrc_   = nullptr; }
    if (bus_)     { gst_object_unref(bus_);       bus_      = nullptr; }
    if (pipeline_){ gst_object_unref(pipeline_);  pipeline_ = nullptr; }

    printf("[Stream] Pipeline stopped\n");
}

StreamPipeline::~StreamPipeline()
{
    stop();
}

// ── Frame push ────────────────────────────────────────────────────────────────

bool StreamPipeline::pushFrame(const cv::Mat& bgr)
{
    if (!running_.load() || !appsrc_) return false;

    // Drop frame if pipeline signals back-pressure
    if (!need_data_.load()) return false;

    if (bgr.empty() || bgr.type() != CV_8UC3) return false;
    if (bgr.cols != width_ || bgr.rows != height_) return false;

    // Serialise (appsrc is not thread-safe)
    std::lock_guard<std::mutex> lk(push_mtx_);

    const gsize  size   = (gsize)bgr.total() * bgr.elemSize();
    GstBuffer*   buffer = gst_buffer_new_allocate(nullptr, size, nullptr);
    if (!buffer) return false;

    // Map and copy BGR pixels
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        gst_buffer_unref(buffer);
        return false;
    }

    if (bgr.isContinuous()) {
        std::memcpy(map.data, bgr.data, size);
    } else {
        // Non-contiguous: copy row by row
        const gsize row_bytes = (gsize)bgr.cols * bgr.elemSize();
        for (int r = 0; r < bgr.rows; ++r)
            std::memcpy(map.data + r * row_bytes, bgr.ptr(r), row_bytes);
    }
    gst_buffer_unmap(buffer, &map);

    // Timestamp
    guint64 fc = frame_count_.fetch_add(1);
    GstClockTime pts = gst_util_uint64_scale_int(fc, GST_SECOND, fps_);
    GstClockTime dur = gst_util_uint64_scale_int(1,  GST_SECOND, fps_);
    GST_BUFFER_PTS(buffer)      = pts;
    GST_BUFFER_DTS(buffer)      = pts;
    GST_BUFFER_DURATION(buffer) = dur;

    GstFlowReturn ret;
    g_signal_emit_by_name(appsrc_, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);

    return (ret == GST_FLOW_OK);
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

void StreamPipeline::cbNeedData(GstAppSrc* /*src*/, guint /*length*/, gpointer user)
{
    auto* self = static_cast<StreamPipeline*>(user);
    self->need_data_.store(true);
}

void StreamPipeline::cbEnoughData(GstAppSrc* /*src*/, gpointer user)
{
    auto* self = static_cast<StreamPipeline*>(user);
    self->need_data_.store(false);
}

gboolean StreamPipeline::cbBusWatch(GstBus* /*bus*/, GstMessage* msg, gpointer user)
{
    auto* self = static_cast<StreamPipeline*>(user);
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar*  dbg = nullptr;
        gst_message_parse_error(msg, &err, &dbg);
        fprintf(stderr, "[Stream][GST ERROR] %s (%s)\n",
                err ? err->message : "?",
                dbg ? dbg : "");
        if (err) g_error_free(err);
        g_free(dbg);
        self->running_.store(false);
        break;
    }
    case GST_MESSAGE_EOS:
        printf("[Stream][GST] EOS received\n");
        break;
    case GST_MESSAGE_WARNING: {
        GError* err = nullptr;
        gchar*  dbg = nullptr;
        gst_message_parse_warning(msg, &err, &dbg);
        fprintf(stderr, "[Stream][GST WARN] %s (%s)\n",
                err ? err->message : "?",
                dbg ? dbg : "");
        if (err) g_error_free(err);
        g_free(dbg);
        break;
    }
    default: break;
    }
    return TRUE;
}
