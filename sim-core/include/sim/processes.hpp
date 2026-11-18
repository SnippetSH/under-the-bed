#pragma once

// 프로세스 함수 모음. World::tick_once()가 docs/spec/m2-overview.md "틱 처리 순서 (M2)"대로 호출해요.
// 단위 테스트가 프로세스를 직접 부를 수 있도록 공개 헤더에 두지만, 게임·CLI는 World API만 써요.
//
// 공통 규칙
// - 각 함수는 "이번 틱(1시간)의 변화량"을 계산해 셀에 반영해요.
// - 1차 반응 이동량은 `풀 × min(1, 속도 × 계수들의 곱)` 형태로, 원천 풀을 넘지 않아요.
// - 어떤 풀도 음수가 되면 안 돼요. 장부(Ledger)는 보존식이 성립하도록 정확히 같은 양을 기록해요.
// - 파일 담당 (docs/spec/m2-overview.md "구현 작업 분할"):
//     temperature.cpp       하우스 기온, 하우스 안 일사, 토양 온도 완화
//     soil_water.cpp        관수(+다짐), 증발·증산·배수
//     aeration.cpp          통기 지수, 경운
//     organic_matter.cpp    퇴비 유기물 투입, 유기물 분해(발열, 산소 계수)
//     nitrification.cpp     2단계 질산화(산소 계수, H+ 생성), 아질산 경보(pH 독성 배율)
//     soil_chemistry.cpp    비료·퇴비 무기 성분, 석회, pH 수지, 유효량, EC
//     leaching.cpp          용탈 (N + K·Ca·Mg·P·S 용액 부분)
//     crop.cpp              작물 수관·증산 요구, 생장·흡수·뿌리 건강도

#include <cstdint>
#include <optional>

#include "sim/event.hpp"
#include "sim/params.hpp"
#include "sim/scenario.hpp"
#include "sim/soil.hpp"

