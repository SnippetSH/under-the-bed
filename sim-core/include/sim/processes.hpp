#pragma once

// 프로세스 함수 모음. World::tick_once()가 docs/spec/m4-overview.md "틱 처리 순서 (M4)"대로 호출해요.
// 단위 테스트가 프로세스를 직접 부를 수 있도록 공개 헤더에 두지만, 게임·CLI는 World API만 써요.
//
// 공통 규칙
// - 각 함수는 "이번 틱(1시간)의 변화량"을 계산해 셀에 반영해요.
// - 1차 반응 이동량은 `풀 × min(1, 속도 × 계수들의 곱)` 형태로, 원천 풀을 넘지 않아요.
// - 어떤 풀도 음수가 되면 안 돼요. 장부(Ledger)는 보존식이 성립하도록 정확히 같은 양을 기록해요.
// - 파일 담당 (docs/spec/m2-overview.md, m3-overview.md, m4-overview.md "구현 작업 분할"):
//     temperature.cpp       토양 온도 완화
//     house_climate.cpp     하우스 다중 피복·측창·난방 열수지, 열 관성, 이동 평균, CO2 수지 (M4)
//     soil_water.cpp        관수(+다짐), 증발·증산·배수
//     aeration.cpp          통기 지수, 경운
//     organic_matter.cpp    퇴비 유기물 투입, 유기물 분해(발열, 산소 계수)
//     nitrification.cpp     2단계 질산화(산소 계수, H+ 생성), 아질산 경보(pH 독성 배율)
//     cation_exchange.cpp   CEC, 망간 산화·환원, 빈자리 φ, 평형·완화, NH4 용액 맞추기 (M3)
//     phosphorus.cpp        인산 고정 (M3)
//     soil_chemistry.cpp    비료·퇴비 무기 성분, 석회, pH 수지, 유효량, EC
//     denitrification.cpp   탈질과 H+ 소비 (M4)
//     leaching.cpp          용탈 (N + K·Ca·Mg·Mn 용액 상태, P·S 용액 부분)
//     crop.cpp              작물 수관·증산 요구, 저온 피해, 총생산·유지 호흡·분배, 수확·낙과·적화,
//                           흡수(길항, Mn, N 전용 속도)·뿌리 건강도·낙엽 (M4)
//     phenology.cpp         활착, 화아분화, 화방 발달·개화 (M4)
//     fruit.cpp             꽃 만들기, 개화 판정, 과실 진행 증가분, 크기 구간 (M4)
//     microbes.cpp          개체군 갱신(d2), 활동 계수, 균근균 완화, 유익균 투입 (M5a)
//     disinfection.cpp      멀칭 증발·지온·반사광, 약제 잔류(d1)·활성, 경운 방출, 소독 무기화·망간(d3) (M5a)
//     soil_disease.cpp      작물 감염(h6, 뿌리 건강도 앞), DiseaseOnset (M5a)
//     house_humidity.cpp    하우스 수증기 수지, 공기 흐름, 잎 젖음 (M5b)
//     foliar_disease.cpp    지상부 병해충: 질소 감수성, 탄저·잿빛곰팡이·흰가루 감염, 응애·천적, 모종 병해,
//                           포기 제거·천적 방사, 하우스 포자 압력 (M5b). 과실 썩음·흰가루 과실은 crop.cpp h4

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <string_view>

#include "sim/event.hpp"
#include "sim/house.hpp"
#include "sim/rng.hpp"
#include "sim/params.hpp"
#include "sim/scenario.hpp"
#include "sim/soil.hpp"

