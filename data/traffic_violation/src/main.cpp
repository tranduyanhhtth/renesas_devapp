/* traffic_violation/src/main.cpp
 * Multi-threaded pipeline:
 *   R_Capture_Thread → g_cap → R_Inf_Thread → g_disp → R_Display_Thread
 *                                                ↑ R_Kbhit_Thread
 * Termination: sem_t terminate_req_sem (init=1); sem_trywait signals global stop.
 */
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cmath>
#include <unistd.h>
#include <climits>
#include <set>
#include <libgen.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "common/config.h"
#include "common/types.h"
#include "VideoInput.h"
#include "models/TrafficDetector.h"
#include "tracking/VehicleTracker.h"
#include "violation/ViolationEngine.h"
#include "violation/HelmetRule.h"
#include "violation/RedLightRule.h"
#include "violation/WrongLaneRule.h"
#include "violation/LaneLineRule.h"
#include "output/ViolationLogger.h"

/* OCR modules */
#include "common/tess_module/TesseractEngine.h"
#include "common/lp_text_proc_module/LpTextProc.h"
#include "common/lp_regex_module/lp_regex_function.h"
#include "common/lp_validate_module/LpValidate.h"

#ifdef WITH_DRP
#  include <sys/ioctl.h>
#  include "drp/image.h"
#  include "drp/wayland.h"
#  include "drp/define.h"
#endif

#define WAIT_TIME          (1000)    /* idle poll interval (µs) */
#define LP_OCR_INTERVAL    (10)      /* run OCR every N inference frames */
#define FPS_PRINT_INTERVAL (1.0)     /* seconds between FPS prints */

static sem_t terminate_req_sem;  /* init=1; sem_trywait signals global stop */

static pthread_t capture_thread;
static pthread_t ai_inf_thread;
static pthread_t display_thread;
static pthread_t kbhit_thread;

/* Inter-thread data: newest-frame-wins — producer overwrites, consumer reads latest */

/** Capture → Inference slot. */
struct CaptureSlot {
    cv::Mat          frame;
    std::mutex       mtx;
    std::atomic<bool> ready{false};      /* true = new frame available for inference */
    std::atomic<bool> disp_ready{false}; /* true = new frame available for display   */
};
static CaptureSlot g_cap;

/** Inference → Display slot (latest overlay/result state). */
struct DisplaySlot {
    std::vector<TrackedVehicle> vehicles;
    std::vector<detection>      dets;
    std::vector<std::string>    labels;
    bool                        red_light{false};
    double                      fps{0.0};
    float                       prep_ms{0.f};
    float                       infer_ms{0.f};
    int                         model_idx{0};
    std::mutex       mtx;
    std::atomic<bool> ready{false};   /* true once first AI result is available */
};
static DisplaySlot g_disp;

static AppConfig  g_cfg;
static constexpr bool kEnableTracking = false;
static constexpr bool kEnableViolationLogging = false;

static std::atomic<int> g_active_model{0};  /* 0=Model1, 1=Model2 */
static std::atomic<int> g_model_req{-1};    /* kbhit model-switch request, -1=none */
static uint64_t         g_drpai_base{0};
static int              g_drpai_fd{-1};     /* /dev/drpai0 kept open for app lifetime */

static VideoInput*      g_video        = nullptr;   /* → R_Capture_Thread  */
static TrafficDetector* g_detector     = nullptr;   /* → R_Inf_Thread      */
static VehicleTracker*  g_tracker      = nullptr;   /* → R_Inf_Thread      */
static ViolationEngine* g_engine       = nullptr;   /* → R_Inf_Thread      */
static ViolationLogger* g_logger       = nullptr;   /* → R_Inf_Thread      */
static cv::VideoWriter* g_vwriter      = nullptr;   /* → R_Inf_Thread      */
static LpValidator*     g_lp_validator = nullptr;   /* → R_Inf_Thread      */

static std::vector<std::pair<std::regex, std::string>> g_lp_regex;

/* For model 2 UI: plate + violation type(s) of currently violating vehicles. */
static std::vector<std::string> g_model2_violation_lines;

static const DetectorConfig& currentDisplayDetectorConfig(int model_idx)
{
    if (model_idx == 1 && g_cfg.detector2_enabled)
        return g_cfg.detector2;
    return g_cfg.detector;
}

static const char* sourceTypeName(SourceType type)
{
    switch (type) {
        case SourceType::FILE: return "FILE";
        case SourceType::MIPI: return "MIPI";
        default: return "UNKNOWN";
    }
}

/* ═══════════════════════════ Helper functions ═══════════════════════════ */

static std::string exeDir()
{
    char buf[PATH_MAX] = {};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = '\0';
    return std::string(dirname(buf));
}

static std::string resolvePath(const std::string& base, const std::string& p)
{
    if (p.empty() || p[0] == '/') return p;
    return base + "/" + p;
}

static bool in01(float v) { return v >= 0.f && v <= 1.f; }

static bool hasValidStopLine(const SceneConfig& sc)
{
    return in01(sc.stop_line_y1) && in01(sc.stop_line_y2) &&
           (sc.stop_line_y2 > sc.stop_line_y1);
}

static bool hasValidTrafficLightRoi(const SceneConfig& sc)
{
    const auto& r = sc.traffic_light_roi;
    return in01(r.x) && in01(r.y) && in01(r.w) && in01(r.h) &&
           (r.w > 0.f) && (r.h > 0.f) && (r.x + r.w <= 1.f) && (r.y + r.h <= 1.f);
}

static bool hasValidLaneZones(const SceneConfig& sc)
{
    for (const auto& key : {"motorbike", "car", "truck"}) {
        auto it = sc.lane_zones.find(key);
        if (it == sc.lane_zones.end()) continue;
        const auto& z = it->second;
        if (in01(z.x_min) && in01(z.x_max) && z.x_max > z.x_min) return true;
    }
    return false;
}

static bool hasValidLaneLines(const SceneConfig& sc)
{
    if (sc.lane_lines.empty()) return false;
    for (const auto& ll : sc.lane_lines) {
        if (in01(ll.x_norm) && ll.overlap_px > 0) return true;
    }
    return false;
}

