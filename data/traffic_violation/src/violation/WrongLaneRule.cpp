/*******************************************************************************
 * traffic_violation/src/violation/WrongLaneRule.cpp
 * Kiểm tra xe đi sai làn đường
 ******************************************************************************/
#include "WrongLaneRule.h"

#include <chrono>

WrongLaneRule::WrongLaneRule(const SceneConfig& scene)
    : m_zones(scene.lane_zones)
{}

void WrongLaneRule::pruneStaleTracks(const std::vector<TrackedVehicle>& vehicles) {
    auto now = std::chrono::steady_clock::now();
    for (auto it = m_history.begin(); it != m_history.end(); ) {
        bool found = false;
        for (const auto& v : vehicles) {
            if (v.track_id == it->first) { found = true; break; }
        }
        
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_seen).count();
        if (!found && elapsed > 2) {
            it = m_history.erase(it);
        } else {
            ++it;
        }
    }
}

std::string WrongLaneRule::vehicleTypeStr(VehicleType t) {
    switch (t) {
        case VehicleType::MOTORBIKE: return "motorbike";
        case VehicleType::CAR:       return "car";
        case VehicleType::TRUCK:     return "truck";
        case VehicleType::BUS:       return "truck"; // dùng lane của truck
        default:                     return "";
    }
}

std::vector<ViolationEvent> WrongLaneRule::check(const FrameContext& ctx) {
    std::vector<ViolationEvent> events;
    if (ctx.frame.empty()) return events;

    int w = ctx.frame.cols;
    pruneStaleTracks(ctx.vehicles);

    for (const auto& v : ctx.vehicles) {
        std::string type_key = vehicleTypeStr(v.type);
        if (type_key.empty()) continue;

        auto it = m_zones.find(type_key);
        if (it == m_zones.end()) continue;

        // Centroid 
        int cx = v.bbox.x;
        int cy = v.bbox.y + v.bbox.h / 2.0f; // Tâm gầm xe để vẽ cho chính xác
        float cx_norm = v.bbox.x / (float)w;

        bool in_lane = (cx_norm >= it->second.x_min) &&
                       (cx_norm <= it->second.x_max);

        auto& hist = m_history[v.track_id];
        hist.last_seen = std::chrono::steady_clock::now();
        
        if (!in_lane) {
            hist.is_violating = true;
        }

        // Chỉ apply tracking nếu vi phạm
        if (hist.is_violating) {
            hist.trail.push_back(cv::Point(cx, cy));
            if (hist.trail.size() > 50) hist.trail.pop_front();
        }

        if (!in_lane) {
            ViolationEvent ev;
            ev.type         = ViolationType::WRONG_LANE;
            ev.track_id     = v.track_id;
            ev.plate        = v.plate.empty()
                              ? "UNKNOWN_T" + std::to_string(v.track_id)
                              : v.plate;
            ev.vehicle_rect = v.bbox.toSafeRect(ctx.frame.cols, ctx.frame.rows);
            if (ev.vehicle_rect.area() == 0) ev.vehicle_rect = cv::Rect(0,0,1,1);
            ev.timestamp    = std::chrono::system_clock::now();
            ctx.frame.copyTo(ev.frame);

            // Vẽ overlay vi phạm: bounding box màu xanh mạ
            if (!ev.frame.empty()) {
                int x0 = static_cast<int>(it->second.x_min * w);
                int x1 = static_cast<int>(it->second.x_max * w);
                cv::line(ev.frame, {x0, 0}, {x0, ev.frame.rows}, cv::Scalar(255, 165, 0), 2);
                cv::line(ev.frame, {x1, 0}, {x1, ev.frame.rows}, cv::Scalar(255, 165, 0), 2);

                cv::rectangle(ev.frame, ev.vehicle_rect, cv::Scalar(0, 255, 0), 2);
                
                // Vẽ chấm đỏ và trail
                if (hist.trail.size() > 1) {
                    for (size_t i = 1; i < hist.trail.size(); ++i) {
                        cv::line(ev.frame, hist.trail[i-1], hist.trail[i], cv::Scalar(0, 0, 255), 2);
                    }
                }
                cv::circle(ev.frame, cv::Point(cx, cy), 5, cv::Scalar(0, 0, 255), -1); // Chấm đỏ
            }
            events.push_back(std::move(ev));
        }
    }
    return events;
}
