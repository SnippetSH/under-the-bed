#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include "sim/calendar.hpp"
#include "sim/params.hpp"
#include "sim/processes.hpp"
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

    // M4 house-climate 명세 초기값 (3중·창 닫힘·난방 없음에서 M1 식과 거의 같음)
    p.house = {
        .layer_transmittance_factor = 0.9,
        .layer_resistance_m2K_per_W = 0.13,
        .infiltration_W_per_m2_per_K = 0.5,
        .window_bypass_fraction = 0.5,
        .window_open_levels = {0.0, 0.3, 0.5, 0.7, 1.0},
        .vent_conductance_W_per_m2_per_K = 30.0,
        .solar_heat_gain_fraction = 0.18,
        .ground_heat_W_per_m2 = 6.1,
        .air_relax_rate_per_hour = 0.7,
        .heater_capacity_W_per_m2 = 150.0,
        .heater_setpoint_min_degC = 0.0,
        .heater_setpoint_max_degC = 30.0,
        .temp_mean_relax_rate_per_hour = 0.042,
        .cover_change_window_doy = {91, 243},
        .min_cover_layers = 2,
        .max_cover_layers = 4,
        .outside_co2_ppm_by_hour = {420.0, 424.0, 428.8, 439.4, 464.7, 494.6, 495.1, 466.2, 441.8, 431.9, 427.6, 423.9,
                                    420.0, 415.2, 402.8, 388.1, 397.3, 404.6, 405.0, 405.5, 407.0, 409.4, 412.5, 416.1},
        .air_volume_m3_per_m2 = 3.0,
        .co2_mg_per_m3_per_ppm = 1.83,
        .infiltration_air_changes_per_hour = 0.3,
        .vent_air_changes_per_hour = 30.0,
        .co2_floor_ppm = 50.0,
    };
    p.calendar = {.day_length_curve = {{{{1, 9.56}}, {{16, 9.79}}, {{31, 10.17}}, {{46, 10.66}}, {{61, 11.21}}, {{76, 11.80}}, {{91, 12.40}}, {{106, 12.98}}, {{121, 13.51}}, {{136, 13.97}}, {{151, 14.31}}, {{166, 14.48}}, {{181, 14.46}}, {{196, 14.26}}, {{211, 13.90}}, {{226, 13.43}}, {{241, 12.88}}, {{256, 12.30}}, {{271, 11.70}}, {{286, 11.12}}, {{301, 10.57}}, {{316, 10.10}}, {{331, 9.74}}, {{346, 9.54}}, {{361, 9.52}}, {{365, 9.55}}}}};
    p.phenology = {
        .establishment_rate_per_hour = 0.0042,
        .establishment_temp_curve = {{{{5, 0}}, {{10, 0.5}}, {{18, 1}}, {{28, 1}}, {{35, 0.3}}}},
        .establishment_growth_curve = {{{{0, 0.3}}, {{1, 1}}}},
        .induction_rate_per_hour = 0.002,
        .induction_day_length_curve = {{{{8, 1}}, {{11.5, 1}}, {{12.5, 0.6}}, {{13.5, 0.1}}, {{14, 0}}}},
        .induction_temp_curve = {{{{3, 0}}, {{8, 0.5}}, {{12, 1}}, {{22, 1}}, {{26, 0.3}}, {{30, 0}}}},
        .induction_nitrogen_curve = {{{{0, 1}}, {{1.1, 1}}, {{1.3, 0.6}}, {{1.5, 0.25}}}},
        .truss_base_temp_degC = 5.0,
        .truss_degree_days_to_anthesis = 350.0,
        .induction_spread_index = 0.0,
        .flowers_per_truss_curve = {{{{1, 6}}, {{3, 12}}, {{6, 18}}, {{10, 22}}}},
    };
    p.fruit = {
        .rank_fraction = {0.06, 0.11, 0.22, 0.39, 0.22},
        .rank_anthesis_delay_days = {0.0, 4.0, 7.0, 10.0, 12.0},
        .anthesis_jitter_days = 4.0,
        .rank_potential_fresh_g = {35.0, 25.0, 17.0, 12.0, 8.0},
        .size_variation_fraction = 0.1,
        .pollination_mean_curve = {{{{3, 0.2}}, {{5, 0.3}}, {{8, 0.6}}, {{12, 0.92}}, {{25, 0.92}}, {{30, 0.65}}, {{35, 0.35}}}},
        .pollination_spread = 0.15,
        .set_threshold = 0.3,
        .deformed_threshold = 0.7,
        .pollination_size_curve = {{{{0.3, 0.4}}, {{0.7, 0.8}}, {{0.9, 1.0}}, {{1, 1.0}}}},
        .dry_matter_fraction = 0.09,
        .base_temp_degC = 5.0,
        .degree_days_to_harvest = 350.0,
        .growth_shape_curve = {{{{0, 0}}, {{0.2, 0.05}}, {{0.5, 0.35}}, {{0.8, 0.85}}, {{1, 1}}}},
        .size_class_bounds_g = {10.0, 15.0, 20.0, 25.0},
        .base_brix = 10.0,
        .brix_supply_curve = {{{{0, 0.7}}, {{0.5, 0.85}}, {{1, 1}}}},
        .brix_k_curve = {{{{0.5, 0.75}}, {{0.8, 0.9}}, {{1, 1}}}},
        .brix_n_excess_curve = {{{{0, 1}}, {{0.3, 1}}, {{0.5, 0.85}}}},
        .brix_water_curve = {{{{0.4, 1.08}}, {{0.7, 1.03}}, {{0.9, 1.0}}, {{1.2, 0.95}}}},
        .firmness_ca_curve = {{{{0.5, 0.6}}, {{0.8, 0.85}}, {{1, 1}}}},
        .firmness_temp_curve = {{{{10, 1}}, {{20, 0.95}}, {{28, 0.75}}}},
        .firmness_n_excess_curve = {{{{0.0, 1.0}}, {{0.3, 0.85}}}},
        .abortion_rate_per_development_index = 8.0,
        .abortion_window_index = 0.35,
        .abortion_satisfaction_curve = {{{{0.0, 1.0}}, {{0.9, 0.6}}, {{1.0, 0.1}}}},
        .rank_abortion_weight = {0.1, 0.4, 1.0, 1.4, 1.8},
        .thinnable_max_index = 0.3,
        .brix_day_temp_score_curve = {{{{3.0, 0.0003}}, {{4.0, 0.0007}}, {{5.0, 0.0015}}, {{6.0, 0.0031}}, {{7.0, 0.006}}, {{8.0, 0.0111}}, {{9.0, 0.0198}}, {{10.0, 0.034}}, {{11.0, 0.0561}}, {{12.0, 0.0889}}, {{13.0, 0.1353}}, {{14.0, 0.1979}}, {{15.0, 0.278}}, {{16.0, 0.3753}}, {{17.0, 0.4868}}, {{18.0, 0.6065}}, {{19.0, 0.7261}}, {{20.0, 0.8353}}, {{21.0, 0.9231}}, {{22.0, 0.9802}}, {{23.0, 1.0}}, {{24.0, 0.9802}}, {{25.0, 0.9231}}, {{26.0, 0.8353}}, {{27.0, 0.7261}}, {{28.0, 0.6065}}, {{29.0, 0.4868}}, {{30.0, 0.3753}}, {{31.0, 0.278}}, {{32.0, 0.1979}}, {{33.0, 0.1353}}, {{34.0, 0.0889}}, {{35.0, 0.0561}}, {{36.0, 0.034}}, {{37.0, 0.0198}}, {{38.0, 0.0111}}, {{39.0, 0.006}}, {{40.0, 0.0031}}, {{41.0, 0.0015}}, {{42.0, 0.0007}}, {{43.0, 0.0003}}}},
        .brix_night_temp_score_curve = {{{{-10.0, 0.0}}, {{-9.0, 0.0}}, {{-8.0, 0.0}}, {{-7.0, 0.0}}, {{-6.0, 0.0}}, {{-5.0, 0.0}}, {{-4.0, 0.0002}}, {{-3.0, 0.0006}}, {{-2.0, 0.0022}}, {{-1.0, 0.0066}}, {{0.0, 0.0181}}, {{1.0, 0.0439}}, {{2.0, 0.0956}}, {{3.0, 0.1863}}, {{4.0, 0.3247}}, {{5.0, 0.5063}}, {{6.0, 0.7066}}, {{7.0, 0.8825}}, {{8.0, 0.9862}}, {{8.5, 1.0}}, {{9.0, 0.94}}, {{10.0, 0.82}}, {{11.0, 0.7}}, {{12.0, 0.58}}, {{13.0, 0.46}}, {{14.0, 0.34}}, {{15.0, 0.22}}, {{16.0, 0.1}}, {{17.0, 0.0}}, {{18.0, 0.0}}, {{19.0, 0.0}}, {{20.0, 0.0}}}},
        .brix_day_weight = 0.6,
        .brix_night_weight = 0.4,
        .brix_temp_score_curve = {{{{0.0, 0.75}}, {{1.0, 1.15}}}},
        .day_solar_threshold_W_per_m2 = 0.0,
    };
    p.denitrification = {
        .rate_per_hour = 0.004,
        .o2_curve = {{{{0, 1}}, {{0.2, 0.6}}, {{0.4, 0.15}}, {{0.5, 0}}, {{1, 0}}}},
        .temp_curve = {{{{0, 0}}, {{10, 0.3}}, {{25, 1}}, {{35, 1}}, {{45, 0.5}}}},
        .carbon_curve = {{{{0, 0.1}}, {{50, 0.6}}, {{200, 1}}}},
        .H_eq_per_eq_N = -1.0,
    };
    p.seedling_grades = {
        {.id = "A", .leaf_g_per_plant = 4.0, .crown_g_per_plant = 3.0, .root_g_per_plant = 2.0,
         .initial_induction_index = 1.0},
        {.id = "B", .leaf_g_per_plant = 3.5, .crown_g_per_plant = 2.0, .root_g_per_plant = 1.5,
         .initial_induction_index = 0.8},
        {.id = "C", .leaf_g_per_plant = 2.5, .crown_g_per_plant = 1.2, .root_g_per_plant = 1.0,
         .initial_induction_index = 0.3},
    };

    p.soil_water = {
        .evap_mm_per_hour_per_W_per_m2 = 0.0004,
        .evap_base_mm_per_hour = 0.005,
        .evap_moisture_curve = {{{{0.0, 0.0}}, {{0.3, 0.15}}, {{0.7, 0.8}}, {{1.0, 1.0}}}},
        .drainage_rate_per_hour = 0.3,
        .drainage_compaction_curve = {{{{0, 1.0}}, {{0.5, 0.6}}, {{1.0, 0.1}}}},
    };

    p.soil_temperature = {.relax_rate_per_hour = 0.04,
                          .heat_degC_per_g_C = 0.4,
                          .evap_cooling_degC_per_mm = 0.0,
                          .deep_coupling_fraction = 0.0,
                          .deep_relax_rate_per_hour = 0.0};

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
    p.antagonism = {.ca_curve = {{{{0.0, 1.0}}, {{0.2, 1.0}}, {{0.4, 0.6}}, {{0.7, 0.35}}, {{1.2, 0.15}}}},
                    .mg_curve = {{{{0.0, 1.0}}, {{0.18, 1.0}}, {{0.35, 0.55}}, {{0.6, 0.3}}, {{1.2, 0.1}}}},
                    .min_demand_factor = 0.6};
    p.phosphorus = {.fixation_rate_per_hour = 0.0015,
                    .fixation_ph_curve = {{{{4.5, 1.0}}, {{5.5, 0.6}}, {{6.3, 0.2}}, {{6.8, 0.2}}, {{7.5, 0.6}}, {{8.5, 1.0}}}},
                    .release_rate_per_hour = 0.00005};
    p.manganese = {
        .retention_ph_curve = {{{{4.5, 450.0}}, {{5.0, 500.0}}, {{5.1, 900.0}}, {{5.3, 1500.0}}, {{6.5, 1500.0}}, {{7.5, 1800.0}}, {{8.5, 2000.0}}}},
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
        .compaction_per_mm_irrigation = 0.001,
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
        .waterlog_curve = {{{{0.0, 0.015}}, {{0.2, 0.006}}, {{0.4, 0.001}}, {{0.5, 0.0}}}},
        .heat_curve = {{{{32, 0}}, {{35, 0.002}}, {{40, 0.01}}, {{45, 0.03}}}},
        .nitrite_curve = {{{{0, 0}}, {{2, 0}}, {{4, 0.004}}, {{8, 0.015}}}},
        .acid_curve = {{{{4.0, 0.006}}, {{4.6, 0.0018}}, {{5.0, 0.0006}}, {{5.4, 0.0001}}, {{5.6, 0.0}}}},
    };
    p.crop = {
        .specific_leaf_area_m2_per_g = 0.02,
        .light_interception_curve = {{{{0, 0}}, {{0.5, 0.3}}, {{1, 0.5}}, {{2, 0.75}}, {{3, 0.88}}, {{4, 0.94}}}},
        .par_fraction = 0.48,
        .light_use_efficiency_g_per_MJ = 2.25,
        .temp_curve = {{{{3.0, 0.0}}, {{8.0, 0.3}}, {{15.0, 0.8}}, {{20.0, 1.0}}, {{25.0, 1.0}}, {{30.0, 0.6}}, {{35.0, 0.2}}, {{40.0, 0.0}}}},
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
        .ca_uptake_rate_per_g_root_per_hour = 0.00019,
        .mg_uptake_rate_per_g_root_per_hour = 0.00021,
        .transpiration_mm_per_hour_per_W_per_m2 = 0.0015,
        .root_recovery_rate_per_hour = 0.002,
        .death_root_health = 0.05,
        .root_dieback_per_damage = 1.0,
        .dry_matter_C_fraction = 0.45,
        .target_Mn_g_per_g = 0.0001,
        .mn_uptake_rate_per_g_root_per_hour = 0.00001,
        .mn_max_uptake_ratio = 30.0,
        .mn_toxicity_curve = {{{{0, 1}}, {{5, 1}}, {{10, 0.7}}, {{20, 0.3}}, {{30, 0.1}}}},
        .vegetative_leaf_fraction = 0.6,
        .vegetative_crown_fraction = 0.15,
        .vegetative_root_fraction = 0.25,
        .max_fruit_assimilate_fraction = 0.65,
        .maintenance_rate_leaf_per_hour = 0.0008,
        .maintenance_rate_crown_per_hour = 0.0004,
        .maintenance_rate_root_per_hour = 0.0006,
        .maintenance_rate_fruit_per_hour = 0.0004,
        .maintenance_temp_curve = {{{{0, 0.18}}, {{10, 0.35}}, {{20, 0.71}}, {{25, 1.0}}, {{30, 1.41}}, {{40, 2.83}}}},
        .leaf_senescence_rate_per_hour = 0.0003,
        .fruit_target_N_g_per_g = 0.015,
        .fruit_target_P_g_per_g = 0.0035,
        .fruit_target_K_g_per_g = 0.025,
        .fruit_target_Ca_g_per_g = 0.0015,
        .fruit_target_Mg_g_per_g = 0.0012,
        .fruit_target_Mn_g_per_g = 0.00002,
        .co2_g_per_g_dry_matter = 1.65,
        .co2_growth_curve = {{{{50, 0}}, {{100, 0.4}}, {{200, 0.75}}, {{400, 1.0}}, {{800, 1.2}}, {{1200, 1.25}}}},
        .cold_leaf_damage_curve = {{{{-6, 0.05}}, {{-3, 0.015}}, {{-1, 0.003}}, {{0, 0}}}},
        .cold_fruit_loss_curve = {{{{-4, 0.2}}, {{-2, 0.05}}, {{-0.5, 0}}, {{0, 0}}}},
        .n_luxury_max_ratio = 1.2,
        // 플러그: 옛 하드 상한(1.2)과 같게 (곡선 항등, 온도 곡선 항등)
        .n_luxury_uptake_curve = {{{{0, 1}}, {{10, 1}}}},
        .mn_uptake_regulation_curve = {{{{0, 1}}, {{100, 1}}}},
        .uptake_temp_curve = {{{{-50, 1}}, {{100, 1}}}},
        .n_uptake_rate_per_g_root_per_hour = 0.00006,
        // 저장 탄수화물 끔 (옛 동작 재현)
        .reserve_capacity_fraction = 0.0,
        .reserve_fill_fraction = 0.0,
        .reserve_release_rate_per_hour = 0.0,
        // 유지 호흡 활성 보정 끔 (항등)
        .maintenance_activity_curve = {{{{-1, 1}}, {{1, 1}}}},
        .rgr_mean_relax_rate_per_hour = 0.0,
    };

    p.composts = {
        {.id = "immature_manure", .name = "미숙 우분 퇴비", .C_g_per_kg = 180, .N_g_per_kg = 12,
         .labile_C_fraction = 0.6, .labile_N_fraction = 0.7, .k_g_per_kg = 12, .ca_g_per_kg = 10, .mg_g_per_kg = 4,
         .p_g_per_kg = 4, .s_g_per_kg = 2, .mn_g_per_kg = 0.3, .direct_acid_eq_per_kg = 0},
        {.id = "mature_manure", .name = "숙성 우분 퇴비", .C_g_per_kg = 150, .N_g_per_kg = 12,
         .labile_C_fraction = 0.1, .labile_N_fraction = 0.1, .k_g_per_kg = 14, .ca_g_per_kg = 15, .mg_g_per_kg = 5,
         .p_g_per_kg = 5, .s_g_per_kg = 2, .mn_g_per_kg = 0.4, .direct_acid_eq_per_kg = 0},
        {.id = "bark_compost", .name = "수피·톱밥 퇴비", .C_g_per_kg = 250, .N_g_per_kg = 5,
         .labile_C_fraction = 0.4, .labile_N_fraction = 0.3, .k_g_per_kg = 3, .ca_g_per_kg = 5, .mg_g_per_kg = 1,
         .p_g_per_kg = 1, .s_g_per_kg = 1, .mn_g_per_kg = 0.2, .direct_acid_eq_per_kg = 0},
    };
    p.fertilizers = {
        {.id = "ammonium_sulfate", .name = "황산암모늄", .nh4_N_g_per_kg = 210, .no3_N_g_per_kg = 0, .s_g_per_kg = 240},
        {.id = "urea", .name = "요소", .nh4_N_g_per_kg = 460, .no3_N_g_per_kg = 0},
        {.id = "calcium_nitrate", .name = "질산칼슘", .nh4_N_g_per_kg = 11, .no3_N_g_per_kg = 144, .ca_g_per_kg = 190},
        {.id = "potassium_sulfate", .name = "황산칼륨", .k_g_per_kg = 415, .s_g_per_kg = 180},
        {.id = "superphosphate", .name = "과인산석회", .ca_g_per_kg = 180, .p_g_per_kg = 87, .s_g_per_kg = 110},
        {.id = "magnesium_sulfate", .name = "황산고토", .mg_g_per_kg = 98, .s_g_per_kg = 130},
        {.id = "manganese_sulfate", .name = "황산망간", .s_g_per_kg = 190, .mn_g_per_kg = 320},
    };
    p.limes = {
        {.id = "calcium_carbonate", .name = "탄산석회", .eq_per_kg = 20.0, .ca_fraction = 1.0, .mg_fraction = 0.0},
        {.id = "dolomite", .name = "고토석회", .eq_per_kg = 21.7, .ca_fraction = 0.6, .mg_fraction = 0.4},
    };

    // ---- M5a (docs/spec/soil-microbes.md, disinfection.md 표의 초기값). 쓰지 않는 축은 항등 [[0,1],[14,1]] ----
    p.root_damage.fusarium_curve = {{{{0, 0}}, {{0.1, 0.0005}}, {{0.4, 0.003}}, {{0.8, 0.01}}}};
    p.root_damage.phytophthora_curve = {{{{0, 0}}, {{0.1, 0.001}}, {{0.4, 0.006}}, {{0.8, 0.02}}}};
    p.root_damage.fumigant_curve = {{{{0, 0}}, {{0.03, 0}}, {{0.1, 0.005}}, {{0.3, 0.03}}, {{1, 0.2}}}};
    {
        const sim::PiecewiseLinear identity = {{{{0, 1}}, {{14, 1}}}};
        sim::MicrobesParams& m = p.microbes;
        m.capacity_floor_index = 0.01;
        m.decomposer_activity_curve = {{{{0, 0.15}}, {{0.5, 0.7}}, {{1, 1}}}};
        m.aob_activity_curve = {{{{0, 0}}, {{0.2, 0.4}}, {{1, 1}}}};
        m.nob_activity_curve = {{{{0, 0}}, {{0.2, 0.4}}, {{1, 1}}}};
        m.antagonist_protection_factor = 0.5;
        m.mycorrhiza_p_release_factor = 2.0;
        m.mycorrhiza_water_relief_factor = 0.3;
        m.mycorrhiza_salt_relief_factor = 0.3;
        m.pathogen_spread_index = 0.0;  // M5a 프로토타입 3차: 셀 간 차이는 단위 테스트에서 꺼요
        m.susceptibility_spread_index = 0.0;
        m.moist_heat_curve = {{{{0.2, 0.3}}, {{0.5, 0.7}}, {{0.8, 1.0}}}};
        m.decomposer_food_curve = {{{{0, 0.4}}, {{10, 0.8}}, {{30, 1}}}};
        m.antagonist_food_curve = {{{{0, 0.3}}, {{20, 0.6}}, {{60, 1}}}};
        m.mycorrhiza_host_curve = {{{{0, 0.05}}, {{5, 0.5}}, {{15, 1}}}};
        m.mycorrhiza_p_curve = {{{{0, 1}}, {{25, 0.6}}, {{50, 0.3}}, {{100, 0.1}}}};
        using G = sim::MicrobeGroup;
        auto& groups = m.groups;
        const auto idx = [](G g) { return sim::microbe(g); };
        groups[idx(G::Decomposer)] = {
            .growth_rate_per_hour = 0.05,
            .temp_curve = {{{{0, 0.1}}, {{10, 0.5}}, {{25, 1}}, {{38, 1}}, {{45, 0.3}}}},
            .moisture_curve = {{{{0.1, 0.2}}, {{0.5, 1}}, {{1.2, 1}}, {{1.8, 0.6}}}},
            .ph_curve = identity,
            .o2_curve = identity,
            .base_death_rate_per_hour = 0.0005,
            .heat_kill_curve = {{{{40, 0}}, {{45, 0.005}}, {{50, 0.03}}, {{55, 0.1}}}},
            .immigration_rate_per_hour = 0.002,
        };
        const sim::PiecewiseLinear nitrifier_temp = {{{{0, 0}}, {{5, 0.2}}, {{25, 1}}, {{35, 1}}, {{40, 0.3}}}};
        const sim::PiecewiseLinear nitrifier_o2 = {{{{0, 0.1}}, {{0.3, 0.6}}, {{0.5, 1}}}};
        const sim::PiecewiseLinear nitrifier_heat = {{{{33, 0}}, {{36, 0.005}}, {{40, 0.02}}, {{45, 0.06}}, {{50, 0.15}}}};
        groups[idx(G::NitrifierAob)] = {
            .growth_rate_per_hour = 0.008,
            .temp_curve = nitrifier_temp,
            .moisture_curve = identity,
            .ph_curve = {{{{4.5, 0.2}}, {{5.5, 0.6}}, {{6.5, 1}}, {{8.5, 1}}}},
            .o2_curve = nitrifier_o2,
            .base_death_rate_per_hour = 0.0002,
            .heat_kill_curve = nitrifier_heat,
            .immigration_rate_per_hour = 0.0005,
        };
        groups[idx(G::NitrifierNob)] = {
            .growth_rate_per_hour = 0.008,
            .temp_curve = nitrifier_temp,
            .moisture_curve = identity,
            .ph_curve = {{{{4.5, 0.2}}, {{5.5, 0.6}}, {{6.5, 1}}, {{7.5, 0.8}}, {{8.5, 0.5}}}},
            .o2_curve = nitrifier_o2,
            .base_death_rate_per_hour = 0.0002,
            .heat_kill_curve = nitrifier_heat,
            .immigration_rate_per_hour = 0.0005,
        };
        groups[idx(G::Antagonist)] = {
            .growth_rate_per_hour = 0.02,
            .temp_curve = {{{{5, 0.2}}, {{20, 1}}, {{35, 1}}, {{42, 0.4}}}},
            .moisture_curve = {{{{0.2, 0.3}}, {{0.5, 1}}, {{1.3, 0.8}}}},
            .ph_curve = identity,
            .o2_curve = identity,
            .base_death_rate_per_hour = 0.0003,
            .heat_kill_curve = {{{{45, 0}}, {{50, 0.005}}, {{55, 0.03}}}},
            .immigration_rate_per_hour = 0.0002,
        };
        groups[idx(G::Antagonist)].competition[idx(G::Fusarium)] = 0.3;
        groups[idx(G::Antagonist)].competition[idx(G::Phytophthora)] = 0.3;
        groups[idx(G::Mycorrhiza)] = {
            .growth_rate_per_hour = 0.004,
            .temp_curve = {{{{8, 0.1}}, {{18, 1}}, {{30, 1}}, {{38, 0.3}}}},
            .moisture_curve = {{{{0.2, 0.5}}, {{0.5, 1}}, {{1.3, 0.6}}}},
            .ph_curve = identity,
            .o2_curve = {{{{0, 0.1}}, {{0.4, 1}}}},
            .base_death_rate_per_hour = 0.0003,
            .heat_kill_curve = {{{{36, 0}}, {{40, 0.01}}, {{45, 0.05}}, {{50, 0.15}}}},
            .immigration_rate_per_hour = 0.0001,
        };
        groups[idx(G::Fusarium)] = {
            .growth_rate_per_hour = 0.01,
            .temp_curve = {{{{10, 0.1}}, {{20, 0.6}}, {{28, 1}}, {{32, 1}}, {{36, 0.5}}}},
            .moisture_curve = identity,
            .ph_curve = {{{{4.5, 1}}, {{5.5, 1}}, {{6.5, 0.6}}, {{7.5, 0.4}}}},
            .o2_curve = identity,
            .base_death_rate_per_hour = 0.00002,
            .heat_kill_curve = {{{{37, 0}}, {{40, 0.017}}, {{43, 0.07}}, {{45, 0.09}}, {{50, 0.25}}, {{55, 0.6}}}},
            .immigration_rate_per_hour = 0.0,
            .saprophytic_capacity_index = 0.3,
            .host_capacity_index = 0.7,
        };
        groups[idx(G::Fusarium)].competition[idx(G::Antagonist)] = 0.8;
        groups[idx(G::Phytophthora)] = {
            .growth_rate_per_hour = 0.03,
            .temp_curve = {{{{7, 0.1}}, {{15, 0.6}}, {{20, 1}}, {{25, 1}}, {{30, 0.4}}, {{32, 0.1}}}},
            .moisture_curve = {{{{0.5, 0.1}}, {{0.9, 0.5}}, {{1.2, 1}}}},
            .ph_curve = identity,
            .o2_curve = {{{{0, 1}}, {{0.3, 0.6}}, {{0.5, 0.1}}}},
            .base_death_rate_per_hour = 0.0001,
            .heat_kill_curve = {{{{37, 0}}, {{40, 0.02}}, {{43, 0.08}}, {{45, 0.12}}, {{50, 0.3}}}},
            .immigration_rate_per_hour = 0.0,
            .saprophytic_capacity_index = 0.2,
            .host_capacity_index = 0.8,
        };
        groups[idx(G::Phytophthora)].competition[idx(G::Antagonist)] = 0.8;

        p.soil_disease[static_cast<std::size_t>(sim::SoilDisease::Fusarium)] = {
            .infection_rate_per_hour = 0.004,
            .infection_env_curve_temp = {{{{15, 0}}, {{22, 0.4}}, {{28, 1}}, {{32, 1}}, {{36, 0.5}}}},
            .infection_env_curve_o2 = {{{{0, 1}}, {{1, 1}}}},
            .infection_env_curve_ph = {{{{4.5, 1.8}}, {{5.0, 1.7}}, {{5.5, 1.4}}, {{6.5, 1}}, {{7.5, 0.8}}}},
            .onset_infection_index = 0.15,
        };
        p.soil_disease[static_cast<std::size_t>(sim::SoilDisease::Phytophthora)] = {
            .infection_rate_per_hour = 0.01,
            .infection_env_curve_temp = {{{{8, 0.1}}, {{15, 0.5}}, {{20, 1}}, {{25, 1}}, {{29, 0.4}}, {{32, 0}}}},
            .infection_env_curve_o2 = {{{{0, 1}}, {{0.25, 0.6}}, {{0.4, 0.05}}, {{0.5, 0}}}},
            .infection_env_curve_ph = identity,
            .onset_infection_index = 0.1,
        };
        p.inoculants = {
            {.id = "bacillus_product", .name = "고초균제", .index_gain_per_g_per_m2 = {}, .C_g_per_kg = 300,
             .N_g_per_kg = 10},
            {.id = "trichoderma_product", .name = "트리코더마제", .index_gain_per_g_per_m2 = {}, .C_g_per_kg = 300,
             .N_g_per_kg = 10},
            {.id = "mycorrhiza_product", .name = "균근균제", .index_gain_per_g_per_m2 = {}, .C_g_per_kg = 200,
             .N_g_per_kg = 5},
        };
        p.inoculants[0].index_gain_per_g_per_m2[idx(G::Antagonist)] = 0.02;
        p.inoculants[1].index_gain_per_g_per_m2[idx(G::Antagonist)] = 0.025;
        p.inoculants[2].index_gain_per_g_per_m2[idx(G::Mycorrhiza)] = 0.02;
    }
    p.disinfection = {.flush_N_g_per_m2_per_index = 6.0, .flush_C_to_N_ratio = 6.0, .mn_reduction_per_index = 0.03,
                      .till_residue_release_fraction = 0.5};
    p.mulches = {
        {.id = "clear_film", .name = "투명 필름", .soil_solar_gain_degC_per_W_per_m2 = 0.025, .evaporation_factor = 0.1,
         .fumigant_retention_factor = 0.3, .canopy_reflect_factor = 0.0},
        {.id = "black_film", .name = "흑색 필름", .soil_solar_gain_degC_per_W_per_m2 = 0.008, .evaporation_factor = 0.2,
         .fumigant_retention_factor = 0.3, .canopy_reflect_factor = 0.0},
        {.id = "white_film", .name = "백색 필름", .soil_solar_gain_degC_per_W_per_m2 = -0.004, .evaporation_factor = 0.2,
         .fumigant_retention_factor = 0.3, .canopy_reflect_factor = 0.25},
    };
    // 약제 kill_rate 순서: decomposer, nitrifier_aob, nitrifier_nob, antagonist, mycorrhiza, fusarium, phytophthora
    p.disinfectants = {
        {.id = "dazomet", .name = "다조멧", .residue_index_per_g_per_m2 = 1.0 / 30.0,
         .kill_rate_per_hour = {0.006, 0.002, 0.001, 0.006, 0.02, 0.012, 0.015}, .decay_rate_per_hour = 0.02,
         .decay_temp_curve = {{{{5, 0.3}}, {{15, 0.6}}, {{25, 1}}, {{35, 1.5}}}},
         .activation_moisture_curve = {{{{0.2, 0.1}}, {{0.5, 0.7}}, {{0.7, 1}}}}},
        {.id = "chloropicrin", .name = "클로로피크린", .residue_index_per_g_per_m2 = 1.0 / 4.0,
         .kill_rate_per_hour = {0.01, 0.03, 0.03, 0.01, 0.02, 0.015, 0.02}, .decay_rate_per_hour = 0.012,
         .decay_temp_curve = {{{{5, 0.4}}, {{15, 0.7}}, {{25, 1}}, {{35, 1.4}}}},
         .activation_moisture_curve = {{{{0.2, 0.6}}, {{0.5, 0.9}}, {{0.7, 1}}}}},
    };
    return p;
}

