/*******************************************************************************
 * traffic_violation/src/violation/HelmetRule.cpp
 * Kiểm tra người lái xe máy không đội mũ bảo hiểm
 ******************************************************************************/
#include "HelmetRule.h"

std::vector<ViolationEvent> HelmetRule::check(const FrameContext& ctx) {
    std::vector<ViolationEvent> events;

    for (const auto& v : ctx.vehicles) {
        // Chỉ áp dụng cho xe máy
        if (v.type != VehicleType::MOTORBIKE) continue;

        if (!v.has_helmet) {
            ViolationEvent ev;
            ev.type         = ViolationType::NO_HELMET;
            ev.track_id     = v.track_id;
            ev.plate        = v.plate.empty()
                              ? "UNKNOWN_T" + std::to_string(v.track_id)
                              : v.plate;
            ev.vehicle_rect = v.bbox.toRect();
            ev.timestamp    = std::chrono::system_clock::now();
            ctx.frame.copyTo(ev.frame);
            events.push_back(std::move(ev));
        }
    }
    return events;
}
