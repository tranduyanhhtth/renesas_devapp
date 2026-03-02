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

/* ── helper: FP16 → FP32 (giống R01 dùng builtin) ──────────────────────────── */
#ifdef WITH_DRP
#include <builtin_fp16.h>
static inline float fp16_to_fp32(uint16_t a) {
    return __extendXfYf2__<uint16_t, uint16_t, 10, float, uint32_t, 23>(a);
}
#endif

/* ─────────────────────────────────────────────────────────────────────────── */
TrafficDetector::TrafficDetector(const DetectorConfig& cfg)
    : m_cfg(cfg)
{
    /* Compute output buffer size từ YOLO params */
    m_inf_out_size = 0;
    for (int g : m_cfg.grids)
        m_inf_out_size += (m_cfg.num_class + 5) * m_cfg.num_bb * g * g;
}

/* ── load ─────────────────────────────────────────────────────────────────── */
bool TrafficDetector::load(uint64_t drpai_base_addr)
{
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
    if (!m_cfg.pre_dir.empty()) {
        int ret = m_preruntime.Load(m_cfg.pre_dir);
        if (ret != 0) {
            std::cerr << "[TrafficDetector] PreRuntime Load failed (pre_dir="
                      << m_cfg.pre_dir << ") ret=" << ret << "\n";
            m_pre_ok = false;
            /* Fall back to CPU preproc – non-fatal */
        } else {
            m_pre_ok = true;
            std::cout << "[TrafficDetector] PreRuntime loaded: " << m_cfg.pre_dir << "\n";
        }
    }

    /* ── DRP-AI TVM Runtime ─────────────────────────────────────────────────── */
    bool ok = m_runtime.LoadModel(m_cfg.model_dir, drpai_base_addr);
    if (!ok) {
        std::cerr << "[TrafficDetector] LoadModel failed: " << m_cfg.model_dir << "\n";
        m_runtime_ok = false;
        return false;
    }
    m_runtime_ok = true;
    std::cout << "[TrafficDetector] Model loaded: " << m_cfg.model_dir << "\n";
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

/* ── postProcess ─────────────────────────────────────────────────────────── */
void TrafficDetector::postProcess(const float* floatarr,
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

    /* ── Preprocessing ──────────────────────────────────────────────────────── */
    if (m_pre_ok)
    {
        /* DRP PreRuntime: resize+normalize trên hardware */
        s_preproc_param_t pre_param;
        pre_param.pre_in_addr = (uint64_t)frame.data;  /* BGR frame từ VideoCapture */

        void*    out_ptr  = nullptr;
        uint32_t out_size = 0;
        uint8_t r = m_preruntime.Pre(&pre_param, &out_ptr, &out_size);
        if (r != 0) {
            /* PreRuntime failed → fall back to CPU */
            std::vector<float> cpu_in;
            cpuPreprocess(frame, cpu_in);
            m_runtime.SetInput(0, cpu_in.data());
        } else {
            m_runtime.SetInput(0, (float*)out_ptr);
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
        if (dtype == InOutDataType::FLOAT16) {
            uint16_t* ptr = reinterpret_cast<uint16_t*>(std::get<1>(out));
            for (int j = 0; j < sz; j++)
                m_output_buf[count + j] = fp16_to_fp32(ptr[j]);
        } else {
            float* ptr = reinterpret_cast<float*>(std::get<1>(out));
            for (int j = 0; j < sz; j++)
                m_output_buf[count + j] = ptr[j];
        }
        count += sz;
    }

    /* ── Post-processing ────────────────────────────────────────────────────── */
    postProcess(m_output_buf.data(), frame.cols, frame.rows, dets);
    return dets;
#endif
}
