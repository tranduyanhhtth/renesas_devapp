/*******************************************************************************
 * traffic_violation/src/violation/ViolationEngine.h
 ******************************************************************************/
#pragma once
#include "IViolationRule.h"
#include "common/config.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <chrono>

class ViolationEngine {
public:
    explicit ViolationEngine(const ViolationConfig& cfg);

    // Thêm rule (plugin pattern)
    void addRule(std::shared_ptr<IViolationRule> rule);

    // Chạy tất cả rules trên 1 frame context.
    // Áp dụng cooldown + confirm_frames trước khi emit event.
    std::vector<ViolationEvent> process(const FrameContext& ctx);

private:
    struct CandidateKey {
        int           track_id;
        ViolationType type;
        bool operator==(const CandidateKey& o) const {
            return track_id == o.track_id && type == o.type;
        }
    };
    struct CandidateKeyHash {
        size_t operator()(const CandidateKey& k) const {
            return std::hash<int>()(k.track_id) ^
                   (std::hash<int>()(static_cast<int>(k.type)) << 16);
        }
    };
    struct CandidateState {
        int  pending_frames{0};
        std::chrono::steady_clock::time_point last_logged{};
        bool ever_logged{false};
    };

    ViolationConfig m_cfg;
    std::vector<std::shared_ptr<IViolationRule>> m_rules;
    std::unordered_map<CandidateKey, CandidateState, CandidateKeyHash> m_state;
};