// 호흡·낙엽을 끈 파라미터 (M4). M4 작물 갱신은 낙엽, 뿌리 고사, 음의 순생장 수축, 낙과를 한 호출 안에서 차례로 하므로
// (strawberry-fruit 7번) 단위 테스트가 흡수·분배·고사 중 하나만 따로 보려면 나머지를 0으로 둬야 해요.
inline sim::Params quiet_params() {
    sim::Params p = test_params();
    p.crop.maintenance_rate_leaf_per_hour = 0.0;
    p.crop.maintenance_rate_crown_per_hour = 0.0;
    p.crop.maintenance_rate_root_per_hour = 0.0;
    p.crop.maintenance_rate_fruit_per_hour = 0.0;
    p.crop.leaf_senescence_rate_per_hour = 0.0;
    return p;
}

// 저장소 data/params.toml의 값 = test_params() + M5a 사전 작업(2026-11-19 승인, m5-overview 13장)으로 바뀐 값.
// test_params()는 단위 테스트가 새 기능(근권 온도, 부드러운 N 상한, Mn 조절, 저장 탄수화물, 호흡 활성 보정,
// 증발 냉각, 포기 간 차이)을 끈 상태로 계속 쓰고, 저장소 값은 여기서 고정해요.
inline sim::Params repo_params() {
    sim::Params p = test_params();
    p.phenology.induction_nitrogen_curve = {{{{0, 1}}, {{1.05, 1}}, {{1.15, 0.7}}, {{1.5, 0.5}}}};
    p.phenology.induction_spread_index = 0.4;
    p.soil_temperature.evap_cooling_degC_per_mm = 2.5;
    // ---- M5a 최종안 (2026-11-19 승인, m5-overview 14.2): 프로토타입 1~4차로 조정한 값 ----
    // M1~M4 값 변경 (14.2 결정 8·9)
    p.soil_temperature.relax_rate_per_hour = 0.08;
    p.nitrification.step2_rate_per_hour = 0.2;
    // 깊은 흙 온도 항 (ADR-0013)
    p.soil_temperature.deep_coupling_fraction = 0.65;
    p.soil_temperature.deep_relax_rate_per_hour = 0.01;
    {
        using G = sim::MicrobeGroup;
        const auto idx = [](G g) { return sim::microbe(g); };
        sim::MicrobesParams& m = p.microbes;
        // 활동 곡선 평탄화 (지수 0.8 이상에서 활동 1), 길항균 보호, 균근균 완화, 셀 간 차이
        m.decomposer_activity_curve = {{{{0, 0.15}}, {{0.5, 0.7}}, {{0.8, 1}}, {{1, 1}}}};
        m.aob_activity_curve = {{{{0, 0}}, {{0.2, 0.4}}, {{0.8, 1}}, {{1, 1}}}};
        m.nob_activity_curve = {{{{0, 0}}, {{0.2, 0.4}}, {{0.8, 1}}, {{1, 1}}}};
        m.antagonist_protection_factor = 0.8;
        m.mycorrhiza_water_relief_factor = 0.1;
        m.mycorrhiza_salt_relief_factor = 0.1;
        m.pathogen_spread_index = 0.5;
        m.susceptibility_spread_index = 0.5;
        auto& g = m.groups;
        g[idx(G::Decomposer)].growth_rate_per_hour = 0.0015;
        g[idx(G::Decomposer)].base_death_rate_per_hour = 0.00003;
        g[idx(G::Decomposer)].heat_kill_curve = {{{{38, 0}}, {{42, 0.004}}, {{45, 0.01}}, {{50, 0.03}}, {{55, 0.1}}}};
        g[idx(G::Decomposer)].immigration_rate_per_hour = 0.0002;
        for (G n : {G::NitrifierAob, G::NitrifierNob}) {
            g[idx(n)].growth_rate_per_hour = 0.003;
            g[idx(n)].base_death_rate_per_hour = 0.00005;
        }
        g[idx(G::Antagonist)].growth_rate_per_hour = 0.0015;
        g[idx(G::Antagonist)].temp_curve = {{{{5, 0.2}}, {{20, 1}}, {{35, 1}}, {{40, 0.3}}, {{44, 0}}}};
        g[idx(G::Antagonist)].base_death_rate_per_hour = 0.00003;
        g[idx(G::Antagonist)].heat_kill_curve = {{{{40, 0}}, {{43, 0.004}}, {{45, 0.008}}, {{50, 0.02}}, {{55, 0.05}}}};
        g[idx(G::Antagonist)].immigration_rate_per_hour = 0.0001;
        g[idx(G::Fusarium)].growth_rate_per_hour = 0.002;
        g[idx(G::Fusarium)].saprophytic_capacity_index = 0.6;
        g[idx(G::Fusarium)].competition[idx(G::Antagonist)] = 0.4;
        g[idx(G::Phytophthora)].growth_rate_per_hour = 0.005;
        g[idx(G::Phytophthora)].saprophytic_capacity_index = 0.5;
        g[idx(G::Phytophthora)].competition[idx(G::Antagonist)] = 0.4;
        p.soil_disease[static_cast<std::size_t>(sim::SoilDisease::Fusarium)].infection_rate_per_hour = 0.002;
        // 소독 (disinfection 표)
        p.disinfection.flush_N_g_per_m2_per_index = 21.0;
        p.disinfection.mn_reduction_per_index = 0.12;
        p.mulches[0].soil_solar_gain_degC_per_W_per_m2 = 0.021;   // clear_film
        p.mulches[2].soil_solar_gain_degC_per_W_per_m2 = -0.02;   // white_film
        // 약제 kill_rate 순서: decomposer, nitrifier_aob, nitrifier_nob, antagonist, mycorrhiza, fusarium, phytophthora
        p.disinfectants[0].kill_rate_per_hour = {0.015, 0.002, 0.001, 0.015, 0.02, 0.02, 0.02};  // dazomet
        p.disinfectants[1].kill_rate_per_hour = {0.01, 0.03, 0.03, 0.01, 0.02, 0.035, 0.03};     // chloropicrin
    }
    sim::CropParams& c = p.crop;
    c.light_use_efficiency_g_per_MJ = 2.1;
    c.mn_uptake_rate_per_g_root_per_hour = 0.00002;
    c.mn_toxicity_curve = {{{{0, 1}}, {{3, 1}}, {{4.5, 0.9}}, {{10, 0.7}}, {{18, 0.5}}, {{30, 0.2}}}};
    c.maintenance_rate_leaf_per_hour = 0.00125;
    c.maintenance_rate_root_per_hour = 0.0004;
    c.n_luxury_max_ratio = 1.6;
    c.n_luxury_uptake_curve = {{{{0, 1}}, {{0.9, 1}}, {{1.1, 0.4}}, {{1.3, 0.1}}, {{1.5, 0}}}};
    c.mn_uptake_regulation_curve = {{{{0, 1}}, {{1, 1}}, {{1.5, 0.2}}, {{2, 0.03}}, {{100, 0.03}}}};
    c.uptake_temp_curve = {{{{5, 0.2}}, {{10, 0.45}}, {{15, 0.75}}, {{20, 1}}, {{30, 1}}, {{35, 0.7}}}};
    c.reserve_capacity_fraction = 0.2;
    c.reserve_fill_fraction = 0.5;
    c.reserve_release_rate_per_hour = 0.0005;
    c.maintenance_activity_curve = {{{{-1, 0.4}}, {{0.0043, 0.4}}, {{0.01, 0.7}}, {{0.02, 0.91}}, {{0.03, 0.97}}, {{0.05, 1}}}};
    c.rgr_mean_relax_rate_per_hour = 0.014;
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
        // M5a: m5-overview 11장 기본 줄 (decomposer, nitrifier_aob, nitrifier_nob, antagonist, mycorrhiza,
        // fusarium, phytophthora)
        .microbe_index = {1.0, 1.0, 1.0, 0.5, 0.1, 0.0, 0.0},
    };
}

