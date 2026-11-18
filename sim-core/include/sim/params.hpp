#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sim/curve.hpp"

namespace sim {

// 코어가 이해하는 파라미터 스키마 버전. data/params.toml의 schema_version과 같아야 해요.
inline constexpr std::int64_t kParamsSchemaVersion = 2;

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

    bool operator==(const NitrificationParams&) const = default;
};

// docs/spec/nitrogen.md "용탈"
struct LeachingParams {
    double nh4_solution_fraction = 0.0;

    bool operator==(const LeachingParams&) const = default;
};

// docs/spec/organic-matter.md "퇴비 정의"
struct CompostDef {
    std::string id;
    std::string name;
    double C_g_per_kg = 0.0;
    double N_g_per_kg = 0.0;
    double labile_C_fraction = 0.0;
    double labile_N_fraction = 0.0;

    bool operator==(const CompostDef&) const = default;
};

// docs/spec/nitrogen.md "비료 정의"
struct FertilizerDef {
    std::string id;
    std::string name;
    double nh4_N_g_per_kg = 0.0;
    double no3_N_g_per_kg = 0.0;

    bool operator==(const FertilizerDef&) const = default;
};

struct Params {
    std::int64_t schema_version = 0;
    HouseParams house;
    SoilWaterParams soil_water;
    SoilTemperatureParams soil_temperature;
    OrganicMatterParams organic_matter;
    NitrificationParams nitrification;
    LeachingParams leaching;
    std::vector<CompostDef> composts;
    std::vector<FertilizerDef> fertilizers;

    bool operator==(const Params&) const = default;
};

}  // namespace sim
