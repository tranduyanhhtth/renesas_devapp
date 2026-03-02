/*******************************************************************************
 * traffic_violation/src/main.cpp
 * Pipeline: 1 model (TrafficDetector) -> tracker -> OCR -> violation rules
 *
 * Architecture follows R01_object_detection - single model, single thread.
 ******************************************************************************/
#include <iostream>
#include <signal.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <climits>
#include <libgen.h>

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
#include <fcntl.h>
#include <sys/ioctl.h>
#include "drp/image.h"
#include "drp/wayland.h"
#include "drp/define.h"
#endif

static std::atomic<bool> g_running{true};
static void sigHandler(int) { g_running = false; }

static std::string exeDir()
{
    char buf[PATH_MAX] = {};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = '\0';
    return std::string(dirname(buf));
}

/* Nếu path đã là absolute thì giữ nguyên, nếu relative thì prefix bằng base */
static std::string resolvePath(const std::string& base, const std::string& p)
{
    if (p.empty() || p[0] == '/') return p;
    return base + "/" + p;
}

/* ---- HSV traffic light detection ---------------------------------------- */
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
    cv::Mat m1, m2, red;
    cv::inRange(hsv, cv::Scalar(  0, 120, 70), cv::Scalar( 10, 255, 255), m1);
    cv::inRange(hsv, cv::Scalar(160, 120, 70), cv::Scalar(180, 255, 255), m2);
    cv::bitwise_or(m1, m2, red);
    st.red = (cv::countNonZero(red) > roi.area() * 0.05f);
    return st;
}

/* ---- LP OCR helper ------------------------------------------------------- */
static std::string runLpOcr(const cv::Mat& frame, const cv::Rect& lp_rect,
    const std::vector<std::pair<std::regex, std::string>>& regex_list,
    LpValidator& validator)
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

    lp_struct lp = match_vn_plate(regex_list, trimmed);
    if (!lp.matched || !validator.is_valid_plate(lp)) return "";
    return validator.format_plate(lp);
}

/* ---- Draw overlay -------------------------------------------------------- */
static void drawOverlay(cv::Mat& frame,
    const std::vector<TrackedVehicle>& vehicles,
    const std::vector<detection>& dets,
    const DetectorConfig& det_cfg,
    const std::vector<std::string>& labels,
    bool red_light, double fps)
{
    for (const auto& d : dets) {
        if (d.prob == 0.f) continue;
        cv::Rect r = d.bbox.toRect();
        cv::Scalar col(180, 180, 180);
        if (det_cfg.isVehicle(d.c))     col = cv::Scalar(0, 220, 0);
        else if (det_cfg.isHelmet(d.c)) col = cv::Scalar(255, 200, 0);
        else if (det_cfg.isLP(d.c))     col = cv::Scalar(0, 200, 255);
        cv::rectangle(frame, r, col, 1);
        if (d.c >= 0 && d.c < (int)labels.size())
            cv::putText(frame, labels[d.c], {r.x, r.y - 3},
                        cv::FONT_HERSHEY_SIMPLEX, 0.4, col, 1);
    }
    for (const auto& v : vehicles) {
        cv::Rect r = v.bbox.toRect();
        cv::Scalar color = (v.type == VehicleType::MOTORBIKE)
                           ? cv::Scalar(0, 255, 0) : cv::Scalar(255, 200, 0);
        cv::rectangle(frame, r, color, 2);
        std::string lbl = "T" + std::to_string(v.track_id);
        if (!v.plate.empty()) lbl += " " + v.plate;
        if (v.type == VehicleType::MOTORBIKE && !v.has_helmet) lbl += " [NO HELMET]";
        cv::putText(frame, lbl, {r.x, r.y - 5},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1);
    }
    cv::Scalar tl_col = red_light ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 200, 0);
    cv::circle(frame, {frame.cols - 30, 30}, 15, tl_col, cv::FILLED);
    cv::putText(frame, red_light ? "RED" : "GO ", {frame.cols - 55, 70},
                cv::FONT_HERSHEY_SIMPLEX, 0.6, tl_col, 2);
    cv::putText(frame, "FPS:" + std::to_string((int)fps),
                {10, 30}, cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 255), 2);
}

