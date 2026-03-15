/*******************************************************************************
 * traffic_violation/src/models/TrafficDetector.cpp
 *
 * Single unified YOLO detector − R01_object_detection style.
 * Model detect tất cả class: motorbike, car, truck, bus, person, helmet, LP.
 ******************************************************************************/
#include "TrafficDetector.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <cmath>
#include <climits>
#include <cfloat>
#include <thread>

/* ── helper: FP16 → FP32 (giống R01 dùng builtin) ──────────────────────────── */
#ifdef WITH_DRP
#include <builtin_fp16.h>
static inline float fp16_to_fp32(uint16_t a) {
    return __extendXfYf2__<uint16_t, uint16_t, 10, float, uint32_t, 23>(a);
}
#endif

/* ─────────────────────────────────────────────────────────────────────────── */
TrafficDetector::~TrafficDetector()
{
#ifdef WITH_DRP
    if (m_drpai_buf) {
        buffer_free_dmabuf(m_drpai_buf);
        free(m_drpai_buf);
        m_drpai_buf = nullptr;
    }
#endif
}

TrafficDetector::TrafficDetector(const DetectorConfig& cfg)
    : m_cfg(cfg)
{
    /* Compute output buffer size from model type */
    m_inf_out_size = 0;
    if (m_cfg.model_type == "yolov8") {
        /* YOLOv8: output shape [num_class+4, total_anchors]
         * total_anchors = sum(grid^2) for grids e.g. {80,40,20} = 8400 */
        m_inf_out_size = (m_cfg.num_class + 4) * m_cfg.totalAnchors();
    } else {
        /* YOLOv3/v5: output shape [layers, num_bb*(num_class+5)*grid^2] */
        for (int g : m_cfg.grids)
            m_inf_out_size += (m_cfg.num_class + 5) * m_cfg.num_bb * g * g;
    }
}

