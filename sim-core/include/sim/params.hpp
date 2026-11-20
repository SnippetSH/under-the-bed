#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sim/curve.hpp"
#include "sim/soil.hpp"

namespace sim {

// 코어가 이해하는 파라미터 스키마 버전. data/params.toml의 schema_version과 같아야 해요.
inline constexpr std::int64_t kParamsSchemaVersion = 7;  // M5b

// 값은 전부 data/params.toml에서 와요 (코드에 기본값을 두지 않아요).
// 각 항목의 의미와 방향은 docs/spec/의 해당 명세를 따라요.

// docs/spec/house-climate.md (M4: 다중 피복·측창·난방·CO2. M1 계수 3개는 삭제)
struct HouseParams {
    double layer_transmittance_factor = 0.0;       // 피복 한 겹의 일사 투과율
    double layer_resistance_m2K_per_W = 0.0;       // 피복 한 겹(+ 공기층)의 단열 저항
    double infiltration_W_per_m2_per_K = 0.0;      // 창을 닫아도 새는 열
    double window_bypass_fraction = 0.0;           // 완전히 열린 창 하나가 그 겹의 단열을 없애는 비율 (개방률에 비례)
    std::vector<double> window_open_levels;        // 허용 개방률 (0부터 엄격 오름차순, 0~1. 디렉터 결정: 0, 0.3, 0.5, 0.7, 1.0)
    double vent_conductance_W_per_m2_per_K = 0.0;  // 모든 겹 양쪽이 열렸을 때 환기 열 교환
    double solar_heat_gain_fraction = 0.0;         // 투과 일사 중 기온을 올리는 몫
    double ground_heat_W_per_m2 = 0.0;             // 지중 열 등 기본 열 유입
    double air_relax_rate_per_hour = 0.0;          // 열 관성
    double heater_capacity_W_per_m2 = 0.0;
    double heater_setpoint_min_degC = 0.0;  // SetHeater 설정 온도 허용 범위 (enqueue 검증)
    double heater_setpoint_max_degC = 0.0;
    double temp_mean_relax_rate_per_hour = 0.0;    // 기온 이동 평균 (strawberry-phenology)
    std::array<std::int64_t, 2> cover_change_window_doy{};  // 피복 겹수 변경 가능 기간 [시작, 끝] day_of_year
    std::int64_t min_cover_layers = 0;
    std::int64_t max_cover_layers = 0;
    // CO2
    std::vector<double> outside_co2_ppm_by_hour;  // 0~23시, 24개
    double air_volume_m3_per_m2 = 0.0;
    double co2_mg_per_m3_per_ppm = 0.0;
    double infiltration_air_changes_per_hour = 0.0;
    double vent_air_changes_per_hour = 0.0;
    double co2_floor_ppm = 0.0;

    bool operator==(const HouseParams&) const = default;
};

// docs/spec/house-humidity.md 3장 (M5b, [humidity])
struct HumidityParams {
    PiecewiseLinear saturation_vapor_curve;     // x = 기온 °C, y = 포화 수증기 g/m³
    double inner_surface_resistance_m2K_per_W = 0.0;
    double condensation_rate_per_hour = 0.0;
    double rh_mean_relax_rate_per_hour = 0.0;
    double vent_airflow_factor = 0.0;
    double fan_airflow_index = 0.0;
    double canopy_rh_boost_index = 0.0;
    double canopy_lai_ref = 0.0;
    double dew_rh_index = 0.0;
    double dew_solar_limit_W_per_m2 = 0.0;
    double wetting_rate_per_hour = 0.0;
    double drying_rate_per_hour = 0.0;
    double airflow_drying_factor = 0.0;
    PiecewiseLinear drying_rh_curve;            // x = 잎 사이 습도, y = 마름 배율
    double drying_solar_factor_per_W_per_m2 = 0.0;
    double overhead_wet_index = 0.0;
    double wet_threshold_index = 0.0;
    double wetness_break_hours = 0.0;

    bool operator==(const HumidityParams&) const = default;
};

// docs/spec/strawberry-phenology.md "낮 길이"
struct CalendarParams {
    PiecewiseLinear day_length_curve;  // x = day_of_year, y = 낮 길이 h

    bool operator==(const CalendarParams&) const = default;
};

// docs/spec/strawberry-phenology.md
struct PhenologyParams {
    double establishment_rate_per_hour = 0.0;
    PiecewiseLinear establishment_temp_curve;    // x = 하우스 기온 °C
    PiecewiseLinear establishment_growth_curve;  // x = 활착 정도, y = 생장·흡수 배율
    double induction_rate_per_hour = 0.0;
    PiecewiseLinear induction_day_length_curve;  // x = 낮 길이 h
    PiecewiseLinear induction_temp_curve;        // x = 하우스 기온 °C
    PiecewiseLinear induction_nitrogen_curve;    // x = 체내 N / N 요구량
    double truss_base_temp_degC = 0.0;
    double truss_degree_days_to_anthesis = 0.0;
    double induction_spread_index = 0.0;  // 정식 때 셀마다 화아분화 진행도를 0~이 값만큼 늦춤 (포기 간 차이, biology 난수, 0~1)
    PiecewiseLinear flowers_per_truss_curve;  // x = 포기당 관부 g, y = 포기당 꽃 수