namespace sim::process {

// ---- temperature.cpp (docs/spec/soil-temperature.md) ----

// 완화 목표 (M5a 프로토타입 3차): (1 − deep_coupling_fraction) × air_target_degC + deep_coupling_fraction × 깊은 흙 온도.
// air_target_degC는 하우스 기온 + 멀칭 지온 이득(soil_relax_target_degC)이에요.
[[nodiscard]] double soil_relax_blend_target_degC(const SoilCell& cell, const SoilTemperatureParams& params,
                                                  double air_target_degC);
// house_air_temp_degC = 하우스 기온 + 멀칭 지온 이득(soil_relax_target_degC). 깊은 흙 혼합은 안에서 해요.
// 순서: 목표 혼합 → 근권 완화 → 증발 냉각 → 깊은 흙 온도가 근권 온도를 따라감
void relax_soil_temperature(SoilCell& cell, const SoilTemperatureParams& params, double house_air_temp_degC,
                            double evaporated_mm = 0.0);

// ---- house_climate.cpp (docs/spec/house-climate.md, M4) ----

// 피복·창 상태에서 나오는 열·빛 계수
struct CoverState {
    double transmittance_factor = 0.0;      // τ = layer_transmittance ^ 겹수
    double conductance_W_per_m2_per_K = 0.0;  // U
    double vent_W_per_m2_per_K = 0.0;         // V = vent_conductance × path
    double path_index = 0.0;                  // 직렬 개구부 경로 sqrt(n) / sqrt(Σ 1/a_l²), 어느 겹이든 닫히면 0
    double resistance_m2K_per_W = 0.0;        // M5b: 피복 저항 합 R (창 개방 반영). 피복 면 온도가 읽어요
};
[[nodiscard]] CoverState cover_state(const HouseParams& params, const HouseState& house);
[[nodiscard]] double inside_solar_W_per_m2(const HouseParams& params, const HouseState& house,
                                           const WeatherHour& weather);
// 평형 기온과 난방 출력 (house-climate "평형 기온과 난방")
struct HouseEquilibrium {
    double air_temp_degC = 0.0;
    double heat_W_per_m2 = 0.0;
};
[[nodiscard]] HouseEquilibrium house_equilibrium(const HouseParams& params, const HouseState& house,
                                                 const WeatherHour& weather);
// 월드 생성: 겹수, 창 닫힘, 난방 꺼짐, 기온 = 첫 틱 T_eq, 이동 평균 = 기온, CO2 = 첫 틱 시각의 바깥 값
[[nodiscard]] HouseState initial_house(const HouseParams& params, int cover_layers, const WeatherHour& weather,
                                       int hour);
// 하우스 단계 2: 평형·난방 → 기온(열 관성) → 이동 평균, 바깥 CO2·하우스 안 일사(파생값), 난방 에너지 장부
void update_house_air(HouseState& house, const HouseParams& params, const WeatherHour& weather, int hour);
// 하우스 단계 4: 이번 틱 작물 순흡수(셀 평균 g 건물/m², 음수 = 방출)와 토양 호흡 C(셀 평균 g/m²)로 CO2 갱신
void update_house_co2(HouseState& house, const HouseParams& params, const CropParams& crop_params,
                      double crop_net_dry_matter_g_per_m2, double soil_respired_C_g_per_m2);
// ---- house_humidity.cpp (docs/spec/house-humidity.md, M5b) ----
// 포화 수증기 농도 g/m³ (표 보간, 0 이상)
[[nodiscard]] double saturation_vapor_g_per_m3(const HumidityParams& params, double temp_degC);
// 피복 안쪽 면 온도 (4.2): frac = R > 0 ? clamp(1 − inner_surface_resistance / R, 0, 1) : 0
[[nodiscard]] double cover_inner_temp_degC(const HouseParams& house_params, const HumidityParams& params,
                                           const HouseState& house, double outside_air_temp_degC);
// 월드 생성: 수증기 = 첫 틱 바깥 수증기, 이동 평균 = 그때 상대습도, vapor_initial_mm 기록, 파생값 채움
void init_house_humidity(HouseState& house, const HouseParams& house_params, const HumidityParams& params,
                         const WeatherHour& weather);
// 하우스 단계 2 직후 (4.1): airflow_index = min(1, vent_airflow_factor × path + fan)
void update_airflow(HouseState& house, const HumidityParams& params);
// 하우스 단계 4, CO2 뒤 (4.2): 음해법 수증기 수지. source_mm = 이번 틱 셀 평균 (증산 + 증발) mm
void update_house_vapor(HouseState& house, const HouseParams& house_params, const HumidityParams& params,
                        const WeatherHour& weather, double source_mm);
// 작물 단계 h, 병해 앞 (4.3): 잎 사이 습도·잎 젖음·연속 젖음 시간. overhead = 이번 틱 두상 관수
void update_leaf_wetness(Crop& crop, const CropParams& crop_params, const HumidityParams& params,
                         double house_rh_index, double airflow_index, double inside_solar_W_per_m2, bool overhead);

// SetCoverLayers 적용: 늘린 겹의 창은 닫힘, 줄인 겹(안쪽부터)의 창 상태는 사라짐
void set_cover_layers(HouseState& house, int layers);
// SetCoverLayers 허용 날짜인지 (cover_change_window_doy 양 끝 포함)
[[nodiscard]] bool in_cover_change_window(const HouseParams& params, int day_of_year);

// ---- soil_water.cpp (docs/spec/soil-water.md + M2 변경) ----

// 명령 단계: 관수량을 더하고(장부 water_in_mm), 다짐 지수를 aeration.compaction_per_mm_irrigation만큼 올림(1에서 멈춤)
void apply_irrigation(SoilCell& cell, double amount_mm, const AerationParams& aeration, Ledger& ledger);

// 셀 단계 a: 증발(× (1 − canopy_interception)) → 증산(min(water, transpiration_demand_mm)) →
//   포화 초과분 즉시 배수 → 포장용수량 초과분 배수(× drainage_compaction_curve(compaction)).
// drained_mm_this_tick 설정, 이번 틱 증발량(mm)을 돌려줘요. 장부 water_evaporated_mm / water_transpired_mm / water_drained_mm 기록.
// M5a: evaporation_factor = 멀칭 증발 배율 (disinfection 1번, 멀칭 없음 = 1)
double update_soil_water(SoilCell& cell, const SoilWaterParams& params, double inside_solar_W_per_m2,
                         double canopy_interception, double transpiration_demand_mm, Ledger& ledger,
                         double evaporation_factor = 1.0);

// ---- aeration.cpp (docs/spec/aeration.md) ----

// 셀 단계 c: 통기 지수 (0~1). 셀을 바꾸지 않아요. World가 cell.aeration_index에 저장해요.
[[nodiscard]] double aeration_index(const SoilCell& cell, const AerationParams& params);

// 명령 단계: 경운. 다짐 = tilled_compaction_index, 살아 있는 작물이 있으면 root_health × (1 − till_root_damage_fraction)
void apply_till(SoilCell& cell, Crop& crop, const AerationParams& params);

// ---- organic_matter.cpp (docs/spec/organic-matter.md + M2 변경) ----

// 명령 단계: 퇴비의 유기 C·N 투입 (무기 성분은 apply_compost_minerals). 장부 C_in_g, N_in_g
void apply_compost(SoilCell& cell, const CompostDef& compost, double kg_per_m2, Ledger& ledger);

// 셀 단계 e: 분해. 환경 계수에 organic_matter.o2_curve(cell.aeration_index)를 곱함 (M2)
// M5a: decomposer_activity_factor = 분해균계수 decomposer_activity_curve(분해균 지수) (soil-microbes 2번)
void decompose_organic_matter(SoilCell& cell, const OrganicMatterParams& params,
                              const SoilTemperatureParams& temperature_params, Ledger& ledger,
                              double decomposer_activity_factor = 1.0);

// ---- nitrification.cpp (docs/spec/nitrogen.md + M2 변경) ----

// 셀 단계 f: 2단계 질산화. 1·2단계에 산소 곡선(cell.aeration_index)을 곱하고,
// 1단계로 질산화된 N(eq = g / ions.nh4_N_g_per_eq) × nitrification_H_eq_per_eq_N을 cell.acid_H_eq_this_tick에 더함
// M5a: 1·2단계 속도에 aob/nob_activity_factor(개체군 활동 곡선)를 곱해요 (soil-microbes 2번)
void nitrify(SoilCell& cell, const NitrificationParams& params, const SoilChemistryParams& chemistry,
             const IonParams& ions, double aob_activity_factor = 1.0, double nob_activity_factor = 1.0);

// 유효 아질산 = no2 × nitrite_toxicity_ph_curve(pH) (M2). 경보와 뿌리 손상이 같이 써요.
[[nodiscard]] double effective_nitrite_g_N_per_m2(const SoilCell& cell, const NitrificationParams& params);

// 셀 단계 k: 아질산 판정. 유효 아질산으로 임계치·해제선을 비교. 처음 넘을 때만 이벤트
[[nodiscard]] std::optional<NitriteThresholdExceededEvent> check_nitrite_alarm(SoilCell& cell,
                                                                               const NitrificationParams& params,
                                                                               std::uint32_t tile_index);

// ---- denitrification.cpp (docs/spec/denitrification.md, M4) ----

// 셀 단계 f1: lost = no3 × min(1, rate × o2(aeration) × temp(soil_temp) × carbon(labile_C)).
// 장부 N_gas_g, acid_H_eq_this_tick += lost / no3_N_g_per_eq × H_eq_per_eq_N (음수 = H+ 소비)
void denitrify(SoilCell& cell, const DenitrificationParams& params, const IonParams& ions, Ledger& ledger);

// ---- soil_chemistry.cpp (docs/spec/soil-chemistry.md) ----

// 명령 단계: 비료. NH4/NO3 + K·Ca·Mg·P·S·Mn, direct_acid는 cell.acid_H_eq_this_tick에. 원소별 *_in_g 장부
// M3: 양이온(NH4·K·Ca·Mg·Mn)은 총량과 용액 상태에 같은 양을 더해요
void apply_fertilizer(SoilCell& cell, const FertilizerDef& fertilizer, double g_per_m2, Ledger& ledger);

// 명령 단계: 퇴비의 무기 성분(K·Ca·Mg·P·S·Mn)과 direct_acid. 원소별 *_in_g 장부. 양이온은 총량과 용액에
void apply_compost_minerals(SoilCell& cell, const CompostDef& compost, double kg_per_m2, Ledger& ledger);

// 명령 단계: 석회. 미용해 풀에 당량을 더하고 Ca·Mg 비율을 당량 가중 평균
void apply_lime(SoilCell& cell, const LimeDef& lime, double g_per_m2);

// 셀 단계 g: 석회 용해(Ca·Mg를 풀과 장부 *_in_g에) → net_H = acid_H_eq_this_tick + pending_H − 용해 당량 →
//   pH −= net_H / (base_buffer + buffer_eq_per_pH_per_eq_cec × cec_mineral + organic_C_buffer × 유기 C),
//   ph_min~ph_max로 제한.
//   acid_H_eq_this_tick, pending_H를 0으로, 장부 H_eq_net += net_H
// M3: 석회 Ca·Mg는 총량과 용액에
void update_lime_and_ph(SoilCell& cell, const Params& params, Ledger& ledger);

// 작물이 이번 틱 가져갈 수 있는 유효량 (g/m²): 용액 부분 × pH 유효도 (M3: 양이온은 용액 상태, Mn은 곡선 없음)
struct NutrientAvailability {
    double nh4_N = 0.0;
    double no3_N = 0.0;
    double p = 0.0;
    double k = 0.0;
    double ca = 0.0;
    double mg = 0.0;
    double mn = 0.0;  // M3: 용액 Mn 그대로
};
[[nodiscard]] NutrientAvailability available_nutrients(const SoilCell& cell, const Params& params);

// 셀 단계 j: 토양 용액 EC (dS/m). 셀을 바꾸지 않아요. World가 cell.ec_dS_per_m에 저장해요.
[[nodiscard]] double soil_ec_dS_per_m(const SoilCell& cell, const Params& params);

// ---- leaching.cpp (docs/spec/nitrogen.md "용탈" + soil-chemistry "용탈 확장") ----

// 셀 단계 i: 배수 비율 f로 NO3·NO2 × f, 양이온(NH4·K·Ca·Mg·Mn)은 용액 상태 × f (총량과 용액에서 함께 뺌),
// P·S는 풀 × X_solution_fraction × f 제거. 원소별 *_leached_g
void leach(SoilCell& cell, const Params& params, Ledger& ledger);

// ---- cation_exchange.cpp (docs/spec/cation-exchange.md, manganese.md, M3) ----
// 용액 상태 규칙: 양이온(NH4, K, Ca, Mg, Mn) 총량을 바꾸는 프로세스는 용액도 함께 맞춰요.
//   투입·무기화·뿌리 고사 반환·환원 → 용액에 더함, 흡수·용탈 → 용액에서 같은 양을 뺌,
//   그 밖의 감소(질산화, 부동화, 산화) → 비례 차감. 0 ≤ 용액 ≤ 총량.

// 전체 CEC = 광물 + cec_organic_eq_per_g_C × 난분해성 C
[[nodiscard]] double cec_eq_per_m2(const SoilCell& cell, const CationExchangeParams& params);
// 망간 교환 세기 w_Mn = manganese.retention_ph_curve(pH) (ADR-0010)
[[nodiscard]] double manganese_retention_factor(const SoilCell& cell, const ManganeseParams& params);
// 셀 단계 f2 앞부분: Mn2+ ↔ 산화물. 두 이동량은 틱 시작 값으로 계산, 산화는 용액 비례 차감, 환원은 용액으로
void redox_manganese(SoilCell& cell, const ManganeseParams& params);

// 빈자리 경쟁 평형 (ADR-0008). cec ≤ 0이면 φ = 0·전부 용액, 양이온 합 0이면 φ = 1.
struct ExchangeEquilibrium {
    double cec_eq_per_m2 = 0.0;
    double exchange_free_site_index = 0.0;  // φ
    double nh4_solution_N_g_per_m2 = 0.0;   // 평형 용액 양
    double k_solution_g_per_m2 = 0.0;
    double ca_solution_g_per_m2 = 0.0;
    double mg_solution_g_per_m2 = 0.0;
    double mn_solution_g_per_m2 = 0.0;
};
[[nodiscard]] ExchangeEquilibrium exchange_equilibrium(const SoilCell& cell, const Params& params);

// 셀 단계 f2: 망간 산화·환원 → CEC → φ(이분법 solver_iterations회) → 평형 → 완화(min(1, exchange_rate))
void update_cation_exchange(SoilCell& cell, const Params& params);
// 월드 생성: 용액을 평형값으로 (완화 없이). 통기 지수를 먼저 계산한 뒤 불러요. 세이브 로드에서는 부르지 않아요.
void set_solution_to_equilibrium(SoilCell& cell, const Params& params);
// NH4 총량이 다른 프로세스로 바뀐 뒤 용액 맞추기: 증가분은 용액에 더하고(무기화), 감소는 비례 차감
void sync_nh4_solution(SoilCell& cell, double old_nh4_N_g_per_m2);

// ---- phosphorus.cpp (docs/spec/phosphorus.md, M3) ----

// 셀 단계 f3: 유효 ↔ 고정 인산. 두 이동량은 틱 시작 값으로 계산하고 각자 원천 풀로 제한
// M5a: 방출 속도 × release_factor (= 1 + mycorrhiza_p_release_factor × 균근균 지수, soil-microbes 2번)
void fix_phosphorus(SoilCell& cell, const PhosphorusParams& params, double release_factor = 1.0);

// ---- crop.cpp (docs/spec/strawberry-minimal.md + M4 strawberry-fruit.md) ----

[[nodiscard]] const SeedlingGradeDef* find_seedling_grade(const Params& params, std::string_view id);

// 시나리오 정식: present = alive = true, root_health = 1, 기관 건물 = 포기당 값 × 재식 밀도,
// induction = 등급 초기값, 체내 양분 = 요구량(적정 농도 × 기관 건물), 수확 기록 크기 구간 = 경계 수 + 1
[[nodiscard]] Crop plant_crop(const SeedlingGradeDef& grade, double plants_per_m2, const Params& params);

// 잎 면적 지수 LAI = 잎 건물 × 비엽면적 (살아 있는지와 무관). M5b 잎 사이 습도·밤 기본 증산이 읽어요
[[nodiscard]] double leaf_area_index(const Crop& crop, const CropParams& params);

// 받는 빛 비율 = light_interception_curve(LAI). 살아 있지 않으면 0
[[nodiscard]] double canopy_interception(const Crop& crop, const CropParams& params);

// 물·염류 스트레스 계수 (water_stress_curve(relative_water), salt_stress_curve(cell.ec_dS_per_m))
struct WaterSaltFactors {
    double water = 1.0;
    double salt = 1.0;
};
[[nodiscard]] WaterSaltFactors water_salt_factors(const SoilCell& cell, const CropParams& params);

// 셀 단계 a에 넘길 증산 요구량 (mm/h). 살아 있지 않으면 0
// (M5b: 습도 되먹임은 되돌림 — ADR-0014. 증산은 M4 식 그대로)
[[nodiscard]] double transpiration_demand_mm(const Crop& crop, const CropParams& params, double inside_solar_W_per_m2,
                                             const WaterSaltFactors& factors);

// 원소별 요구량 (M4 strawberry-fruit "요구량"): target_X × (잎 + 관부 + 뿌리) + fruit_target_X × 과실
struct NutrientRequirement {
    double N = 0.0, P = 0.0, K = 0.0, Ca = 0.0, Mg = 0.0, Mn = 0.0;
};
[[nodiscard]] NutrientRequirement nutrient_requirement(const Crop& crop, const CropParams& params);
// 과실 건물 합(crop.fruit_g_per_m2()와 같은 값)을 이미 구했을 때 쓰는 형태. 결과는 위와 비트 단위로 같아요.
[[nodiscard]] NutrientRequirement nutrient_requirement(const Crop& crop, const CropParams& params,
                                                       double fruit_dry_g_per_m2);

// 영양 기관 건물 (잎 + 관부 + 뿌리)과 전체 건물 (+ 과실)
[[nodiscard]] double vegetative_biomass_g_per_m2(const Crop& crop);
[[nodiscard]] double total_biomass_g_per_m2(const Crop& crop);

// 원소별 충족도 (0~1)와 과잉 지수 (≥0). X = N, P, K, Ca, Mg, Mn (M3). M4: 분모는 요구량
struct NutrientStatus {
    double sufficiency_N = 0.0, sufficiency_P = 0.0, sufficiency_K = 0.0, sufficiency_Ca = 0.0, sufficiency_Mg = 0.0;
    double excess_N = 0.0, excess_P = 0.0, excess_K = 0.0, excess_Ca = 0.0, excess_Mg = 0.0;
    double sufficiency_Mn = 0.0, excess_Mn = 0.0;
    double plant_Mn_ratio = 0.0;  // 체내 Mn / Mn 요구량 (M4: 관부·과실 포함). 독성 곡선의 x (manganese.md)
};
[[nodiscard]] NutrientStatus nutrient_status(const Crop& crop, const CropParams& params);

// 작물 단계 h의 환경 (하우스 단계 2의 결과와 틱 정보)
struct CropEnvironment {
    double inside_solar_W_per_m2 = 0.0;
    double house_air_temp_degC = 0.0;
    double house_air_temp_mean_degC = 0.0;
    double house_co2_ppm = 0.0;  // 틱 시작 때 값
    int day_of_year = 1;
    std::uint64_t tick = 0;
    StressCombination mode = StressCombination::Minimum;
    std::uint32_t tile_index = 0;
    // M5a 백색 필름 반사광 (disinfection 1번): h3 광합성 광량만 inside_solar × (1 + 이 값 × (1 − interception))
    double canopy_reflect_factor = 0.0;
    // M5b 지상부 병해충 (foliar-disease 4장): 모두 틱 시작 때의 하우스 값. 기본값 0 = 병해충 없음
    double house_rh_mean_index = 0.0;            // 천적 부화·응애 습도 배율
    double botrytis_spore_index = 0.0;
    double powdery_mildew_spore_index = 0.0;
    double anthracnose_sporulation_index = 0.0;  // 탄저병 물 튐 원천 (포기 → 포기)
    double house_spider_mite_mean_index = 0.0;   // 응애 이동의 하우스 평균
    double inside_solar_mean_W_per_m2 = 0.0;     // 흰가루병 약광
    bool overhead_irrigated = false;             // 이번 틱 두상 관수 (탄저병 물 튐 Δ3)
};

struct CropTickResult {
    std::optional<PlantDiedEvent> died;
    std::optional<FruitHarvestedEvent> harvested;
    std::array<std::optional<DiseaseOnsetEvent>, kSoilDiseaseCount> disease_onsets{};  // M5a h6 (SoilDisease 순서)
    // M5b h6·h7 (FoliarDisease 순서)
    std::array<std::optional<FoliarDiseaseOnsetEvent>, kFoliarDiseaseCount> foliar_onsets{};
};

// M4 셀 단계 h (살아 있는 작물만): h0 저온 피해 → h1 생물계절(활착·화아분화·화방·꽃 만들기·개화 판정) →
//   h2 스트레스 → h3 총생산 − 유지 호흡 → 분배(따라잡기 수요, 순생장 < 0이면 수축)·크기 충족도 이동 평균 →
//   h4 과실 진행·품질 → 수확(반출) → 낙과 → h5 흡수(토양 풀에서 빼고 체내·장부 *_uptake_g, pending_H) →
//   h6 뿌리 건강도·고사 → 낙엽 → 고사 판정. 고사하면 PlantDied, 수확하면 FruitHarvested 이벤트를 돌려줘요.
//   난수는 biology_rng에서 목록 순서로 (저온 손실 → 꽃 만들기(화방 순) → 개화 판정 → 낙과)
// M5a: h2·h5의 물·염류 계수에 균근균 완화(cell.microbe_index), h3 광량에 env.canopy_reflect_factor,
//   h6 앞에 감염 진행(update_soil_infection), 뿌리 손상 합에 fusarium·phytophthora·fumigant 곡선
// M5b (foliar-disease 5장): h3 총생산 × (1 − 흰가루 피해) × (1 − 응애 피해)(틱 시작 값), h4 수확 때 흰가루 과실 판정 →
//   낙과 → 잿빛곰팡이 과실 썩음(낙엽 규칙), h6 질소 감수성 → 토양 병해 → 탄저·잿빛곰팡이·흰가루(희석) → 뿌리 손상 합에
//   탄저병, h7 응애·천적. foliar_rng = RngStream::Foliar (nullptr이면 확률 판정을 하지 않아요: 단위 테스트 호환)
[[nodiscard]] CropTickResult update_crop(Crop& crop, SoilCell& cell, const Params& params,
                                         const NutrientAvailability& available, const CropEnvironment& env,
                                         Pcg32& biology_rng, Ledger& ledger, Pcg32* foliar_rng = nullptr);

// M5a Plant 명령 (disinfection 6번): 죽은 작물의 체내 원소·건물(+ 남은 저장)을 낙엽 규칙으로 토양에 돌려주고
// 작물을 비워요(Crop{}). 잎·과실 C는 C_leaf_litter_g, 관부·뿌리·저장 C는 C_root_litter_g.
void return_dead_crop_to_soil(Crop& crop, SoilCell& cell, const CropParams& params, Ledger& ledger);

// ---- phenology.cpp (docs/spec/strawberry-phenology.md, M4) ----

// h1: 활착 → 화아분화(새 화방) → 화방 발달·개화(꽃 만들기). 활착 배율을 돌려줘요.
// fruit_dry_g_per_m2는 지금 과실 건물 합(crop.fruit_g_per_m2())이에요 (화아분화의 체내 N 비율 분모).
double update_phenology(Crop& crop, const Params& params, const CropEnvironment& env, double fruit_dry_g_per_m2,
                        Pcg32& biology_rng);

// ---- fruit.cpp (docs/spec/strawberry-fruit.md, M4) ----

// 꽃 만들기 (화방 개화 시): 소수부 난수 1회, 과위 최대 나머지 배분, 과위별 개화 지연
void create_flowers(Crop& crop, std::uint32_t truss_index, double flowers_per_m2, const FruitParams& params,
                    std::uint64_t tick, Pcg32& biology_rng);
// 개화 판정 (anthesis_tick ≤ tick인 꽃): 수정률 난수 → 낙화/기형/최대 생체중(크기 난수)
void judge_anthesis(Crop& crop, const FruitParams& params, double house_air_temp_mean_degC, std::uint64_t tick,
                    Pcg32& biology_rng);
// 과실 진행 증가분 (적산온도 / 일 / 수확 적산온도)
[[nodiscard]] double fruit_development_increment(const FruitParams& params, double house_air_temp_degC);
// 6c 적화·적과 (명령 단계, 구현은 crop.cpp): per_plant × plants_per_m2를 난수 반올림한 개수만큼,
// 진행도 thinnable_max_index 미만의 꽃·어린 과실을 높은 과위·늦게 핀 것부터 제거. 건물·양분은 낙엽 규칙으로 토양에.
// 실제로 딴 개수를 돌려줘요.
std::uint64_t thin_flowers(Crop& crop, SoilCell& cell, const Params& params, double per_plant, Pcg32& biology_rng,
                           Ledger& ledger);
// 크기 구간 인덱스 (경계 이상이면 다음 구간)
[[nodiscard]] std::size_t size_class_index(const FruitParams& params, double fresh_g);

// ---- microbes.cpp (docs/spec/soil-microbes.md, M5a) ----

// 수용력 계산에 쓰는 셀의 작물 정보 (살아 있는 작물이 없으면 모두 0)
struct MicrobeHost {
    double root_g_per_m2 = 0.0;
    std::array<double, kSoilDiseaseCount> infection_index{};  // SoilDisease 순서
    double anthracnose_infection_index = 0.0;                 // M5b: 탄저병 무리의 숙주 수용력
};
// 무리별 적합도 = temp × moisture × ph × o2 곡선 (토양 온도, relative_water, pH, aeration_index)
[[nodiscard]] MicrobeIndices microbe_suitability(const SoilCell& cell, const MicrobesParams& params);
// 무리별 수용력 (명세 표의 규칙, capacity_floor_index 하한)
[[nodiscard]] MicrobeIndices microbe_capacity(const SoilCell& cell, const MicrobesParams& params,
                                              const MicrobeHost& host);
// 무리별 약제 사멸 비율 /h = Σ_약제 kill_rate × 잔류 × 활성
[[nodiscard]] MicrobeIndices fumigant_kill_per_hour(const SoilCell& cell,
                                                    const std::vector<DisinfectantDef>& disinfectants);
struct MicrobeGroupStep {
    double next_index = 0.0;
    double disinfection_kill_index = 0.0;  // 이번 틱 소독(열 + 약제) 사멸분
};
// 무리 g 하나의 다음 지수. 모든 입력이 단계 시작 값이라 호출 순서와 무관해요.
[[nodiscard]] MicrobeGroupStep microbe_group_step(std::size_t g, const MicrobeIndices& start,
                                                  const MicrobeIndices& suitability, const MicrobeIndices& capacity,
                                                  const MicrobeIndices& fumigant_kill, const SoilCell& cell,
                                                  const MicrobesParams& params);
// 셀 단계 d2: 모든 무리를 단계 시작 값으로 계산해 한꺼번에 갱신하고 disinfection_kill_index를 기록
void update_microbes(SoilCell& cell, const Params& params, const MicrobeHost& host);

// 활동 계수 (soil-microbes 2번). 개체군 지수만 입력으로 받아요.
[[nodiscard]] double decomposer_activity_factor(const SoilCell& cell, const MicrobesParams& params);
[[nodiscard]] double aob_activity_factor(const SoilCell& cell, const MicrobesParams& params);
[[nodiscard]] double nob_activity_factor(const SoilCell& cell, const MicrobesParams& params);
// 인산 방출 배율 = 1 + mycorrhiza_p_release_factor × 균근균 지수
[[nodiscard]] double mycorrhiza_p_release_factor(const SoilCell& cell, const MicrobesParams& params);
// water' = water + (1 − water) × water_relief × 균근균 지수, salt'도 같은 식
[[nodiscard]] WaterSaltFactors mycorrhiza_relieved(const WaterSaltFactors& factors, const SoilCell& cell,
                                                   const MicrobesParams& params);
// 명령 단계 AddInoculant: P += (1 − P) × min(1, g × gain), 담체 C·N → 이분해성 풀, 장부 C_in·N_in·inoculant_applied
void apply_inoculant(SoilCell& cell, const InoculantDef& inoculant, double g_per_m2, Ledger& ledger);

// ---- disinfection.cpp (docs/spec/disinfection.md, M5a) ----

// 셀의 멀칭 정의 (덮지 않았거나 인덱스가 범위 밖이면 nullptr)
[[nodiscard]] const MulchDef* mulch_of(const SoilCell& cell, const Params& params);
// 셀 단계 a 증발 배율 (멀칭 없음 = 1)
[[nodiscard]] double mulch_evaporation_factor(const SoilCell& cell, const Params& params);
// 셀 단계 b 완화 목표 = 하우스 기온 + soil_solar_gain × inside_solar × (1 − interception) (멀칭 없음 = 하우스 기온)
[[nodiscard]] double soil_relax_target_degC(const SoilCell& cell, const Params& params, double house_air_temp_degC,
                                            double inside_solar_W_per_m2, double canopy_interception);
// h3 반사광 배율 (멀칭 없음 = 0)
[[nodiscard]] double mulch_canopy_reflect_factor(const SoilCell& cell, const Params& params);
// 약제 활성 = activation_moisture_curve(relative_water)
[[nodiscard]] double fumigant_activation(const DisinfectantDef& disinfectant, const SoilCell& cell);
// Σ_약제 잔류 × 활성 (뿌리 손상 fumigant_curve의 x)
[[nodiscard]] double fumigant_exposure_index(const SoilCell& cell, const Params& params);
// 셀 단계 d1: 잔류 −= 잔류 × min(1, decay × decay_temp_curve(지온) × 필름 보존)
void decay_fumigant(SoilCell& cell, const Params& params);
// 경운: 잔류 ×= (1 − till_residue_release_fraction)
void release_fumigant_on_till(SoilCell& cell, const DisinfectionParams& params);
// 명령 단계 ApplyDisinfectant: 잔류 += g × residue_index_per_g, 장부 disinfectant_applied_g
void apply_disinfectant(SoilCell& cell, std::size_t disinfectant_index, const DisinfectantDef& disinfectant,
                        double g_per_m2, Ledger& ledger);
// 셀 단계 d3: 분해균 소독 사멸분으로 난분해성 N → NH4+(용액), C → 이분해성, 망간 산화물 → 유효태(용액)
void apply_disinfection_flush(SoilCell& cell, const DisinfectionParams& params, Ledger& ledger);

// ---- soil_disease.cpp (docs/spec/soil-microbes.md 3번, M5a) ----

// 감염 압력 = 병원균 × env_temp × env_o2 × env_ph × (1 − antagonist_protection × 길항균)
[[nodiscard]] double infection_pressure(SoilDisease disease, const SoilCell& cell, const Params& params);
// 작물 단계 h6 앞: infection += rate × pressure × (1 − infection) (줄지 않음). 처음 onset 이상이면 이벤트
[[nodiscard]] std::array<std::optional<DiseaseOnsetEvent>, kSoilDiseaseCount> update_soil_infection(
    Crop& crop, const SoilCell& cell, const Params& params, std::uint32_t tile_index);

// ---- foliar_disease.cpp (docs/spec/foliar-disease.md, M5b) ----

// 셀 멀칭의 탄저병 물 튐 계수 (멀칭 없음 = 맨흙 1)
[[nodiscard]] double mulch_soil_splash_factor(const SoilCell& cell, const Params& params);
[[nodiscard]] double mulch_plant_splash_factor(const SoilCell& cell, const Params& params);
// 환기 횟수 ach = 틈새 + 환기 × path (CO2·수증기와 같은 값)
[[nodiscard]] double house_air_changes_per_hour(const HouseParams& params, const HouseState& house);
// 정식 (4.7): 모종 등급의 값으로 ① 잠복 탄저(fraction > 0이면 난수 1회) ② 응애(fraction > 0이면 난수 1회),
// 흰가루 시작 병반(난수 없음), 토양 무기 N 이동 평균 = 그 셀의 지금 NH4 + NO3
void apply_seedling_disease(Crop& crop, const SeedlingGradeDef& grade, const SoilCell& cell, Pcg32& foliar_rng);
// 4.1: 토양 무기 N 이동 평균 갱신 후 s_N = n_susceptibility_curve(평균)
double update_n_susceptibility(Crop& crop, const SoilCell& cell, const FoliarDiseaseParams& params);
// h3 총생산 배율 = (1 − 흰가루 피해(병든 잎 면적)) × (1 − 응애 피해(2주 평균 밀도))
[[nodiscard]] double foliar_photosynthesis_factor(const Crop& crop, const FoliarDiseaseParams& params);
using FoliarOnsets = std::array<std::optional<FoliarDiseaseOnsetEvent>, kFoliarDiseaseCount>;
// h6 (4.2~4.4): 탄저병 → 잿빛곰팡이 → 흰가루병(희석 포함). s_N은 update_n_susceptibility 값.
// leaf_before_growth_g_per_m2 = 이번 틱 분배 전 잎 건물 (희석: 늘었으면 inf_p × 이전 ÷ 지금)
void update_foliar_infection(Crop& crop, const SoilCell& cell, const Params& params, const CropEnvironment& env,
                             double n_susceptibility_factor, double leaf_before_growth_g_per_m2,
                             std::uint32_t tile_index, FoliarOnsets& onsets);
// h7 (4.5): 응애·칠레이리응애, 응애 2주 이동 평균
void update_mites(Crop& crop, const FoliarDiseaseParams& params, const CropEnvironment& env, std::uint32_t tile_index,
                  FoliarOnsets& onsets);
// (M5b, 15.3 항목 11) 천적 하우스 단위 탐색: 틱 시작(명령 뒤, 셀 계산 앞) 살아 있는 포기의 천적·응애 합.
// 셀 계산 앞에 모든 포기를 한 번에 옮기므로 셀 순서와 무관하고, 목표를 자르지 않으면 천적 합이 보존돼요
struct PredatorSearchSums {
    double predator_sum = 0.0;  // Σ 살아 있는 포기 D
    double mite_sum = 0.0;      // Σ 살아 있는 포기 M
    std::size_t live_crops = 0;
};
void add_predator_search_sums(PredatorSearchSums& sums, const Crop& crop);
// D_mean·M_mean = 합 ÷ 포기 수. target = M_mean > 0 ? D_mean × M ÷ M_mean : D_mean (0~1로 자름).
// D += predator_search_rate × (target − D). M은 이 포기의 틱 시작 응애
void apply_predator_search(Crop& crop, const PredatorSearchSums& sums, const MiteParams& params);
// 이번 틱 파생값(증가분·포식량·썩은 과실 수)을 0으로 (모든 작물, 틱마다 작물 단계 앞)
void reset_foliar_tick_values(Crop& crop);
// RemovePlant (4.6): 체내 원소·건물(+ 달린 과실, 저장)을 *_removed_g로 내보내고 작물을 비워요. 수확 기록은 남겨요
void remove_plant(Crop& crop, Ledger& ledger);
// ReleasePredators (4.5): 살아 있는 작물만 D = min(1, D + per_m2 × index_per_individual_per_m2)
void release_predators(Crop& crop, const PredatorDef& predator, double per_m2);

// 하우스 포자 원천 합 (셀 계산 뒤, 타일마다 add_foliar_house_sources)
struct FoliarHouseSources {
    double anthracnose_sporulation_sum = 0.0;  // Σ 살아 있는 포기 sporulation_curve(inf_a)
    double botrytis_source_sum = 0.0;          // Σ (src_inf × inf_b × 포자 형성 온도 × 젖음 + src_rot × 썩은 수)
    double powdery_mildew_source_sum = 0.0;    // Σ 살아 있는 포기 src × inf_p
    double spider_mite_sum = 0.0;              // Σ 살아 있는 포기 응애 밀도
    std::size_t live_crops = 0;
};
void add_foliar_house_sources(FoliarHouseSources& sources, const Crop& crop, const Params& params,
                              double house_air_temp_degC);
// 하우스 단계 4 (수증기 뒤): 포자 압력 2개, 탄저병 포자 원천, 하우스 평균 응애, 하우스 안 일사 이동 평균
void update_foliar_house(HouseState& house, const Params& params, const FoliarInit& foliar,
                         const FoliarHouseSources& sources, std::size_t cells);
// 월드 생성: 포자 압력 = 배경값, 원천·응애 평균 = 정식한 포기에서, 일사 평균 = 첫 틱 하우스 안 일사
void init_foliar_house(HouseState& house, const Params& params, const FoliarInit& foliar,
                       const FoliarHouseSources& sources, std::size_t cells);

}  // namespace sim::process
