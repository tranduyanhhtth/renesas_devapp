/*******************************************************************************
 * traffic_violation/src/models/TrafficDetector.h
 *
 * Unified YOLO detector cho tất cả object (vehicle, person, helmet, LP).
 * Kiến trúc giống R01_object_detection − 1 model, 1 luồng inference.
 *
 * Tham số model (num_class, anchors, grids) load từ config.yaml → linh hoạt
 * thay đổi sau khi training xong mà không cần recompile.
 ******************************************************************************/
#pragma once
#include "box.h"
#include "common/config.h"
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>

#ifdef WITH_DRP
#include "MeraDrpRuntimeWrapper.h"
#include "PreRuntime.h"
#endif

class TrafficDetector
{
public:
    explicit TrafficDetector(const DetectorConfig& cfg);
    ~TrafficDetector() = default;

    /* Load model. Trả về false nếu thất bại (app vẫn có thể chạy stub). */
    bool load(uint64_t drpai_base_addr = 0);

    /* Chạy inference trên frame (BGR, bất kỳ resolution). */
    std::vector<detection> detect(const cv::Mat& frame);

    float lastInferMs() const { return m_infer_ms; }
    float lastPrepMs()  const { return m_prep_ms; }

    const std::vector<std::string>& labels() const { return m_labels; }
    const DetectorConfig& cfg() const { return m_cfg; }

private:
    /* CPU preprocessing: BGR → resize → chuyển HWC→CHW → /255 */
    void cpuPreprocess(const cv::Mat& src, std::vector<float>& dst) const;

    /* YOLO post-processing: dispatch based on model_type */
    void postProcess(const float* buf, int frame_w, int frame_h,
                     std::vector<detection>& dets) const;

    /* Anchor-based decode (YOLOv3 / YOLOv5) */
    void postProcessAnchored(const float* buf, int frame_w, int frame_h,
                             std::vector<detection>& dets) const;

    /* Anchor-free decode (YOLOv8) – output [num_class+4, total_anchors] */
    void postProcessYolov8(const float* buf, int frame_w, int frame_h,
                           std::vector<detection>& dets) const;

    static double sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }

    /* Offset helpers (R01 style) */
    int32_t yolo_offset(int n, int b, int y, int x) const;
    int32_t yolo_index (int n, int offs, int channel) const;

    DetectorConfig m_cfg;
    std::vector<std::string> m_labels;
    int m_inf_out_size{0};

    float m_infer_ms{0.f};
    float m_prep_ms {0.f};

#ifdef WITH_DRP
    MeraDrpRuntimeWrapper m_runtime;
    PreRuntime            m_preruntime;
    bool                  m_runtime_ok{false};
    bool                  m_pre_ok    {false};
    std::vector<float>    m_output_buf;
#endif
};
