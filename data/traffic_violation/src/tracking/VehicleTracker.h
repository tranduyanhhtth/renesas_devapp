/*******************************************************************************
 * traffic_violation/src/tracking/VehicleTracker.h
 * IoU-based tracker – nhận detection từ TrafficDetector (single model)
 ******************************************************************************/
#pragma once
#include "common/types.h"
#include "common/config.h"
#include <vector>
#include <unordered_map>
#include <opencv2/video/tracking.hpp>

class VehicleTracker {
public:
    VehicleTracker(const DetectorConfig& det_cfg,
                   int   max_missed_frames = 5,
                   float iou_threshold     = 0.35f);

    /* Cập nhật tracker với vehicle detections (chỉ class vehicle/person).
     * Input: ALL detections từ model, tracker tự filter vehicle class.
     * Trả về danh sách track hiện tại. */
    const std::vector<TrackedVehicle>& update(
        const std::vector<detection>& all_dets,
        int frame_width, int frame_height);

    void setPlate (int track_id, const std::string& plate);
    void setHelmet(int track_id, bool has_helmet);

    const std::vector<TrackedVehicle>& tracks() const { return m_tracks; }

private:
    static float computeIou(const Box& a, const Box& b);
    VehicleType  classToType(int class_id) const;
    void initKalman(int track_id, float cx, float cy);
    cv::Point2f predictCenter(int track_id, const cv::Point2f& fallback);
    cv::Point2f correctCenter(int track_id, float cx, float cy);

    DetectorConfig m_det;
    int   m_max_missed;
    float m_iou_thresh;
    float m_dist_thresh_px{90.f};
    int   m_next_id{0};
    std::vector<TrackedVehicle> m_tracks;
    std::unordered_map<int, cv::KalmanFilter> m_kalman;
    std::unordered_map<int, bool> m_kalman_inited;
};
