#pragma once

// 프로세스 함수 모음. World::tick_once()가 docs/spec/m1-overview.md "틱 처리 순서"대로 호출해요.
// 단위 테스트가 프로세스를 직접 부를 수 있도록 공개 헤더에 두지만, 게임·CLI는 World API만 써요.
//
// 공통 규칙
// - 각 함수는 "이번 틱(1시간)의 변화량"을 계산해 셀에 반영해요.
// - 1차 반응 이동량은 `풀 × min(1, 속도 × 계수들의 곱)` 형태로, 원천 풀을 넘지 않아요.
// - 어떤 풀도 음수가 되면 안 돼요. 장부(Ledger)는 보존식이 성립하도록 정확히 같은 양을 기록해요.
// - 파일 담당 (구현 작업 분할):
//     temperature.cpp       하우스 기온, 하우스 안 일사, 토양 온도 완화
//     soil_water.cpp        관수, 증발·배수
//     leaching.cpp          용탈
//     organic_matter.cpp    퇴비 투입, 유기물 분해(발열 포함)
//     nitrification.cpp     비료 투입, 2단계 질산화, 아질산 경보

#include <cstdint>
#include <optional>

#include "sim/event.hpp"
#include "sim/params.hpp"
#include "sim/scenario.hpp"
#include "sim/soil.hpp"

namespace sim::process {

// ---- temperature.cpp (docs/spec/soil-temperature.md) ----

// 하우스 기온 = 외기 + base_offset + solar_gain × 외부 일사
[[nodiscard]] double house_air_temp_degC(const HouseParams& params, const WeatherHour& weather);

// 하우스 안 일사 = 외부 일사 × 투과율 (soil-water의 증발 입력)
[[nodiscard]] double inside_solar_W_per_m2(const HouseParams& params, const WeatherHour& weather);

// 셀 단계 b: 토양 온도를 하우스 기온 쪽으로 완화
void relax_soil_temperature(SoilCell& cell, const SoilTemperatureParams& params, double house_air_temp_degC);

// ---- soil_water.cpp (docs/spec/soil-water.md) ----

// 명령 단계: 관수량을 더하고 장부 water_in_mm에 기록
void apply_irrigation(SoilCell& cell, double amount_mm, Ledger& ledger);

// 셀 단계 a: 증발 → 배수. drained_mm_this_tick을 이번 틱 배수량으로 설정하고 장부에 기록
void update_soil_water(SoilCell& cell, const SoilWaterParams& params, double inside_solar_W_per_m2,
                       Ledger& ledger);

// ---- leaching.cpp (docs/spec/nitrogen.md "용탈") ----

// 셀 단계 h: drained_mm_this_tick 비율만큼 NO3-, NO2-, (일부) NH4+ 제거, 장부 N_leached_g에 기록
void leach(SoilCell& cell, const LeachingParams& params, Ledger& ledger);

// ---- organic_matter.cpp (docs/spec/organic-matter.md) ----

// 명령 단계: 퇴비 투입. 장부 C_in_g, N_in_g에 기록
void apply_compost(SoilCell& cell, const CompostDef& compost, double kg_per_m2, Ledger& ledger);

// 셀 단계 e: 이분해성 → 난분해성 풀 순서로 분해, 부식화, 무기화/부동화, 발열.
// 발열은 temperature 파라미터의 heat_degC_per_g_C로 soil_temp_degC에 더하고, 장부 C_respired_g에 기록
void decompose_organic_matter(SoilCell& cell, const OrganicMatterParams& params,
                              const SoilTemperatureParams& temperature_params, Ledger& ledger);

// ---- nitrification.cpp (docs/spec/nitrogen.md) ----

// 명령 단계: 비료 투입. 장부 N_in_g에 기록
void apply_fertilizer(SoilCell& cell, const FertilizerDef& fertilizer, double g_per_m2, Ledger& ledger);

// 셀 단계 f: NH4+ → NO2- → NO3- 2단계 질산화
void nitrify(SoilCell& cell, const NitrificationParams& params);

// 셀 단계 i: 아질산 임계치 판정. 처음 넘을 때만 이벤트를 돌려주고 nitrite_alarm을 갱신
[[nodiscard]] std::optional<NitriteThresholdExceededEvent> check_nitrite_alarm(SoilCell& cell,
                                                                               const NitrificationParams& params,
                                                                               std::uint32_t tile_index);

}  // namespace sim::process
