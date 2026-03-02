/*******************************************************************************
 * traffic_violation/src/violation/LaneLineRule.h
 * Kiểm tra xe đè vạch kẻ đường (lane line crossing)
 *
 * Không dùng segmentation mask – dùng vị trí vạch tĩnh từ config.yaml.
 * Xe vi phạm khi bbox của xe straddling qua vạch ít nhất overlap_px mỗi bên.
 ******************************************************************************/
#pragma once
#include "IViolationRule.h"
#include "common/config.h"

class LaneLineRule final : public IViolationRule {
public:
    explicit LaneLineRule(const SceneConfig& scene);
    std::string name() const override { return "LaneLineRule"; }
    std::vector<ViolationEvent> check(const FrameContext& ctx) override;

private:
    std::vector<LaneLine> m_lines;
};
