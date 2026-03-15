/*******************************************************************************
 * traffic_violation/src/violation/RedLightRule.cpp
 * Kiểm tra xe vượt qua stop-line khi đèn đỏ
 * Camera đặt sau xe, xe đi từ dưới lên
 * 
 ******************************************************************************/
#include "RedLightRule.h"
#include <numeric>
#include <cmath>
#include <iostream>
 
RedLightRule::RedLightRule(const SceneConfig& scene)
    : m_stop_line_y1(scene.stop_line_y1)
    , m_stop_line_y2(scene.stop_line_y2)
    , m_stop_line_y (scene.stop_line_y2) // Đè vạch (chạm mép dưới) là vi phạm ngay
{}
 
std::vector<ViolationEvent> RedLightRule::check(const FrameContext& ctx) {
    std::vector<ViolationEvent> events;
 
    if (!ctx.light.red)      return events;   
    if (ctx.frame.empty())   return events;
 
    const float frame_h  = static_cast<float>(ctx.frame.rows);
    const float frame_w  = static_cast<float>(ctx.frame.cols);
    const float stop_px  = m_stop_line_y * frame_h;  
 
    // Dọn dẹp xe đã biến mất khỏi scene
    pruneStaleTracks(ctx);
 
    for (const auto& v : ctx.vehicles) {
        float cx       = v.bbox.x;                    // tâm X
        float top_y    = v.bbox.y - v.bbox.h / 2.f;  // cạnh trên
        float bottom_y = v.bbox.y + v.bbox.h / 2.f;  // cạnh dưới (gầm xe) ← lưu vào trajectory và check crossing
 
        // ── Cập nhật history ──────────────────────────────────────────────────
        auto& hist = m_history[v.track_id];
        hist.last_seen = std::chrono::steady_clock::now();
        hist.cy_history.push_back(bottom_y);
        if ((int)hist.cy_history.size() > HISTORY_LEN)
            hist.cy_history.pop_front();
 
        // Cần ít nhất 3 frame để tính velocity tin cậy
        if ((int)hist.cy_history.size() < 3) {
            // Khởi tạo trạng thái ban đầu: xe còn ở dưới stop-line?
            hist.was_before_line = (bottom_y > stop_px);
            continue;
        }
 
        // ── Tính velocity Y (pixel/frame, dương = đi xuống) ──────────────────
        float vy = estimateVelocityY(hist.cy_history);
 
        // ── Điều kiện 1: Xe phải đang chuyển động lên ──────────────────────
        if (vy > -MIN_MOVE_PX) {
            // Xe đứng yên hoặc đang lùi → không vi phạm
            hist.was_before_line = (bottom_y > stop_px);
            continue;
        }
 
        // ── Điều kiện 2: Xe phải VƯỢT QUA hoặc ĐÈ lên stop-line ───────────────
        // Vùng đỏ là từ y1 đến y2. Nếu `bottom_y` (gầm xe) nằm trong vùng [y1, y2]
        // hoặc đã vượt qua y1, thì coi là đè vạch/vượt đèn.
        // Chú ý: Trục y tăng dần từ trên xuống dưới.
        float y1_px = m_stop_line_y1 * frame_h;
        float y2_px = m_stop_line_y2 * frame_h;
        
        // Cạnh dưới (gầm xe) phải lọt vào hoặc qua vạch trên (y1_px)
        bool is_violating_now = (bottom_y <= y2_px);
 
        bool just_crossed = hist.was_before_line && is_violating_now;
 
        // Cập nhật trạng thái cho frame sau
        hist.was_before_line = !is_violating_now;
 
        if (!just_crossed) continue;
 
        // ── Điều kiện 3: Chạm vạch hoặc đè vạch là vi phạm ───────────────────
        // Không yêu cầu phải vượt sâu qua vạch nữa, loại bỏ check CROSS_CONFIRM_Y
 
        // ── Điều kiện 4: Không tính xe đang đỗ/dừng sẵn sau vạch ────────────
        // Nếu ngay từ frame đầu xe đã ở dưới vạch → không phải crossing
        if (hist.has_crossed) continue;  // đã ghi nhận crossing này rồi
        hist.has_crossed = true;
 
        // ── Tạo ViolationEvent ────────────────────────────────────────────────
        ViolationEvent ev;
        ev.type         = ViolationType::RED_LIGHT;
        ev.track_id     = v.track_id;
        ev.plate        = v.plate.empty()
                          ? "UNKNOWN_T" + std::to_string(v.track_id)
                          : v.plate;
        ev.vehicle_rect = v.bbox.toRect();
        ev.timestamp    = std::chrono::system_clock::now();
        ctx.frame.copyTo(ev.frame);
 
        // Vẽ overlay lên frame bằng chứng
        if (!ev.frame.empty()) {
            int stop_y_px = static_cast<int>(stop_px);
            drawViolationOverlay(ev.frame, stop_y_px);
 
            // Vẽ bbox xe vi phạm
            cv::rectangle(ev.frame,
                          v.bbox.toRect(),
                          cv::Scalar(0, 0, 255), 2);
 
            // Label biển số + tốc độ (đặt phía trên bbox)
            std::string label = ev.plate;
            cv::putText(ev.frame, label,
                        cv::Point((int)(v.bbox.x), (int)(top_y) - 8),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6,
                        cv::Scalar(0, 0, 255), 2);
        }
 
        events.push_back(std::move(ev));
 
        std::cout << "[RedLightRule] CROSS DETECTED"
                  << "  track=" << v.track_id
                  << "  plate=" << ev.plate
                  << "  vy="    << vy << "px/frame"
                  << "  bottom_y=" << bottom_y << "  stop_px=" << stop_px
                  << "\n";
    }
 
    return events;
}
 