static bool isReasonableBox(const Box& b, int fw, int fh)
{
    if (!std::isfinite(b.x) || !std::isfinite(b.y) ||
        !std::isfinite(b.w) || !std::isfinite(b.h)) return false;
    if (b.w < 2.f || b.h < 2.f) return false;
    /* Accept large boxes and rely on toSafeRect clipping when drawing.
     * Reject only obviously invalid magnitudes to avoid overflow artifacts. */
    if (b.w > fw * 4.f || b.h > fh * 4.f) return false;
    return true;
}

static void ensureWaylandRuntimeEnv()
{
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg != nullptr && xdg[0] != '\0') return;

    struct stat st;
    const char* fallback = "/run/user/0";
    if (stat(fallback, &st) == 0 && S_ISDIR(st.st_mode)) {
        setenv("XDG_RUNTIME_DIR", fallback, 0);
        printf("[Main] XDG_RUNTIME_DIR not set -> using %s\n", fallback);
    }
}

/* Populate ViolationEngine rules for the given model index.  Clears existing rules first. */
static void buildViolationRules(ViolationEngine* engine, int model_idx,
                                const AppConfig& cfg)
{
    engine->clearRules();
    if (model_idx == 0) {
        if (!cfg.scene.calibrated_for_violation) {
            printf("[Rules] Scene not calibrated -> traffic-violation rules disabled\n");
            return;
        }
        /* Model 1: traffic-violation rules */
        if (cfg.violation.red_light &&
            hasValidStopLine(cfg.scene) && hasValidTrafficLightRoi(cfg.scene))
            engine->addRule(std::make_shared<RedLightRule>(cfg.scene));
        else if (cfg.violation.red_light)
            printf("[Rules] Skip RedLightRule: stop_line/traffic_light_roi invalid\n");

        if (cfg.violation.wrong_lane && hasValidLaneZones(cfg.scene))
            engine->addRule(std::make_shared<WrongLaneRule>(cfg.scene));
        else if (cfg.violation.wrong_lane)
            printf("[Rules] Skip WrongLaneRule: lane_zones invalid\n");

        if (cfg.violation.lane_line && hasValidLaneLines(cfg.scene))
            engine->addRule(std::make_shared<LaneLineRule>(cfg.scene));
        else if (cfg.violation.lane_line)
            printf("[Rules] Skip LaneLineRule: lane_lines invalid/empty\n");
    } else {
        /* Model 2: helmet-detection rules */
        if (cfg.violation.helmet)
            engine->addRule(std::make_shared<HelmetRule>());
    }
}

static TrafficLightState detectTrafficLight(const cv::Mat& frame,
                                             const SceneConfig& scene)
{
    TrafficLightState st;
    if (frame.empty()) return st;
    const auto& r = scene.traffic_light_roi;
    cv::Rect roi(
        (int)(r.x * frame.cols), (int)(r.y * frame.rows),
        (int)(r.w * frame.cols), (int)(r.h * frame.rows));
    roi &= cv::Rect(0, 0, frame.cols, frame.rows);
    if (roi.area() < 4) return st;
    cv::Mat hsv;
    cv::cvtColor(frame(roi), hsv, cv::COLOR_BGR2HSV);
    cv::Mat m1, m2, red_mask;
    cv::inRange(hsv, cv::Scalar(  0, 120, 70), cv::Scalar( 10, 255, 255), m1);
    cv::inRange(hsv, cv::Scalar(160, 120, 70), cv::Scalar(180, 255, 255), m2);
    cv::bitwise_or(m1, m2, red_mask);
    st.red = (cv::countNonZero(red_mask) > roi.area() * 0.05f);
    return st;
}

/* ──────────────────────────── LP OCR helper ──────────────────────────── */
/* Uses global g_lp_regex / g_lp_validator – called only from R_Inf_Thread */
static std::string runLpOcr(const cv::Mat& frame, const cv::Rect& lp_rect)
{
    cv::Rect safe = lp_rect & cv::Rect(0, 0, frame.cols, frame.rows);
    if (safe.area() < 100) return "";
    cv::Mat gray;
    cv::cvtColor(frame(safe).clone(), gray, cv::COLOR_BGR2GRAY);
    if (gray.rows < LP_MIN_CROP_HEIGHT) {
        int nw = (int)(gray.cols * (double)LP_MIN_CROP_HEIGHT / gray.rows);
        cv::resize(gray, gray, cv::Size(nw, LP_MIN_CROP_HEIGHT), 0, 0, cv::INTER_CUBIC);
    }
    auto& eng = TesseractEngine::getInstance().getEngine();
    eng.SetImage(gray.data, gray.cols, gray.rows, 1, (int)gray.step);
    eng.SetSourceResolution(LP_TESS_RESOLUTION);
    char* raw = eng.GetUTF8Text();
    std::string trimmed = lp_trim_normalize(raw);
    delete[] raw;
    lp_struct lp = match_vn_plate(g_lp_regex, trimmed);
    if (!lp.matched || !g_lp_validator->is_valid_plate(lp)) return "";
    return g_lp_validator->format_plate(lp);
}