    bool operator==(const PhenologyParams&) const = default;
};

// docs/spec/strawberry-phenology.md "모종 등급" (ADR-0006)
struct SeedlingGradeDef {
    std::string id;
    double leaf_g_per_plant = 0.0;
    double crown_g_per_plant = 0.0;
    double root_g_per_plant = 0.0;
    double initial_induction_index = 0.0;  // 초기 화아분화 진행도
    // M5b 모종 병해 (foliar-disease 3.6, 디렉터 결정 8). 정식 때 Foliar 난수로 포기마다 판정
    double latent_anthracnose_fraction = 0.0;  // 잠복 탄저 포기 비율
    double latent_anthracnose_index = 0.0;     // 잠복 포기의 시작 감염 지수
    double spider_mite_fraction = 0.0;         // 응애를 달고 온 포기 비율
    double spider_mite_start_index = 0.0;      // 그 포기의 시작 응애 지수
    double powdery_mildew_start_index = 0.0;   // 모든 포기의 시작 병든 잎 면적 (난수 없음)

    bool operator==(const SeedlingGradeDef&) const = default;
};

// docs/spec/strawberry-fruit.md "과실" (과위 배열 4개 rank_*는 길이가 같아요 = 과위 수)
struct FruitParams {
    std::vector<double> rank_fraction;
    std::vector<double> rank_anthesis_delay_days;
    double anthesis_jitter_days = 0.0;  // 과실마다 개화 시점 편차 (±, 일)
    std::vector<double> rank_potential_fresh_g;
    double size_variation_fraction = 0.0;
    PiecewiseLinear pollination_mean_curve;  // x = 하우스 기온 이동 평균 °C
    double pollination_spread = 0.0;
    double set_threshold = 0.0;
    double deformed_threshold = 0.0;
    PiecewiseLinear pollination_size_curve;  // x = 수정률
    double dry_matter_fraction = 0.0;
    double base_temp_degC = 0.0;
    double degree_days_to_harvest = 0.0;
    PiecewiseLinear growth_shape_curve;      // x = 진행도, y = 최대 크기 중 도달 비율
    std::vector<double> size_class_bounds_g;  // 오름차순, 구간 수 = 경계 수 + 1
    double base_brix = 0.0;
    PiecewiseLinear brix_supply_curve;   // x = 크기 충족도 이동 평균 (size_satisfaction_mean)
    PiecewiseLinear brix_k_curve;        // x = K 충족도
    PiecewiseLinear brix_n_excess_curve; // x = N 과잉 지수
    PiecewiseLinear brix_water_curve;    // x = relative_water
    PiecewiseLinear firmness_ca_curve;   // x = Ca 충족도
    PiecewiseLinear firmness_temp_curve; // x = 하우스 기온 °C
    PiecewiseLinear firmness_n_excess_curve;
    // 6b 낙과, 6c 적화·적과 (2026-11-18 디렉터 결정)
    double abortion_rate_per_development_index = 0.0;  // 진행도 1당 낙과 확률 (p_tick = 이 값 × dD × 곡선 × 과위 배율)
    double abortion_window_index = 0.0;
    PiecewiseLinear abortion_satisfaction_curve;  // x = size_satisfaction_mean
    std::vector<double> rank_abortion_weight;     // 과위 수와 같은 길이
    double thinnable_max_index = 0.0;
    // 당도 온도 주·야간 이중 가중 (2026-11-18 디렉터 결정)
    PiecewiseLinear brix_day_temp_score_curve;    // x = 낮 하우스 기온 °C, y = S_day
    PiecewiseLinear brix_night_temp_score_curve;  // x = 밤 하우스 기온 °C, y = S_night
    double brix_day_weight = 0.0;
    double brix_night_weight = 0.0;
    PiecewiseLinear brix_temp_score_curve;        // x = K_temp, y = 당도 배율
    double day_solar_threshold_W_per_m2 = 0.0;

