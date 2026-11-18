#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sim/curve.hpp"

namespace sim {

// 코어가 이해하는 파라미터 스키마 버전. data/params.toml의 schema_version과 같아야 해요.
inline constexpr std::int64_t kParamsSchemaVersion = 4;

// 값은 전부 data/params.toml에서 와요 (코드에 기본값을 두지 않아요).
// 각 항목의 의미와 방향은 docs/spec/의 해당 명세를 따라요.

// docs/spec/soil-temperature.md
struct HouseParams {
    double base_offset_degC = 0.0;
    double solar_gain_degC_per_W_per_m2 = 0.0;
    double solar_transmittance_factor = 0.0;  // soil-water와 공유

    bool operator==(const HouseParams&) const = default;
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

    bool operator==(const RootDamageParams&) const = default;
};

// docs/spec/strawberry-minimal.md
struct CropParams {
    double specific_leaf_area_m2_per_g = 0.0;
    PiecewiseLinear light_interception_curve;  // x = LAI
    double par_fraction = 0.0;
    double light_use_efficiency_g_per_MJ = 0.0;
    PiecewiseLinear temp_curve;  // x = 하우스 기온 °C
    double leaf_allocation_fraction = 0.0;
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

    bool operator==(const CropParams&) const = default;
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
    std::vector<CompostDef> composts;
    std::vector<FertilizerDef> fertilizers;
    std::vector<LimeDef> limes;

    bool operator==(const Params&) const = default;
};

}  // namespace sim