static std::vector<std::string> collectModel2ViolationLines(
    const cv::Mat& frame,
    const std::vector<detection>& dets,
    const DetectorConfig& det_cfg,
    bool red_light,
    bool run_ocr_this_frame)
{
    std::vector<std::string> lines;
    if (frame.empty()) return lines;

    const int fw = frame.cols;
    const int fh = frame.rows;
    std::set<std::string> dedup;

    for (const auto& veh : dets) {
        if (veh.prob == 0.f || !det_cfg.isVehicle(veh.c)) continue;
        if (!isReasonableBox(veh.bbox, fw, fh)) continue;

        bool lane_line_violation = false;
        bool red_light_violation = false;

        if (g_cfg.violation.lane_line && hasValidLaneLines(g_cfg.scene)) {
            float x_left  = veh.bbox.x - veh.bbox.w / 2.f;
            float x_right = veh.bbox.x + veh.bbox.w / 2.f;
            for (const auto& ll : g_cfg.scene.lane_lines) {
                float x_line = ll.x_norm * (float)fw;
                float left_overlap  = x_line - x_left;
                float right_overlap = x_right - x_line;
                if (left_overlap >= (float)ll.overlap_px &&
                    right_overlap >= (float)ll.overlap_px)
                {
                    lane_line_violation = true;
                    break;
                }
            }
        }

        if (g_cfg.violation.red_light && red_light && hasValidStopLine(g_cfg.scene)) {
            float bottom_y = veh.bbox.y + veh.bbox.h / 2.f;
            float y2_px = g_cfg.scene.stop_line_y2 * (float)fh;
            red_light_violation = (bottom_y <= y2_px);
        }

        if (!lane_line_violation && !red_light_violation) continue;

        std::string plate = "LP?";
        if (g_cfg.enable_lp_ocr && run_ocr_this_frame && det_cfg.isLP(det_cfg.class_license_plate)) {
            float best_inter = 0.f;
            cv::Rect best_lp;
            for (const auto& lp : dets) {
                if (lp.prob == 0.f || !det_cfg.isLP(lp.c)) continue;
                if (!isReasonableBox(lp.bbox, fw, fh)) continue;
                float inter = lp.bbox.intersectArea(veh.bbox);
                if (inter < lp.bbox.w * lp.bbox.h * 0.2f) continue;
                cv::Rect lp_rect = lp.bbox.toSafeRect(fw, fh);
                if (lp_rect.area() < 100) continue;
                if (inter > best_inter) {
                    best_inter = inter;
                    best_lp = lp_rect;
                }
            }
            if (best_lp.area() > 0) {
                std::string ocr = runLpOcr(frame, best_lp);
                if (!ocr.empty()) plate = ocr;
            }
        }

        std::string reason;
        if (red_light_violation) reason += "RED_LIGHT";
        if (lane_line_violation) {
            if (!reason.empty()) reason += "+";
            reason += "LANE_LINE";
        }
        std::string line = plate + " | " + reason;
        if (dedup.insert(line).second) lines.push_back(line);
    }
    return lines;
}

/* ──────────────────────────── Draw overlay ───────────────────────────── */
static void drawOverlay(cv::Mat& frame,
    const std::vector<TrackedVehicle>& vehicles,
    const std::vector<detection>&      dets,
    const std::vector<std::string>&    labels,
    int model_idx,
    bool red_light, double fps)
{
    const int fw = frame.cols, fh = frame.rows;
    const auto& det_cfg = currentDisplayDetectorConfig(model_idx);
    const int raw_box_thickness = (model_idx == 1) ? 2 : 1;
    /* Raw detections (thin box) */
    for (const auto& d : dets) {
        if (d.prob == 0.f || !d.bbox.isValid()) continue;

        bool draw_det = false;
        if (model_idx == 1) {
            /* Model 2: draw all detections from post-process so UI always matches Detected count. */
            draw_det = true;
        } else {
            draw_det = (det_cfg.isVehicle(d.c) || det_cfg.isHelmet(d.c) ||
                        det_cfg.isNoHelmet(d.c) || det_cfg.isLP(d.c));
        }
        if (!draw_det) continue;

        cv::Rect r = d.bbox.toSafeRect(fw, fh);
        if (r.area() <= 0) continue;

        cv::Scalar col(180, 180, 180);
        if      (det_cfg.isVehicle(d.c)) col = cv::Scalar(0, 220, 0);
        else if (det_cfg.isHelmet(d.c))  col = cv::Scalar(255, 200, 0);
        else if (det_cfg.isNoHelmet(d.c)) col = cv::Scalar(0, 0, 255);
        else if (det_cfg.isLP(d.c))      col = cv::Scalar(0, 200, 255);
        cv::rectangle(frame, r, col, raw_box_thickness);
        if (d.c >= 0 && d.c < (int)labels.size()) {
            char txt[96];
            std::snprintf(txt, sizeof(txt), "%s %.2f", labels[d.c].c_str(), d.prob);
            cv::putText(frame, txt, {r.x, std::max(r.y - 3, 12)},
                        cv::FONT_HERSHEY_SIMPLEX, 0.45, col, raw_box_thickness);
        }
    }
    /* Tracked vehicles (thick box + label) */
    for (const auto& v : vehicles) {
        cv::Rect r = v.bbox.toSafeRect(fw, fh);
        if (r.area() <= 0) continue;
        cv::Scalar color = (v.type == VehicleType::MOTORBIKE)
                           ? cv::Scalar(0, 255, 0) : cv::Scalar(255, 200, 0);
        cv::rectangle(frame, r, color, 2);
        std::string lbl = "T" + std::to_string(v.track_id);
        if (!v.plate.empty())                                           lbl += " " + v.plate;
        if (model_idx == 1 && v.type == VehicleType::MOTORBIKE && !v.has_helmet)
            lbl += " [NO HELMET]";  /* only Model 2 has helmet detection */
        cv::putText(frame, lbl, {r.x, r.y - 5},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1);
    }
    /* Traffic-light indicator */
    cv::Scalar tl_col = red_light ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 200, 0);
    // cv::circle(frame, {frame.cols - 30, 30}, 15, tl_col, cv::FILLED);
    // cv::putText(frame, red_light ? "RED" : "GO ",
    //             {frame.cols - 55, 70},
    //             cv::FONT_HERSHEY_SIMPLEX, 0.6, tl_col, 2);
    // /* FPS */
    // cv::putText(frame, "FPS:" + std::to_string((int)fps),
    //             {10, 30}, cv::FONT_HERSHEY_SIMPLEX, 0.8,
    //             cv::Scalar(0, 255, 255), 2);
}

/* Pre-computed display geometry — computed once, reused every frame.
 * All buffers are allocated BEFORE the display loop to eliminate
 * per-frame heap alloc/free (160 MB/s @ 20fps) which caused jitter. */
struct DisplayGeom {
    int panel_w, video_w;
    int rw, rh, ox, oy;  /* resize target & letterbox offsets */
};
static DisplayGeom computeDisplayGeom(int src_w, int src_h) {
    DisplayGeom g;
    g.panel_w = std::min(320, IMAGE_OUTPUT_WIDTH / 3);
    g.video_w = IMAGE_OUTPUT_WIDTH - g.panel_w;
    double sx = (double)g.video_w / src_w;
    double sy = (double)IMAGE_OUTPUT_HEIGHT / src_h;
    double s  = std::min(sx, sy);
    g.rw = std::max(1, (int)(src_w * s));
    g.rh = std::max(1, (int)(src_h * s));
    g.ox = (g.video_w - g.rw) / 2;
    g.oy = (IMAGE_OUTPUT_HEIGHT - g.rh) / 2;
    return g;
}