    bool operator==(const FruitParams&) const = default;
};

// ---- M5b 지상부 병해충 (docs/spec/foliar-disease.md 3장) ----

// [foliar_disease.anthracnose] (3.2)
struct AnthracnoseParams {
    PiecewiseLinear temp_curve;      // x = 하우스 기온 °C
    PiecewiseLinear wetness_curve;   // x = 연속 젖음 시간 h
    double contact_rate_per_hour = 0.0;
    double progress_rate_per_hour = 0.0;
    double splash_transfer_index = 0.0;  // 두상 관수 1회(틱)가 압력 1에서 옮기는 감염
    PiecewiseLinear sporulation_curve;   // x = 감염 지수, y = 포자(물 튐 원천)
    double onset_infection_index = 0.0;
    // M5b (15.3 항목 34): 병든 관부·잔재가 흙 접종원을 쌓아요 (살아 있는 포기, 시간당 × sporulation_curve(감염))
    double soil_deposit_per_hour = 0.0;
    double residue_index = 0.0;  // 포기가 죽을 때 한 번 남기는 접종원 (× 감염 지수). 뽑으면 남기지 않음

    bool operator==(const AnthracnoseParams&) const = default;
};

// [foliar_disease.botrytis] (3.3)
struct BotrytisParams {
    PiecewiseLinear infection_temp_curve;  // x = 하우스 기온 °C
    PiecewiseLinear wetness_curve;         // x = 연속 젖음 시간 h
    double infection_rate_per_hour = 0.0;
    double infection_decay_rate_per_hour = 0.0;
    PiecewiseLinear sporulation_temp_curve;  // x = 하우스 기온 °C
    double spore_source_per_infection_per_hour = 0.0;
    double spore_source_per_rotten_fruit_per_hour = 0.0;
    double spore_decay_rate_per_hour = 0.0;
    double spore_vent_fraction = 0.0;  // 환기 횟수 1당 빠지는 비율
    double fruit_rot_prob_per_hour = 0.0;
    PiecewiseLinear fruit_stage_curve;  // x = 과실 발달 0~1
    double onset_infection_index = 0.0;

    bool operator==(const BotrytisParams&) const = default;
};

// [foliar_disease.powdery_mildew] (3.4)
struct PowderyMildewParams {
    PiecewiseLinear temp_curve;        // x = 하우스 기온 °C
    PiecewiseLinear rh_curve;          // x = canopy_rh_index
    double free_water_inhibition_factor = 0.0;
    PiecewiseLinear light_curve;       // x = 하우스 안 일사 하루 이동 평균 W/m²
    PiecewiseLinear density_curve;     // x = LAI
    PiecewiseLinear young_leaf_curve;  // x = rgr_mean_per_day
    double infection_rate_per_hour = 0.0;
    double spore_source_per_infection_per_hour = 0.0;
    double spore_decay_rate_per_hour = 0.0;
    double spore_vent_fraction = 0.0;
    PiecewiseLinear photosynthesis_damage_curve;  // x = 병든 잎 면적, y = 총생산 감소 비율
    PiecewiseLinear fruit_prob_curve;             // x = 병든 잎 면적, y = 흰가루 과실 확률
    double onset_infection_index = 0.0;

    bool operator==(const PowderyMildewParams&) const = default;
};

// [foliar_disease.mites] (3.5)
struct MiteParams {
    PiecewiseLinear mite_growth_curve_per_hour;  // x = 하우스 기온 °C, y = 시간당 내적 증가율
    PiecewiseLinear mite_rh_curve;               // x = house_rh_mean_index
    double mite_spread_rate_per_hour = 0.0;
    PiecewiseLinear photosynthesis_damage_curve;  // x = spider_mite_mean_index
    PiecewiseLinear predator_temp_curve;          // x = 하우스 기온 °C
    PiecewiseLinear predator_hatch_curve;         // x = house_rh_mean_index
    double attack_rate_per_hour = 0.0;
    double handling_time_hours = 0.0;
    double conversion_factor = 0.0;
    double predator_death_rate_per_hour = 0.0;
    double predator_starvation_death_per_hour = 0.0;
    // M5b (15.3 항목 33): x = 하우스 기온 °C, y = 기본 사망·굶주림 사망 배율 (추우면 대사가 느려 덜 죽음)
    PiecewiseLinear predator_death_temp_curve;
    double predator_search_rate_per_hour = 0.0;  // M5b (15.3 항목 11): 천적이 하우스 단위로 응애 많은 포기로 모임 (0~1)
    double mite_onset_index = 0.0;

    bool operator==(const MiteParams&) const = default;
};

// [foliar_disease] 공통 (3.1) + 하위 4개
struct FoliarDiseaseParams {
    PiecewiseLinear n_susceptibility_curve;  // x = soil_mineral_N_mean g/m², y = 감염 배율
    double n_mean_relax_rate_per_hour = 0.0;
    double mite_mean_relax_rate_per_hour = 0.0;
    AnthracnoseParams anthracnose;
    BotrytisParams botrytis;
    PowderyMildewParams powdery_mildew;
    MiteParams mites;