/* ========================================================================= */
int main(int argc, char* argv[])
{
    signal(SIGINT,  sigHandler);
    signal(SIGTERM, sigHandler);

    /* Thư mục chứa binary – dùng làm base cho mọi relative path */
    const std::string base = exeDir();

    /* Config: ưu tiên argument, fallback về <exe_dir>/config.yaml */
    std::string config_path = (argc > 1) ? argv[1] : resolvePath(base, "config.yaml");

    AppConfig cfg = AppConfig::fromFile(config_path);

    /* Resolve các path tương đối trong config theo thư mục binary */
    cfg.detector.model_dir  = resolvePath(base, cfg.detector.model_dir);
    cfg.detector.pre_dir    = resolvePath(base, cfg.detector.pre_dir);
    cfg.detector.label_file = resolvePath(base, cfg.detector.label_file);
    cfg.output.save_dir     = resolvePath(base, cfg.output.save_dir);

    std::cout << "[Main] Exe dir : " << base << "\n";
    std::cout << "[Main] Config  : " << config_path << "\n";
    std::cout << "[Main] Model   : " << cfg.detector.model_dir << "\n";
    std::cout << "[Main] Input   : " << cfg.detector.input_width
              << "x" << cfg.detector.input_height
              << "  classes=" << cfg.detector.num_class << "\n";

    /* DRP-AI base address */
    uint64_t drpai_base = 0;
#ifdef WITH_DRP
    {
        int fd = open("/dev/drpai0", O_RDWR);
        if (fd < 0) {
            std::cerr << "[Main] /dev/drpai0 unavailable\n";
        } else {
            unsigned long addr = 0;
            if (ioctl(fd, DRPAI_GET_DRPAI_AREA, &addr) == 0)
                drpai_base = static_cast<uint64_t>(addr);
            ::close(fd);
        }
        std::cout << "[Main] DRP-AI base: 0x" << std::hex << drpai_base << std::dec << "\n";
    }
#endif

    /* Single unified detector */
    TrafficDetector detector(cfg.detector);
    if (!detector.load(drpai_base)) {
        std::cerr << "[Main] TrafficDetector::load() failed\n";
        return 1;
    }

    /* LP OCR resources */
    auto lp_regex_list = create_lp_regex_list();
    LpValidator lp_validator;

    /* Tracker */
    VehicleTracker tracker(cfg.detector, 6, 0.35f);

    /* Violation engine */
    ViolationEngine engine(cfg.violation);
    if (cfg.violation.helmet)
        engine.addRule(std::make_shared<HelmetRule>());
    if (cfg.violation.red_light)
        engine.addRule(std::make_shared<RedLightRule>(cfg.scene));
    if (cfg.violation.wrong_lane)
        engine.addRule(std::make_shared<WrongLaneRule>(cfg.scene));
    if (cfg.violation.lane_line && !cfg.scene.lane_lines.empty())
        engine.addRule(std::make_shared<LaneLineRule>(cfg.scene));

    /* Logger */
    ViolationLogger logger(cfg.output);

    /* Video writer */
    cv::VideoWriter vwriter;
    if (!cfg.output.annotated_video.empty())
        vwriter.open(cfg.output.annotated_video,
                     cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                     cfg.video_fps, cv::Size(cfg.video_width, cfg.video_height));

    /* Open video */
    VideoInput video(cfg.video_source,
                     cfg.video_width, cfg.video_height,
                     cfg.video_fps, cfg.gstreamer_pipeline);
    if (!video.open()) { std::cerr << "[Main] Cannot open video source\n"; return 1; }
    std::cout << "[Main] Pipeline started. Ctrl+C to stop.\n";

#ifdef WITH_DRP
    Wayland wayland;
    Image   img_disp;
    bool    wayland_ok = false;
    if (wayland.init(IMAGE_OUTPUT_WIDTH, IMAGE_OUTPUT_HEIGHT,
                     IMAGE_OUTPUT_CHANNEL_BGRA) == 0)
    {
        img_disp.init(cfg.video_width, cfg.video_height, CAM_IMAGE_CHANNEL_BGR,
                      IMAGE_OUTPUT_WIDTH, IMAGE_OUTPUT_HEIGHT, IMAGE_OUTPUT_CHANNEL_BGRA);
        wayland_ok = true;
        std::cout << "[Main] Wayland " << IMAGE_OUTPUT_WIDTH
                  << "x" << IMAGE_OUTPUT_HEIGHT << "\n";
    }
#endif

    /* Main loop */
    cv::Mat frame;
    int  frame_idx   = 0;
    auto t_fps_start = std::chrono::steady_clock::now();
    double loop_fps  = 0.;

    while (g_running && video.isOpen())
    {
        if (!video.getFrame(frame) || frame.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        ++frame_idx;

        /* 1. Single-model inference */
        auto all_dets = detector.detect(frame);

        /* 2. Update tracker (vehicle/person classes filtered internally) */
        const auto& tracks = tracker.update(all_dets, frame.cols, frame.rows);

        /* 3. Helmet association for motorbike tracks */
        for (const auto& v : tracks) {
            if (v.type != VehicleType::MOTORBIKE) continue;
            cv::Rect rider = v.bbox.toRect();
            bool found = false;
            for (const auto& d : all_dets) {
                if (!cfg.detector.isHelmet(d.c) || d.prob == 0.f) continue;
                cv::Rect hbox = d.bbox.toRect();
                if ((hbox & rider).area() > hbox.area() * 0.3f) { found = true; break; }
            }
            tracker.setHelmet(v.track_id, found);
        }

        /* 4. LP OCR every 10 frames */
        if (cfg.enable_lp_ocr && (frame_idx % 10 == 0)) {
            for (const auto& v : tracks) {
                if (!v.plate.empty()) continue;
                cv::Rect vbox = v.bbox.toRect();
                for (const auto& d : all_dets) {
                    if (!cfg.detector.isLP(d.c) || d.prob == 0.f) continue;
                    cv::Rect lbox = d.bbox.toRect();
                    if ((lbox & vbox).area() < lbox.area() * 0.2f) continue;
                    std::string plate = runLpOcr(frame, lbox, lp_regex_list, lp_validator);
                    if (!plate.empty()) { tracker.setPlate(v.track_id, plate); break; }
                }
            }
        }

        /* 5. Traffic light state */
        TrafficLightState light = detectTrafficLight(frame, cfg.scene);

        /* 6. Build context and evaluate violations */
        FrameContext ctx;
        ctx.frame        = frame.clone();
        ctx.frame_ts_sec = frame_idx / (double)cfg.video_fps;
        ctx.vehicles     = tracker.tracks();
        ctx.light        = light;

        drawOverlay(ctx.frame, ctx.vehicles, all_dets,
                    cfg.detector, detector.labels(), light.red, loop_fps);

        for (const auto& ev : engine.process(ctx))
            logger.log(ev);

        /* 7. Output */
        if (vwriter.isOpened()) vwriter.write(ctx.frame);

#ifdef WITH_DRP
        if (wayland_ok) {
            cv::Mat bgra, resized;
            cv::cvtColor(ctx.frame, bgra, cv::COLOR_BGR2BGRA);
            cv::resize(bgra, resized,
                       cv::Size(IMAGE_OUTPUT_WIDTH, IMAGE_OUTPUT_HEIGHT));
            img_disp.set_mat(resized);
            wayland.commit(nullptr, img_disp.img_mat.data);
        }
#endif

        /* FPS */
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - t_fps_start).count();
        if (elapsed >= 1.0) {
            loop_fps    = frame_idx / elapsed;
            frame_idx   = 0;
            t_fps_start = now;
        }
    }

    /* Shutdown */
    video.close();
    if (vwriter.isOpened()) vwriter.release();
    logger.flush();
#ifdef WITH_DRP
    if (wayland_ok) wayland.exit();
#endif
    std::cout << "[Main] Done. Violations logged: " << logger.totalLogged() << "\n";
    return 0;
}
