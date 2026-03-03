/*******************************************************************************
 * traffic_violation/src/main.cpp
 *
 * Multi-threaded pipeline (mirrors R01_object_detection architecture):
 *
 *   R_Capture_Thread                        R_Kbhit_Thread
 *        │                                        │
 *        ▼  g_cap (newest-frame-wins slot)        │ sem_trywait
 *   R_Inf_Thread (DRP-AI + tracker + violations)  │
 *        │                                        │
 *        ▼  g_disp (ready-flag slot)              │
 *   R_Display_Thread (Wayland / imshow)  ←────────┘
 *
 * Synchronisation: sem_t terminate_req_sem (init=1, R01 pattern).
 *   Every thread checks sem value == 1 each iteration.
 *   Any error / key-press → sem_trywait → all threads exit cleanly.
 *
 * Ownership:
 *   g_video     → R_Capture_Thread only
 *   g_detector, g_tracker, g_engine, g_logger, g_vwriter → R_Inf_Thread only
 *   Wayland instance   → R_Display_Thread only  (stack-local)
 ******************************************************************************/
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <climits>
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

/* ─────────────────────────────── Constants ─────────────────────────────── */
/** Poll interval when nothing to do (µs). */
#define WAIT_TIME          (1000)
/** LP OCR is run every N inference frames to amortise Tesseract cost. */
#define LP_OCR_INTERVAL    (10)
/** FPS print interval (seconds). */
#define FPS_PRINT_INTERVAL (1.0)

/* ─────────────────────── Termination semaphore ──────────────────────────
 * Initialised to 1.  All thread loops run while value == 1.
 * sem_trywait() (decrement without blocking) signals global stop. */
static sem_t terminate_req_sem;

/* ─────────────────────────── Thread handles ─────────────────────────────── */
static pthread_t capture_thread;
static pthread_t ai_inf_thread;
static pthread_t display_thread;
static pthread_t kbhit_thread;

/* ──────────────────────── Inter-thread data slots ───────────────────────
 * "Newest-frame-wins" strategy: producer always overwrites; consumer always
 * reads the latest.  This prevents stale-frame accumulation when DRP-AI
 * inference is slower than capture, which is the common case.            */

/** Capture → Inference slot. */
struct CaptureSlot {
    cv::Mat          frame;
    std::mutex       mtx;
    std::atomic<bool> ready{false};   /* true = new frame available */
};
static CaptureSlot g_cap;

/** Inference → Display slot. */
struct DisplaySlot {
    cv::Mat          frame;
    std::mutex       mtx;
    std::atomic<bool> ready{false};
};
static DisplaySlot g_disp;

/* ───────────────────────── Application global state ────────────────────
 * Each pointer is exclusively accessed from one thread after init, so no
 * further synchronisation is needed for the objects themselves.           */
static AppConfig  g_cfg;

static VideoInput*      g_video        = nullptr;   /* → R_Capture_Thread  */
static TrafficDetector* g_detector     = nullptr;   /* → R_Inf_Thread      */
static VehicleTracker*  g_tracker      = nullptr;   /* → R_Inf_Thread      */
static ViolationEngine* g_engine       = nullptr;   /* → R_Inf_Thread      */
static ViolationLogger* g_logger       = nullptr;   /* → R_Inf_Thread      */
static cv::VideoWriter* g_vwriter      = nullptr;   /* → R_Inf_Thread      */
static LpValidator*     g_lp_validator = nullptr;   /* → R_Inf_Thread      */

static std::vector<std::pair<std::regex, std::string>> g_lp_regex;

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

/* ────────────────── Traffic-light HSV detection ──────────────────────── */
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

