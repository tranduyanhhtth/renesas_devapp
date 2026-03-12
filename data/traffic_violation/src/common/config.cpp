/*******************************************************************************
 * traffic_violation/src/common/config.cpp
 * Parse config.yaml – single detector model
 ******************************************************************************/
#include "config.h"
#include <iostream>
#include <fstream>
#include <sys/stat.h>

static cv::FileNode safeNode(const cv::FileNode& p, const std::string& k) { return p[k]; }
static std::string  nodeStr  (const cv::FileNode& n, const std::string& d="") {
    if (n.empty() || n.type()!=cv::FileNode::STRING) return d;
    return static_cast<std::string>(n); }
static int   nodeInt  (const cv::FileNode& n, int   d=0)    { if(n.empty()) return d; return (int)n; }
static float nodeFloat(const cv::FileNode& n, float d=0.f)  { if(n.empty()) return d; return (float)n; }
static bool  nodeBool (const cv::FileNode& n, bool  d=false){ if(n.empty()) return d; return (int)n!=0; }

static std::vector<int> nodeIntList(const cv::FileNode& n) {
    std::vector<int> v;
    if (n.empty() || n.type()!=cv::FileNode::SEQ) return v;
    for (auto it=n.begin();it!=n.end();++it) v.push_back((int)*it);
    return v;
}
static std::vector<float> nodeFloatList(const cv::FileNode& n) {
    std::vector<float> v;
    if (n.empty() || n.type()!=cv::FileNode::SEQ) return v;
    for (auto it=n.begin();it!=n.end();++it) v.push_back((float)*it);
    return v;
}

static DetectorConfig parseDetector(const cv::FileNode& d) {
    DetectorConfig c;
    c.model_type     = nodeStr  (d["model_type"], "yolov3");
    c.model_dir      = nodeStr  (d["model_dir"]);
    c.pre_dir        = nodeStr  (d["pre_dir"]);
    c.label_file     = nodeStr  (d["label_file"]);
    c.input_width    = nodeInt  (d["input_width"],  416);
    c.input_height   = nodeInt  (d["input_height"], 416);
    c.conf_threshold = nodeFloat(d["conf_threshold"], 0.50f);
    c.nms_threshold  = nodeFloat(d["nms_threshold"],  0.45f);
    c.num_class  = nodeInt(d["num_class"],  7);
    c.num_bb     = nodeInt(d["num_bb"],     3);
    c.num_layers = nodeInt(d["num_layers"], 3);
    auto grids = nodeIntList(d["grids"]);
    if (!grids.empty()) c.grids = grids;
    else if (c.model_type == "yolov8") c.grids = {80, 40, 20};
    else                               c.grids = {13, 26, 52};
    auto anchors = nodeFloatList(d["anchors"]);
    if (!anchors.empty()) c.anchors = anchors;
    else if (c.model_type != "yolov8")
        c.anchors = {10,13, 16,30, 33,23, 30,61, 62,45, 59,119, 116,90, 156,198, 373,326};
    c.class_motorbike     = nodeInt(d["class_motorbike"],     0);
    c.class_car           = nodeInt(d["class_car"],           1);
    c.class_truck         = nodeInt(d["class_truck"],         2);
    c.class_bus           = nodeInt(d["class_bus"],           3);
    c.class_person        = nodeInt(d["class_person"],        4);
    c.class_helmet        = nodeInt(d["class_helmet"],        5);
    c.class_license_plate = nodeInt(d["class_license_plate"], 6);
    return c;
}

static bool fileNonEmpty(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;  /* does not exist */
    if (st.st_size == 0) return false;               /* empty file     */
    /* Quick sanity: first byte should be printable (YAML starts with '%' or a key) */
    std::ifstream f(path, std::ios::binary);
    char c = 0;
    return f.get(c) && c != '\0';
}