/* renderToCanvas — writes one display frame into caller-supplied pre-allocated buffers.
 *
 * Design (camera_service_ai RK3568 in-place overlay pattern):
 *   canvas  pre-allocated 1280×720 BGR  — reused every frame, NO malloc/memset
 *   bgra    pre-allocated 1280×720 BGRA — reused every frame, NO malloc/memset
 *   src     camera frame taken BY VALUE so caller can std::move(tmp) in → O(1)
 *
 * Per-frame memory ops eliminated vs old composeDisplayFrame:
 *   • cv::Mat canvas(1280×720, Scalar(20,20,20)) — was 2.76 MB memset/frame
 *   • cv::Mat bgra from cvtColor                 — was 3.58 MB malloc/frame
 *   • passing src by value at call site           — was 1.23 MB memcpy/frame
 * Total saved: ~7.6 MB × 25 fps = ~190 MB/s of cache-thrashing eliminated.
 *
 * Only the video ROI and right panel are repainted each call; the dark-gray
 * letterbox bars (if any) are filled ONCE at startup and left unchanged.    */
static void renderToCanvas(
    cv::Mat&                            canvas,   /* 1280×720 BGR  pre-alloc IN/OUT */
    cv::Mat&                            bgra,     /* 1280×720 BGRA pre-alloc OUT    */
    cv::Mat&                            src,      /* camera frame — mutable in-place (no copy) */
    const DisplayGeom&                  geom,
    const std::vector<TrackedVehicle>&  vehicles,
    const std::vector<detection>&       dets,
    const std::vector<std::string>&     labels,
    const std::vector<std::string>&     model2_violation_lines,
    int model_idx, bool red_light,
    double infer_fps, double capture_fps,
    float prep_ms, float infer_ms)
{
    /* 1. Draw AI overlay on src in-place (src is our local copy, safe to modify) */
    drawOverlay(src, vehicles, dets, labels, model_idx, red_light, infer_fps);

    /* 2. Copy/resize into canvas video ROI — no intermediate allocation.
     * When capture resolution == display resolution (common: 960×540 → 960×540),
     * copyTo skips the full resize interpolation pipeline (~4 ms saved).      */
    {
        auto roi = canvas(cv::Rect(geom.ox, geom.oy, geom.rw, geom.rh));
        if (src.cols == geom.rw && src.rows == geom.rh)
            src.copyTo(roi);
        else
            cv::resize(src, roi, cv::Size(geom.rw, geom.rh), 0, 0, cv::INTER_LINEAR);
    }

    /* 3. Repaint letterbox bars (small regions, only if source is not 4:3) */
    if (geom.oy > 0) {
        canvas(cv::Rect(0, 0, geom.video_w, geom.oy))
              .setTo(cv::Scalar(20, 20, 20));
        int bot_y = geom.oy + geom.rh;
        int bot_h = IMAGE_OUTPUT_HEIGHT - bot_y;
        if (bot_h > 0)
            canvas(cv::Rect(0, bot_y, geom.video_w, bot_h))
                  .setTo(cv::Scalar(20, 20, 20));
    }

    /* 4. Right-panel background — setTo on 320×720 (0.44MB, but no malloc) */
    canvas(cv::Rect(geom.video_w, 0, geom.panel_w, IMAGE_OUTPUT_HEIGHT))
          .setTo(cv::Scalar(35, 35, 35));
    cv::line(canvas, cv::Point(geom.video_w, 0),
             cv::Point(geom.video_w, IMAGE_OUTPUT_HEIGHT),
             cv::Scalar(90, 90, 90), 2);

    /* 5. Right-panel text */
    int y = 36;
    const int x  = geom.video_w + 16;
    const int lh = 28;
    auto put = [&](const std::string& s, cv::Scalar c,
                   double scale = 0.62, int thick = 1) {
        cv::putText(canvas, s, {x, y}, cv::FONT_HERSHEY_SIMPLEX, scale, c, thick);
        y += lh;
    };
    put("TRAFFIC VIOLATION", cv::Scalar(0, 220, 255), 0.70, 2);
    y += 6;
    put(std::string("Source: ") + sourceTypeName(sourceTypeFromString(g_cfg.video_source_type)),
        cv::Scalar(230,230,230));
    put("Model : " + std::to_string(model_idx + 1), cv::Scalar(230,230,230));
    put("Cfg   : " + std::to_string(g_cfg.video_width) + "x" +
        std::to_string(g_cfg.video_height) + " @" +
        std::to_string(g_cfg.video_fps) + "fps", cv::Scalar(210,210,210), 0.54);
    put("InfLim: " + std::to_string(g_cfg.inference_fps), cv::Scalar(210,210,210), 0.54);
    put("Cap FPS: " + cv::format("%.1f", capture_fps), cv::Scalar(180,255,180));
    put("Inf FPS: " + cv::format("%.1f", infer_fps),   cv::Scalar(0,255,255));
    put("Prep ms: " + cv::format("%.1f", prep_ms),     cv::Scalar(200,200,255));
    put("Inf ms : " + cv::format("%.1f", infer_ms),    cv::Scalar(200,200,255));
    put(std::string("Light  : ") + (red_light ? "RED" : "GO"),
        red_light ? cv::Scalar(0,0,255) : cv::Scalar(0,220,0));
    put(std::string("Track : ") + (kEnableTracking ? "ON" : "OFF"), cv::Scalar(230,230,230));
    y += 10;
    put("Detected:", cv::Scalar(255, 220, 0), 0.62, 2);
    std::map<std::string, int> counts;
    for (const auto& d : dets) {
        if (d.prob == 0.f) continue;
        std::string name = (d.c >= 0 && d.c < (int)labels.size())
            ? labels[d.c] : ("cls_" + std::to_string(d.c));
        counts[name]++;
    }
    if (counts.empty()) {
        put("(none)", cv::Scalar(170,170,170));
    } else {
        for (const auto& kv : counts) {
            put("- " + kv.first + ": " + std::to_string(kv.second),
                cv::Scalar(220,220,220), 0.54);
            if (y > IMAGE_OUTPUT_HEIGHT - 80) break;
        }
    }
    if (model_idx == 1) {
        y += 10;
        put("Violation LP (M2):", cv::Scalar(255, 220, 0), 0.62, 2);
        if (model2_violation_lines.empty()) {
            put("(none)", cv::Scalar(170,170,170), 0.54);
        } else {
            for (const auto& line : model2_violation_lines) {
                put("- " + line, cv::Scalar(220,220,220), 0.52);
                if (y > IMAGE_OUTPUT_HEIGHT - 110) break;
            }
        }
    }
    y = std::max(y + 12, IMAGE_OUTPUT_HEIGHT - 92);
    put("Keys:", cv::Scalar(255, 220, 0), 0.62, 2);
    put("1: Model 1",  cv::Scalar(220,220,220), 0.54);
    put("2: Model 2",  cv::Scalar(220,220,220), 0.54);
    put("other: Exit", cv::Scalar(220,220,220), 0.54);

    /* BGR→BGRA for wayland.commit() */
    cv::cvtColor(canvas, bgra, cv::COLOR_BGR2BGRA);
}