/* ──────────────────────────── Draw overlay ───────────────────────────── */
static void drawOverlay(cv::Mat& frame,
    const std::vector<TrackedVehicle>& vehicles,
    const std::vector<detection>&      dets,
    bool red_light, double fps)
{
    /* Raw detections (thin box) */
    for (const auto& d : dets) {
        if (d.prob == 0.f) continue;
        cv::Rect r = d.bbox.toRect();
        cv::Scalar col(180, 180, 180);
        if      (g_cfg.detector.isVehicle(d.c)) col = cv::Scalar(0, 220, 0);
        else if (g_cfg.detector.isHelmet(d.c))  col = cv::Scalar(255, 200, 0);
        else if (g_cfg.detector.isLP(d.c))      col = cv::Scalar(0, 200, 255);
        cv::rectangle(frame, r, col, 1);
        const auto& lbs = g_detector->labels();
        if (d.c >= 0 && d.c < (int)lbs.size())
            cv::putText(frame, lbs[d.c], {r.x, r.y - 3},
                        cv::FONT_HERSHEY_SIMPLEX, 0.4, col, 1);
    }
    /* Tracked vehicles (thick box + label) */
    for (const auto& v : vehicles) {
        cv::Rect r = v.bbox.toRect();
        cv::Scalar color = (v.type == VehicleType::MOTORBIKE)
                           ? cv::Scalar(0, 255, 0) : cv::Scalar(255, 200, 0);
        cv::rectangle(frame, r, color, 2);
        std::string lbl = "T" + std::to_string(v.track_id);
        if (!v.plate.empty())                              lbl += " " + v.plate;
        if (v.type == VehicleType::MOTORBIKE && !v.has_helmet) lbl += " [NO HELMET]";
        cv::putText(frame, lbl, {r.x, r.y - 5},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1);
    }
    /* Traffic-light indicator */
    cv::Scalar tl_col = red_light ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 200, 0);
    cv::circle(frame, {frame.cols - 30, 30}, 15, tl_col, cv::FILLED);
    cv::putText(frame, red_light ? "RED" : "GO ",
                {frame.cols - 55, 70},
                cv::FONT_HERSHEY_SIMPLEX, 0.6, tl_col, 2);
    /* FPS */
    cv::putText(frame, "FPS:" + std::to_string((int)fps),
                {10, 30}, cv::FONT_HERSHEY_SIMPLEX, 0.8,
                cv::Scalar(0, 255, 255), 2);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *                            THREAD FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * R_Capture_Thread
 * ─────────────────
 * Reads the latest frame from the video source (VideoInput drives its own
 * internal grab-loop; getFrame() is non-blocking and thread-safe).
 * Writes into g_cap using newest-frame-wins: old unconsumed frames are silently
 * overwritten so that R_Inf_Thread always works on a fresh image.
 */
