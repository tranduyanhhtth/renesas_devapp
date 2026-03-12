/*******************************************************************************
 * traffic_violation/src/common/config.h
 * Cấu hình ứng dụng – single model, load từ config.yaml
 ******************************************************************************/
#pragma once
#include <string>
#include <vector>
#include <map>
#include <opencv2/opencv.hpp>

/* ─── Single detector config (replaces 4 separate ModelConfig) ────────────── */
struct DetectorConfig {
    std::string model_dir;
    std::string pre_dir;
    std::string label_file;
    int   input_width  = 416;
    int   input_height = 416;
    float conf_threshold = 0.50f;
    float nms_threshold  = 0.45f;
    int   drpai_freq     = 2;

    /* Model variant – controls post-processing dispatch:
     *   "yolov3" / "yolov5" : anchor-based, output [N, num_bb*(num_class+5)*grid^2]
     *   "yolov8"            : anchor-free,  output [num_class+4, num_anchors_total] */
    std::string model_type = "yolov3";

    /* YOLOv3/v5 architecture – match training config */
    int   num_class  = 7;
    int   num_bb     = 3;
    int   num_layers = 3;
    std::vector<int>   grids;    /* yolov3/v5: {13,26,52}  |  yolov8: {80,40,20} */
    std::vector<float> anchors;  /* yolov3/v5 only: 2*num_bb*num_layers values */

    /* Total anchor points = sum(g*g for g in grids). Computed automatically. */
    int totalAnchors() const {
        int t = 0; for (int g : grids) t += g * g; return t;
    }

    /* Class ID assignments (match label order used during training) */
    int class_motorbike     = 0;
    int class_car           = 1;
    int class_truck         = 2;
    int class_bus           = 3;
    int class_person        = 4;
    int class_helmet        = 5;
    int class_license_plate = 6;

    /* Helper: is class_id a vehicle to track? */
    bool isVehicle(int c) const {
        return c == class_motorbike || c == class_car
            || c == class_truck    || c == class_bus;
    }
    bool isRider(int c) const { return c == class_person; }
    /* Guard against -1 (class not present in this model, e.g. COCO has no helmet) */
    bool isHelmet(int c) const { return class_helmet        >= 0 && c == class_helmet; }
    bool isLP    (int c) const { return class_license_plate >= 0 && c == class_license_plate; }
};

/* ─── Scene / rules / output config ─────────────────────────────────────── */
struct LaneZone { float x_min, x_max; };
struct RoiF     { float x{0.f}, y{0.f}, w{0.f}, h{0.f}; };

/* Static lane divider line used for "đè vạch" detection */
struct LaneLine {
    float x_norm;      /* normalized x position of the line (0..1) */
    int   overlap_px;  /* vehicle must overlap line by >= this many px on each side */
};

struct SceneConfig {
    float stop_line_y1 = 0.60f;
    float stop_line_y2 = 0.63f;
    RoiF  traffic_light_roi;
    std::map<std::string, LaneZone> lane_zones;
    std::vector<LaneLine> lane_lines;  /* static lane divider lines */
};

struct ViolationConfig {
    bool  helmet      = true;
    bool  red_light   = true;
    bool  wrong_lane  = true;
    bool  lane_line   = true;   /* đè vạch kẻ đường */
    float cooldown_sec   = 5.0f;
    int   confirm_frames = 3;
};

struct OutputConfig {
    std::string save_dir         = "./violations";
    std::string filename_pattern = "{plate}_{datetime}_{violation}";
    bool        save_full_frame  = true;
    bool        save_json        = true;
    std::string annotated_video;
};

/* ─── AppConfig ──────────────────────────────────────────────────────────── */
struct AppConfig {
    /* Video
     * source_type: "file"  → video file  (source = /path/to/video.mp4)
     *              "usb"   → USB camera  (source = /dev/video0 or index "0")
     *              "mipi"  → MIPI camera (source = /dev/video0, runs media-ctl)
     *              "rtsp"  → RTSP stream (source = rtsp://ip:port/stream)
     *              "custom"→ use gstreamer_pipeline verbatim
     */
    std::string video_source;
    std::string video_source_type = "file";   /* file | usb | mipi | rtsp | custom */
    int   video_width  = 1920;
    int   video_height = 1080;
    int   video_fps    = 30;
    std::string gstreamer_pipeline;

    /* Single unified detector (Model 1 – traffic violations) */
    DetectorConfig detector;

    /* Optional second model (Model 2 – e.g. helmet detection).
     * Loaded on demand when user presses key '2'.  When false the
     * second detector block is absent from config.yaml. */
    DetectorConfig detector2;
    bool           detector2_enabled{false};

    /* Scene / violation / output */
    SceneConfig     scene;
    ViolationConfig violation;
    OutputConfig    output;

    /* Enable LP OCR (Tesseract) */
    bool enable_lp_ocr = true;

    static AppConfig fromFile(const std::string& path);
};