    bool operator==(const FoliarDiseaseParams&) const = default;
};

// 천적 제품 `[[predator]]` (3.5)
struct PredatorDef {
    std::string id;
    std::string name;
    double index_per_individual_per_m2 = 0.0;  // 1마리/m²당 천적 지수

    bool operator==(const PredatorDef&) const = default;
};

// docs/spec/denitrification.md (M4)
struct DenitrificationParams {
    double rate_per_hour = 0.0;
    PiecewiseLinear o2_curve;      // x = aeration_index
    PiecewiseLinear temp_curve;    // x = soil_temp_degC
    PiecewiseLinear carbon_curve;  // x = labile_C g/m²
    double H_eq_per_eq_N = 0.0;    // 음수 = H+ 소비(염기)

    bool operator==(const DenitrificationParams&) const = default;
};

// docs/spec/soil-water.md
struct SoilWaterParams {
    double evap_mm_per_hour_per_W_per_m2 = 0.0;
    double evap_base_mm_per_hour = 0.0;
    PiecewiseLinear evap_moisture_curve;  // x = relative_water
    double drainage_rate_per_hour = 0.0;
    PiecewiseLinear drainage_compaction_curve;  // x = compaction_index, y = 배수 속도 배율 (M2, aeration)

    bool operator==(const SoilWaterParams&) const = default;
};

// docs/spec/soil-temperature.md
struct SoilTemperatureParams {
    double relax_rate_per_hour = 0.0;
    double heat_degC_per_g_C = 0.0;
    double evap_cooling_degC_per_mm = 0.0;  // 증발 1 mm당 토양 온도 하강 (soil-temperature)
    // M5a 프로토타입 3차: 깊은 흙 온도 항. 완화 목표 중 깊은 흙 온도의 비율(0~1, 0이면 이전 식 그대로)
    double deep_coupling_fraction = 0.0;
    double deep_relax_rate_per_hour = 0.0;  // 깊은 흙 온도가 근권 온도를 따라가는 시간당 비율 (0~1)

    bool operator==(const SoilTemperatureParams&) const = default;
};

// docs/spec/organic-matter.md
struct OrganicMatterParams {
    double labile_decay_rate_per_hour = 0.0;
    double stable_decay_rate_per_hour = 0.0;
    double humification_fraction = 0.0;
    double humus_C_to_N_ratio = 0.0;
    PiecewiseLinear temp_curve;      // x = soil_temp_degC
    PiecewiseLinear moisture_curve;  // x = relative_water
    PiecewiseLinear o2_curve;        // x = aeration_index (M2)

    bool operator==(const OrganicMatterParams&) const = default;
};

// docs/spec/nitrogen.md
struct NitrificationParams {
    double step1_rate_per_hour = 0.0;
    double step2_rate_per_hour = 0.0;
    PiecewiseLinear temp_curve;                     // x = soil_temp_degC
    PiecewiseLinear moisture_curve;                 // x = relative_water
    PiecewiseLinear step1_ph_curve;                 // x = pH
    PiecewiseLinear step2_ph_curve;                 // x = pH
    PiecewiseLinear ammonia_fraction_ph_curve;      // x = pH, y = NH3 비율
    PiecewiseLinear free_ammonia_inhibition_curve;  // x = 유리 암모니아 지수 g N/m²
    double nitrite_toxicity_threshold_g_N_per_m2 = 0.0;
    double nitrite_alarm_reset_fraction = 0.0;
    PiecewiseLinear step1_o2_curve;             // x = aeration_index (M2)
    PiecewiseLinear step2_o2_curve;             // x = aeration_index, 1단계보다 민감 (M2)
    PiecewiseLinear nitrite_toxicity_ph_curve;  // x = pH, y = 아질산 독성 배율 (M2)

    bool operator==(const NitrificationParams&) const = default;
};

// docs/spec/cation-exchange.md (M3)
struct CationExchangeParams {
    double cec_organic_eq_per_g_C = 0.0;
    double exchange_rate_per_hour = 0.0;
    double nh4_retention_factor = 0.0;
    double k_retention_factor = 0.0;
    double ca_retention_factor = 0.0;
    double mg_retention_factor = 0.0;
    std::int64_t solver_iterations = 0;
    double buffer_eq_per_pH_per_eq_cec = 0.0;

    bool operator==(const CationExchangeParams&) const = default;
};

// docs/spec/cation-exchange.md "흡수 길항" (M3). x = 길항 지수
struct AntagonismParams {
    PiecewiseLinear ca_curve;
    PiecewiseLinear mg_curve;
    double min_demand_factor = 0.0;  // Ca·Mg 수요 상한에 거는 길항 배율의 하한 (흡수 능력에는 적용 안 함)