/* ── load ─────────────────────────────────────────────────────────────────── */
bool TrafficDetector::load(uint64_t drpai_base_addr)
{
    constexpr int kLoadRetryCount = 3;
    constexpr auto kLoadRetryDelay = std::chrono::milliseconds(1000);

    /* Load label file */
    if (!m_cfg.label_file.empty()) {
        std::ifstream f(m_cfg.label_file);
        std::string line;
        while (std::getline(f, line))
            if (!line.empty()) m_labels.push_back(line);
        if (!m_labels.empty())
            std::cout << "[TrafficDetector] Loaded " << m_labels.size()
                      << " labels from " << m_cfg.label_file << "\n";
    }

#ifndef WITH_DRP
    std::cout << "[TrafficDetector] Stub mode (no DRP-AI)\n";
    return true;
#else
    /* Allocate output buffer */
    m_output_buf.resize(m_inf_out_size, 0.f);

    /* ── Pre-processing Runtime ─────────────────────────────────────────────── */
    /* NOTE: PreRuntime and TVM DRP-AI runtime share the /dev/drpai0 device file.
     * PreRuntime::Load() must be called BEFORE TVM LoadModel() because it
     * initialises the DRP-AI memory regions via DRPAI_ASSIGN ioctls.  Without
     * that initialisation, TVM's graph executor crashes in get_input() when it
     * tries to access DRP-AI-mapped tensors.  This mirrors R01's call order:
     *   preruntime.Load(pre_dir)  → then  runtime.LoadModel(model_dir, addr)  */
    m_pre_ok = false;
    if (!m_cfg.pre_dir.empty()) {
        int ret = -1;
        for (int attempt = 1; attempt <= kLoadRetryCount; ++attempt) {
            ret = m_preruntime.Load(m_cfg.pre_dir);
            if (ret == 0) break;
            std::cerr << "[TrafficDetector] PreRuntime Load attempt "
                      << attempt << "/" << kLoadRetryCount
                      << " failed (pre_dir=" << m_cfg.pre_dir
                      << ") ret=" << ret << "\n";
            if (attempt < kLoadRetryCount)
                std::this_thread::sleep_for(kLoadRetryDelay);
        }
        if (ret != 0) {
            std::cerr << "[TrafficDetector] PreRuntime Load failed (pre_dir="
                      << m_cfg.pre_dir << ") ret=" << ret
                      << " -> fallback to CPU preprocessing\n";
            m_pre_ok = false;
            /* Fall back to CPU preproc – non-fatal */
        } else {
            m_pre_ok = true;
            std::cout << "[TrafficDetector] PreRuntime loaded: " << m_cfg.pre_dir << "\n";

            /* Allocate MMNGR DMA buffer for PreRuntime physical address.
             * Size = model input (W x H x 3 BGR), same as R01's drpai_buf.
             * PreRuntime receives phy_addr and handles resize+normalise itself. */
            m_drpai_buf = (dma_buffer*)malloc(sizeof(dma_buffer));
            int r2 = buffer_alloc_dmabuf(m_drpai_buf,
                         m_cfg.input_width * m_cfg.input_height * 3);
            if (r2 != 0) {
                std::cerr << "[TrafficDetector] DMA buffer alloc failed (ret=" << r2 << ")\n";
                free(m_drpai_buf);
                m_drpai_buf = nullptr;
                m_pre_ok = false;  /* fall back to CPU */
            } else {
                std::cout << "[TrafficDetector] DMA buf " << m_cfg.input_width
                          << "x" << m_cfg.input_height << "x3"
                          << " phy=0x" << std::hex << m_drpai_buf->phy_addr
                          << std::dec << "\n";
            }
        }
    }

    /* ── DRP-AI TVM Runtime ─────────────────────────────────────────────────── */
    bool ok = false;
    for (int attempt = 1; attempt <= kLoadRetryCount; ++attempt) {
        try {
            ok = m_runtime.LoadModel(m_cfg.model_dir, drpai_base_addr);
        } catch (const std::exception& e) {
            std::cerr << "[TrafficDetector] LoadModel attempt "
                      << attempt << "/" << kLoadRetryCount
                      << " threw exception: " << e.what() << "\n";
            ok = false;
        } catch (...) {
            std::cerr << "[TrafficDetector] LoadModel attempt "
                      << attempt << "/" << kLoadRetryCount
                      << " threw unknown exception\n";
            ok = false;
        }
        if (ok) break;
        if (attempt < kLoadRetryCount)
            std::this_thread::sleep_for(kLoadRetryDelay);
    }
    if (!ok) {
        std::cerr << "[TrafficDetector] LoadModel failed: " << m_cfg.model_dir << "\n";
        m_runtime_ok = false;
        return false;
    }
    m_runtime_ok = true;
    std::cout << "[TrafficDetector] Model loaded: " << m_cfg.model_dir << "\n";

    /* ── Resize output buffer to the ACTUAL TVM model output (prevents heap
     * corruption when the compiled model's output size differs from the
     * value computed from config grids/num_class).                        ── */
    {
        int64_t actual_out = 0;
        int nout = m_runtime.GetNumOutput();
        for (int i = 0; i < nout; i++) {
            auto o = m_runtime.GetOutput(i);
            actual_out += std::get<2>(o);
        }
        if (actual_out > 0 && actual_out != (int64_t)m_inf_out_size) {
            std::cout << "[TrafficDetector] Output buf: config=" << m_inf_out_size
                      << " actual=" << actual_out << " -> using actual\n";
            m_inf_out_size = (int)actual_out;
        }
        m_output_buf.resize(m_inf_out_size, 0.f);
        std::cout << "[TrafficDetector] Output buf: " << m_inf_out_size
                  << " floats (" << nout << " output head(s))\n";
    }

    return true;
#endif
}

/* ── cpuPreprocess ────────────────────────────────────────────────────────── */
void TrafficDetector::cpuPreprocess(const cv::Mat& src, std::vector<float>& dst) const
{
    cv::Mat resized, rgb;
    cv::resize(src, resized,
               cv::Size(m_cfg.input_width, m_cfg.input_height),
               0, 0, cv::INTER_LINEAR);
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

    int W = m_cfg.input_width;
    int H = m_cfg.input_height;
    dst.resize(3 * H * W);

    /* HWC → CHW, normalize /255 */
    for (int c = 0; c < 3; c++)
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                dst[c * H * W + y * W + x] =
                    static_cast<float>(rgb.at<cv::Vec3b>(y, x)[c]) / 255.f;
}

