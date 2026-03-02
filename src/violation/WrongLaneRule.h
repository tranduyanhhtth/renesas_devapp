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

    static std::string vehicleTypeStr(VehicleType t);
};
