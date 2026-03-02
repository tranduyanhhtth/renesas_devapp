/*******************************************************************************
 * traffic_violation/src/violation/RedLightRule.h
 ******************************************************************************/
#pragma once
#include "IViolationRule.h"
#include "common/config.h"

class RedLightRule final : public IViolationRule {
public:
    explicit RedLightRule(const SceneConfig& scene);
    std::string name() const override { return "RedLightRule"; }
    std::vector<ViolationEvent> check(const FrameContext& ctx) override;

private:
    float m_stop_line_y1;
    float m_stop_line_y2;
};
