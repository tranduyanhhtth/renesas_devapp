/*******************************************************************************
 * traffic_violation/src/violation/HelmetRule.h
 ******************************************************************************/
#pragma once
#include "IViolationRule.h"

class HelmetRule final : public IViolationRule {
public:
    std::string name() const override { return "HelmetRule"; }
    std::vector<ViolationEvent> check(const FrameContext& ctx) override;
};