    bool operator==(const AntagonismParams&) const = default;
};

// docs/spec/phosphorus.md (M3)
struct PhosphorusParams {
    double fixation_rate_per_hour = 0.0;
    PiecewiseLinear fixation_ph_curve;  // x = pH
    double release_rate_per_hour = 0.0;

    bool operator==(const PhosphorusParams&) const = default;
};

// docs/spec/manganese.md (M3)
struct ManganeseParams {
    PiecewiseLinear retention_ph_curve;  // x = pH, y = w_Mn (ADR-0010)
    double oxidation_rate_per_hour = 0.0;
    PiecewiseLinear oxidation_ph_curve;
    PiecewiseLinear oxidation_o2_curve;
    double reduction_rate_per_hour = 0.0;
    PiecewiseLinear reduction_ph_curve;
    PiecewiseLinear reduction_o2_curve;

    bool operator==(const ManganeseParams&) const = default;
};

// docs/spec/aeration.md (M2)
struct AerationParams {
    PiecewiseLinear compaction_cap_curve;  // x = compaction_index, y = 통기 지수 상한
    double organic_C_cap_bonus_per_g_C = 0.0;
    double compaction_per_mm_irrigation = 0.0;
    double tilled_compaction_index = 0.0;
    double till_root_damage_fraction = 0.0;

    bool operator==(const AerationParams&) const = default;
};

// docs/spec/soil-chemistry.md "이온" — 원소 기준 1 당량의 무게 [g/eq]
struct IonParams {
    double nh4_N_g_per_eq = 0.0;
    double no3_N_g_per_eq = 0.0;
    double no2_N_g_per_eq = 0.0;
    double k_g_per_eq = 0.0;
    double ca_g_per_eq = 0.0;
    double mg_g_per_eq = 0.0;
    double p_g_per_eq = 0.0;
    double s_g_per_eq = 0.0;
    double mn_g_per_eq = 0.0;  // M3

    bool operator==(const IonParams&) const = default;
};

// docs/spec/soil-chemistry.md "용액 비율" 외 (M2). K·Ca·Mg·NH4 용액 비율은 M3에서 용액 상태로 대체 (cation-exchange)
struct SoilChemistryParams {
    double p_solution_fraction = 0.0;
    double s_solution_fraction = 0.0;
    double nitrification_H_eq_per_eq_N = 0.0;
    double uptake_H_eq_per_eq_NH4 = 0.0;
    double uptake_H_eq_per_eq_NO3 = 0.0;  // 음수 = OH- 방출
    double organic_C_buffer_eq_per_g_C_per_pH = 0.0;
    double ph_min = 0.0;
    double ph_max = 0.0;
    double lime_dissolution_rate_per_hour = 0.0;
    PiecewiseLinear lime_dissolution_ph_curve;        // x = pH
    PiecewiseLinear lime_dissolution_moisture_curve;  // x = relative_water
    double ec_meq_per_L_per_dS_per_m = 0.0;
    double ec_min_water_mm = 0.0;

    bool operator==(const SoilChemistryParams&) const = default;
};

// docs/spec/soil-chemistry.md "유효도 곡선" (x = pH, y = 0~1)
struct AvailabilityParams {
    PiecewiseLinear n_ph_curve;
    PiecewiseLinear p_ph_curve;
    PiecewiseLinear k_ph_curve;
    PiecewiseLinear ca_ph_curve;
    PiecewiseLinear mg_ph_curve;

    bool operator==(const AvailabilityParams&) const = default;
};

// docs/spec/strawberry-minimal.md "뿌리 건강도" (y = 시간당 손상 비율)
struct RootDamageParams {
    PiecewiseLinear waterlog_curve;  // x = aeration_index
    PiecewiseLinear heat_curve;      // x = soil_temp_degC
    PiecewiseLinear nitrite_curve;   // x = 유효 아질산 g N/m² (pH 배율 적용 후)
    PiecewiseLinear acid_curve;      // x = pH (ADR-0007, 알루미늄 독성 근사)
    // M5a: x = 감염 지수 (soil-microbes 3번), x = 약제별 잔류 × 활성의 합 (disinfection 3번)
    PiecewiseLinear fusarium_curve;
    PiecewiseLinear phytophthora_curve;
    PiecewiseLinear fumigant_curve;
    PiecewiseLinear anthracnose_curve;  // M5b: x = 탄저병 감염 지수 (foliar-disease 3.2)

