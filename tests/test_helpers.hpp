#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include "sim/calendar.hpp"
#include "sim/params.hpp"
#include "sim/scenario.hpp"
#include "sim/world.hpp"

namespace test {

inline std::filesystem::path repo_path(const std::string& relative) {
    return std::filesystem::path(SIM_REPO_DIR) / relative;
}

// M1 명세(docs/spec/*.md) 표의 초기값과 같은 파라미터. data/params.toml과 별개인 테스트 고정값이에요.
// 단위 테스트는 이 값에 의존하지 않고 방향만 검증하는 것을 원칙으로 해요.
inline sim::Params test_params() {
    sim::Params p;
    p.schema_version = sim::kParamsSchemaVersion;

    p.house = {.base_offset_degC = 2.0, .solar_gain_degC_per_W_per_m2 = 0.015, .solar_transmittance_factor = 0.7};

    p.soil_water = {
        .evap_mm_per_hour_per_W_per_m2 = 0.0004,
        .evap_base_mm_per_hour = 0.005,
        .evap_moisture_curve = {{{{0.0, 0.0}}, {{0.3, 0.15}}, {{0.7, 0.8}}, {{1.0, 1.0}}}},
        .drainage_rate_per_hour = 0.3,
        .drainage_compaction_curve = {{{{0, 1.0}}, {{0.5, 0.6}}, {{1.0, 0.1}}}},
    };

    p.soil_temperature = {.relax_rate_per_hour = 0.04, .heat_degC_per_g_C = 0.4};

    p.organic_matter = {
        .labile_decay_rate_per_hour = 0.004,
        .stable_decay_rate_per_hour = 0.00002,
        .humification_fraction = 0.3,
        .humus_C_to_N_ratio = 10.0,
        .temp_curve = {{{{0, 0.05}}, {{10, 0.25}}, {{20, 0.55}}, {{30, 0.85}}, {{37, 1.0}}, {{45, 1.0}},
                        {{55, 0.5}}, {{65, 0.1}}}},
        .moisture_curve = {{{{0, 0}}, {{0.2, 0.1}}, {{0.6, 0.8}}, {{0.9, 1.0}}, {{2.0, 1.0}}}},
        .o2_curve = {{{{0, 0.15}}, {{0.3, 0.6}}, {{0.6, 1.0}}, {{1, 1.0}}}},
    };

    p.nitrification = {
        .step1_rate_per_hour = 0.01,
        .step2_rate_per_hour = 0.08,
        .temp_curve = {{{{0, 0}}, {{5, 0.1}}, {{15, 0.5}}, {{25, 0.9}}, {{32, 1.0}}, {{40, 0.6}}, {{50, 0.1}}}},
        .moisture_curve = {{{{0, 0}}, {{0.3, 0.2}}, {{0.6, 0.8}}, {{0.9, 1.0}}, {{2.0, 1.0}}}},
        .step1_ph_curve = {{{{4.5, 0.1}}, {{5.5, 0.5}}, {{6.5, 1.0}}, {{8.0, 1.0}}, {{9.0, 0.6}}}},
        .step2_ph_curve = {{{{4.5, 0.1}}, {{5.5, 0.5}}, {{6.5, 1.0}}, {{7.2, 0.9}}, {{8.0, 0.35}}, {{9.0, 0.1}}}},
        .ammonia_fraction_ph_curve = {{{{6, 0.0006}}, {{7, 0.0056}}, {{7.5, 0.0175}}, {{8, 0.053}}, {{8.5, 0.15}},
                                       {{9, 0.36}}, {{10, 0.85}}}},
        .free_ammonia_inhibition_curve = {{{{0, 1}}, {{0.05, 0.9}}, {{0.3, 0.4}}, {{1.0, 0.1}}, {{3.0, 0.05}}}},
        .nitrite_toxicity_threshold_g_N_per_m2 = 2.0,
        .nitrite_alarm_reset_fraction = 0.8,
        .step1_o2_curve = {{{{0, 0.05}}, {{0.3, 0.5}}, {{0.6, 1.0}}, {{1, 1.0}}}},
        .step2_o2_curve = {{{{0, 0.0}}, {{0.3, 0.2}}, {{0.6, 0.8}}, {{0.8, 1.0}}, {{1, 1.0}}}},
        .nitrite_toxicity_ph_curve = {{{{4.5, 8.0}}, {{5.5, 3.0}}, {{6.5, 1.3}}, {{7.0, 1.0}}, {{9.0, 1.0}}}},
    };

    p.cation_exchange = {.cec_organic_eq_per_g_C = 0.0015, .exchange_rate_per_hour = 0.3, .nh4_retention_factor = 70.0,
                         .k_retention_factor = 250.0, .ca_retention_factor = 760.0, .mg_retention_factor = 380.0,
                         .solver_iterations = 60, .buffer_eq_per_pH_per_eq_cec = 0.15};
    p.antagonism = {.ca_curve = {{{{0.0, 1.0}}, {{0.3, 1.0}}, {{0.6, 0.5}}, {{1.0, 0.2}}, {{2.0, 0.05}}}},
                    .mg_curve = {{{{0.0, 1.0}}, {{0.25, 1.0}}, {{0.5, 0.4}}, {{1.0, 0.15}}, {{2.0, 0.05}}}},
                    .min_demand_factor = 0.6};
    p.phosphorus = {.fixation_rate_per_hour = 0.0015,
                    .fixation_ph_curve = {{{{4.5, 1.0}}, {{5.5, 0.6}}, {{6.3, 0.2}}, {{6.8, 0.2}}, {{7.5, 0.6}}, {{8.5, 1.0}}}},
                    .release_rate_per_hour = 0.00005};
    p.manganese = {
        .retention_ph_curve = {{{{4.5, 450.0}}, {{5.0, 500.0}}, {{5.5, 800.0}}, {{6.0, 1200.0}}, {{6.5, 1500.0}}, {{7.5, 1800.0}}, {{8.5, 2000.0}}}},
        .oxidation_rate_per_hour = 0.002,
        .oxidation_ph_curve = {{{{4.5, 0.005}}, {{5.0, 0.02}}, {{5.3, 0.06}}, {{5.5, 0.12}}, {{5.8, 0.3}}, {{6.0, 0.5}}, {{6.5, 1.0}}, {{7.0, 2.0}}, {{7.5, 4.0}}, {{8.5, 10.0}}}},
        .oxidation_o2_curve = {{{{0, 0.0}}, {{0.3, 0.2}}, {{0.6, 0.8}}, {{0.8, 1.0}}, {{1, 1.0}}}},
        .reduction_rate_per_hour = 0.00006,
        .reduction_ph_curve = {{{{4.5, 6.0}}, {{5.0, 3.0}}, {{5.5, 1.8}}, {{6.5, 1.0}}, {{7.5, 0.5}}, {{8.5, 0.3}}}},
        .reduction_o2_curve = {{{{0, 20.0}}, {{0.2, 10.0}}, {{0.4, 4.0}}, {{0.6, 1.5}}, {{0.8, 1.0}}, {{1, 1.0}}}},
    };

    // ---- M2 (docs/spec/aeration.md, soil-chemistry.md, strawberry-minimal.md) ----
    p.aeration = {
        .compaction_cap_curve = {{{{0, 1.0}}, {{0.5, 0.8}}, {{1.0, 0.35}}}},
        .organic_C_cap_bonus_per_g_C = 0.00004,
        .compaction_per_mm_irrigation = 0.0015,
        .tilled_compaction_index = 0.1,
        .till_root_damage_fraction = 0.5,
    };
    p.ions = {.nh4_N_g_per_eq = 14.007, .no3_N_g_per_eq = 14.007, .no2_N_g_per_eq = 14.007, .k_g_per_eq = 39.098,
              .ca_g_per_eq = 20.039, .mg_g_per_eq = 12.153, .p_g_per_eq = 30.974, .s_g_per_eq = 16.03,
              .mn_g_per_eq = 27.469};
    p.soil_chemistry = {
        .p_solution_fraction = 0.002,
        .s_solution_fraction = 0.8,
        .nitrification_H_eq_per_eq_N = 2.0,
        .uptake_H_eq_per_eq_NH4 = 1.0,
        .uptake_H_eq_per_eq_NO3 = -1.0,
        .organic_C_buffer_eq_per_g_C_per_pH = 0.002,
        .ph_min = 3.0,
        .ph_max = 10.0,
        .lime_dissolution_rate_per_hour = 0.004,
        .lime_dissolution_ph_curve = {{{{4.5, 1.0}}, {{5.5, 0.7}}, {{6.5, 0.3}}, {{7.5, 0.05}}, {{8.0, 0.0}}}},
        .lime_dissolution_moisture_curve = {{{{0, 0}}, {{0.3, 0.3}}, {{0.7, 1.0}}, {{2.0, 1.0}}}},
        .ec_meq_per_L_per_dS_per_m = 10.0,
        .ec_min_water_mm = 1.0,
    };
    p.availability = {
        .n_ph_curve = {{{{4.0, 0.5}}, {{5.0, 0.9}}, {{5.5, 1.0}}, {{8.0, 1.0}}, {{9.0, 0.8}}}},
        .p_ph_curve = {{{{4.5, 0.2}}, {{5.5, 0.6}}, {{6.0, 0.95}}, {{6.8, 1.0}}, {{7.5, 0.35}}, {{8.0, 0.1}}, {{8.5, 0.05}}}},
        .k_ph_curve = {{{{4.0, 0.5}}, {{5.0, 0.8}}, {{5.8, 1.0}}, {{9.0, 1.0}}}},
        .ca_ph_curve = {{{{4.0, 0.3}}, {{5.0, 0.55}}, {{5.5, 0.75}}, {{6.5, 1.0}}, {{9.0, 1.0}}}},
        .mg_ph_curve = {{{{4.0, 0.3}}, {{5.0, 0.55}}, {{5.5, 0.75}}, {{6.5, 1.0}}, {{9.0, 1.0}}}},
    };
    p.root_damage = {
        .waterlog_curve = {{{{0, 0.01}}, {{0.2, 0.004}}, {{0.4, 0.0005}}, {{0.5, 0}}}},
        .heat_curve = {{{{32, 0}}, {{35, 0.002}}, {{40, 0.01}}, {{45, 0.03}}}},
        .nitrite_curve = {{{{0, 0}}, {{2, 0}}, {{4, 0.004}}, {{8, 0.015}}}},
        .acid_curve = {{{{4.0, 0.006}}, {{4.6, 0.0018}}, {{5.0, 0.0006}}, {{5.4, 0.0001}}, {{5.6, 0.0}}}},
    };
    p.crop = {
        .specific_leaf_area_m2_per_g = 0.02,
        .light_interception_curve = {{{{0, 0}}, {{0.5, 0.3}}, {{1, 0.5}}, {{2, 0.75}}, {{3, 0.88}}, {{4, 0.94}}}},
        .par_fraction = 0.48,
        .light_use_efficiency_g_per_MJ = 2.5,
        .temp_curve = {{{{3, 0}}, {{8, 0.3}}, {{15, 0.8}}, {{20, 1.0}}, {{25, 1.0}}, {{30, 0.6}}, {{35, 0.2}}, {{40, 0}}}},
        .leaf_allocation_fraction = 0.7,
        .water_stress_curve = {{{{0.2, 0}}, {{0.4, 0.3}}, {{0.6, 0.9}}, {{0.75, 1.0}}, {{2.0, 1.0}}}},
        .salt_stress_curve = {{{{0, 1}}, {{2.0, 1.0}}, {{3.5, 0.6}}, {{5.5, 0.25}}, {{8, 0.05}}}},
        .nutrient_stress_curve = {{{{0, 0}}, {{0.5, 0.3}}, {{0.8, 0.8}}, {{1.0, 1.0}}}},
        .target_N_g_per_g = 0.028,
        .target_P_g_per_g = 0.0035,
        .target_K_g_per_g = 0.020,
        .target_Ca_g_per_g = 0.012,
        .target_Mg_g_per_g = 0.004,
        .luxury_uptake_ratio = 1.3,
        .uptake_rate_per_g_root_per_hour = 0.002,
        .ca_uptake_rate_per_g_root_per_hour = 0.0001,
        .mg_uptake_rate_per_g_root_per_hour = 0.0001,
        .transpiration_mm_per_hour_per_W_per_m2 = 0.0015,
        .root_recovery_rate_per_hour = 0.002,
        .death_root_health = 0.05,
        .root_dieback_per_damage = 1.0,
        .dry_matter_C_fraction = 0.45,
        .target_Mn_g_per_g = 0.0001,
        .mn_uptake_rate_per_g_root_per_hour = 0.0002,
        .mn_max_uptake_ratio = 30.0,
        .mn_toxicity_curve = {{{{0, 1}}, {{5, 1}}, {{10, 0.7}}, {{20, 0.3}}, {{30, 0.1}}}},
    };

    p.composts = {
        {.id = "immature_manure", .name = "미숙 우분 퇴비", .C_g_per_kg = 180, .N_g_per_kg = 12,
         .labile_C_fraction = 0.6, .labile_N_fraction = 0.7, .k_g_per_kg = 12, .ca_g_per_kg = 10, .mg_g_per_kg = 4,
         .p_g_per_kg = 4, .s_g_per_kg = 2, .direct_acid_eq_per_kg = 0},
        {.id = "mature_manure", .name = "숙성 우분 퇴비", .C_g_per_kg = 150, .N_g_per_kg = 12,
         .labile_C_fraction = 0.1, .labile_N_fraction = 0.1, .k_g_per_kg = 14, .ca_g_per_kg = 15, .mg_g_per_kg = 5,
         .p_g_per_kg = 5, .s_g_per_kg = 2, .direct_acid_eq_per_kg = 0},
        {.id = "bark_compost", .name = "수피·톱밥 퇴비", .C_g_per_kg = 250, .N_g_per_kg = 5,
         .labile_C_fraction = 0.4, .labile_N_fraction = 0.3, .k_g_per_kg = 3, .ca_g_per_kg = 5, .mg_g_per_kg = 1,
         .p_g_per_kg = 1, .s_g_per_kg = 1, .direct_acid_eq_per_kg = 0},
    };
    p.fertilizers = {
        {.id = "ammonium_sulfate", .name = "황산암모늄", .nh4_N_g_per_kg = 210, .no3_N_g_per_kg = 0, .s_g_per_kg = 240},
        {.id = "urea", .name = "요소", .nh4_N_g_per_kg = 460, .no3_N_g_per_kg = 0},
        {.id = "calcium_nitrate", .name = "질산칼슘", .nh4_N_g_per_kg = 11, .no3_N_g_per_kg = 144, .ca_g_per_kg = 190},
        {.id = "potassium_sulfate", .name = "황산칼륨", .k_g_per_kg = 415, .s_g_per_kg = 180},
        {.id = "superphosphate", .name = "과인산석회", .ca_g_per_kg = 180, .p_g_per_kg = 87, .s_g_per_kg = 110},
        {.id = "magnesium_sulfate", .name = "황산고토", .mg_g_per_kg = 98, .s_g_per_kg = 130},
    };
    p.limes = {
        {.id = "calcium_carbonate", .name = "탄산석회", .eq_per_kg = 20.0, .ca_fraction = 1.0, .mg_fraction = 0.0},
        {.id = "dolomite", .name = "고토석회", .eq_per_kg = 21.7, .ca_fraction = 0.6, .mg_fraction = 0.4},
    };
    return p;
}

// m1-overview.md "시나리오 TOML" 예시와 같은 균일 토양
inline sim::SoilInit test_soil() {
    return sim::SoilInit{
        .field_capacity_mm = 60.0,
        .water_mm = 45.0,
        .soil_temp_degC = 20.0,
        .ph = 6.5,
        .nh4_N_g_per_m2 = 2.0,
        .no2_N_g_per_m2 = 0.0,
        .no3_N_g_per_m2 = 10.0,
        .labile_C_g_per_m2 = 50.0,
        .labile_N_g_per_m2 = 5.0,
        .stable_C_g_per_m2 = 3600.0,
        .stable_N_g_per_m2 = 360.0,
        .saturation_mm = 110.0,
        .compaction_index = 0.3,
        .base_buffer_eq_per_m2_per_pH = 1.25,
        .k_g_per_m2 = 60.0,
        .ca_g_per_m2 = 400.0,
        .mg_g_per_m2 = 60.0,
        .p_g_per_m2 = 25.0,
        .s_g_per_m2 = 3.0,
        .cec_mineral_eq_per_m2 = 25.0,
        .mn_g_per_m2 = 5.0,
        .mn_oxide_g_per_m2 = 100.0,
        .p_fixed_g_per_m2 = 150.0,
    };
}

// test_soil()로 만든 셀 하나
inline sim::SoilCell test_cell() {
    const sim::SoilInit s = test_soil();
    sim::SoilCell cell{
        .field_capacity_mm = s.field_capacity_mm,
        .water_mm = s.water_mm,
        .drained_mm_this_tick = 0.0,
        .soil_temp_degC = s.soil_temp_degC,
        .labile_C_g_per_m2 = s.labile_C_g_per_m2,
        .labile_N_g_per_m2 = s.labile_N_g_per_m2,
        .stable_C_g_per_m2 = s.stable_C_g_per_m2,
        .stable_N_g_per_m2 = s.stable_N_g_per_m2,
        .nh4_N_g_per_m2 = s.nh4_N_g_per_m2,
        .no2_N_g_per_m2 = s.no2_N_g_per_m2,
        .no3_N_g_per_m2 = s.no3_N_g_per_m2,
        .ph = s.ph,
        .nitrite_alarm = false,
    };
    cell.saturation_mm = s.saturation_mm;
    cell.base_buffer_eq_per_m2_per_pH = s.base_buffer_eq_per_m2_per_pH;
    cell.compaction_index = s.compaction_index;
    cell.aeration_index = 1.0;  // 산소 충분 (M1 단위 테스트의 전제)
    cell.k_g_per_m2 = s.k_g_per_m2;
    cell.ca_g_per_m2 = s.ca_g_per_m2;
    cell.mg_g_per_m2 = s.mg_g_per_m2;
    cell.p_g_per_m2 = s.p_g_per_m2;
    cell.s_g_per_m2 = s.s_g_per_m2;
    cell.cec_mineral_eq_per_m2 = s.cec_mineral_eq_per_m2;
    cell.mn_g_per_m2 = s.mn_g_per_m2;
    cell.mn_oxide_g_per_m2 = s.mn_oxide_g_per_m2;
    cell.p_fixed_g_per_m2 = s.p_fixed_g_per_m2;
    return cell;
}

// strawberry-minimal 프로토타입 기준 정식 상태 (잎 30, 뿌리 10 g/m²)
inline sim::CropInit test_crop_init() { return sim::CropInit{.leaf_g_per_m2 = 30.0, .root_g_per_m2 = 10.0}; }

// 시간마다 값이 다른 기상 표: 조회 위치가 틀리면 바로 드러나요.
inline sim::Scenario test_scenario(int start_day_of_year = 1, int rows = 2, int cols = 3) {
    sim::Scenario s;
    s.name = "test";
    s.seed = 12345;
    s.start_day_of_year = start_day_of_year;
    s.bed_rows = rows;
    s.bed_cols = cols;
    s.soil = test_soil();
    s.weather_hourly.reserve(sim::kHoursPerYear);
    for (int day = 1; day <= sim::kDaysPerYear; ++day) {
        for (int hour = 0; hour < sim::kHoursPerDay; ++hour) {
            s.weather_hourly.push_back(sim::WeatherHour{
                .outside_air_temp_degC = day / 20.0 + hour / 100.0,
                .solar_radiation_W_per_m2 = (hour >= 6 && hour < 18) ? hour * 10.0 : 0.0,
                .outside_rh_percent = 50.0,
            });
        }
    }
    return s;
}

inline sim::World make_world(sim::Scenario scenario = test_scenario()) {
    auto world = sim::World::create(test_params(), std::move(scenario));
    if (!world) {
        throw std::runtime_error(world.error().message);
    }
    return std::move(*world);
}

// enqueue 결과를 바로 꺼내는 테스트용 도우미 (실패하면 예외)
inline std::uint64_t must_enqueue(sim::World& world, sim::Command command) {
    auto seq = world.enqueue(std::move(command));
    if (!seq) {
        throw std::runtime_error(seq.error().message);
    }
    return *seq;
}

}  // namespace test
