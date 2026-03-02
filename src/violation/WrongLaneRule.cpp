/*******************************************************************************
 * traffic_violation/src/violation/WrongLaneRule.cpp
 * Kiểm tra xe đi sai làn đường
 ******************************************************************************/
#include "WrongLaneRule.h"

WrongLaneRule::WrongLaneRule(const SceneConfig& scene)
    : m_zones(scene.lane_zones)
{}

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

    for (const auto& v : ctx.vehicles) {
        std::string type_key = vehicleTypeStr(v.type);
        if (type_key.empty()) continue;

        auto it = m_zones.find(type_key);
        if (it == m_zones.end()) continue;

        // Centroid X (normalized)
        float cx_norm = v.bbox.x / (float)w;

        bool in_lane = (cx_norm >= it->second.x_min) &&
                       (cx_norm <= it->second.x_max);

        if (!in_lane) {
            ViolationEvent ev;
            ev.type         = ViolationType::WRONG_LANE;
            ev.track_id     = v.track_id;
            ev.plate        = v.plate.empty()
                              ? "UNKNOWN_T" + std::to_string(v.track_id)
                              : v.plate;
            ev.vehicle_rect = v.bbox.toRect();
            ev.timestamp    = std::chrono::system_clock::now();
            ctx.frame.copyTo(ev.frame);

            // Vẽ lane boundaries lên frame
            if (!ev.frame.empty()) {
                int x0 = static_cast<int>(it->second.x_min * w);
                int x1 = static_cast<int>(it->second.x_max * w);
                cv::line(ev.frame, {x0, 0}, {x0, ev.frame.rows},
                         cv::Scalar(255, 165, 0), 2);
                cv::line(ev.frame, {x1, 0}, {x1, ev.frame.rows},
                         cv::Scalar(255, 165, 0), 2);
            }
            events.push_back(std::move(ev));
        }
    }
    return events;
}