    bool operator==(const RootDamageParams&) const = default;
};

// docs/spec/strawberry-minimal.md
struct CropParams {
    double specific_leaf_area_m2_per_g = 0.0;
    PiecewiseLinear light_interception_curve;  // x = LAI
    double par_fraction = 0.0;
    double light_use_efficiency_g_per_MJ = 0.0;
    PiecewiseLinear temp_curve;  // x = 하우스 기온 °C
    PiecewiseLinear water_stress_curve;     // x = relative_water
    PiecewiseLinear salt_stress_curve;      // x = ec_dS_per_m
    PiecewiseLinear nutrient_stress_curve;  // x = 충족도
    double target_N_g_per_g = 0.0;
    double target_P_g_per_g = 0.0;
    double target_K_g_per_g = 0.0;
    double target_Ca_g_per_g = 0.0;
    double target_Mg_g_per_g = 0.0;
    double luxury_uptake_ratio = 0.0;
    double uptake_rate_per_g_root_per_hour = 0.0;
    double ca_uptake_rate_per_g_root_per_hour = 0.0;  // M3: Ca·Mg 전용 흡수 속도 (cation-exchange "흡수 길항")
    double mg_uptake_rate_per_g_root_per_hour = 0.0;
    double transpiration_mm_per_hour_per_W_per_m2 = 0.0;
    double root_recovery_rate_per_hour = 0.0;
    double death_root_health = 0.0;
    double root_dieback_per_damage = 0.0;  // 손상 비율 1당 죽는 뿌리 건물 비율 (뿌리 고사)
    double dry_matter_C_fraction = 0.0;    // 건물 중 탄소 비율
    // M3 망간 (docs/spec/manganese.md)
    double target_Mn_g_per_g = 0.0;
    double mn_uptake_rate_per_g_root_per_hour = 0.0;
    double mn_max_uptake_ratio = 0.0;
    PiecewiseLinear mn_toxicity_curve;  // x = 체내 Mn / 적정량
    // M4 (strawberry-fruit, house-climate): 분배·호흡·낙엽·과실 농도·CO2·저온 피해
    double vegetative_leaf_fraction = 0.0;
    double vegetative_crown_fraction = 0.0;
    double vegetative_root_fraction = 0.0;
    double max_fruit_assimilate_fraction = 0.0;
    double maintenance_rate_leaf_per_hour = 0.0;
    double maintenance_rate_crown_per_hour = 0.0;
    double maintenance_rate_root_per_hour = 0.0;
    double maintenance_rate_fruit_per_hour = 0.0;
    PiecewiseLinear maintenance_temp_curve;  // x = 하우스 기온 °C
    double leaf_senescence_rate_per_hour = 0.0;
    double fruit_target_N_g_per_g = 0.0;
    double fruit_target_P_g_per_g = 0.0;
    double fruit_target_K_g_per_g = 0.0;
    double fruit_target_Ca_g_per_g = 0.0;
    double fruit_target_Mg_g_per_g = 0.0;
    double fruit_target_Mn_g_per_g = 0.0;
    double co2_g_per_g_dry_matter = 0.0;
    PiecewiseLinear co2_growth_curve;        // x = 하우스 CO2 ppm
    PiecewiseLinear cold_leaf_damage_curve;  // x = 하우스 기온 °C, y = 시간당 잎 손실 비율
    PiecewiseLinear cold_fruit_loss_curve;   // x = 하우스 기온 °C, y = 시간당 과실·꽃 손실 비율
    // N 전용 사치 흡수 상한·흡수 속도 (strawberry-minimal 3번, M3 Ca·Mg 전용 속도와 같은 방식)
    double n_luxury_max_ratio = 0.0;           // N 수요 상한 (적정 대비, 부드러운 상한의 끝)
    PiecewiseLinear n_luxury_uptake_curve;     // x = 체내 N / 요구량, y = N 흡수 능력 배율
    PiecewiseLinear mn_uptake_regulation_curve; // x = 체내 Mn / 요구량, y = Mn 흡수 능력 배율 (manganese)
    PiecewiseLinear uptake_temp_curve;         // x = 토양 온도 °C, y = 흡수 능력 배율 (모든 원소)
    double n_uptake_rate_per_g_root_per_hour = 0.0;
    // 저장 탄수화물 풀 (strawberry-fruit 4b). capacity 0이면 기능 꺼짐
    double reserve_capacity_fraction = 0.0;       // 저장 상한 = 이 값 × (관부 + 뿌리 건물)
    double reserve_fill_fraction = 0.0;           // 과실이 다 채워진 틱에 영양 생장 몫 중 저장으로 가는 비율
    double reserve_release_rate_per_hour = 0.0;   // 과실 공급 부족 시 틱당 저장에서 꺼낼 수 있는 비율
    // 유지 호흡 활성 보정 (strawberry-fruit 4번, TOMSIM, references/strawberry-respiration-reserves.md)
    PiecewiseLinear maintenance_activity_curve;   // x = 식물 RGR 이동 평균 /일, y = 유지 호흡 배율
    double rgr_mean_relax_rate_per_hour = 0.0;    // RGR 이동 평균의 시간당 완화 비율

