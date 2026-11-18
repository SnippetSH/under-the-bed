#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sim/calendar.hpp"
#include "sim/house.hpp"
#include "sim/scenario.hpp"
#include "sim/soil.hpp"

namespace sim {

// 읽기 전용 상태 복사본. 엔진과 CLI는 이것만 보고 그려요.
// "플레이어가 측정한 값"은 측정 기능이 생기는 M7에서 추가해요. 지금은 진짜 값만 있어요.
struct Snapshot {
    std::uint64_t tick;   // 시작 이후 경과 시간 [h]
    CalendarTime time;    // tick 시점의 달력
    WeatherHour weather;  // tick부터 시작하는 한 시간의 기상 입력
    int bed_rows;
    int bed_cols;
    std::size_t pending_command_count;  // 다음 틱 시작 시 적용될 명령 수

    double house_air_temp_degC;  // 마지막으로 처리한 틱의 하우스 기온 (틱 0에서는 첫 틱의 평형 기온)
    HouseState house;            // M4 하우스 기후 상태 (house-climate)
    std::vector<SoilCell> soil;  // 행 우선, bed_rows × bed_cols
    std::vector<Crop> crops;     // 행 우선, soil과 같은 인덱스 (M2)
    Ledger ledger;
};

}  // namespace sim