/* ── Thread functions ── */

/* Reads latest frame from VideoInput; writes to g_cap (newest-frame-wins). */
void* R_Capture_Thread(void* /*threadid*/)
{
    int32_t sem_check = 0;
    int8_t  ret       = 0;
    uint64_t last_published_seq = UINT64_MAX; /* force first publish */

    printf("[Capture] Thread started\n");

    if (!g_video->open()) {
        fprintf(stderr, "[Capture][ERROR] Cannot open video source\n");
        goto err;
    }

    while (1)
    {
        /* Termination check */
        errno = 0;
        ret   = sem_getvalue(&terminate_req_sem, &sem_check);
        if (0 != ret) {
            fprintf(stderr, "[Capture][ERROR] sem_getvalue errno=%d\n", errno);
            goto err;
        }
        if (1 != sem_check) goto capture_end;

        {
            uint64_t cur_seq = g_video->frameSeq();
            if (cur_seq == last_published_seq) {
                usleep(WAIT_TIME);
                if (!g_video->isOpen()) goto capture_end;
                continue;
            }

            cv::Mat frame;
            if (!g_video->getFrame(frame) || frame.empty()) {
                if (!g_video->isOpen()) goto capture_end;
                usleep(WAIT_TIME);
                continue;
            }

            last_published_seq = cur_seq;

            {
                std::lock_guard<std::mutex> lk(g_cap.mtx);
                g_cap.frame = std::move(frame);
            }
            g_cap.ready.store(true);
            g_cap.disp_ready.store(true);
        }
    }

err:
    sem_trywait(&terminate_req_sem);
    goto capture_end;

capture_end:
    g_video->close();
    g_cap.ready.store(true);   /* unblock inference thread */
    printf("[Capture] Thread terminated\n");
    pthread_exit(NULL);
}

/* AI pipeline: capture frame → DRP-AI detect → publish display metadata */
void* R_Inf_Thread(void* /*threadid*/)
{
    int32_t sem_check = 0;
    int8_t  ret       = 0;
    int     frame_idx = 0;

    auto   t_fps    = std::chrono::steady_clock::now();
    double loop_fps = 0.0;
    const int infer_fps_limit = g_cfg.inference_fps;
    const auto infer_interval = std::chrono::microseconds(
        (infer_fps_limit > 0) ? (1000000 / infer_fps_limit) : 0);
    auto next_infer_tp = std::chrono::steady_clock::now();

    printf("[Inference] Thread started\n");

    while (1)
    {
        errno = 0;
        ret   = sem_getvalue(&terminate_req_sem, &sem_check);
        if (0 != ret) {
            fprintf(stderr, "[Inference][ERROR] sem_getvalue errno=%d\n", errno);
            goto err;
        }
        if (1 != sem_check) goto inf_end;

        if (g_cfg.inference_fps < 0) { usleep(50000); continue; }

        /* Model-switch requested by kbhit thread? */
        {
            int req = g_model_req.exchange(-1);
            if (req >= 0 && req != g_active_model.load()) {
                printf("[Inference] Switching to Model %d...\n", req + 1);
                const DetectorConfig& dcfg =
                    (req == 0) ? g_cfg.detector : g_cfg.detector2;
                /* Tear down current detector; tracking/logger are optional. */
                delete g_detector;  g_detector = nullptr;
                delete g_tracker;   g_tracker  = nullptr;
                g_detector = new TrafficDetector(dcfg);
                if (kEnableTracking)
                    g_tracker = new VehicleTracker(dcfg, 6, 0.35f);
                if (g_engine)
                    buildViolationRules(g_engine, req, g_cfg);
                if (!g_detector->load(g_drpai_base + DRPAI_MEM_OFFSET)) {
                    fprintf(stderr, "[Inference][ERROR] Model %d load failed\n", req + 1);
                    goto err;
                }
                g_active_model.store(req);
                printf("[Inference] Now running Model %d (%s)\n",
                       req + 1, dcfg.model_dir.c_str());
            }
        }
        /* Rate limiter */
        if (infer_fps_limit > 0) {
            auto now = std::chrono::steady_clock::now();
            if (now < next_infer_tp)
                std::this_thread::sleep_until(next_infer_tp);
        }

        if (!g_cap.ready.load()) { usleep(WAIT_TIME); continue; }

        cv::Mat frame;
        {
            std::lock_guard<std::mutex> lk(g_cap.mtx);
            if (g_cap.frame.empty()) { g_cap.ready.store(false); continue; }
            frame = std::move(g_cap.frame);
            g_cap.ready.store(false);
        }
        if (infer_fps_limit > 0)
            next_infer_tp = std::chrono::steady_clock::now() + infer_interval;

        ++frame_idx;

        const DetectorConfig& active_det_cfg =
            currentDisplayDetectorConfig(g_active_model.load());

        /* 1. DRP-AI inference */
        std::vector<detection> dets;
        try {
            dets = g_detector->detect(frame);
        } catch (const std::exception& e) {
            fprintf(stderr, "[Inference][WARN] detect() threw: %s — skipping frame\n", e.what());
            continue;
        } catch (...) {
            fprintf(stderr, "[Inference][WARN] detect() threw unknown exception — skipping frame\n");
            continue;
        }

        /* 2. Traffic-light state */
        TrafficLightState light = detectTrafficLight(frame, g_cfg.scene);

        std::vector<TrackedVehicle> disp_vehicles;
        if (kEnableTracking && g_tracker) {
            disp_vehicles = g_tracker->update(dets, frame.cols, frame.rows);
        }

        std::vector<std::string> model2_lines;
        if (g_active_model.load() == 1) {
            bool run_ocr_this_frame = (frame_idx % LP_OCR_INTERVAL == 0);
            model2_lines = collectModel2ViolationLines(frame, dets, active_det_cfg,
                                                       light.red, run_ocr_this_frame);
        }

        /* 3. Publish to display thread */
        std::vector<std::string> labels_snapshot = g_detector->labels();
        {
            std::lock_guard<std::mutex> lk(g_disp.mtx);
            g_disp.vehicles  = disp_vehicles;
            g_disp.dets      = dets;
            g_disp.labels    = labels_snapshot;
            g_model2_violation_lines = model2_lines;
            g_disp.red_light = light.red;
            g_disp.fps       = loop_fps;
            g_disp.prep_ms   = g_detector->lastPrepMs();
            g_disp.infer_ms  = g_detector->lastInferMs();
            g_disp.model_idx = g_active_model.load();
        }
        g_disp.ready.store(true);

        /* 4. Optional video-file write */
        if (g_vwriter && g_vwriter->isOpened()) {
            cv::Mat annotated = frame.clone();
            drawOverlay(annotated, disp_vehicles, dets, labels_snapshot,
                        g_active_model.load(), light.red, loop_fps);
            g_vwriter->write(annotated);
        }

        /* 5. FPS accounting */
        {
            auto now = std::chrono::steady_clock::now();
            double sec = std::chrono::duration<double>(now - t_fps).count();
            if (sec >= FPS_PRINT_INTERVAL) {
                loop_fps  = frame_idx / sec;
                frame_idx = 0;
                t_fps     = now;
                printf("[Inference] FPS=%.1f | prep=%.1fms  inf=%.1fms\n",
                       loop_fps,
                       g_detector->lastPrepMs(),
                       g_detector->lastInferMs());
            }
        }

    }

err:
    sem_trywait(&terminate_req_sem);
    goto inf_end;

inf_end:
    if (g_logger && kEnableViolationLogging) g_logger->flush();
    printf("[Inference] Thread terminated\n");
    pthread_exit(NULL);
}