    bool operator==(const CropParams&) const = default;
};

// ---- M5a 토양 미생물 (docs/spec/soil-microbes.md) ----

// 무리 하나 `[microbes.<id>]`. 수용력 규칙은 무리마다 코드가 정해요(명세 표):
// decomposer = decomposer_food_curve(labile C), 질산화균 = 1, antagonist = antagonist_food_curve(labile C),
// mycorrhiza = host_curve(뿌리) × p_curve(유효 P), 병원균 = saprophytic + host × 작물 감염 지수.
struct MicrobeGroupParams {
    double growth_rate_per_hour = 0.0;
    PiecewiseLinear temp_curve;      // x = 토양 온도 °C
    PiecewiseLinear moisture_curve;  // x = relative_water
    PiecewiseLinear ph_curve;        // x = pH
    PiecewiseLinear o2_curve;        // x = aeration_index
    double base_death_rate_per_hour = 0.0;
    PiecewiseLinear heat_kill_curve;  // x = 토양 온도 °C, y = 시간당 사멸 비율
    double immigration_rate_per_hour = 0.0;
    MicrobeIndices competition{};  // 다른 무리 -> α (자기 자신은 0)
    // 병원균만 (fusarium, phytophthora, M5b anthracnose). 다른 무리는 0 (로더가 키를 요구하지 않음)
    double saprophytic_capacity_index = 0.0;
    double host_capacity_index = 0.0;

    bool operator==(const MicrobeGroupParams&) const = default;
};

// 공통 `[microbes]`
struct MicrobesParams {
    double capacity_floor_index = 0.0;
    PiecewiseLinear decomposer_activity_curve;  // x = 분해균 지수, y = 분해균계수
    PiecewiseLinear aob_activity_curve;         // x = 1단계균 지수, y = 질산화 1단계 속도 배율
    PiecewiseLinear nob_activity_curve;         // x = 2단계균 지수
    double antagonist_protection_factor = 0.0;
    double mycorrhiza_p_release_factor = 0.0;
    double mycorrhiza_water_relief_factor = 0.0;
    double mycorrhiza_salt_relief_factor = 0.0;
    // M5a 셀 간 차이 (disease 난수 스트림, 0이면 난수를 뽑지 않음, ADR-0013)
    double pathogen_spread_index = 0.0;        // 월드 생성 때 병원균 시작 지수 × (1 + 값 × (2U − 1)), 0~1
    double susceptibility_spread_index = 0.0;  // 정식 때 포기 감수성 1 + 값 × (2U − 1), 0~1
    PiecewiseLinear moist_heat_curve;       // x = relative_water, y = 열 사멸 배율
    PiecewiseLinear decomposer_food_curve;  // x = 이분해성 C g/m², y = 수용력
    PiecewiseLinear antagonist_food_curve;  // x = 이분해성 C g/m²
    PiecewiseLinear mycorrhiza_host_curve;  // x = 뿌리 건물 g/m²
    PiecewiseLinear mycorrhiza_p_curve;     // x = 유효 P g/m²
    std::array<MicrobeGroupParams, kMicrobeGroupCount> groups;  // MicrobeGroup 순서

    bool operator==(const MicrobesParams&) const = default;
};

// 작물 감염 `[soil_disease.<id>]` (SoilDisease 순서)
struct SoilDiseaseParams {
    double infection_rate_per_hour = 0.0;
    PiecewiseLinear infection_env_curve_temp;  // x = 토양 온도 °C
    PiecewiseLinear infection_env_curve_o2;    // x = aeration_index
    PiecewiseLinear infection_env_curve_ph;    // x = pH
    double onset_infection_index = 0.0;        // DiseaseOnset 이벤트 기준

    bool operator==(const SoilDiseaseParams&) const = default;
};

// 유익균 제품 `[[inoculant]]`
struct InoculantDef {
    std::string id;
    std::string name;
    MicrobeIndices index_gain_per_g_per_m2{};  // 무리별 1 g/m²당 지수 증가 (포화식), 없는 무리는 0
    double C_g_per_kg = 0.0;                   // 담체 유기물 (이분해성으로)
    double N_g_per_kg = 0.0;

    bool operator==(const InoculantDef&) const = default;
};

// ---- M5a 소독 (docs/spec/disinfection.md) ----

struct DisinfectionParams {
    double flush_N_g_per_m2_per_index = 0.0;  // 분해균 소독 사멸 지수 1당 유기 N -> NH4+
    double flush_C_to_N_ratio = 0.0;
    double mn_reduction_per_index = 0.0;  // 분해균 소독 사멸 지수 1당 산화물 -> 유효태 비율
    double till_residue_release_fraction = 0.0;

