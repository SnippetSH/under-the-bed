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

// docs/spec/soil-chemistry.md "석회" (M2)
struct AddLimeCommand {
    TileArea area;
    std::string lime_id;
    double g_per_m2 = 0.0;

    bool operator==(const AddLimeCommand&) const = default;
};

// docs/spec/aeration.md "경운" (M2)
struct TillCommand {
    TileArea area;

    bool operator==(const TillCommand&) const = default;
};

// ---- M4 하우스 기후 (docs/spec/house-climate.md "명령") ----

enum class WindowSide : std::uint8_t { Left = 0, Right = 1 };

// 측창 한 개의 개방률 변경. layer 0 = 가장 바깥, layer < 지금 겹수
struct SetWindowCommand {
    int layer = 0;
    WindowSide side = WindowSide::Left;
    double open_fraction = 0.0;  // house.window_open_levels 중 하나 (아니면 enqueue에서 거부)

    bool operator==(const SetWindowCommand&) const = default;
};

// 난방기 켜기/끄기와 설정 온도 (설정 온도는 params 범위 안이어야 해요)
struct SetHeaterCommand {
    bool on = false;
    double setpoint_degC = 0.0;

    bool operator==(const SetHeaterCommand&) const = default;
};

// 피복 겹수 변경. 명령이 적용될 틱의 날짜가 휴작기(cover_change_window_doy) 안일 때만 받아요.
struct SetCoverLayersCommand {
    int layers = 0;

    bool operator==(const SetCoverLayersCommand&) const = default;
};

// 적화·적과 (docs/spec/strawberry-fruit.md 6c, 2026-11-18 디렉터 결정). 칸마다 per_plant × plants_per_m2개를
// 난수 반올림해서, 진행도 thinnable_max_index 미만의 꽃·어린 과실을 높은 과위·늦게 핀 것부터 따요.
struct ThinFlowersCommand {
    TileArea area;
    double per_plant = 0.0;

    bool operator==(const ThinFlowersCommand&) const = default;
};

// ---- M5a (docs/spec/disinfection.md, soil-microbes.md, m5-overview 8장) ----

// 멀칭 덮기·걷기. mulch_id = params [[mulch]] id, "none"이면 걷음
struct SetMulchCommand {
    TileArea area;
    std::string mulch_id;

    bool operator==(const SetMulchCommand&) const = default;
};

// 약제 소독: 잔류 += g_per_m2 × residue_index_per_g_per_m2
struct ApplyDisinfectantCommand {
    TileArea area;
    std::string disinfectant_id;
    double g_per_m2 = 0.0;

    bool operator==(const ApplyDisinfectantCommand&) const = default;
};

// 유익균 제품 투입
struct AddInoculantCommand {
    TileArea area;
    std::string inoculant_id;
    double g_per_m2 = 0.0;

    bool operator==(const AddInoculantCommand&) const = default;
};

// 정식 (하우스 전체, 2026-11-19 디렉터 결정). 살아 있는 작물이 하나라도 있으면 enqueue에서 거부
struct PlantCommand {
    std::string seedling_grade;
    double plants_per_m2 = 0.0;

    bool operator==(const PlantCommand&) const = default;
};

// ---- M5b (house-humidity 4.4, foliar-disease 4.5·4.6) ----

// 유동팬 가동/정지 (하우스 전체, 다음 틱부터)
struct SetFanCommand {
    bool on = false;

    bool operator==(const SetFanCommand&) const = default;
};

// 병든 포기 제거: 영역 안 작물(살아 있든 죽었든)을 없애고 체내 원소·건물은 계 밖으로 (*_removed_g).
// 영역에 작물이 하나도 없으면 enqueue에서 거부. 뽑은 칸은 빈 칸으로 남아요 (보식 없음)
struct RemovePlantCommand {
    TileArea area;

    bool operator==(const RemovePlantCommand&) const = default;
};

// 천적 방사: 살아 있는 포기마다 D = min(1, D + per_m2 × index_per_individual_per_m2)
struct ReleasePredatorsCommand {
    TileArea area;
    std::string predator_id;  // params [[predator]] id
    double per_m2 = 0.0;      // 마리/m²

    bool operator==(const ReleasePredatorsCommand&) const = default;
};

// 점적 호스 설치 (하우스 전체). 이미 깔려 있으면 enqueue에서 거부
struct InstallDripCommand {
    bool operator==(const InstallDripCommand&) const = default;
};

// 플레이어 행동. 직렬화는 variant 인덱스를 쓰므로, 기존 항목의 순서를 바꾸지 말고 뒤에만 추가해요.
using Command = std::variant<NoopCommand, IrrigateCommand, AddCompostCommand, AddFertilizerCommand, AddLimeCommand,
                             TillCommand, SetWindowCommand, SetHeaterCommand, SetCoverLayersCommand,
                             ThinFlowersCommand, SetMulchCommand, ApplyDisinfectantCommand, AddInoculantCommand,
                             PlantCommand, SetFanCommand, RemovePlantCommand, ReleasePredatorsCommand,
                             InstallDripCommand>;

struct QueuedCommand {
    std::uint64_t seq;  // 투입 순서 일련번호
    Command command;

    bool operator==(const QueuedCommand&) const = default;
};

}  // namespace sim
