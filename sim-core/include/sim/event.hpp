#pragma once

#include <cstdint>
#include <variant>

#include "sim/soil.hpp"

namespace sim {

// 명령이 적용됨. 영농일지의 플레이어 행동 자동 기록에 써요.
struct CommandAppliedEvent {
    std::uint64_t command_seq;

    bool operator==(const CommandAppliedEvent&) const = default;
};

// 아질산이 독성 임계치를 처음 넘음 (docs/spec/nitrogen.md "아질산 독성 판정")
struct NitriteThresholdExceededEvent {
    std::uint32_t tile_index;  // 행 우선 인덱스
    double no2_N_g_per_m2;

    bool operator==(const NitriteThresholdExceededEvent&) const = default;
};

// 작물 고사 (docs/spec/strawberry-minimal.md "뿌리 건강도")
struct PlantDiedEvent {
    std::uint32_t tile_index;
    RootDamageCause cause;  // 고사한 틱의 가장 큰 손상 항

    bool operator==(const PlantDiedEvent&) const = default;
};

// 자동 수확 (docs/spec/strawberry-fruit.md "수확"). 그 틱에 딴 과실을 타일마다 하나로 묶어요.
struct FruitHarvestedEvent {
    std::uint32_t tile_index;
    std::uint32_t fruits;    // 정상과 개수
    std::uint32_t deformed;  // 기형과 개수
    double fresh_g;          // 정상과 + 기형과 생체중
    double mean_brix;        // 정상과 생체중 가중 평균 (정상과가 없으면 0)
    double mean_firmness;

    bool operator==(const FruitHarvestedEvent&) const = default;
};

using EventPayload =
    std::variant<CommandAppliedEvent, NitriteThresholdExceededEvent, PlantDiedEvent, FruitHarvestedEvent>;

struct Event {
    std::uint64_t tick;  // 이벤트가 발생한 틱 (처리 중이던 틱 번호)
    EventPayload payload;

    bool operator==(const Event&) const = default;
};

}  // namespace sim
