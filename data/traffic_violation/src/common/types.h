/*******************************************************************************
 * traffic_violation/src/common/types.h
 * Shared types − R01-compatible Box/detection + domain types
 ******************************************************************************/
#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

/* ─── Bounding box (center-x, center-y, width, height) in pixels ─────────── */
struct Box {
    float x, y, w, h;
    cv::Rect toRect() const {
        int rw = static_cast<int>(w); if (rw < 1) rw = 1;
        int rh = static_cast<int>(h); if (rh < 1) rh = 1;
        return cv::Rect(static_cast<int>(x - w/2), static_cast<int>(y - h/2), rw, rh);
    }
    cv::Rect toSafeRect(int fw, int fh) const {
        cv::Rect r = toRect();
        return r & cv::Rect(0, 0, fw, fh);
    }
    bool isValid() const { return w > 0.5f && h > 0.5f; }
    float intersectArea(const Box& o) const {
        float ix1 = std::max(x-w/2, o.x-o.w/2), iy1 = std::max(y-h/2, o.y-o.h/2);
        float ix2 = std::min(x+w/2, o.x+o.w/2), iy2 = std::min(y+h/2, o.y+o.h/2);
        return std::max(0.f,ix2-ix1)*std::max(0.f,iy2-iy1);
    }
};

/* ─── detection − R01-compatible (lowercase, Box + class_id + prob) ───────── */
typedef struct detection {
    Box     bbox;
    int32_t c;      /* class id                       */
    float   prob;   /* objectness × class_probability */
} detection;

/* Alias: code cũ dùng "Detection" (uppercase) vẫn compile */
using Detection = detection;

/* ─── Vehicle type ──────────────────────────────────────────────────────────── */
enum class VehicleType { UNKNOWN, MOTORBIKE, CAR, TRUCK, BUS };

/* ─── Tracked vehicle ───────────────────────────────────────────────────────── */
struct TrackedVehicle {
    int         track_id;
    VehicleType type;
    Box         bbox;
    std::string plate;
    bool        has_helmet{true};
    int         missed_frames{0};
    std::vector<cv::Point2f> trajectory;
};

/* ─── Violation ─────────────────────────────────────────────────────────────── */
enum class ViolationType { NO_HELMET, RED_LIGHT, WRONG_LANE, LANE_LINE_CROSS };

inline std::string violationName(ViolationType v) {
    switch (v) {
        case ViolationType::NO_HELMET:       return "NO_HELMET";
        case ViolationType::RED_LIGHT:       return "RED_LIGHT";
        case ViolationType::WRONG_LANE:      return "WRONG_LANE";
        case ViolationType::LANE_LINE_CROSS: return "LANE_LINE_CROSS";
        default:                             return "UNKNOWN";
    }
}

struct ViolationEvent {
    ViolationType       type;
    int                 track_id;
    std::string         plate;
    cv::Mat             frame;
    cv::Rect            vehicle_rect;
    std::chrono::system_clock::time_point timestamp;
};

/* ─── Traffic light, Frame context ─────────────────────────────────────────── */
struct TrafficLightState { bool red = false; };

struct FrameContext {
    cv::Mat                     frame;
    double                      frame_ts_sec{0.};
    std::vector<TrackedVehicle> vehicles;
    TrafficLightState           light;
};