/* Renders latest frame + AI overlay to Wayland (WITH_DRP) or cv::imshow. */
void* R_Display_Thread(void* /*threadid*/)
{
    int32_t sem_check = 0;
    int8_t  ret       = 0;

    /* Pre-compute display geometry (integers only, no Mat allocation) */
    const DisplayGeom geom = computeDisplayGeom(g_cfg.video_width, g_cfg.video_height);

    /* Declare empty (unallocated) — actual create() happens after Wayland init */
    cv::Mat canvas;
    cv::Mat bgra;

    printf("[Display] Thread started\n");

#ifdef WITH_DRP
    Wayland wayland;
    if (wayland.init(IMAGE_OUTPUT_WIDTH, IMAGE_OUTPUT_HEIGHT,
                     IMAGE_OUTPUT_CHANNEL_BGRA) != 0)
    {
        fprintf(stderr, "[Display][ERROR] Wayland init failed\n");
        goto err;
    }
    printf("[Display] Wayland %dx%d ready\n",
           IMAGE_OUTPUT_WIDTH, IMAGE_OUTPUT_HEIGHT);
#endif

    canvas.create(IMAGE_OUTPUT_HEIGHT, IMAGE_OUTPUT_WIDTH, CV_8UC3);
    bgra.create  (IMAGE_OUTPUT_HEIGHT, IMAGE_OUTPUT_WIDTH, CV_8UC4);
    canvas.setTo(cv::Scalar(20, 20, 20));
    cv::cvtColor(canvas, bgra, cv::COLOR_BGR2BGRA);

    {
    /* Vsync-driven loop: wayland.commit() blocks on eglSwapBuffers (~60 Hz).  Re-render
     * only when VideoInput produces a new frame seq; otherwise re-commit existing bgra. */
    uint64_t last_disp_seq = UINT64_MAX;
    cv::Mat tmp;

    using Clock = std::chrono::steady_clock;
    using Ms    = std::chrono::duration<double, std::milli>;
    auto   t_last_disp_frame = Clock::now();
    double disp_max_gap_ms   = 0.0;
    int    disp_jitter_count = 0;
    int    disp_frame_count  = 0;
    double commit_max_ms     = 0.0;
    auto   t_stats           = Clock::now();

    while (1)
    {
        errno = 0;
        ret   = sem_getvalue(&terminate_req_sem, &sem_check);
        if (0 != ret) {
            fprintf(stderr, "[Display][ERROR] sem_getvalue errno=%d\n", errno);
            goto err;
        }
        if (1 != sem_check) goto disp_end;

        {
            uint64_t cur_seq = g_video ? g_video->frameSeq() : 0;
            if (cur_seq != last_disp_seq) {
                auto   t_now_disp = Clock::now();
                double gap_ms     = Ms(t_now_disp - t_last_disp_frame).count();
                t_last_disp_frame = t_now_disp;
                if (gap_ms > disp_max_gap_ms) disp_max_gap_ms = gap_ms;
                if (gap_ms > 70.0) {   /* > 1.5× nominal 40ms + 1 vsync */
                    // fprintf(stderr, "[DISP][JITTER] frame_gap=%.0fms\n", gap_ms);
                    ++disp_jitter_count;
                }
                ++disp_frame_count;

                if (g_video && g_video->getFrame(tmp) && !tmp.empty()) {
                    std::vector<TrackedVehicle> vehicles;
                    std::vector<detection> dets;
                    std::vector<std::string> labels;
                    std::vector<std::string> model2_lines;
                    bool red_light = false;
                    double fps = 0.0;
                    float prep_ms = 0.f, infer_ms = 0.f;
                    int model_idx = 0;
                    if (g_disp.ready.load()) {
                        std::lock_guard<std::mutex> lk(g_disp.mtx);
                        vehicles  = g_disp.vehicles;
                        dets      = g_disp.dets;
                        labels    = g_disp.labels;
                        model2_lines = g_model2_violation_lines;
                        red_light = g_disp.red_light;
                        fps       = g_disp.fps;
                        prep_ms   = g_disp.prep_ms;
                        infer_ms  = g_disp.infer_ms;
                        model_idx = g_disp.model_idx;
                    }
                    renderToCanvas(canvas, bgra, tmp, geom,
                                   vehicles, dets, labels, model2_lines,
                                   model_idx, red_light, fps,
                                   g_video ? g_video->fpsMeasured() : 0.0,
                                   prep_ms, infer_ms);
                    last_disp_seq = cur_seq;
                }
            }
        }

#ifdef WITH_DRP
        {
            auto   t_c0     = Clock::now();
            wayland.commit(bgra.data, NULL);
            double c_ms     = Ms(Clock::now() - t_c0).count();
            if (c_ms > commit_max_ms) commit_max_ms = c_ms;
            if (c_ms > 40.0) {
                // fprintf(stderr, "[DISP][VSYNC_SLOW] commit=%.0fms\n", c_ms);
            }
        }
        {
            double secs = Ms(Clock::now() - t_stats).count() / 1000.0;
            if (secs >= 1.0) {
                // printf("[DISP] new_frames/s=%d  max_gap=%.0fms  jitter=%d  commit_max=%.0fms\n",
                //        disp_frame_count, disp_max_gap_ms, disp_jitter_count, commit_max_ms);
                fflush(stdout);
                disp_frame_count  = 0;
                disp_max_gap_ms   = 0.0;
                disp_jitter_count = 0;
                commit_max_ms     = 0.0;
                t_stats           = Clock::now();
            }
        }
#else
        cv::imshow("Traffic Violation - ESC to quit", canvas);
        if (cv::waitKey(16) == 27) { printf("[Display] ESC pressed\n"); goto err; }
#endif
    }
    } /* end vsync-driven display block */

err:
    sem_trywait(&terminate_req_sem);
    goto disp_end;

disp_end:
#ifdef WITH_DRP
    wayland.exit();
#else
    cv::destroyAllWindows();
#endif
    printf("[Display] Thread terminated\n");
    pthread_exit(NULL);
}