// test_soil()로 만든 셀 하나.
// M5a: 멀칭 없음, 약제 잔류 0 (test_params() 약제 수만큼)은 월드 생성과 같아요.
// 단, microbe_index는 0으로 남겨요: 작물 갱신이 셀의 균근균 지수로 물·염류 스트레스를 완화하므로,
// M2~M4 작물·과실 단위 테스트가 옛 식 그대로 돌도록 해요. 미생물 테스트는 지수를 직접 넣어요.
// (활동 계수는 프로세스 함수 인자, 기본값 1이라 이 셀의 지수와 무관해요.)
inline sim::SoilCell test_cell() {
    const sim::SoilInit s = test_soil();
    sim::SoilCell cell{
        .field_capacity_mm = s.field_capacity_mm,
        .water_mm = s.water_mm,
        .drained_mm_this_tick = 0.0,
        .soil_temp_degC = s.soil_temp_degC,
        .deep_soil_temp_degC = s.soil_temp_degC,  // M5a 프로토타입 3차: 월드 생성과 같이 시나리오 지온에서 시작
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
    cell.fumigant_residue_index.assign(test_params().disinfectants.size(), 0.0);
    // M3: 용액 상태를 평형값으로 맞춰요 (World 생성과 같은 초기화). 총량을 바꾼 테스트는 test::settle()을 다시 불러요.
    sim::process::set_solution_to_equilibrium(cell, test_params());
    return cell;
}

// 셀의 총량·pH 등을 바꾼 뒤 용액 상태를 평형으로 다시 맞춰요 (M3).
inline void settle(sim::SoilCell& cell, const sim::Params& params) {
    sim::process::set_solution_to_equilibrium(cell, params);
}

// M4: 모종 B 8포기/m² (잎 28, 관부 16, 뿌리 12 g/m², m4-overview "시나리오 이행")
inline sim::CropInit test_crop_init() {
    return sim::CropInit{.seedling_grade = "B", .plants_per_m2 = 8.0, .initial_establishment_index = 1.0};
}

// 시간마다 값이 다른 기상 표: 조회 위치가 틀리면 바로 드러나요.
inline sim::Scenario test_scenario(int start_day_of_year = 1, int rows = 2, int cols = 3) {
    sim::Scenario s;
    s.name = "test";
    s.seed = 12345;
    s.start_day_of_year = start_day_of_year;
    s.bed_rows = rows;
    s.bed_cols = cols;
    s.cover_layers = 3;
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

// M4 작물 갱신용 환경 (이동 평균 = 기온, CO2 400 ppm = 배율 1). 일사 0이면 밤
inline sim::process::CropEnvironment crop_env(double inside_solar_W_per_m2, double air_degC, int day_of_year = 1,
                                              std::uint64_t tick = 0) {
    return sim::process::CropEnvironment{.inside_solar_W_per_m2 = inside_solar_W_per_m2,
                                         .house_air_temp_degC = air_degC,
                                         .house_air_temp_mean_degC = air_degC,
                                         .house_co2_ppm = 400.0,
                                         .day_of_year = day_of_year,
                                         .tick = tick};
}

// 시나리오 시드의 생물 변이 스트림 (ADR-0004)
inline sim::Pcg32 biology_rng(std::uint64_t seed) {
    return sim::make_stream(seed, static_cast<std::uint64_t>(sim::RngStream::Biology));
}

}  // namespace test

// ---- M4 API 호환 (테스트 셋업 전용) ----
// M2·M3 단위 테스트가 옛 시그니처로 부르던 plant_crop / update_crop을 M4 API로 이어 줘요.
// CO2 400 ppm(co2_growth_curve = 1), 이동 평균 = 기온, 1월 1일(단일), 틱 0, 고정 시드 생물 스트림.
namespace sim::process {

inline Crop plant_crop(const CropInit& init, const CropParams& crop_params) {
    Params p = test::test_params();
    p.crop = crop_params;
    const SeedlingGradeDef* grade = find_seedling_grade(p, init.seedling_grade);
    if (grade == nullptr) {
        throw std::runtime_error("unknown seedling grade in test");
    }
    Crop crop = plant_crop(*grade, init.plants_per_m2, p);
    crop.establishment_index = init.initial_establishment_index;
    return crop;
}

inline constexpr double kTestCo2_ppm = 400.0;

inline std::optional<PlantDiedEvent> update_crop(Crop& crop, SoilCell& cell, const Params& params,
                                                 const NutrientAvailability& available, double inside_solar_W_per_m2,
                                                 double house_air_temp_degC, StressCombination mode,
                                                 std::uint32_t tile_index, Ledger& ledger) {
    const CropEnvironment env{
        .inside_solar_W_per_m2 = inside_solar_W_per_m2,
        .house_air_temp_degC = house_air_temp_degC,
        .house_air_temp_mean_degC = house_air_temp_degC,
        .house_co2_ppm = kTestCo2_ppm,
        .day_of_year = 1,
        .tick = 0,
        .mode = mode,
        .tile_index = tile_index,
    };
    Pcg32 rng = make_stream(test::test_scenario().seed, static_cast<std::uint64_t>(RngStream::Biology));
    return update_crop(crop, cell, params, available, env, rng, ledger).died;
}

}  // namespace sim::process
