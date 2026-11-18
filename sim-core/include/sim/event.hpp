#pragma once

#include <cstdint>
#include <variant>

namespace sim {

// 명령이 적용됨. 영농일지의 플레이어 행동 자동 기록에 써요.
struct CommandAppliedEvent {
    std::uint64_t command_seq;

    bool operator==(const CommandAppliedEvent&) const = default;
};

using EventPayload = std::variant<CommandAppliedEvent>;

struct Event {
    std::uint64_t tick;  // 이벤트가 발생한 틱 (처리 중이던 틱 번호)
    EventPayload payload;

    bool operator==(const Event&) const = default;
};

}  // namespace sim