/* Non-blocking key detection: '1'/'2' for model switch, other → stop */
void* R_Kbhit_Thread(void* /*threadid*/)
{
    int32_t sem_check = 0;
    int8_t  ret       = 0;
    int32_t c         = 0;

    printf("[KbHit] Thread started\n");
    printf("==============================================\n");
    printf("  Keys:\n");
    printf("    '1'   → switch to Model 1 (traffic violations)\n");
    printf("    '2'   → switch to Model 2 (helmet detection)\n");
    printf("    other → stop application\n");
    printf("==============================================\n");

    errno = 0;
    ret   = fcntl(0, F_SETFL, O_NONBLOCK);
    if (-1 == ret) {
        fprintf(stderr, "[KbHit][ERROR] fcntl errno=%d\n", errno);
        goto err;
    }

    while (1)
    {
        errno = 0;
        ret   = sem_getvalue(&terminate_req_sem, &sem_check);
        if (0 != ret) {
            fprintf(stderr, "[KbHit][ERROR] sem_getvalue errno=%d\n", errno);
            goto err;
        }
        if (1 != sem_check) goto key_end;

        c = getchar();
        if (EOF != c) {
            if (c == '1') {
                if (g_active_model.load() != 0) {
                    g_model_req.store(0);
                    printf("[KbHit] Model 1 requested (traffic violations)\n");
                } else {
                    printf("[KbHit] Already on Model 1\n");
                }
            } else if (c == '2') {
                if (!g_cfg.detector2_enabled) {
                    printf("[KbHit] Model 2 not configured in config.yaml\n");
                } else if (g_active_model.load() != 1) {
                    g_model_req.store(1);
                    printf("[KbHit] Model 2 requested (helmet detection)\n");
                } else {
                    printf("[KbHit] Already on Model 2\n");
                }
            } else if (c == '\n' || c == '\r') {
                /* bare newline from piped input – ignore */
            } else {
                printf("[KbHit] Key detected – stopping pipeline.\n");
                goto err;
            }
        }
        usleep(WAIT_TIME);
    }

err:
    sem_trywait(&terminate_req_sem);
    goto key_end;

key_end:
    printf("[KbHit] Thread terminated\n");
    pthread_exit(NULL);
}

int main(int argc, char* argv[])
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    ensureWaylandRuntimeEnv();

    /* Demote OMX decoders — they share mmngr CMA with DRP-AI and conflict */
    setenv("GST_PLUGIN_FEATURE_RANK",
           "omxh264dec:0,omxh265dec:0,omxvp8dec:0,omxvp9dec:0,omxav1dec:0",
           1 /* overwrite */);

    int8_t  ret_main       = 0;
    int32_t sem_create     = -1;
    int32_t create_kbhit   = -1;
    int32_t create_capture = -1;
    int32_t create_inf     = -1;
    int32_t create_display = -1;

    /* 1. Load config */
    const std::string base = exeDir();
    std::string config_path = (argc > 1)
                              ? std::string(argv[1])
                              : resolvePath(base, "config.yaml");

    {
        struct stat _st;
        if (argc == 1 && stat(config_path.c_str(), &_st) != 0) {
            config_path = "config.yaml";
            fprintf(stderr, "[Main] config.yaml not found next to binary – trying cwd\n");
        }
    }

    printf("[Main] Exe dir  : %s\n", base.c_str());
    printf("[Main] Config   : %s\n", config_path.c_str());
    fflush(stdout);

    g_cfg = AppConfig::fromFile(config_path);
    g_cfg.detector.model_dir  = resolvePath(base, g_cfg.detector.model_dir);
    g_cfg.detector.pre_dir    = resolvePath(base, g_cfg.detector.pre_dir);
    g_cfg.detector.label_file = resolvePath(base, g_cfg.detector.label_file);
    g_cfg.output.save_dir     = resolvePath(base, g_cfg.output.save_dir);
    if (g_cfg.detector2_enabled) {
        g_cfg.detector2.model_dir  = resolvePath(base, g_cfg.detector2.model_dir);
        g_cfg.detector2.pre_dir    = resolvePath(base, g_cfg.detector2.pre_dir);
        g_cfg.detector2.label_file = resolvePath(base, g_cfg.detector2.label_file);
        printf("[Main] Model 2  : %s\n", g_cfg.detector2.model_dir.c_str());
    }

    printf("[Main] Model    : %s\n", g_cfg.detector.model_dir.c_str());
    printf("[Main] Input    : %dx%d  fps=%d\n",
           g_cfg.video_width, g_cfg.video_height, g_cfg.video_fps);

    /* 2. DRP-AI base address */
    uint64_t drpai_base = 0;
