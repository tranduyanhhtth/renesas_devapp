/*******************************************************************************
 * traffic_violation/src/violation/IViolationRule.h
 * Plugin interface – thêm loại vi phạm mới chỉ cần tạo class mới
 ******************************************************************************/
#pragma once
#include "common/types.h"
#include <string>
#include <vector>

class IViolationRule {
public:
    virtual ~IViolationRule() = default;

    // Tên rule (để log)
    virtual std::string name() const = 0;

    // Kiểm tra vi phạm trên toàn bộ frame context.
    // Trả về danh sách sự kiện vi phạm (có thể rỗng).
    virtual std::vector<ViolationEvent> check(const FrameContext& ctx) = 0;

    // Bật/tắt rule
    bool enabled = true;
};