/* ── yolo_offset / yolo_index (R01 style) ───────────────────────────────── */
int32_t TrafficDetector::yolo_offset(int n, int b, int y, int x) const
{
    int num = m_cfg.grids[n];
    int prev = 0;
    for (int i = 0; i < n; i++)
        prev += m_cfg.num_bb * (m_cfg.num_class + 5) * m_cfg.grids[i] * m_cfg.grids[i];
    return prev + b * (m_cfg.num_class + 5) * num * num + y * num + x;
}

int32_t TrafficDetector::yolo_index(int n, int offs, int channel) const
{
    int num_grid = m_cfg.grids[n];
    return offs + channel * num_grid * num_grid;
}

/* ── postProcess: dispatch ────────────────────────────────────────────────────────────── */
void TrafficDetector::postProcess(const float* floatarr,
                                   int frame_w, int frame_h,
                                   std::vector<detection>& dets) const
{
    dets.clear();
    if (m_cfg.model_type == "yolov8")
        postProcessYolov8(floatarr, frame_w, frame_h, dets);
    else
        postProcessAnchored(floatarr, frame_w, frame_h, dets);
}

/* ── postProcessYolov8 ───────────────────────────────────────────────────────────── */
/* Output layout (DRP-AI TVM-compiled YOLOv8):                                  */
/*   Flat array [num_class+4, total_anchors] stored row-major.                  */
/*   Row 0-3   : cx, cy, w, h in model pixel space [0, input_width]             */
/*   Row 4-..  : class scores, already sigmoid-decoded by TVM                   */
/*   Confidence = max class score (anchor-free, no objectness).                 */
void TrafficDetector::postProcessYolov8(const float* floatarr,
                                         int frame_w, int frame_h,
                                         std::vector<detection>& dets) const
{
    const int   NA        = m_cfg.totalAnchors();  /* e.g. 8400 for 640x640 */
    const int   NC        = m_cfg.num_class;
    const float scale_x   = static_cast<float>(frame_w) / m_cfg.input_width;
    const float scale_y   = static_cast<float>(frame_h) / m_cfg.input_height;

    for (int j = 0; j < NA; j++)
    {
        /* Find best class (rows 4 .. 4+NC-1) */
        float max_cls  = 0.f;
        int   pred_cls = -1;
        for (int k = 0; k < NC; k++) {
            float s = floatarr[(4 + k) * NA + j];
            if (s > max_cls) { max_cls = s; pred_cls = k; }
        }
        if (max_cls < m_cfg.conf_threshold) continue;

        /* Box in model pixel space → scale to frame pixels */
        float cx = floatarr[0 * NA + j] * scale_x;
        float cy = floatarr[1 * NA + j] * scale_y;
        float w  = floatarr[2 * NA + j] * scale_x;
        float h  = floatarr[3 * NA + j] * scale_y;

        detection d;
        d.bbox = { cx, cy, w, h };
        d.c    = pred_cls;
        d.prob = max_cls;
        /* Skip degenerate / NaN boxes that crash cv::rectangle on aarch64 */
        if (w < 1.f || h < 1.f || !std::isfinite(w) || !std::isfinite(h) ||
            !std::isfinite(cx) || !std::isfinite(cy)) continue;
        dets.push_back(d);
    }

    filter_boxes_nms(dets, static_cast<int32_t>(dets.size()), m_cfg.nms_threshold);
    dets.erase(std::remove_if(dets.begin(), dets.end(),
               [](const detection& d){ return d.prob == 0.f; }), dets.end());
}

