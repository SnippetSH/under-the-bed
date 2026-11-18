#pragma once

#include <cstdint>
#include <variant>

namespace sim {

// 아무 효과도 없는 명령. M0에서 명령 큐 구조와 적용 시점을 검증하는 데만 써요.
struct NoopCommand {
    bool operator==(const NoopCommand&) const = default;
};

// 플레이어 행동. 새 행동은 해당 프로세스의 명세와 함께 여기에 추가해요.
// 직렬화는 variant 인덱스를 쓰므로, 기존 항목의 순서를 바꾸지 말고 뒤에만 추가해요.
using Command = std::variant<NoopCommand>;

struct QueuedCommand {
    std::uint64_t seq;  // 투입 순서 일련번호
    Command command;

    bool operator==(const QueuedCommand&) const = default;
};

}  // namespace sim
