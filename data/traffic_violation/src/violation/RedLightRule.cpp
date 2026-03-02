/*******************************************************************************
 * traffic_violation/src/violation/RedLightRule.cpp
 * Kiểm tra xe vượt qua stop-line khi đèn đỏ
 ******************************************************************************/
#include "RedLightRule.h"

RedLightRule::RedLightRule(const SceneConfig& scene)
    : m_stop_line_y1(scene.stop_line_y1)
    , m_stop_line_y2(scene.stop_line_y2)
{}

std::vector<ViolationEvent> RedLightRule::check(const FrameContext& ctx) {
    std::vector<ViolationEvent> events;
    if (!ctx.light.red) return events;  // Đèn không đỏ → bỏ qua
    if (ctx.frame.empty()) return events;

    int h = ctx.frame.rows;
    float stop_y1_px = m_stop_line_y1 * h;
    float stop_y2_px = m_stop_line_y2 * h;

    for (const auto& v : ctx.vehicles) {
        // Centroid y của xe (dưới cùng của bbox)
        float bottom_y = v.bbox.y + v.bbox.h / 2.f;
        float center_y = v.bbox.y;

        // Xe vượt qua stop-line khi bottom của bbox đã qua y1
        // và centroid đang trong vùng stop-line hoặc đã qua
        bool crossed = (center_y > stop_y1_px) && (bottom_y > stop_y2_px);

        if (crossed) {
            ViolationEvent ev;
            ev.type         = ViolationType::RED_LIGHT;
            ev.track_id     = v.track_id;
            ev.plate        = v.plate.empty()
                              ? "UNKNOWN_T" + std::to_string(v.track_id)
                              : v.plate;
            ev.vehicle_rect = v.bbox.toRect();
            ev.timestamp    = std::chrono::system_clock::now();
            ctx.frame.copyTo(ev.frame);

            // Vẽ stop-line lên frame
            if (!ev.frame.empty()) {
                int sy = static_cast<int>((m_stop_line_y1 + m_stop_line_y2) / 2 * h);
                cv::line(ev.frame, {0, sy}, {ev.frame.cols, sy},
                         cv::Scalar(0, 0, 255), 3);
            }
            events.push_back(std::move(ev));
        }
    }
    return events;
}