void* R_Capture_Thread(void* /*threadid*/)
{
    int32_t sem_check = 0;
    int8_t  ret       = 0;

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

        cv::Mat frame;
        if (!g_video->getFrame(frame) || frame.empty()) {
            if (!g_video->isOpen()) goto capture_end;
            usleep(WAIT_TIME);
            continue;
        }

        /* Newest-frame-wins write */
        {
            std::lock_guard<std::mutex> lk(g_cap.mtx);
            g_cap.frame = std::move(frame);
        }
        g_cap.ready.store(true);
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

/*
 * R_Inf_Thread
 * ─────────────
 * Core AI pipeline, running in a single dedicated thread so DRP-AI
 * (PreRuntime + LoadModel/Run) never contends with display or capture:
 *
 *   1  Read frame from g_cap
 *   2  DRP-AI inference (TrafficDetector::detect)  ← slowest step
 *   3  Tracker update
 *   4  Helmet association (motorbike ↔ helmet box)
 *   5  LP OCR every LP_OCR_INTERVAL frames
 *   6  Traffic-light HSV test
 *   7  Violation evaluation + logging
 *   8  Draw overlay on annotated frame
 *   9  Optional video-file write
 *  10  Push annotated frame → g_disp
 */
void* R_Inf_Thread(void* /*threadid*/)
{
    int32_t sem_check = 0;
    int8_t  ret       = 0;
    int     frame_idx = 0;

    auto   t_fps    = std::chrono::steady_clock::now();
    double loop_fps = 0.0;

    printf("[Inference] Thread started\n");

    while (1)
    {
        /* ── Termination check ──────────────────────────────────────────── */
        errno = 0;
        ret   = sem_getvalue(&terminate_req_sem, &sem_check);
        if (0 != ret) {
            fprintf(stderr, "[Inference][ERROR] sem_getvalue errno=%d\n", errno);
            goto err;
        }
        if (1 != sem_check) goto inf_end;

        /* ── Wait for new frame ─────────────────────────────────────────── */
        if (!g_cap.ready.load()) { usleep(WAIT_TIME); continue; }

        cv::Mat frame;
        {
            std::lock_guard<std::mutex> lk(g_cap.mtx);
            if (g_cap.frame.empty()) { g_cap.ready.store(false); continue; }
            frame = g_cap.frame.clone();   /* own a copy for this iteration */
            g_cap.ready.store(false);
        }
        ++frame_idx;

        /* ── 1. DRP-AI inference ─────────────────────────────────────────── */
        auto dets = g_detector->detect(frame);

        /* ── 2. Tracker update ───────────────────────────────────────────── */
        const auto& tracks = g_tracker->update(dets, frame.cols, frame.rows);

        /* ── 3. Helmet association ───────────────────────────────────────── */
        for (const auto& v : tracks) {
            if (v.type != VehicleType::MOTORBIKE) continue;
            cv::Rect rider = v.bbox.toRect();
            bool found = false;
            for (const auto& d : dets) {
                if (!g_cfg.detector.isHelmet(d.c) || d.prob == 0.f) continue;
                if ((d.bbox.toRect() & rider).area() > d.bbox.toRect().area() * 0.3f)
                    { found = true; break; }
            }
            g_tracker->setHelmet(v.track_id, found);
        }

        /* ── 4. LP OCR (amortised) ───────────────────────────────────────── */
        if (g_cfg.enable_lp_ocr && (frame_idx % LP_OCR_INTERVAL == 0)) {
            for (const auto& v : tracks) {
                if (!v.plate.empty()) continue;
                cv::Rect vbox = v.bbox.toRect();
                for (const auto& d : dets) {
                    if (!g_cfg.detector.isLP(d.c) || d.prob == 0.f) continue;
                    cv::Rect lbox = d.bbox.toRect();
                    if ((lbox & vbox).area() < lbox.area() * 0.2f) continue;
                    std::string plate = runLpOcr(frame, lbox);
                    if (!plate.empty()) { g_tracker->setPlate(v.track_id, plate); break; }
                }
            }
        }

        /* ── 5. Traffic-light state ──────────────────────────────────────── */
        TrafficLightState light = detectTrafficLight(frame, g_cfg.scene);

        /* ── 6. Violation evaluation ──────────────────────────────────────── */
        FrameContext ctx;
        ctx.frame        = frame;
        ctx.frame_ts_sec = frame_idx / (double)g_cfg.video_fps;
        ctx.vehicles     = g_tracker->tracks();
        ctx.light        = light;

        for (const auto& ev : g_engine->process(ctx))
            g_logger->log(ev);

        /* ── 7. Overlay ──────────────────────────────────────────────────── */
        drawOverlay(ctx.frame, ctx.vehicles, dets, light.red, loop_fps);

        /* ── 8. Optional video-file write ───────────────────────────────── */
        if (g_vwriter && g_vwriter->isOpened())
            g_vwriter->write(ctx.frame);

        /* ── 9. Push to display slot ─────────────────────────────────────── */
        {
            std::lock_guard<std::mutex> lk(g_disp.mtx);
            g_disp.frame = std::move(ctx.frame);
        }
        g_disp.ready.store(true);

        /* ── 10. FPS accounting ─────────────────────────────────────────── */
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
    g_logger->flush();
    g_disp.ready.store(true);   /* unblock display thread */
    printf("[Inference] Thread terminated\n");
    pthread_exit(NULL);
}

/*
 * R_Display_Thread
 * ─────────────────
 * Decoupled from inference so slow Wayland commit / imshow does not stall
 * DRP-AI.  On WITH_DRP: resize → BGRA → wayland.commit().
 * On CPU-only builds: cv::imshow (ESC to quit).
 */
void* R_Display_Thread(void* /*threadid*/)
{
    int32_t sem_check = 0;
    int8_t  ret       = 0;

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

    while (1)
    {
        errno = 0;
        ret   = sem_getvalue(&terminate_req_sem, &sem_check);
        if (0 != ret) {
            fprintf(stderr, "[Display][ERROR] sem_getvalue errno=%d\n", errno);
            goto err;
        }
        if (1 != sem_check) goto disp_end;

        if (!g_disp.ready.load()) { usleep(WAIT_TIME); continue; }

        cv::Mat frame;
        {
            std::lock_guard<std::mutex> lk(g_disp.mtx);
            frame = g_disp.frame.clone();
            g_disp.ready.store(false);
        }
        if (frame.empty()) continue;

#ifdef WITH_DRP
        {
            cv::Mat bgra, resized;
            cv::cvtColor(frame, bgra, cv::COLOR_BGR2BGRA);
            cv::resize(bgra, resized,
                       cv::Size(IMAGE_OUTPUT_WIDTH, IMAGE_OUTPUT_HEIGHT));
            wayland.commit(resized.data, NULL);
        }
#else
        cv::imshow("Traffic Violation - ESC to quit", frame);
        if (cv::waitKey(1) == 27) { printf("[Display] ESC pressed\n"); goto err; }
#endif
    }

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

/*
 * R_Kbhit_Thread
 * ───────────────
 * Non-blocking ENTER-key detection (identical pattern to R01_object_detection).
 * Sets stdin O_NONBLOCK, polls with WAIT_TIME sleep.
 * Key press → sem_trywait → all threads exit their next loop iteration.
 */
void* R_Kbhit_Thread(void* /*threadid*/)
{
    int32_t sem_check = 0;
    int8_t  ret       = 0;
    int32_t c         = 0;

    printf("[KbHit] Thread started\n");
    printf("==============================================\n");
    printf("  Press ENTER key to stop the application.  \n");
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
            printf("[KbHit] Key detected – stopping pipeline.\n");
            goto err;
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

/* ═══════════════════════════════════════════════════════════════════════════
 *                                 main()
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(int argc, char* argv[])
{
    int8_t  ret_main       = 0;
    int32_t sem_create     = -1;
    int32_t create_kbhit   = -1;
    int32_t create_capture = -1;
    int32_t create_inf     = -1;
    int32_t create_display = -1;

    /* ── 1. Load config ──────────────────────────────────────────────────── */
    const std::string base = exeDir();
    std::string config_path = (argc > 1)
                              ? std::string(argv[1])
                              : resolvePath(base, "config.yaml");

    /* If config not found next to the binary, fall back to cwd */
    {
        struct stat _st;
        if (argc == 1 && stat(config_path.c_str(), &_st) != 0) {
            config_path = "config.yaml";
            fprintf(stderr, "[Main] config.yaml not found next to binary – "
                            "trying cwd\n");
        }
    }

    /* Print paths BEFORE loading so the user sees them even if load crashes */
    printf("[Main] Exe dir  : %s\n", base.c_str());
    printf("[Main] Config   : %s\n", config_path.c_str());
    fflush(stdout);

    g_cfg = AppConfig::fromFile(config_path);
    g_cfg.detector.model_dir  = resolvePath(base, g_cfg.detector.model_dir);
    g_cfg.detector.pre_dir    = resolvePath(base, g_cfg.detector.pre_dir);
    g_cfg.detector.label_file = resolvePath(base, g_cfg.detector.label_file);
    g_cfg.output.save_dir     = resolvePath(base, g_cfg.output.save_dir);

    printf("[Main] Model    : %s\n", g_cfg.detector.model_dir.c_str());
    printf("[Main] Input    : %dx%d  fps=%d\n",
           g_cfg.video_width, g_cfg.video_height, g_cfg.video_fps);

    /* ── 2. DRP-AI base address ──────────────────────────────────────────── */
    uint64_t drpai_base = 0;
#ifdef WITH_DRP
    {
        int fd = open("/dev/drpai0", O_RDWR);
        if (fd < 0) {
            fprintf(stderr, "[Main][ERROR] Cannot open /dev/drpai0\n");
            ret_main = -1;
            goto end_main;
        }
        unsigned long addr = 0;
        if (ioctl(fd, DRPAI_GET_DRPAI_AREA, &addr) == 0)
            drpai_base = static_cast<uint64_t>(addr);
        ::close(fd);
        printf("[Main] DRP-AI base: 0x%lx\n", (unsigned long)drpai_base);
    }
#endif

    /* ── 3. Construct pipeline objects ──────────────────────────────────── */
    g_detector     = new TrafficDetector(g_cfg.detector);
    g_tracker      = new VehicleTracker(g_cfg.detector, 6, 0.35f);
    g_engine       = new ViolationEngine(g_cfg.violation);
    g_logger       = new ViolationLogger(g_cfg.output);
    g_lp_regex     = create_lp_regex_list();
    g_lp_validator = new LpValidator();
    g_video        = new VideoInput(g_cfg.video_source,
                                    sourceTypeFromString(g_cfg.video_source_type),
                                    g_cfg.video_width, g_cfg.video_height,
                                    g_cfg.video_fps,
                                    g_cfg.gstreamer_pipeline,
                                    /* loop_file= */ false);

    if (g_cfg.violation.helmet)
        g_engine->addRule(std::make_shared<HelmetRule>());
    if (g_cfg.violation.red_light)
        g_engine->addRule(std::make_shared<RedLightRule>(g_cfg.scene));
    if (g_cfg.violation.wrong_lane)
        g_engine->addRule(std::make_shared<WrongLaneRule>(g_cfg.scene));
    if (g_cfg.violation.lane_line && !g_cfg.scene.lane_lines.empty())
        g_engine->addRule(std::make_shared<LaneLineRule>(g_cfg.scene));

    if (!g_cfg.output.annotated_video.empty()) {
        g_vwriter = new cv::VideoWriter(
            g_cfg.output.annotated_video,
            cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
            g_cfg.video_fps,
            cv::Size(g_cfg.video_width, g_cfg.video_height));
    }

    /* ── 4. Load DRP-AI model ────────────────────────────────────────────── */
    if (!g_detector->load(drpai_base)) {
        fprintf(stderr, "[Main][ERROR] TrafficDetector::load() failed\n");
        ret_main = -1;
        goto end_cleanup;
    }

    /* ── 5. Termination semaphore (init = 1) ─────────────────────────────── */
    sem_create = sem_init(&terminate_req_sem, 0, 1);
    if (0 != sem_create) {
        fprintf(stderr, "[Main][ERROR] sem_init failed\n");
        ret_main = -1;
        goto end_cleanup;
    }

    /* ── 6. Launch threads ───────────────────────────────────────────────── */
    /*
     * Launch order: Kbhit first (user can abort early at any time),
     * then Capture (producer), then Inference (consumer/producer),
     * then Display (final consumer) – ensures no consumer starts
     * before its producer is ready.
     */
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

    printf("[Main] All 4 threads running. Press ENTER to stop.\n");

    /* ── 7. Join threads ─────────────────────────────────────────────────── */
    /*
     * Join in reverse-dependency order (consumer first so graceful shutdown):
     *   Display ← Inference ← Capture ← Kbhit
     */
end_threads:
    if (0 == create_display) { pthread_join(display_thread, NULL); printf("[Main] Display   joined\n"); }
    if (0 == create_inf)     { pthread_join(ai_inf_thread,  NULL); printf("[Main] Inference joined\n"); }
    if (0 == create_capture) { pthread_join(capture_thread, NULL); printf("[Main] Capture   joined\n"); }
    if (0 == create_kbhit)   { pthread_join(kbhit_thread,   NULL); printf("[Main] KbHit     joined\n"); }

    if (0 == sem_create) sem_destroy(&terminate_req_sem);

    /* ── 8. Release resources ────────────────────────────────────────────── */
end_cleanup:
    if (g_vwriter)      { g_vwriter->release(); delete g_vwriter;  g_vwriter      = nullptr; }
    delete g_lp_validator; g_lp_validator = nullptr;
    delete g_video;        g_video        = nullptr;
    delete g_logger;       g_logger       = nullptr;
    delete g_engine;       g_engine       = nullptr;
    delete g_tracker;      g_tracker      = nullptr;
    delete g_detector;     g_detector     = nullptr;

end_main:
    printf("[Main] Done. Violations logged: %zu  ret=%d\n",
           g_logger ? g_logger->totalLogged() : 0, ret_main);
    return ret_main;
}