#ifndef DRPAI_MEM_OFFSET
#   define DRPAI_MEM_OFFSET  (0x38E0000UL)  /* PreRuntime + TVM model offset */
#endif

    if (g_cfg.detector.model_dir.empty()) {
        fprintf(stderr, "[Main][ERROR] model_dir empty — config.yaml not loaded.\n");
        ret_main = -1;
        goto end_main;
    }

#ifdef WITH_DRP
    g_drpai_fd = open("/dev/drpai0", O_RDWR);
    if (g_drpai_fd < 0) {
        fprintf(stderr, "[Main][ERROR] Cannot open /dev/drpai0: %s\n", strerror(errno));
        ret_main = -1;
        goto end_main;
    }
    {
        drpai_data_t drpai_data;
        drpai_data.address = 0;
        drpai_data.size    = 0;
        if (ioctl(g_drpai_fd, DRPAI_GET_DRPAI_AREA, &drpai_data) == 0)
            drpai_base = static_cast<uint64_t>(drpai_data.address);
    }
    printf("[Main] DRP-AI fd=%d  base: 0x%lx  model offset: 0x%lx\n",
           g_drpai_fd, (unsigned long)drpai_base, (unsigned long)DRPAI_MEM_OFFSET);
#endif
    g_drpai_base = drpai_base;   /* expose for R_Inf_Thread model-switch reload */

    /* 3. Construct pipeline objects */
    g_detector     = new TrafficDetector(g_cfg.detector);
    if (kEnableTracking)
        g_tracker  = new VehicleTracker(g_cfg.detector, 6, 0.35f);
    if (kEnableTracking || kEnableViolationLogging)
        g_engine   = new ViolationEngine(g_cfg.violation);
    if (kEnableViolationLogging)
        g_logger   = new ViolationLogger(g_cfg.output);
    g_lp_regex     = create_lp_regex_list();
    g_lp_validator = new LpValidator();
    g_video        = new VideoInput(g_cfg.video_source,
                                    sourceTypeFromString(g_cfg.video_source_type),
                                    g_cfg.video_width, g_cfg.video_height,
                                    g_cfg.video_fps,
                                    g_cfg.gstreamer_pipeline,
                                        g_cfg.video_loop,
                                        g_cfg.video_realtime);

    if (g_engine) buildViolationRules(g_engine, 0, g_cfg);   /* load model-1 rules */

    if (!g_cfg.output.annotated_video.empty()) {
        g_vwriter = new cv::VideoWriter(
            g_cfg.output.annotated_video,
            cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
            g_cfg.video_fps,
            cv::Size(g_cfg.video_width, g_cfg.video_height));
    }

    /* 4. Load DRP-AI model (skip when inference_fps < 0) */
    if (g_cfg.inference_fps >= 0) {
        if (!g_detector->load(drpai_base + DRPAI_MEM_OFFSET)) {
            fprintf(stderr, "[Main][ERROR] TrafficDetector::load() failed\n");
            ret_main = -1;
            goto end_cleanup;
        }
    } else {
        printf("[Main] Inference disabled (inference_fps=%d) — skipping model load\n",
               g_cfg.inference_fps);
    }

    /* 5. Termination semaphore */
    sem_create = sem_init(&terminate_req_sem, 0, 1);
    if (0 != sem_create) {
        fprintf(stderr, "[Main][ERROR] sem_init failed\n");
        ret_main = -1;
        goto end_cleanup;
    }

    /* 6. Launch threads */
    create_kbhit = pthread_create(&kbhit_thread, NULL, R_Kbhit_Thread, NULL);
    if (0 != create_kbhit) {
        fprintf(stderr, "[Main][ERROR] Failed to create KbHit thread\n");
        ret_main = -1;
        goto end_threads;
    }

    create_capture = pthread_create(&capture_thread, NULL, R_Capture_Thread, NULL);
    if (0 != create_capture) {
        sem_trywait(&terminate_req_sem);
        fprintf(stderr, "[Main][ERROR] Failed to create Capture thread\n");
        ret_main = -1;
        goto end_threads;
    }

    create_inf = pthread_create(&ai_inf_thread, NULL, R_Inf_Thread, NULL);
    if (0 != create_inf) {
        sem_trywait(&terminate_req_sem);
        fprintf(stderr, "[Main][ERROR] Failed to create Inference thread\n");
        ret_main = -1;
        goto end_threads;
    }

    create_display = pthread_create(&display_thread, NULL, R_Display_Thread, NULL);
    if (0 != create_display) {
        sem_trywait(&terminate_req_sem);
        fprintf(stderr, "[Main][ERROR] Failed to create Display thread\n");
        ret_main = -1;
        goto end_threads;
    }

    printf("[Main] All 4 threads running.\n");
    printf("  '1' → switch to Model 1 (traffic violations)\n");
    if (g_cfg.detector2_enabled)
        printf("  '2' → switch to Model 2 (helmet detection)\n");
    printf("  any other key → stop\n");

    /* 7. Join threads */
end_threads:
    if (0 == create_display) { pthread_join(display_thread, NULL); printf("[Main] Display   joined\n"); }
    if (0 == create_inf)     { pthread_join(ai_inf_thread,  NULL); printf("[Main] Inference joined\n"); }
    if (0 == create_capture) { pthread_join(capture_thread, NULL); printf("[Main] Capture   joined\n"); }
    if (0 == create_kbhit)   { pthread_join(kbhit_thread,   NULL); printf("[Main] KbHit     joined\n"); }

    if (0 == sem_create) sem_destroy(&terminate_req_sem);

    /* 8. Release resources */
end_cleanup:
    if (g_vwriter)      { g_vwriter->release(); delete g_vwriter;  g_vwriter      = nullptr; }
    delete g_lp_validator; g_lp_validator = nullptr;
    delete g_video;        g_video        = nullptr;
    delete g_logger;       g_logger       = nullptr;
    delete g_engine;       g_engine       = nullptr;
    delete g_tracker;      g_tracker      = nullptr;
    delete g_detector;     g_detector     = nullptr;
    if (g_drpai_fd >= 0) {
        close(g_drpai_fd);
        g_drpai_fd = -1;
    }

end_main:
    printf("[Main] Done. Violations logged: %zu  ret=%d\n",
           g_logger ? g_logger->totalLogged() : 0, ret_main);
    return ret_main;
}
