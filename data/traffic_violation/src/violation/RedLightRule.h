/*******************************************************************************
 * traffic_violation/src/violation/RedLightRule.h
 ******************************************************************************/
#pragma once
#include "IViolationRule.h"
#include "common/config.h"
#include <deque>
#include <unordered_map>
#include <chrono>
 
class RedLightRule final : public IViolationRule {
public:
    explicit RedLightRule(const SceneConfig& scene);
    std::string name() const override { return "RedLightRule"; }
    std::vector<ViolationEvent> check(const FrameContext& ctx) override;
 
private:
    // Cấu hình stop-line 
    float m_stop_line_y;        // vạch dừng chính (trung bình y1+y2)
    float m_stop_line_y1;       // cạnh trên vùng dừng
    float m_stop_line_y2;       // cạnh dưới vùng dừng
 
    // Ngưỡng phát hiện 
    static constexpr int   HISTORY_LEN      = 8;    // số frame lưu lịch sử
    static constexpr float MIN_MOVE_PX      = 2.0f; // vy tối thiểu để coi là đang chạy
    static constexpr float CROSS_CONFIRM_Y  = 0.05f;// phải vượt qua thêm 5% frame chiều cao
 
    // Trạng thái mỗi xe 
    struct TrackHistory {
        std::deque<float> cy_history;   // lịch sử centroid Y (pixel)
        bool  was_before_line = true;   // frame trước xe còn ở phía trên stop-line
        bool  has_crossed     = false;  // đã phát hiện crossing chưa
        std::chrono::steady_clock::time_point last_seen;
    };
 
    std::unordered_map<int, TrackHistory> m_history; // track_id → history
 
    // Helpers
    void   pruneStaleTracks(const FrameContext& ctx);
    float  estimateVelocityY(const std::deque<float>& history) const;
    bool   hasCrossedLine(const TrackHistory& h, float current_cy,
                          float stop_px, float frame_h) const;
    void   drawViolationOverlay(cv::Mat& frame, int stop_y_px) const;
};