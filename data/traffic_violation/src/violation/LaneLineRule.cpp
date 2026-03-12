/*******************************************************************************
 * traffic_violation/src/violation/LaneLineRule.cpp
 * Kiểm tra xe đè vạch kẻ đường – dùng vạch tĩnh từ config.yaml
 *
 * Logic: Xe vi phạm khi bbox của xe straddling qua vạch.
 *   - Gọi vị trí vạch (pixel): x_line = x_norm * frame.cols
 *   - left_overlap  = x_line - (bbox.x - bbox.w/2)
 *   - right_overlap = (bbox.x + bbox.w/2) - x_line
 *   - Vi phạm khi cả 2 vế >= overlap_px (xe thực sự nằm trên vạch)
 ******************************************************************************/
#include "LaneLineRule.h"
#include <cmath>

LaneLineRule::LaneLineRule(const SceneConfig& scene)
    : m_lines(scene.lane_lines)
{}

std::vector<ViolationEvent> LaneLineRule::check(const FrameContext& ctx) {
    std::vector<ViolationEvent> events;
    if (m_lines.empty() || ctx.frame.empty()) return events;

    int w = ctx.frame.cols;
    int h = ctx.frame.rows;

    for (const auto& v : ctx.vehicles) {
        /* Pixel bbox edges */
        float x_left  = v.bbox.x - v.bbox.w / 2.f;
        float x_right = v.bbox.x + v.bbox.w / 2.f;

        bool violated = false;
        int  hit_line_px = 0;

        for (const auto& ll : m_lines) {
            float x_line = ll.x_norm * (float)w;

            float left_overlap  = x_line  - x_left;
            float right_overlap = x_right - x_line;

            if (left_overlap  >= (float)ll.overlap_px &&
                right_overlap >= (float)ll.overlap_px)
            {
                violated = true;
                hit_line_px = static_cast<int>(x_line);
                break;   /* vi phạm vạch đầu tiên, không cần kiểm tra tiếp */
            }
        }

        if (!violated) continue;

        ViolationEvent ev;
        ev.type         = ViolationType::LANE_LINE_CROSS;
        ev.track_id     = v.track_id;
        ev.plate        = v.plate.empty()
                          ? "UNKNOWN_T" + std::to_string(v.track_id)
                          : v.plate;
        ev.vehicle_rect = v.bbox.toSafeRect(ctx.frame.cols, ctx.frame.rows);
        if (ev.vehicle_rect.area() == 0) ev.vehicle_rect = cv::Rect(0,0,1,1);
        ev.timestamp    = std::chrono::system_clock::now();
        ctx.frame.copyTo(ev.frame);

        /* Vẽ vạch vi phạm lên frame ảnh lưu */
        if (!ev.frame.empty()) {
            cv::line(ev.frame,
                     cv::Point(hit_line_px, 0),
                     cv::Point(hit_line_px, h),
                     cv::Scalar(0, 165, 255), 3);   /* cam */
            cv::putText(ev.frame, "LANE LINE",
                        cv::Point(hit_line_px + 4, 30),
                        cv::FONT_HERSHEY_SIMPLEX, 0.8,
                        cv::Scalar(0, 165, 255), 2);
        }
        events.push_back(std::move(ev));
    }
    return events;
}