/* ── postProcessAnchored (YOLOv3 / YOLOv5) ─────────────────────────────── */
void TrafficDetector::postProcessAnchored(const float* floatarr,
                                           int frame_w, int frame_h,
                                           std::vector<detection>& dets) const
{
    dets.clear();

    int num_layers  = (int)m_cfg.grids.size();
    int NUM_CLASS   = m_cfg.num_class;
    int NUM_BB      = m_cfg.num_bb;

    /* correct_yolo_boxes scale (darknet-style letterbox correction) */
    float new_w = (float)m_cfg.input_width;
    float new_h = (float)m_cfg.input_height;

    for (int n = 0; n < num_layers; n++)
    {
        int  num_grid     = m_cfg.grids[n];
        int  anchor_offset = 2 * NUM_BB * (num_layers - (n + 1));

        for (int b = 0; b < NUM_BB; b++)
        {
            for (int y = 0; y < num_grid; y++)
            {
                for (int x = 0; x < num_grid; x++)
                {
                    int offs = yolo_offset(n, b, y, x);
                    float tc = floatarr[yolo_index(n, offs, 4)];
                    float tx = floatarr[offs];
                    float ty = floatarr[yolo_index(n, offs, 1)];
                    float tw = floatarr[yolo_index(n, offs, 2)];
                    float th = floatarr[yolo_index(n, offs, 3)];

                    /* Decode center + size */
                    float cx = ((float)x + (float)sigmoid(tx)) / (float)num_grid;
                    float cy = ((float)y + (float)sigmoid(ty)) / (float)num_grid;
                    float bw = (float)std::exp(tw) * m_cfg.anchors[anchor_offset + 2*b    ] / (float)m_cfg.input_width;
                    float bh = (float)std::exp(th) * m_cfg.anchors[anchor_offset + 2*b + 1] / (float)m_cfg.input_height;

                    /* correct_yolo_boxes (letterbox compensation) */
                    cx = (cx - (m_cfg.input_width  - new_w) / 2.f / m_cfg.input_width)
                         / (new_w / m_cfg.input_width);
                    cy = (cy - (m_cfg.input_height - new_h) / 2.f / m_cfg.input_height)
                         / (new_h / m_cfg.input_height);
                    bw *= (float)(m_cfg.input_width  / new_w);
                    bh *= (float)(m_cfg.input_height / new_h);

                    /* Scale to frame pixels */
                    cx = std::round(cx * frame_w);
                    cy = std::round(cy * frame_h);
                    bw = std::round(bw * frame_w);
                    bh = std::round(bh * frame_h);

                    float objectness = (float)sigmoid(tc);

                    /* Class with max probability */
                    float max_prob = 0.f;
                    int   pred_cls = -1;
                    for (int i = 0; i < NUM_CLASS; i++) {
                        float p = (float)sigmoid(floatarr[yolo_index(n, offs, 5 + i)]);
                        if (p > max_prob) { max_prob = p; pred_cls = i; }
                    }

                    float prob = max_prob * objectness;
                    if (prob > m_cfg.conf_threshold)
                    {
                        if (bw < 1.f || bh < 1.f || !std::isfinite(bw) || !std::isfinite(bh)) continue;
                        detection d;
                        d.bbox = {cx, cy, bw, bh};
                        d.c    = pred_cls;
                        d.prob = prob;
                        dets.push_back(d);
                    }
                }
            }
        }
    }
    /* NMS filter */
    filter_boxes_nms(dets, (int32_t)dets.size(), m_cfg.nms_threshold);
    /* Remove suppressed (prob == 0) */
    dets.erase(std::remove_if(dets.begin(), dets.end(),
               [](const detection& d){ return d.prob == 0.f; }), dets.end());
}