namespace sim::process {

// ---- temperature.cpp (docs/spec/soil-temperature.md) ----

[[nodiscard]] double house_air_temp_degC(const HouseParams& params, const WeatherHour& weather);
[[nodiscard]] double inside_solar_W_per_m2(const HouseParams& params, const WeatherHour& weather);
void relax_soil_temperature(SoilCell& cell, const SoilTemperatureParams& params, double house_air_temp_degC);

// ---- soil_water.cpp (docs/spec/soil-water.md + M2 변경) ----

// 명령 단계: 관수량을 더하고(장부 water_in_mm), 다짐 지수를 aeration.compaction_per_mm_irrigation만큼 올림(1에서 멈춤)
void apply_irrigation(SoilCell& cell, double amount_mm, const AerationParams& aeration, Ledger& ledger);

// 셀 단계 a: 증발(× (1 − canopy_interception)) → 증산(min(water, transpiration_demand_mm)) →
//   포화 초과분 즉시 배수 → 포장용수량 초과분 배수(× drainage_compaction_curve(compaction)).
// drained_mm_this_tick 설정, 장부 water_evaporated_mm / water_transpired_mm / water_drained_mm 기록.
void update_soil_water(SoilCell& cell, const SoilWaterParams& params, double inside_solar_W_per_m2,
                       double canopy_interception, double transpiration_demand_mm, Ledger& ledger);

// ---- aeration.cpp (docs/spec/aeration.md) ----

// 셀 단계 c: 통기 지수 (0~1). 셀을 바꾸지 않아요. World가 cell.aeration_index에 저장해요.
[[nodiscard]] double aeration_index(const SoilCell& cell, const AerationParams& params);

// 명령 단계: 경운. 다짐 = tilled_compaction_index, 살아 있는 작물이 있으면 root_health × (1 − till_root_damage_fraction)
void apply_till(SoilCell& cell, Crop& crop, const AerationParams& params);

// ---- organic_matter.cpp (docs/spec/organic-matter.md + M2 변경) ----

// 명령 단계: 퇴비의 유기 C·N 투입 (무기 성분은 apply_compost_minerals). 장부 C_in_g, N_in_g
void apply_compost(SoilCell& cell, const CompostDef& compost, double kg_per_m2, Ledger& ledger);

// 셀 단계 e: 분해. 환경 계수에 organic_matter.o2_curve(cell.aeration_index)를 곱함 (M2)
void decompose_organic_matter(SoilCell& cell, const OrganicMatterParams& params,
                              const SoilTemperatureParams& temperature_params, Ledger& ledger);

// ---- nitrification.cpp (docs/spec/nitrogen.md + M2 변경) ----

// 셀 단계 f: 2단계 질산화. 1·2단계에 산소 곡선(cell.aeration_index)을 곱하고,
// 1단계로 질산화된 N(eq = g / ions.nh4_N_g_per_eq) × nitrification_H_eq_per_eq_N을 cell.acid_H_eq_this_tick에 더함
void nitrify(SoilCell& cell, const NitrificationParams& params, const SoilChemistryParams& chemistry,
             const IonParams& ions);

// 유효 아질산 = no2 × nitrite_toxicity_ph_curve(pH) (M2). 경보와 뿌리 손상이 같이 써요.
[[nodiscard]] double effective_nitrite_g_N_per_m2(const SoilCell& cell, const NitrificationParams& params);

// 셀 단계 k: 아질산 판정. 유효 아질산으로 임계치·해제선을 비교. 처음 넘을 때만 이벤트
[[nodiscard]] std::optional<NitriteThresholdExceededEvent> check_nitrite_alarm(SoilCell& cell,
                                                                               const NitrificationParams& params,
                                                                               std::uint32_t tile_index);

// ---- soil_chemistry.cpp (docs/spec/soil-chemistry.md) ----

// 명령 단계: 비료. NH4/NO3 + K·Ca·Mg·P·S, direct_acid는 cell.acid_H_eq_this_tick에. 원소별 *_in_g 장부
void apply_fertilizer(SoilCell& cell, const FertilizerDef& fertilizer, double g_per_m2, Ledger& ledger);

// 명령 단계: 퇴비의 무기 성분(K·Ca·Mg·P·S)과 direct_acid. 원소별 *_in_g 장부
void apply_compost_minerals(SoilCell& cell, const CompostDef& compost, double kg_per_m2, Ledger& ledger);

// 명령 단계: 석회. 미용해 풀에 당량을 더하고 Ca·Mg 비율을 당량 가중 평균
void apply_lime(SoilCell& cell, const LimeDef& lime, double g_per_m2);

// 셀 단계 g: 석회 용해(Ca·Mg를 풀과 장부 *_in_g에) → net_H = acid_H_eq_this_tick + pending_H − 용해 당량 →
//   pH −= net_H / (base_buffer + organic_C_buffer × 유기 C), ph_min~ph_max로 제한.
//   acid_H_eq_this_tick, pending_H를 0으로, 장부 H_eq_net += net_H
void update_lime_and_ph(SoilCell& cell, const SoilChemistryParams& params, const IonParams& ions, Ledger& ledger);

// 작물이 이번 틱 가져갈 수 있는 유효량 (g/m²): 용액 부분 × pH 유효도
struct NutrientAvailability {
    double nh4_N = 0.0;
    double no3_N = 0.0;
    double p = 0.0;
    double k = 0.0;
    double ca = 0.0;
    double mg = 0.0;
};
[[nodiscard]] NutrientAvailability available_nutrients(const SoilCell& cell, const Params& params);

// 셀 단계 j: 토양 용액 EC (dS/m). 셀을 바꾸지 않아요. World가 cell.ec_dS_per_m에 저장해요.
[[nodiscard]] double soil_ec_dS_per_m(const SoilCell& cell, const Params& params);

// ---- leaching.cpp (docs/spec/nitrogen.md "용탈" + soil-chemistry "용탈 확장") ----

// 셀 단계 i: 배수 비율 f로 NO3·NO2·(NH4 × nh4_solution_fraction)·(X × X_solution_fraction) 제거, 원소별 *_leached_g
void leach(SoilCell& cell, const LeachingParams& params, const SoilChemistryParams& chemistry, Ledger& ledger);

// ---- crop.cpp (docs/spec/strawberry-minimal.md) ----

// 시나리오 정식: present = alive = true, root_health = 1, 체내 양분 = 적정 농도 × (잎 + 뿌리)
[[nodiscard]] Crop plant_crop(const CropInit& init, const CropParams& params);

// 받는 빛 비율 = light_interception_curve(LAI). 살아 있지 않으면 0
[[nodiscard]] double canopy_interception(const Crop& crop, const CropParams& params);

// 물·염류 스트레스 계수 (water_stress_curve(relative_water), salt_stress_curve(cell.ec_dS_per_m))
struct WaterSaltFactors {
    double water = 1.0;
    double salt = 1.0;
};
[[nodiscard]] WaterSaltFactors water_salt_factors(const SoilCell& cell, const CropParams& params);

// 셀 단계 a에 넘길 증산 요구량 (mm/h). 살아 있지 않으면 0
[[nodiscard]] double transpiration_demand_mm(const Crop& crop, const CropParams& params, double inside_solar_W_per_m2,
                                             const WaterSaltFactors& factors);

// 원소별 충족도 (0~1)와 과잉 지수 (≥0). X = N, P, K, Ca, Mg
struct NutrientStatus {
    double sufficiency_N = 0.0, sufficiency_P = 0.0, sufficiency_K = 0.0, sufficiency_Ca = 0.0, sufficiency_Mg = 0.0;
    double excess_N = 0.0, excess_P = 0.0, excess_K = 0.0, excess_Ca = 0.0, excess_Mg = 0.0;
};
[[nodiscard]] NutrientStatus nutrient_status(const Crop& crop, const CropParams& params);

// 셀 단계 h (살아 있는 작물만): 스트레스 → 생장 → 흡수(토양 풀에서 빼고 체내·장부 *_uptake_g, pending_H 기록) →
//   뿌리 건강도(손상 4종 또는 회복) → 고사 판정. 고사하면 이벤트를 돌려줌
[[nodiscard]] std::optional<PlantDiedEvent> update_crop(Crop& crop, SoilCell& cell, const Params& params,
                                                        const NutrientAvailability& available,
                                                        double inside_solar_W_per_m2, double house_air_temp_degC,
                                                        StressCombination mode, std::uint32_t tile_index,
                                                        Ledger& ledger);

}  // namespace sim::process
