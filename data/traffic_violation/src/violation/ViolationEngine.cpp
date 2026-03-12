/*******************************************************************************
 * traffic_violation/src/violation/ViolationEngine.cpp
 ******************************************************************************/
#include "ViolationEngine.h"
#include <iostream>

ViolationEngine::ViolationEngine(const ViolationConfig& cfg) : m_cfg(cfg) {}

void ViolationEngine::addRule(std::shared_ptr<IViolationRule> rule) {
    m_rules.push_back(std::move(rule));
}

void ViolationEngine::clearRules() {
    m_rules.clear();
    m_state.clear();
}

std::vector<ViolationEvent> ViolationEngine::process(const FrameContext& ctx) {
    std::vector<ViolationEvent> confirmed;

    for (auto& rule : m_rules) {
        if (!rule->enabled) continue;

        auto candidates = rule->check(ctx);

        for (auto& ev : candidates) {
            CandidateKey key{ev.track_id, ev.type};
            auto& state = m_state[key];

            ++state.pending_frames;

            // Chưa đủ confirm_frames → skip
            if (state.pending_frames < m_cfg.confirm_frames) continue;

            // Kiểm tra cooldown
            auto now = std::chrono::steady_clock::now();
            if (state.ever_logged) {
                double elapsed = std::chrono::duration<double>(
                    now - state.last_logged).count();
                if (elapsed < m_cfg.cooldown_sec) continue;
            }

            // ── Emit confirmed violation ──────────────────────────────────────
            state.last_logged = now;
            state.ever_logged = true;
            state.pending_frames = 0;
            confirmed.push_back(ev);

            std::cout << "[ViolationEngine] CONFIRMED: "
                      << violationName(ev.type)
                      << "  plate=" << (ev.plate.empty() ? "?" : ev.plate)
                      << "  track=" << ev.track_id << "\n";
        }
    }

    // Decay pending counts for tracks not seen in this frame
    // (remove stale candidates)
    for (auto it = m_state.begin(); it != m_state.end(); ) {
        bool found = false;
        for (auto& v : ctx.vehicles)
            if (v.track_id == it->first.track_id) { found = true; break; }
        if (!found) it->second.pending_frames = 0;
        ++it;
    }

    return confirmed;
}