/* ── detect ──────────────────────────────────────────────────────────────── */
std::vector<detection> TrafficDetector::detect(const cv::Mat& frame)
{
    std::vector<detection> dets;
    if (frame.empty()) return dets;

#ifndef WITH_DRP
    /* ── Stub mode: trả về empty (test pipeline structure) ── */
    return dets;
#else
    if (!m_runtime_ok) return dets;

    auto t0 = std::chrono::steady_clock::now();
    m_prep_ms = 0.f;

    /* ── Preprocessing ──────────────────────────────────────────────────────── */
    if (m_pre_ok && m_drpai_buf)
    {
        /* DRP PreRuntime with proper MMNGR DMA buffer (like R01_object_detection).
         * Steps (R01 pattern):
         *   1. Copy RAW camera frame (640x480 BGR) to DMA buffer virtual addr (NO pre-resize)
         *      PreRuntime was compiled with IMG_IWIDTH=640, IMG_IHEIGHT=480 →
         *      it does the resize to model input size (640x640) internally.
         *   2. Flush CPU cache so physical address is coherent
         *   3. Pass: phy_addr, pre_in_shape_w=frame.cols, pre_in_shape_h=frame.rows */
        /* ── Resize frame to sq×sq before DMA copy ────────────────────────────
         * The PreRuntime binary expects pre_in_shape_w=sq, pre_in_shape_h=sq
         * (640×640).  DMA buffer = sq×sq×3 bytes.  Row stride must be sq×3.
         *
         * OLD pad approach produced stride 960×3=2880 bytes/row while PreRuntime
         * expected 640×3=1920 bytes/row → corrupted input → zero detections.
         *
         * FIX: cv::resize to sq×sq so stride = sq×3 exactly.
         * postProcessYolov8 applies scale_x=frame.cols/sq, scale_y=frame.rows/sq
         * to map boxes back to frame pixel space → aspect-ratio compensated.   */
        auto tp = std::chrono::steady_clock::now();
        const int sq = m_cfg.input_width;
        cv::Mat sq_frame;
        cv::resize(frame, sq_frame, cv::Size(sq, sq), 0, 0, cv::INTER_LINEAR);
        /* sq_frame: sq×sq BGR, stride = sq*3 bytes — exactly what PreRuntime needs */
        memcpy(m_drpai_buf->mem, sq_frame.data, (size_t)sq * sq * 3);

        int fr = buffer_flush_dmabuf(m_drpai_buf->idx, m_drpai_buf->size);
        if (fr != 0)
            std::cerr << "[TrafficDetector] buffer_flush_dmabuf failed (ret=" << fr << ")\n";

        s_preproc_param_t pre_param;
        /* Always supply SQUARE shape = input_width × input_width (R01 style). */
        pre_param.pre_in_shape_w = (uint16_t)sq;
        pre_param.pre_in_shape_h = (uint16_t)sq;
        pre_param.pre_in_addr    = (uintptr_t)m_drpai_buf->phy_addr;

        void*    out_ptr  = nullptr;
        uint32_t out_size = 0;
        uint8_t r = m_preruntime.Pre(&pre_param, &out_ptr, &out_size);
        m_prep_ms = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - tp).count();
        if (r != 0 || out_ptr == nullptr) {
            /* PreRuntime failed → fall back to CPU */
            std::cerr << "[TrafficDetector] PreRuntime.Pre failed (r=" << (int)r
                      << ") → CPU fallback\n";
            std::vector<float> cpu_in;
            cpuPreprocess(frame, cpu_in);
            m_runtime.SetInput(0, cpu_in.data());
        } else {
            /* Match R01_object_detection: avoid an extra FP32 tensor copy here. */
            m_runtime.SetInput(0, reinterpret_cast<float*>(out_ptr));
        }
    }
    else
    {
        /* CPU preprocessing */
        std::vector<float> cpu_in;
        auto tp = std::chrono::steady_clock::now();
        cpuPreprocess(frame, cpu_in);
        m_prep_ms = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - tp).count();
        m_runtime.SetInput(0, cpu_in.data());
    }

    auto t1 = std::chrono::steady_clock::now();

    /* ── Inference ──────────────────────────────────────────────────────────── */
    m_runtime.Run(m_cfg.drpai_freq);
    auto t2 = std::chrono::steady_clock::now();
    m_infer_ms = std::chrono::duration<float, std::milli>(t2 - t1).count();

    /* ── Collect output ─────────────────────────────────────────────────────── */
    int num_out = m_runtime.GetNumOutput();
    int count   = 0;
    for (int i = 0; i < num_out; i++)
    {
        auto out = m_runtime.GetOutput(i);
        int64_t sz  = std::get<2>(out);
        InOutDataType dtype = std::get<0>(out);

        /* Safety: expand buffer on-the-fly if model output exceeds our estimate.
         * This prevents silent heap-overflow-induced heap corruption. */
        if (count + sz > (int64_t)m_output_buf.size()) {
            std::cerr << "[TrafficDetector][WARN] output buf too small ("
                      << m_output_buf.size() << "), expanding to "
                      << (count + sz) << "\n";
            m_output_buf.resize(count + sz, 0.f);
        }

        if (dtype == InOutDataType::FLOAT16) {
            uint16_t* ptr = reinterpret_cast<uint16_t*>(std::get<1>(out));
            for (int64_t j = 0; j < sz; j++)
                m_output_buf[count + j] = fp16_to_fp32(ptr[j]);
        } else {
            float* ptr = reinterpret_cast<float*>(std::get<1>(out));
            for (int64_t j = 0; j < sz; j++)
                m_output_buf[count + j] = ptr[j];
        }
        count += (int)sz;
    }

    /* ── Post-processing ────────────────────────────────────────────────────── */
    postProcess(m_output_buf.data(), frame.cols, frame.rows, dets);
    return dets;
#endif
}
