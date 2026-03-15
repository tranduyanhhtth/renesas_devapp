/*******************************************************************************
 * traffic_violation/src/violation/WrongLaneRule.h
 ******************************************************************************/
#pragma once
#include "IViolationRule.h"
#include "common/config.h"

class WrongLaneRule final : public IViolationRule {
public:
    explicit WrongLaneRule(const SceneConfig& scene);
    std::string name() const override { return "WrongLaneRule"; }
    std::vector<ViolationEvent> check(const FrameContext& ctx) override;

private:
    std::map<std::string, LaneZone> m_zones;
    
    // Lưu lịch sử di chuyển của xe (tính theo track_id) để vẽ trail
    struct TrackHistory {
        std::deque<cv::Point> trail;
        std::chrono::time_point<std::chrono::steady_clock> last_seen;
        bool is_violating = false;
    };
    std::map<int, TrackHistory> m_history;
    
    void pruneStaleTracks(const std::vector<TrackedVehicle>& vehicles);

    static std::string vehicleTypeStr(VehicleType t);
};