// ── Helpers ───────────────────────────────────────────────────────────────────
 
/**
 * Xóa state của các track_id không còn xuất hiện trong frame hiện tại
 * hoặc đã biến mất quá 2 giây (tránh m_history phình to)
 */
void RedLightRule::pruneStaleTracks(const FrameContext& ctx) {
    auto now = std::chrono::steady_clock::now();
    constexpr double STALE_SEC = 2.0;
 
    for (auto it = m_history.begin(); it != m_history.end(); ) {
        // Kiểm tra track còn trong frame không
        bool found = false;
        for (const auto& v : ctx.vehicles)
            if (v.track_id == it->first) { found = true; break; }
 
        // Kiểm tra timeout
        double elapsed = std::chrono::duration<double>(
            now - it->second.last_seen).count();
 
        if (!found && elapsed > STALE_SEC)
            it = m_history.erase(it);
        else
            ++it;
    }
}
 
/**
 * Tính velocity Y trung bình từ history (pixel/frame)
 * Dùng linear regression đơn giản trên chuỗi cy
 * Dương = xe đi xuống (vi phạm), Âm = xe lùi lên
 */
float RedLightRule::estimateVelocityY(const std::deque<float>& history) const {
    int n = (int)history.size();
    if (n < 2) return 0.f;
 
    // Simple: trung bình delta giữa các frame liên tiếp
    // (ổn định hơn chỉ lấy first-last khi có noise)
    float sum_delta = 0.f;
    for (int i = 1; i < n; ++i)
        sum_delta += (history[i] - history[i-1]);
 
    return sum_delta / (float)(n - 1);
}
 
/**
 * Vẽ stop-line và label lên frame bằng chứng
 */
void RedLightRule::drawViolationOverlay(cv::Mat& frame, int stop_y_px) const {
    // Vẽ vùng stop-line (2 đường)
    int y1_px = static_cast<int>(m_stop_line_y1 * frame.rows);
    int y2_px = static_cast<int>(m_stop_line_y2 * frame.rows);
 
    // Vùng tô màu đỏ nhạt giữa 2 vạch
    cv::Mat overlay = frame.clone();
    cv::rectangle(overlay,
                  cv::Point(0, y1_px),
                  cv::Point(frame.cols, y2_px),
                  cv::Scalar(0, 0, 180), cv::FILLED);
    cv::addWeighted(overlay, 0.3, frame, 0.7, 0, frame);
 
    // Đường stop-line chính
    cv::line(frame,
             cv::Point(0, stop_y_px),
             cv::Point(frame.cols, stop_y_px),
             cv::Scalar(0, 0, 255), 3);
 
    // Label
    cv::putText(frame, "STOP LINE",
                cv::Point(8, stop_y_px - 8),
                cv::FONT_HERSHEY_SIMPLEX, 0.7,
                cv::Scalar(0, 0, 255), 2);
 
    // Banner vi phạm góc trên
    cv::rectangle(frame,
                  cv::Point(0, 0),
                  cv::Point(280, 36),
                  cv::Scalar(0, 0, 200), cv::FILLED);
    cv::putText(frame, "!! RED LIGHT VIOLATION !!",
                cv::Point(4, 26),
                cv::FONT_HERSHEY_SIMPLEX, 0.7,
                cv::Scalar(255, 255, 255), 2);
}