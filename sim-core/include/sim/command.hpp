#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace sim {

// 명령 대상 영역 (행 우선 그리드의 직사각형). 그리드를 벗어나면 enqueue에서 거부해요.
struct TileArea {
    int row = 0;
    int col = 0;
    int rows = 0;
    int cols = 0;

    bool operator==(const TileArea&) const = default;
};

[[nodiscard]] constexpr TileArea whole_grid(int bed_rows, int bed_cols) {
    return TileArea{.row = 0, .col = 0, .rows = bed_rows, .cols = bed_cols};
}

// 아무 효과도 없는 명령. 명령 큐 구조 검증용 (M0)
struct NoopCommand {
    bool operator==(const NoopCommand&) const = default;
};

// docs/spec/soil-water.md "관수"
struct IrrigateCommand {
    TileArea area;
    double amount_mm = 0.0;

    bool operator==(const IrrigateCommand&) const = default;
};

// docs/spec/organic-matter.md "투입"
struct AddCompostCommand {
    TileArea area;
    std::string compost_id;
    double kg_per_m2 = 0.0;

    bool operator==(const AddCompostCommand&) const = default;
};

// docs/spec/nitrogen.md "비료 투입"
struct AddFertilizerCommand {
    TileArea area;
    std::string fertilizer_id;
    double g_per_m2 = 0.0;

    bool operator==(const AddFertilizerCommand&) const = default;
};

// 플레이어 행동. 직렬화는 variant 인덱스를 쓰므로, 기존 항목의 순서를 바꾸지 말고 뒤에만 추가해요.
using Command = std::variant<NoopCommand, IrrigateCommand, AddCompostCommand, AddFertilizerCommand>;

struct QueuedCommand {
    std::uint64_t seq;  // 투입 순서 일련번호
    Command command;

    bool operator==(const QueuedCommand&) const = default;
};

}  // namespace sim