    bool operator==(const DisinfectionParams&) const = default;
};

// 멀칭 필름 `[[mulch]]`
struct MulchDef {
    std::string id;
    std::string name;
    double soil_solar_gain_degC_per_W_per_m2 = 0.0;  // 음수 = 반사로 식음
    double evaporation_factor = 0.0;
    double fumigant_retention_factor = 0.0;
    double canopy_reflect_factor = 0.0;
    // M5b 탄저병 물 튐 두 경로 (foliar-disease 3.2, 디렉터 결정 3). 맨흙 = 1
    double soil_splash_factor = 0.0;   // 흙·잔재 접종원이 포기에 닿는 비율
    double plant_splash_factor = 0.0;  // 병든 포기의 포자가 물방울로 다른 포기에 튀는 비율

    bool operator==(const MulchDef&) const = default;
};

// 소독 약제 `[[disinfectant]]`
struct DisinfectantDef {
    std::string id;
    std::string name;
    double residue_index_per_g_per_m2 = 0.0;
    MicrobeIndices kill_rate_per_hour{};        // 잔류 1·활성 1일 때 무리별 시간당 사멸 비율
    double decay_rate_per_hour = 0.0;           // 필름 없음, 25 °C
    PiecewiseLinear decay_temp_curve;           // x = 토양 온도 °C
    PiecewiseLinear activation_moisture_curve;  // x = relative_water

    bool operator==(const DisinfectantDef&) const = default;
};

// docs/spec/soil-chemistry.md "석회 정의"
struct LimeDef {
    std::string id;
    std::string name;
    double eq_per_kg = 0.0;
    double ca_fraction = 0.0;  // 당량 기준
    double mg_fraction = 0.0;

    bool operator==(const LimeDef&) const = default;
};

// docs/spec/organic-matter.md "퇴비 정의" (+ M2 무기 성분, soil-chemistry)
struct CompostDef {
    std::string id;
    std::string name;
    double C_g_per_kg = 0.0;
    double N_g_per_kg = 0.0;
    double labile_C_fraction = 0.0;
    double labile_N_fraction = 0.0;
    double k_g_per_kg = 0.0;
    double ca_g_per_kg = 0.0;
    double mg_g_per_kg = 0.0;
    double p_g_per_kg = 0.0;
    double s_g_per_kg = 0.0;
    double mn_g_per_kg = 0.0;  // M3
    double direct_acid_eq_per_kg = 0.0;  // 음수 = 염기

    bool operator==(const CompostDef&) const = default;
};

// docs/spec/nitrogen.md "비료 정의"
struct FertilizerDef {
    std::string id;
    std::string name;
    double nh4_N_g_per_kg = 0.0;
    double no3_N_g_per_kg = 0.0;
    double k_g_per_kg = 0.0;
    double ca_g_per_kg = 0.0;
    double mg_g_per_kg = 0.0;
    double p_g_per_kg = 0.0;
    double s_g_per_kg = 0.0;
    double mn_g_per_kg = 0.0;  // M3
    double direct_acid_eq_per_kg = 0.0;  // 질산화·흡수로 설명되지 않는 직접 효과. 음수 = 염기

    bool operator==(const FertilizerDef&) const = default;
};

struct Params {
    std::int64_t schema_version = 0;
    HouseParams house;
    CalendarParams calendar;
    SoilWaterParams soil_water;
    SoilTemperatureParams soil_temperature;
    OrganicMatterParams organic_matter;
    NitrificationParams nitrification;
    CationExchangeParams cation_exchange;
    AntagonismParams antagonism;
    PhosphorusParams phosphorus;
    ManganeseParams manganese;
    AerationParams aeration;
    IonParams ions;
    SoilChemistryParams soil_chemistry;
    AvailabilityParams availability;
    RootDamageParams root_damage;
    CropParams crop;
    PhenologyParams phenology;
    FruitParams fruit;
    DenitrificationParams denitrification;
    MicrobesParams microbes;                                        // M5a
    std::array<SoilDiseaseParams, kSoilDiseaseCount> soil_disease;  // M5a, SoilDisease 순서
    DisinfectionParams disinfection;                                // M5a
    HumidityParams humidity;                                        // M5b
    FoliarDiseaseParams foliar_disease;                             // M5b
    std::vector<SeedlingGradeDef> seedling_grades;
    std::vector<CompostDef> composts;
    std::vector<FertilizerDef> fertilizers;
    std::vector<LimeDef> limes;
    std::vector<MulchDef> mulches;               // M5a
    std::vector<DisinfectantDef> disinfectants;  // M5a
    std::vector<InoculantDef> inoculants;        // M5a
    std::vector<PredatorDef> predators;          // M5b

    bool operator==(const Params&) const = default;
};

}  // namespace sim