AppConfig AppConfig::fromFile(const std::string& path) {
    AppConfig cfg;

    /* Guard against the OpenCV 4.1.0 bug that throws cv::Exception (error -49)
     * when FileStorage::open() is called on an empty or missing YAML file,
     * instead of simply returning isOpened()==false. */
    if (!fileNonEmpty(path)) {
        std::cerr << "[Config] '" << path << "' not found or empty – using defaults\n";
        std::cerr << "[Config] Hint: copy config.yaml next to the binary, or pass path as argv[1]\n";
        cfg.detector.grids   = {13,26,52};
        cfg.detector.anchors = {10,13, 16,30, 33,23, 30,61, 62,45, 59,119, 116,90, 156,198, 373,326};
        return cfg;
    }

    cv::FileStorage fs;
    try {
        fs.open(path, cv::FileStorage::READ);
    } catch (const cv::Exception& e) {
        std::cerr << "[Config] cv::FileStorage exception on '" << path << "': " << e.what() << "\n";
        std::cerr << "[Config] Using defaults\n";
        cfg.detector.grids   = {13,26,52};
        cfg.detector.anchors = {10,13, 16,30, 33,23, 30,61, 62,45, 59,119, 116,90, 156,198, 373,326};
        return cfg;
    }
    if (!fs.isOpened()) {
        std::cerr << "[Config] Cannot open '" << path << "' – using defaults\n";
        cfg.detector.grids   = {13,26,52};
        cfg.detector.anchors = {10,13, 16,30, 33,23, 30,61, 62,45, 59,119, 116,90, 156,198, 373,326};
        return cfg;
    }
    std::cout << "[Config] Loaded: " << path << "\n";
    // video
    auto vi = fs["video"];
    cfg.video_source_type    = nodeStr  (vi["source_type"], "file");
    cfg.video_source         = nodeStr  (vi["source"], "0");
    cfg.video_width          = nodeInt  (vi["width"],  1920);
    cfg.video_height         = nodeInt  (vi["height"], 1080);
    cfg.video_fps            = nodeInt  (vi["fps"],    30);
    cfg.gstreamer_pipeline   = nodeStr  (vi["gstreamer_pipeline"]);
    // single detector (model 1)
    cfg.detector = parseDetector(fs["detector"]);
    cfg.detector.drpai_freq = nodeInt(fs["drpai"]["freq_index"], 2);
    // optional second model (model 2: e.g. helmet detection)
    if (!fs["detector2"].empty()) {
        cfg.detector2_enabled = true;
        cfg.detector2 = parseDetector(fs["detector2"]);
        cfg.detector2.drpai_freq = cfg.detector.drpai_freq;
        std::cout << "[Config] detector2 enabled: " << cfg.detector2.model_dir << "\n";
    }
    // enable_lp_ocr
    cfg.enable_lp_ocr = nodeBool(fs["enable_lp_ocr"], true);
    // scene
    auto sc = fs["scene"];
    cfg.scene.stop_line_y1 = nodeFloat(sc["stop_line"]["y1"], 0.60f);
    cfg.scene.stop_line_y2 = nodeFloat(sc["stop_line"]["y2"], 0.63f);
    auto tl = sc["traffic_light_roi"];
    cfg.scene.traffic_light_roi = { nodeFloat(tl["x"],0.80f), nodeFloat(tl["y"],0.05f),
                                     nodeFloat(tl["w"],0.08f), nodeFloat(tl["h"],0.20f) };
    auto lz = sc["lane_zones"];
    for (const std::string& key : {"motorbike","car","truck"}) {
        auto node = lz[key];
        if (!node.empty()) cfg.scene.lane_zones[key] = { nodeFloat(node["x_min"],0.f), nodeFloat(node["x_max"],1.f) };
    }
    // lane_lines: list of static vertical lane divider positions
    auto ll_node = sc["lane_lines"];
    if (!ll_node.empty() && ll_node.type() == cv::FileNode::SEQ) {
        for (auto it = ll_node.begin(); it != ll_node.end(); ++it) {
            LaneLine ll;
            ll.x_norm    = nodeFloat((*it)["x_norm"],   0.5f);
            ll.overlap_px = nodeInt ((*it)["overlap_px"], 8);
            cfg.scene.lane_lines.push_back(ll);
        }
    }
    // violation
    auto vn = fs["violation"]["rules"];
    cfg.violation.helmet     = nodeBool (vn["helmet"],     true);
    cfg.violation.red_light  = nodeBool (vn["red_light"],  true);
    cfg.violation.wrong_lane = nodeBool (vn["wrong_lane"], true);
    cfg.violation.lane_line  = nodeBool (vn["lane_line"],  true);
    cfg.violation.cooldown_sec   = nodeFloat(fs["violation"]["cooldown_sec"],   5.0f);
    cfg.violation.confirm_frames = nodeInt  (fs["violation"]["confirm_frames"],  3);
    // output
    auto op = fs["output"];
    cfg.output.save_dir        = nodeStr (op["save_dir"],       "./violations");
    cfg.output.save_full_frame = nodeBool(op["save_full_frame"], true);
    cfg.output.save_json       = nodeBool(op["save_json"],       true);
    cfg.output.annotated_video = nodeStr (op["annotated_video"]);
    return cfg;
}
