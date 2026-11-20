#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "sim/calendar.hpp"
#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {

std::string weather_csv(std::size_t rows, std::string_view newline = "\n") {
    std::string text = "# comment line\n";
    text += simdata::kWeatherCsvHeader;
    text += newline;
    for (std::size_t i = 0; i < rows; ++i) {
        text += std::format("{},{},{},{},{}{}", i / sim::kHoursPerDay + 1, i % sim::kHoursPerDay,
                            -2.5, 100.0, 60.0, newline);
    }
    return text;
}

constexpr std::string_view kValidScenario = R"(
schema_version = 7
[scenario]
name = "t"
seed = 7
start_day_of_year = 100
weather_csv = "w.csv"
[house]
bed_rows = 2
bed_cols = 5
cover_layers = 3
drip_installed = true
[microbes]
decomposer = 1.0
nitrifier_aob = 1.0
nitrifier_nob = 0.9
antagonist = 0.5
mycorrhiza = 0.1
fusarium = 0.0
phytophthora = 0.25
anthracnose = 0.0
[foliar]
botrytis_background_index = 0.0
powdery_mildew_background_index = 0.0
[soil]
field_capacity_mm = 60.0
water_mm = 45
soil_temp_degC = 20.0
ph = 6.5
nh4_N_g_per_m2 = 2.0
no2_N_g_per_m2 = 0.0
no3_N_g_per_m2 = 10.0
labile_C_g_per_m2 = 50.0
labile_N_g_per_m2 = 5.0
stable_C_g_per_m2 = 3600.0
stable_N_g_per_m2 = 360.0
saturation_mm = 110.0
compaction_index = 0.3
base_buffer_eq_per_m2_per_pH = 5.0
k_g_per_m2 = 60.0
ca_g_per_m2 = 400.0
mg_g_per_m2 = 60.0
p_g_per_m2 = 25.0
s_g_per_m2 = 3.0
cec_mineral_eq_per_m2 = 25.0
mn_g_per_m2 = 5.0
mn_oxide_g_per_m2 = 100.0
p_fixed_g_per_m2 = 150.0
[difficulty]
stress_combination = "minimum"
)";

// 형식 검증용 params 문서 (값은 명세 값이 아니어도 돼요)
constexpr std::string_view kValidParams = R"(
schema_version = 7
[house]
layer_transmittance_factor = 0.9
layer_resistance_m2K_per_W = 0.13
infiltration_W_per_m2_per_K = 0.5
window_bypass_fraction = 0.5
window_open_levels = [0, 0.3, 0.5, 0.7, 1.0]
vent_conductance_W_per_m2_per_K = 30.0
solar_heat_gain_fraction = 0.063
ground_heat_W_per_m2 = 6.1
air_relax_rate_per_hour = 0.7
heater_capacity_W_per_m2 = 150.0
heater_setpoint_min_degC = 0.0
heater_setpoint_max_degC = 30.0
temp_mean_relax_rate_per_hour = 0.042
cover_change_window_doy = [91, 243]
min_cover_layers = 2
max_cover_layers = 4
outside_co2_ppm_by_hour = [420.0, 424.0, 428.8, 439.4, 464.7, 494.6, 495.1, 466.2, 441.8, 431.9, 427.6, 423.9, 420.0, 415.2, 402.8, 388.1, 397.3, 404.6, 405.0, 405.5, 407.0, 409.4, 412.5, 416]
air_volume_m3_per_m2 = 3.0
co2_mg_per_m3_per_ppm = 1.83
infiltration_air_changes_per_hour = 0.3
vent_air_changes_per_hour = 30.0
co2_floor_ppm = 50.0
[calendar]
day_length_curve = [[1, 9.56], [181, 14.46], [365, 9.55]]
[phenology]
establishment_rate_per_hour = 0.0042
establishment_temp_curve = [[5, 0], [18, 1], [35, 0.3]]
establishment_growth_curve = [[0, 0.3], [1, 1]]
induction_rate_per_hour = 0.002
induction_day_length_curve = [[8, 1], [14, 0]]
induction_temp_curve = [[3, 0], [12, 1], [30, 0]]
induction_nitrogen_curve = [[0, 1], [1.0, 1], [1.3, 0.25]]
truss_base_temp_degC = 5.0
truss_degree_days_to_anthesis = 600.0
induction_spread_index = 0
flowers_per_truss_curve = [[1, 6], [10, 22]]
[fruit]
rank_fraction = [0.07, 0.13, 0.27, 0.53]
rank_anthesis_delay_days = [0.0, 2.0, 4, 6.0]
anthesis_jitter_days = 3
rank_potential_fresh_g = [35.0, 25.0, 16.0, 10.0]
size_variation_fraction = 0.1
pollination_mean_curve = [[3, 0.35], [12, 0.92], [35, 0.35]]
pollination_spread = 0.15
set_threshold = 0.3
deformed_threshold = 0.7
pollination_size_curve = [[0.3, 0.4], [1, 1.0]]
dry_matter_fraction = 0.09
base_temp_degC = 5.0
degree_days_to_harvest = 450.0
growth_shape_curve = [[0, 0], [1, 1]]
size_class_bounds_g = [10.0, 15.0, 20.0, 25.0]
base_brix = 10.0
brix_supply_curve = [[0, 0.7], [1, 1]]
brix_day_temp_score_curve = [[3, 0.0003], [23, 1], [43, 0.0003]]
brix_night_temp_score_curve = [[-10, 0], [8.5, 1], [17, 0]]
brix_day_weight = 0.6
brix_night_weight = 0.4
brix_temp_score_curve = [[0, 0.75], [1, 1.15]]
day_solar_threshold_W_per_m2 = 0
abortion_rate_per_development_index = 8.0
abortion_window_index = 0.25
abortion_satisfaction_curve = [[0, 1], [0.6, 0.5], [0.85, 0], [1, 0]]
rank_abortion_weight = [0.1, 0.4, 1.0, 1.6]
thinnable_max_index = 0.3
brix_k_curve = [[0.5, 0.75], [1, 1]]
brix_n_excess_curve = [[0, 1], [0.3, 0.85]]
brix_water_curve = [[0.4, 1.08], [1.2, 0.95]]
firmness_ca_curve = [[0.5, 0.6], [1, 1]]
firmness_temp_curve = [[10, 1], [28, 0.75]]
firmness_n_excess_curve = [[0, 1], [0.3, 0.85]]
[denitrification]
rate_per_hour = 0.004
o2_curve = [[0, 1], [0.5, 0], [1, 0]]
temp_curve = [[0, 0], [25, 1], [45, 0.5]]
carbon_curve = [[0, 0.1], [200, 1]]
H_eq_per_eq_N = -1.0
[soil_water]
evap_mm_per_hour_per_W_per_m2 = 0.0004
evap_base_mm_per_hour = 0.005
evap_moisture_curve = [[0.0, 0.0], [1.0, 1.0]]
drainage_rate_per_hour = 0.3
drainage_compaction_curve = [[0, 1.0], [1.0, 0.1]]
[soil_temperature]
relax_rate_per_hour = 0.04
heat_degC_per_g_C = 0.4
evap_cooling_degC_per_mm = 0.0
deep_coupling_fraction = 0
deep_relax_rate_per_hour = 0
[organic_matter]
labile_decay_rate_per_hour = 0.004
stable_decay_rate_per_hour = 0.00002
humification_fraction = 0.3
humus_C_to_N_ratio = 10
temp_curve = [[0, 0.05], [10, 0.25]]
moisture_curve = [[0, 0], [2.0, 1.0]]
o2_curve = [[0, 0.15], [1, 1.0]]
[nitrification]
step1_rate_per_hour = 0.01
step2_rate_per_hour = 0.08
temp_curve = [[0, 0], [50, 0.1]]
moisture_curve = [[0, 0], [2.0, 1.0]]
step1_ph_curve = [[4.5, 0.1], [9.0, 0.6]]
step2_ph_curve = [[4.5, 0.1], [9.0, 0.1]]
ammonia_fraction_ph_curve = [[6, 0.0006], [10, 0.85]]
free_ammonia_inhibition_curve = [[0, 1], [3.0, 0.05]]
nitrite_toxicity_threshold_g_N_per_m2 = 2.0
nitrite_alarm_reset_fraction = 0.8
step1_o2_curve = [[0, 0.05], [1, 1.0]]
step2_o2_curve = [[0, 0.0], [1, 1.0]]
nitrite_toxicity_ph_curve = [[4.5, 8.0], [9.0, 1.0]]
[cation_exchange]
cec_organic_eq_per_g_C = 0.0015
exchange_rate_per_hour = 0.3
nh4_retention_factor = 70.0
k_retention_factor = 250.0
ca_retention_factor = 760.0
mg_retention_factor = 380.0
solver_iterations = 60
buffer_eq_per_pH_per_eq_cec = 0.15
[antagonism]
ca_curve = [[0.0, 1.0], [1.2, 0.15]]
mg_curve = [[0.0, 1.0], [1.2, 0.1]]
min_demand_factor = 0.6
[phosphorus]
fixation_rate_per_hour = 0.0015
fixation_ph_curve = [[4.5, 1.0], [8.5, 1.0]]
release_rate_per_hour = 0.00005
[manganese]
retention_ph_curve = [[4.5, 450.0], [8.5, 2000.0]]
oxidation_rate_per_hour = 0.002
oxidation_ph_curve = [[4.5, 0.005], [8.5, 10.0]]
oxidation_o2_curve = [[0, 0.0], [1, 1.0]]
reduction_rate_per_hour = 0.00006
reduction_ph_curve = [[4.5, 6.0], [8.5, 0.3]]
reduction_o2_curve = [[0, 20.0], [1, 1.0]]
[aeration]
compaction_cap_curve = [[0, 1.0], [1.0, 0.35]]
organic_C_cap_bonus_per_g_C = 0.00004
compaction_per_mm_irrigation = 0.0015
tilled_compaction_index = 0.1
till_root_damage_fraction = 0.5
[ions]
nh4_N_g_per_eq = 14.007
no3_N_g_per_eq = 14.007
no2_N_g_per_eq = 14.007
k_g_per_eq = 39.098
ca_g_per_eq = 20.039
mg_g_per_eq = 12.153
p_g_per_eq = 30.974
s_g_per_eq = 16.03
mn_g_per_eq = 27.469
[soil_chemistry]
p_solution_fraction = 0.002
s_solution_fraction = 0.8
nitrification_H_eq_per_eq_N = 2.0
uptake_H_eq_per_eq_NH4 = 1.0
uptake_H_eq_per_eq_NO3 = -1.0
organic_C_buffer_eq_per_g_C_per_pH = 0.002
ph_min = 3.0
ph_max = 10.0
lime_dissolution_rate_per_hour = 0.004
lime_dissolution_ph_curve = [[4.5, 1.0], [8.0, 0.0]]
lime_dissolution_moisture_curve = [[0, 0], [2.0, 1.0]]
ec_meq_per_L_per_dS_per_m = 10.0
ec_min_water_mm = 1.0
[availability]
n_ph_curve = [[4.0, 0.5], [9.0, 0.8]]
p_ph_curve = [[4.5, 0.2], [8.5, 0.05]]
k_ph_curve = [[4.0, 0.5], [9.0, 1.0]]
ca_ph_curve = [[4.0, 0.3], [9.0, 1.0]]
mg_ph_curve = [[4.0, 0.3], [9.0, 1.0]]
[root_damage]
waterlog_curve = [[0, 0.01], [0.5, 0]]
heat_curve = [[32, 0], [45, 0.03]]
nitrite_curve = [[0, 0], [8, 0.015]]
acid_curve = [[4.0, 0.006], [5.6, 0.0]]
fusarium_curve = [[0, 0], [0.8, 0.01]]
phytophthora_curve = [[0, 0], [0.8, 0.02]]
fumigant_curve = [[0, 0], [1, 0.2]]
anthracnose_curve = [[0, 0], [0.8, 0.012]]
[crop]
specific_leaf_area_m2_per_g = 0.02
light_interception_curve = [[0, 0], [4, 0.94]]
par_fraction = 0.48
light_use_efficiency_g_per_MJ = 2.5
temp_curve = [[3, 0], [40, 0]]
water_stress_curve = [[0.2, 0], [2.0, 1.0]]
salt_stress_curve = [[0, 1], [8, 0.05]]
nutrient_stress_curve = [[0, 0], [1.0, 1.0]]
target_N_g_per_g = 0.028
target_P_g_per_g = 0.0035
target_K_g_per_g = 0.020
target_Ca_g_per_g = 0.012
target_Mg_g_per_g = 0.004
luxury_uptake_ratio = 1.3
uptake_rate_per_g_root_per_hour = 0.002
ca_uptake_rate_per_g_root_per_hour = 0.00019
mg_uptake_rate_per_g_root_per_hour = 0.00021
n_luxury_max_ratio = 1.3
n_luxury_uptake_curve = [[0, 1], [1.3, 1]]
mn_uptake_regulation_curve = [[0, 1], [100, 1]]
uptake_temp_curve = [[0, 1], [40, 1]]
n_uptake_rate_per_g_root_per_hour = 0.000015
reserve_capacity_fraction = 0
reserve_fill_fraction = 0
reserve_release_rate_per_hour = 0
maintenance_activity_curve = [[-1, 1], [1, 1]]
rgr_mean_relax_rate_per_hour = 0
transpiration_mm_per_hour_per_W_per_m2 = 0.0015
transpiration_humidity_curve = [[0, 1], [1, 1]]
basal_transpiration_mm_per_hour_per_lai = 0
root_recovery_rate_per_hour = 0.002
death_root_health = 0.05
root_dieback_per_damage = 1.0
dry_matter_C_fraction = 0.45
target_Mn_g_per_g = 0.0001
mn_uptake_rate_per_g_root_per_hour = 0.00004
mn_max_uptake_ratio = 30.0
mn_toxicity_curve = [[0, 1], [30, 0.1]]
vegetative_leaf_fraction = 0.6
vegetative_crown_fraction = 0.15
vegetative_root_fraction = 0.25
max_fruit_assimilate_fraction = 0.65
maintenance_rate_leaf_per_hour = 0.00125
maintenance_rate_crown_per_hour = 0.0004
maintenance_rate_root_per_hour = 0.0006
maintenance_rate_fruit_per_hour = 0.0004
maintenance_temp_curve = [[0, 0.18], [25, 1.0], [40, 2.83]]
leaf_senescence_rate_per_hour = 0.0005
fruit_target_N_g_per_g = 0.015
fruit_target_P_g_per_g = 0.0035
fruit_target_K_g_per_g = 0.025
fruit_target_Ca_g_per_g = 0.0015
fruit_target_Mg_g_per_g = 0.0012
fruit_target_Mn_g_per_g = 0.00002
co2_g_per_g_dry_matter = 1.65
co2_growth_curve = [[50, 0], [400, 1.0], [1200, 1.25]]
cold_leaf_damage_curve = [[-6, 0.05], [0, 0]]
cold_fruit_loss_curve = [[-4, 0.2], [0, 0]]
[microbes]
capacity_floor_index = 0.01
decomposer_activity_curve = [[0, 0.15], [1, 1]]
aob_activity_curve = [[0, 0], [1, 1]]
nob_activity_curve = [[0, 0], [1, 1]]
antagonist_protection_factor = 0.5
mycorrhiza_p_release_factor = 2.0
mycorrhiza_water_relief_factor = 0.3
mycorrhiza_salt_relief_factor = 0.3
pathogen_spread_index = 0
susceptibility_spread_index = 0
moist_heat_curve = [[0.2, 0.3], [0.8, 1.0]]
decomposer_food_curve = [[0, 0.4], [30, 1]]
antagonist_food_curve = [[0, 0.3], [60, 1]]
mycorrhiza_host_curve = [[0, 0.05], [15, 1]]
mycorrhiza_p_curve = [[0, 1], [100, 0.1]]
[microbes.decomposer]
growth_rate_per_hour = 0.01
temp_curve = [[0, 0], [25, 1]]
moisture_curve = [[0, 1], [14, 1]]
ph_curve = [[0, 1], [14, 1]]
o2_curve = [[0, 1], [14, 1]]
base_death_rate_per_hour = 0.0002
heat_kill_curve = [[40, 0], [50, 0.03]]
immigration_rate_per_hour = 0.001
competition = {}
[microbes.nitrifier_aob]
growth_rate_per_hour = 0.01
temp_curve = [[0, 0], [25, 1]]
moisture_curve = [[0, 1], [14, 1]]
ph_curve = [[0, 1], [14, 1]]
o2_curve = [[0, 1], [14, 1]]
base_death_rate_per_hour = 0.0002
heat_kill_curve = [[40, 0], [50, 0.03]]
immigration_rate_per_hour = 0.001
competition = {}
[microbes.nitrifier_nob]
growth_rate_per_hour = 0.01
temp_curve = [[0, 0], [25, 1]]
moisture_curve = [[0, 1], [14, 1]]
ph_curve = [[0, 1], [14, 1]]
o2_curve = [[0, 1], [14, 1]]
base_death_rate_per_hour = 0.0002
heat_kill_curve = [[40, 0], [50, 0.03]]
immigration_rate_per_hour = 0.001
competition = {}
[microbes.antagonist]
growth_rate_per_hour = 0.01
temp_curve = [[0, 0], [25, 1]]
moisture_curve = [[0, 1], [14, 1]]
ph_curve = [[0, 1], [14, 1]]
o2_curve = [[0, 1], [14, 1]]
base_death_rate_per_hour = 0.0002
heat_kill_curve = [[40, 0], [50, 0.03]]
immigration_rate_per_hour = 0.001
competition = { fusarium = 0.3, phytophthora = 0.3 }
[microbes.mycorrhiza]
growth_rate_per_hour = 0.01
temp_curve = [[0, 0], [25, 1]]
moisture_curve = [[0, 1], [14, 1]]
ph_curve = [[0, 1], [14, 1]]
o2_curve = [[0, 1], [14, 1]]
base_death_rate_per_hour = 0.0002
heat_kill_curve = [[40, 0], [50, 0.03]]
immigration_rate_per_hour = 0.001
competition = {}
[microbes.fusarium]
growth_rate_per_hour = 0.01
temp_curve = [[0, 0], [25, 1]]
moisture_curve = [[0, 1], [14, 1]]
ph_curve = [[0, 1], [14, 1]]
o2_curve = [[0, 1], [14, 1]]
base_death_rate_per_hour = 0.0002
heat_kill_curve = [[40, 0], [50, 0.03]]
immigration_rate_per_hour = 0.001
competition = { antagonist = 0.8 }
saprophytic_capacity_index = 0.3
host_capacity_index = 0.7
[microbes.phytophthora]
growth_rate_per_hour = 0.01
temp_curve = [[0, 0], [25, 1]]
moisture_curve = [[0, 1], [14, 1]]
ph_curve = [[0, 1], [14, 1]]
o2_curve = [[0, 1], [14, 1]]
base_death_rate_per_hour = 0.0002
heat_kill_curve = [[40, 0], [50, 0.03]]
immigration_rate_per_hour = 0.001
competition = { antagonist = 0.8 }
saprophytic_capacity_index = 0.3
host_capacity_index = 0.7
[microbes.anthracnose]
growth_rate_per_hour = 0.003
temp_curve = [[9, 0], [27, 1]]
moisture_curve = [[0, 1], [14, 1]]
ph_curve = [[0, 1], [14, 1]]
o2_curve = [[0, 1], [14, 1]]
base_death_rate_per_hour = 0.002
heat_kill_curve = [[40, 0], [50, 0.03]]
immigration_rate_per_hour = 0
competition = { antagonist = 0.4 }
saprophytic_capacity_index = 0.05
host_capacity_index = 0.8
[soil_disease.fusarium]
infection_rate_per_hour = 0.004
infection_env_curve_temp = [[15, 0], [28, 1]]
infection_env_curve_o2 = [[0, 1], [1, 1]]
infection_env_curve_ph = [[4.5, 1.8], [6.5, 1]]
onset_infection_index = 0.15
[soil_disease.phytophthora]
infection_rate_per_hour = 0.01
infection_env_curve_temp = [[8, 0.1], [25, 1]]
infection_env_curve_o2 = [[0, 1], [0.5, 0]]
infection_env_curve_ph = [[0, 1], [14, 1]]
onset_infection_index = 0.1
[disinfection]
flush_N_g_per_m2_per_index = 6.0
flush_C_to_N_ratio = 6
mn_reduction_per_index = 0.03
till_residue_release_fraction = 0.5
[humidity]
saturation_vapor_curve = [[0, 4.85], [20, 17.3]]
inner_surface_resistance_m2K_per_W = 0.09
condensation_rate_per_hour = 4
rh_mean_relax_rate_per_hour = 0.014
vent_airflow_factor = 1
fan_airflow_index = 0.6
canopy_rh_boost_index = 0.08
canopy_lai_ref = 3
dew_rh_index = 0.92
dew_solar_limit_W_per_m2 = 50
wetting_rate_per_hour = 0.5
drying_rate_per_hour = 0.3
airflow_drying_factor = 2
drying_rh_curve = [[0.5, 1.5], [1, 0]]
drying_solar_factor_per_W_per_m2 = 0.01
overhead_wet_index = 1
wet_threshold_index = 0.5
wetness_break_hours = 4
)" R"(
[foliar_disease]
n_susceptibility_curve = [[0, 1], [30, 2]]
n_mean_relax_rate_per_hour = 0.003
mite_mean_relax_rate_per_hour = 0.003
[foliar_disease.anthracnose]
temp_curve = [[9, 0], [27, 1], [38, 0]]
wetness_curve = [[0, 0], [36, 1]]
contact_rate_per_hour = 0.0005
progress_rate_per_hour = 0.004
splash_transfer_index = 0.01
sporulation_curve = [[0, 0], [0.6, 1]]
onset_infection_index = 0.15
soil_deposit_per_hour = 0
residue_index = 0
[foliar_disease.botrytis]
infection_temp_curve = [[5, 0.02], [20, 1], [33, 0]]
wetness_curve = [[0, 0], [32, 1]]
infection_rate_per_hour = 0.03
infection_decay_rate_per_hour = 0.002
sporulation_temp_curve = [[5, 0.1], [17.5, 1], [30, 0]]
spore_source_per_infection_per_hour = 0.02
spore_source_per_rotten_fruit_per_hour = 0.01
spore_decay_rate_per_hour = 0.02
spore_vent_fraction = 0.05
fruit_rot_prob_per_hour = 0.002
fruit_stage_curve = [[0, 1], [1, 1]]
onset_infection_index = 0.2
[foliar_disease.powdery_mildew]
temp_curve = [[5, 0.05], [20, 1], [32, 0]]
rh_curve = [[0.6, 0], [0.75, 1]]
free_water_inhibition_factor = 0.7
light_curve = [[0, 1.3], [120, 1]]
density_curve = [[0, 0.8], [4, 1.2]]
young_leaf_curve = [[0, 0.3], [0.03, 1]]
infection_rate_per_hour = 0.004
spore_source_per_infection_per_hour = 0.02
spore_decay_rate_per_hour = 0.005
spore_vent_fraction = 0.05
photosynthesis_damage_curve = [[0, 0], [1, 0.5]]
fruit_prob_curve = [[0, 0], [0.6, 0.17]]
onset_infection_index = 0.05
[foliar_disease.mites]
mite_growth_curve_per_hour = [[12, 0], [25, 0.0063], [40, 0]]
mite_rh_curve = [[0.2, 0.8], [1, 0.8]]
mite_spread_rate_per_hour = 0.0005
photosynthesis_damage_curve = [[0, 0], [1, 0.6]]
predator_temp_curve = [[10, 0], [25, 1], [35, 0]]
predator_hatch_curve = [[0.4, 0], [1, 1]]
attack_rate_per_hour = 5
handling_time_hours = 1.25
conversion_factor = 0.01
predator_death_rate_per_hour = 0.0022
predator_starvation_death_per_hour = 0.012
predator_death_temp_curve = [[0, 1], [40, 1]]
predator_search_rate_per_hour = 0.05
mite_onset_index = 0.05
)" R"(
[[seedling_grade]]
id = "A"
leaf_g_per_plant = 4.0
crown_g_per_plant = 3
root_g_per_plant = 2.0
initial_induction_index = 0.8
latent_anthracnose_fraction = 0
latent_anthracnose_index = 0.05
spider_mite_fraction = 0
spider_mite_start_index = 0.01
powdery_mildew_start_index = 0
[[seedling_grade]]
id = "C"
leaf_g_per_plant = 2.5
crown_g_per_plant = 1.2
root_g_per_plant = 1.0
initial_induction_index = 0.0
latent_anthracnose_fraction = 0.25
latent_anthracnose_index = 0.05
spider_mite_fraction = 0.15
spider_mite_start_index = 0.01
powdery_mildew_start_index = 0.02
[[compost]]
id = "c1"
name = "compost one"
C_g_per_kg = 180.0
N_g_per_kg = 12.0
labile_C_fraction = 0.6
labile_N_fraction = 0.7
k_g_per_kg = 12.0
ca_g_per_kg = 10.0
mg_g_per_kg = 4.0
p_g_per_kg = 4.0
s_g_per_kg = 2.0
mn_g_per_kg = 0.3
direct_acid_eq_per_kg = 0.0
[[compost]]
id = "c2"
name = "b"
C_g_per_kg = 150
N_g_per_kg = 12
labile_C_fraction = 0.1
labile_N_fraction = 0.1
k_g_per_kg = 12.0
ca_g_per_kg = 10.0
mg_g_per_kg = 4.0
p_g_per_kg = 4.0
s_g_per_kg = 2.0
mn_g_per_kg = 0.3
direct_acid_eq_per_kg = 0.0
[[fertilizer]]
id = "f1"
name = "ammonium sulfate"
nh4_N_g_per_kg = 210.0
no3_N_g_per_kg = 0.0
k_g_per_kg = 12.0
ca_g_per_kg = 10.0
mg_g_per_kg = 4.0
p_g_per_kg = 4.0
s_g_per_kg = 2.0
mn_g_per_kg = 0.3
direct_acid_eq_per_kg = 0.0
[[lime]]
id = "l1"
name = "lime one"
eq_per_kg = 20.0
ca_fraction = 1.0
mg_fraction = 0.0
[[mulch]]
id = "clear_film"
name = "clear"
soil_solar_gain_degC_per_W_per_m2 = 0.025
evaporation_factor = 0.1
fumigant_retention_factor = 0.3
canopy_reflect_factor = 0.0
soil_splash_factor = 0.1
plant_splash_factor = 2.5
[[mulch]]
id = "white_film"
name = "white"
soil_solar_gain_degC_per_W_per_m2 = -0.004
evaporation_factor = 0.2
fumigant_retention_factor = 0.3
canopy_reflect_factor = 0.25
soil_splash_factor = 0.1
plant_splash_factor = 2.5
[[disinfectant]]
id = "dazomet"
name = "dazomet"
residue_index_per_g_per_m2 = 0.03333333333333333
kill_rate_per_hour = { decomposer = 0.006, nitrifier_aob = 0.002, nitrifier_nob = 0.001, antagonist = 0.006, mycorrhiza = 0.02, fusarium = 0.012, anthracnose = 0.02, phytophthora = 0.015 }
decay_rate_per_hour = 0.02
decay_temp_curve = [[5, 0.3], [35, 1.5]]
activation_moisture_curve = [[0.2, 0.1], [0.7, 1]]
[[inoculant]]
id = "bacillus_product"
name = "bacillus"
index_gain_per_g_per_m2 = { antagonist = 0.02 }
C_g_per_kg = 300.0
N_g_per_kg = 10.0
[[predator]]
id = "persimilis"
name = "persimilis"
index_per_individual_per_m2 = 0.00003
)";

// 문서 안의 문자열을 한 번 바꿔요 (없으면 테스트 실수라서 실패).
std::string replaced(std::string_view text, std::string_view from, std::string_view to) {
    std::string out(text);
    const auto pos = out.find(from);
    REQUIRE(pos != std::string::npos);
    out.replace(pos, from.size(), to);
    return out;
}

// [[schedule]] 항목을 유효한 시나리오 뒤에 붙여요.
std::string scenario_with(std::string_view extra) {
    return std::string(kValidScenario) + std::string(extra);
}

const sim::AddCompostCommand& as_compost(const simdata::ScheduledCommand& c) {
    return std::get<sim::AddCompostCommand>(c.command);
}

}  // namespace

TEST_CASE("Params TOML loads strictly", "[unit][loader]") {
    SECTION("valid document fills every field") {
        auto p = simdata::parse_params_toml(kValidParams, "p.toml");
        REQUIRE(p.has_value());
        CHECK(p->schema_version == 7);
        CHECK(p->house.ground_heat_W_per_m2 == 6.1);  // M4: house.base_offset_degC 삭제로 같은 float 키 검사로 바꿈
        CHECK(p->soil_water.evap_moisture_curve.points.size() == 2);
        CHECK(p->soil_water.evap_moisture_curve(0.5) == 0.5);
        CHECK(p->organic_matter.humus_C_to_N_ratio == 10.0);  // 정수로 쓴 실수 키
        CHECK(p->nitrification.nitrite_toxicity_threshold_g_N_per_m2 == 2.0);
        CHECK(p->cation_exchange.nh4_retention_factor == 70.0);
        CHECK(p->cation_exchange.solver_iterations == 60);
        REQUIRE(p->composts.size() == 2);
        CHECK(p->composts[0].id == "c1");
        CHECK(p->composts[1].C_g_per_kg == 150.0);
        REQUIRE(p->fertilizers.size() == 1);
        CHECK(p->fertilizers[0].nh4_N_g_per_kg == 210.0);
    }
    SECTION("M2 tables, curves and entries are read") {
        auto p = simdata::parse_params_toml(kValidParams, "p.toml");
        REQUIRE(p.has_value());
        CHECK(p->soil_water.drainage_compaction_curve.points.size() == 2);
        CHECK(p->organic_matter.o2_curve(1.0) == 1.0);
        CHECK(p->nitrification.step2_o2_curve.points.size() == 2);
        CHECK(p->nitrification.nitrite_toxicity_ph_curve(4.5) == 8.0);
        CHECK(p->aeration.tilled_compaction_index == 0.1);
        CHECK(p->ions.ca_g_per_eq == 20.039);
        CHECK(p->soil_chemistry.uptake_H_eq_per_eq_NO3 == -1.0);
        CHECK(p->soil_chemistry.ph_max == 10.0);
        CHECK(p->availability.p_ph_curve.points.size() == 2);
        CHECK(p->root_damage.acid_curve.points.size() == 2);
        CHECK(p->crop.target_P_g_per_g == 0.0035);
        CHECK(p->crop.death_root_health == 0.05);
        CHECK(p->composts[0].k_g_per_kg == 12.0);
        CHECK(p->composts[1].direct_acid_eq_per_kg == 0.0);
        CHECK(p->fertilizers[0].s_g_per_kg == 2.0);
        REQUIRE(p->limes.size() == 1);
        CHECK(p->limes[0].id == "l1");
        CHECK(p->limes[0].eq_per_kg == 20.0);
    }
    SECTION("missing M2 table is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "[aeration]\n", "[aerat]\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'aeration'"));
    }
    SECTION("missing M2 key in an existing table is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "o2_curve = [[0, 0.15], [1, 1.0]]\n", ""),
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'organic_matter.o2_curve'"));
    }
    SECTION("missing inorganic key in a [[compost]] entry is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "direct_acid_eq_per_kg = 0.0\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'compost[0].direct_acid_eq_per_kg'"));
    }
    SECTION("unknown key in [[lime]] is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "eq_per_kg = 20.0\n", "eq_per_kg = 20.0\nph = 9\n"),
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'lime[0].ph'"));
    }
    SECTION("missing [[lime]] is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "[[lime]]", "[[lyme]]"), "p.toml");
        REQUIRE_FALSE(p.has_value());
    }
    SECTION("float key given as integer is accepted") {
        // M4: house.base_offset_degC 삭제로 같은 의도의 house 키(heater_capacity_W_per_m2)로 바꿈
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "heater_capacity_W_per_m2 = 150.0", "heater_capacity_W_per_m2 = 150"), "p.toml");
        REQUIRE(p.has_value());
        CHECK(p->house.heater_capacity_W_per_m2 == 150.0);
    }
    SECTION("unknown key in a nested table is rejected") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "[cation_exchange]\n", "[cation_exchange]\nnh4_fraction = 1\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("p.toml"));
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'cation_exchange.nh4_fraction'"));
    }
    SECTION("unknown key inside a [[compost]] entry is rejected") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "id = \"c2\"\n", "id = \"c2\"\nK_g_per_kg = 1\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'compost[1].K_g_per_kg'"));
    }
    SECTION("unknown top-level table is rejected") {
        auto p = simdata::parse_params_toml(std::string(kValidParams) + "[weather]\nx = 1\n", "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'weather'"));
    }
    SECTION("missing key is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "drainage_rate_per_hour = 0.3\n", ""),
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message,
                   ContainsSubstring("missing required key 'soil_water.drainage_rate_per_hour'"));
    }
    SECTION("missing table is rejected") {
        auto p = simdata::parse_params_toml("schema_version = 7\n", "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'house'"));
    }
    SECTION("missing key in a [[fertilizer]] entry is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "no3_N_g_per_kg = 0.0\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message,
                   ContainsSubstring("missing required key 'fertilizer[0].no3_N_g_per_kg'"));
    }
    SECTION("curve with a non-pair element is rejected") {
        constexpr std::string_view good = "[[0.0, 0.0], [1.0, 1.0]]";
        auto triple = simdata::parse_params_toml(
            replaced(kValidParams, good, "[[0.0, 0.0], [1.0, 1.0, 2.0]]"), "p.toml");
        REQUIRE_FALSE(triple.has_value());
        CHECK_THAT(triple.error().message, ContainsSubstring("soil_water.evap_moisture_curve[1]"));
        CHECK_FALSE(simdata::parse_params_toml(replaced(kValidParams, good, "[[0.0, 0.0], 1.0]"), "p.toml")
                        .has_value());
        CHECK_FALSE(simdata::parse_params_toml(replaced(kValidParams, good, "[[0.0, 0.0], [1.0, \"a\"]]"),
                                               "p.toml")
                        .has_value());
    }
    SECTION("wrong type is rejected") {
        auto p = simdata::parse_params_toml("schema_version = \"2\"\n", "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("must be a integer"));
        auto q = simdata::parse_params_toml(
            replaced(kValidParams, "humification_fraction = 0.3", "humification_fraction = \"x\""), "p.toml");
        REQUIRE_FALSE(q.has_value());
        CHECK_THAT(q.error().message, ContainsSubstring("organic_matter.humification_fraction"));
    }
    SECTION("old schema version is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "schema_version = 7", "schema_version = 4"),
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK(p.error().code == sim::ErrorCode::UnsupportedVersion);
    }
    SECTION("missing schema_version") {
        auto p = simdata::parse_params_toml("", "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'schema_version'"));
    }
    SECTION("syntax error reports the location") {
        auto p = simdata::parse_params_toml("schema_version = \n", "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("p.toml:1:"));
    }
}

// 2026-11-19 디렉터 승인: 비교 대상을 test_params()에서 repo_params()(test_params + M5a 사전 작업 값)로 바꿈
TEST_CASE("Repository params.toml matches the spec values used by tests", "[unit][loader][data]") {
    auto loaded = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(loaded.has_value());
    CHECK(*loaded == test::repo_params());
}

TEST_CASE("Scenario TOML loads strictly", "[unit][loader]") {
    SECTION("valid") {
        auto doc = simdata::parse_scenario_toml(kValidScenario, "s.toml");
        REQUIRE(doc.has_value());
        CHECK(doc->scenario.name == "t");
        CHECK(doc->scenario.seed == 7);
        CHECK(doc->scenario.start_day_of_year == 100);
        CHECK(doc->scenario.bed_rows == 2);
        CHECK(doc->scenario.bed_cols == 5);
        CHECK(doc->weather_csv == "w.csv");
        CHECK(doc->scenario.soil.water_mm == 45.0);  // 정수로 쓴 실수 키
        CHECK(doc->scenario.soil.stable_N_g_per_m2 == 360.0);
        CHECK(doc->schedule.empty());
        CHECK(doc->scenario.soil.saturation_mm == 110.0);
        CHECK(doc->scenario.soil.compaction_index == 0.3);
        CHECK(doc->scenario.soil.base_buffer_eq_per_m2_per_pH == 5.0);
        CHECK(doc->scenario.soil.k_g_per_m2 == 60.0);
        CHECK(doc->scenario.soil.ca_g_per_m2 == 400.0);
        CHECK(doc->scenario.soil.mg_g_per_m2 == 60.0);
        CHECK(doc->scenario.soil.p_g_per_m2 == 25.0);
        CHECK(doc->scenario.soil.s_g_per_m2 == 3.0);
        CHECK_FALSE(doc->scenario.crop.has_value());
        CHECK(doc->scenario.stress_combination == sim::StressCombination::Minimum);
    }
    SECTION("[crop] present") {
        auto doc = simdata::parse_scenario_toml(
            replaced(kValidScenario, "[difficulty]", "[crop]\nseedling_grade = \"B\"\nplants_per_m2 = 8\ninitial_establishment_index = 0\n[difficulty]"),
            "s.toml");
        REQUIRE(doc.has_value());
        REQUIRE(doc->scenario.crop.has_value());
        CHECK(doc->scenario.crop->seedling_grade == "B");
        CHECK(doc->scenario.crop->plants_per_m2 == 8.0);  // 정수로 쓴 실수 키
    }
    SECTION("[crop] with a missing or unknown key is rejected") {
        auto missing = simdata::parse_scenario_toml(
            replaced(kValidScenario, "[difficulty]", "[crop]\nseedling_grade = \"B\"\n[difficulty]"), "s.toml");
        REQUIRE_FALSE(missing.has_value());
        CHECK_THAT(missing.error().message, ContainsSubstring("missing required key 'crop.plants_per_m2'"));
        auto unknown = simdata::parse_scenario_toml(
            replaced(kValidScenario, "[difficulty]",
                     "[crop]\nseedling_grade = \"B\"\nplants_per_m2 = 8\ninitial_establishment_index = 0\nfruit_g_per_m2 = 1\n[difficulty]"),
            "s.toml");
        REQUIRE_FALSE(unknown.has_value());
        CHECK_THAT(unknown.error().message, ContainsSubstring("unknown key 'crop.fruit_g_per_m2'"));
    }
    SECTION("[difficulty] product") {
        auto doc = simdata::parse_scenario_toml(replaced(kValidScenario, "\"minimum\"", "\"product\""), "s.toml");
        REQUIRE(doc.has_value());
        CHECK(doc->scenario.stress_combination == sim::StressCombination::Product);
    }
    SECTION("[difficulty] invalid value") {
        auto doc = simdata::parse_scenario_toml(replaced(kValidScenario, "\"minimum\"", "\"average\""), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("difficulty.stress_combination"));
    }
    SECTION("missing [difficulty]") {
        auto doc = simdata::parse_scenario_toml(
            replaced(kValidScenario, "[difficulty]\nstress_combination = \"minimum\"\n", ""), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("missing required key 'difficulty'"));
    }
    SECTION("missing M2 key in [soil]") {
        auto doc = simdata::parse_scenario_toml(replaced(kValidScenario, "saturation_mm = 110.0\n", ""), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("missing required key 'soil.saturation_mm'"));
    }
    SECTION("missing [soil] table") {
        auto doc = simdata::parse_scenario_toml(replaced(kValidScenario, "[soil]", "[soyl]"), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("missing required key 'soil'"));
    }
    SECTION("unknown key in [soil]") {
        auto doc = simdata::parse_scenario_toml(
            replaced(kValidScenario, "[difficulty]", "clay_percent = 3\n[difficulty]"), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("unknown key 'soil.clay_percent'"));
    }
    SECTION("missing key in [soil]") {
        auto doc = simdata::parse_scenario_toml(replaced(kValidScenario, "ph = 6.5\n", ""), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("missing required key 'soil.ph'"));
    }
    SECTION("unknown nested key names its table") {
        auto doc = simdata::parse_scenario_toml(
            replaced(kValidScenario, "bed_cols = 5\n", "bed_cols = 5\nbed_depth_cm = 30\n"), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("unknown key 'house.bed_depth_cm'"));
    }
    SECTION("missing table") {
        auto doc = simdata::parse_scenario_toml("schema_version = 7\n[scenario]\n", "s.toml");
        CHECK_FALSE(doc.has_value());
    }
    SECTION("negative seed") {
        CHECK_FALSE(
            simdata::parse_scenario_toml(replaced(kValidScenario, "seed = 7", "seed = -1"), "s.toml").has_value());
    }
    SECTION("unsupported schema version") {
        auto doc = simdata::parse_scenario_toml(replaced(kValidScenario, "schema_version = 7", "schema_version = 4"),
                                                "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK(doc.error().code == sim::ErrorCode::UnsupportedVersion);
    }
}

TEST_CASE("Scenario schedule expands into sorted commands", "[unit][loader][schedule]") {
    SECTION("compost with the default area covers the whole grid") {
        auto doc = simdata::parse_scenario_toml(scenario_with(R"(
[[schedule]]
tick = 5
action = "add_compost"
compost = "immature_manure"
kg_per_m2 = 3
)"),
                                                "s.toml");
        REQUIRE(doc.has_value());
        REQUIRE(doc->schedule.size() == 1);
        CHECK(doc->schedule[0].tick == 5);
        const auto& c = as_compost(doc->schedule[0]);
        CHECK(c.compost_id == "immature_manure");
        CHECK(c.kg_per_m2 == 3.0);
        CHECK(c.area == sim::TileArea{0, 0, 2, 5});
    }
    SECTION("explicit area, irrigate and fertilizer") {
        auto doc = simdata::parse_scenario_toml(scenario_with(R"(
[[schedule]]
tick = 0
action = "irrigate"
amount_mm = 12.5
area = [1, 2, 1, 3]
[[schedule]]
tick = 1
action = "add_fertilizer"
fertilizer = "urea"
g_per_m2 = 20
)"),
                                                "s.toml");
        REQUIRE(doc.has_value());
        REQUIRE(doc->schedule.size() == 2);
        const auto& irrigate = std::get<sim::IrrigateCommand>(doc->schedule[0].command);
        CHECK(irrigate.amount_mm == 12.5);
        CHECK(irrigate.area == sim::TileArea{1, 2, 1, 3});
        const auto& fert = std::get<sim::AddFertilizerCommand>(doc->schedule[1].command);
        CHECK(fert.fertilizer_id == "urea");
        CHECK(fert.g_per_m2 == 20.0);
        CHECK(fert.area == sim::TileArea{0, 0, 2, 5});
    }
    SECTION("repeats expand and the result is sorted stably by tick") {
        auto doc = simdata::parse_scenario_toml(scenario_with(R"(
[[schedule]]
tick = 10
action = "irrigate"
amount_mm = 1
repeat_every_ticks = 24
repeat_count = 3
[[schedule]]
tick = 0
action = "add_compost"
compost = "a"
kg_per_m2 = 1
[[schedule]]
tick = 10
action = "add_compost"
compost = "b"
kg_per_m2 = 2
)"),
                                                "s.toml");
        REQUIRE(doc.has_value());
        REQUIRE(doc->schedule.size() == 5);
        const std::uint64_t ticks[] = {0, 10, 10, 34, 58};
        for (std::size_t i = 0; i < 5; ++i) {
            CHECK(doc->schedule[i].tick == ticks[i]);
        }
        CHECK(as_compost(doc->schedule[0]).compost_id == "a");
        // 같은 틱 10은 파일 순서대로: 관수(첫 항목) 다음에 퇴비 b
        CHECK(std::holds_alternative<sim::IrrigateCommand>(doc->schedule[1].command));
        CHECK(as_compost(doc->schedule[2]).compost_id == "b");
        CHECK(std::holds_alternative<sim::IrrigateCommand>(doc->schedule[3].command));
        CHECK(std::holds_alternative<sim::IrrigateCommand>(doc->schedule[4].command));
    }
    SECTION("add_lime and till") {
        auto doc = simdata::parse_scenario_toml(scenario_with(R"(
[[schedule]]
tick = 3
action = "add_lime"
lime = "dolomite"
g_per_m2 = 150
area = [0, 0, 1, 2]
[[schedule]]
tick = 4
action = "till"
)"),
                                                "s.toml");
        REQUIRE(doc.has_value());
        REQUIRE(doc->schedule.size() == 2);
        const auto& lime = std::get<sim::AddLimeCommand>(doc->schedule[0].command);
        CHECK(lime.lime_id == "dolomite");
        CHECK(lime.g_per_m2 == 150.0);
        CHECK(lime.area == sim::TileArea{0, 0, 1, 2});
        const auto& till = std::get<sim::TillCommand>(doc->schedule[1].command);
        CHECK(till.area == sim::TileArea{0, 0, 2, 5});
    }
    SECTION("till with an area") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"till\"\narea = [1, 1, 1, 1]\n"), "s.toml");
        REQUIRE(doc.has_value());
        CHECK(std::get<sim::TillCommand>(doc->schedule[0].command).area == sim::TileArea{1, 1, 1, 1});
    }
    SECTION("till with an amount key is rejected") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"till\"\ng_per_m2 = 5\n"), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("unknown key 'schedule[0].g_per_m2'"));
    }
    SECTION("add_lime without a lime id is rejected") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"add_lime\"\ng_per_m2 = 5\n"), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("missing required key 'schedule[0].lime'"));
    }
    SECTION("unknown action") {
        auto doc = simdata::parse_scenario_toml(scenario_with("[[schedule]]\ntick = 0\naction = \"harvest\"\n"),
                                                "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("unknown action 'harvest'"));
    }
    SECTION("key not valid for the action") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"irrigate\"\namount_mm = 3\nkg_per_m2 = 1\n"),
            "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("unknown key 'schedule[0].kg_per_m2'"));
    }
    SECTION("missing action-specific key") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"add_compost\"\nkg_per_m2 = 1\n"), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("missing required key 'schedule[0].compost'"));
    }
    SECTION("repeat keys must come as a pair") {
        auto only_every = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"irrigate\"\namount_mm = 3\nrepeat_every_ticks = 24\n"),
            "s.toml");
        CHECK_FALSE(only_every.has_value());
        auto only_count = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"irrigate\"\namount_mm = 3\nrepeat_count = 2\n"),
            "s.toml");
        CHECK_FALSE(only_count.has_value());
    }
    SECTION("repeat values below 1 and malformed area are rejected") {
        constexpr std::string_view head = "[[schedule]]\ntick = 0\naction = \"irrigate\"\namount_mm = 3\n";
        CHECK_FALSE(simdata::parse_scenario_toml(
                        scenario_with(std::string(head) + "repeat_every_ticks = 24\nrepeat_count = 0\n"), "s.toml")
                        .has_value());
        CHECK_FALSE(simdata::parse_scenario_toml(
                        scenario_with(std::string(head) + "repeat_every_ticks = 0\nrepeat_count = 2\n"), "s.toml")
                        .has_value());
        CHECK_FALSE(simdata::parse_scenario_toml(scenario_with(std::string(head) + "area = [0, 0, 1]\n"), "s.toml")
                        .has_value());
    }
    SECTION("negative tick") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = -1\naction = \"irrigate\"\namount_mm = 3\n"), "s.toml");
        CHECK_FALSE(doc.has_value());
    }
}

TEST_CASE("Weather CSV loads strictly", "[unit][loader]") {
    SECTION("valid, with comments and CRLF line endings") {
        auto w = simdata::parse_weather_csv(weather_csv(sim::kHoursPerYear, "\r\n"), "w.csv");
        REQUIRE(w.has_value());
        CHECK(w->size() == sim::kHoursPerYear);
        CHECK(w->front().outside_air_temp_degC == -2.5);
    }
    SECTION("missing a row") {
        auto w = simdata::parse_weather_csv(weather_csv(sim::kHoursPerYear - 1), "w.csv");
        REQUIRE_FALSE(w.has_value());
        CHECK_THAT(w.error().message, ContainsSubstring("expected 8760 data rows"));
    }
    SECTION("extra row") {
        std::string text = weather_csv(sim::kHoursPerYear);
        text += "1,0,0,0,0\n";
        CHECK_FALSE(simdata::parse_weather_csv(text, "w.csv").has_value());
    }
    SECTION("wrong header") {
        std::string text = weather_csv(sim::kHoursPerYear);
        text.replace(text.find("hour,"), 5, "hr,");
        CHECK_FALSE(simdata::parse_weather_csv(text, "w.csv").has_value());
    }
    SECTION("rows out of order") {
        std::string text = weather_csv(sim::kHoursPerYear);
        const auto pos = text.find("\n1,1,");
        text.replace(pos, 5, "\n1,2,");
        auto w = simdata::parse_weather_csv(text, "w.csv");
        REQUIRE_FALSE(w.has_value());
        CHECK_THAT(w.error().message, ContainsSubstring("expected day_of_year=1 hour=1"));
    }
    SECTION("malformed number") {
        std::string text = weather_csv(sim::kHoursPerYear);
        text.replace(text.find("-2.5"), 4, "abc");
        CHECK_FALSE(simdata::parse_weather_csv(text, "w.csv").has_value());
    }
}

TEST_CASE("Repository data files load and build a world", "[unit][loader][data]") {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    auto scenario = simdata::load_scenario(test::repo_path("data/scenarios/empty.toml"));
    REQUIRE(scenario.has_value());
    CHECK(scenario->weather_hourly.size() == sim::kHoursPerYear);
    CHECK(sim::World::create(*params, std::move(*scenario)).has_value());
}

TEST_CASE("Scenario bundle carries the schedule", "[unit][loader][data]") {
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/empty.toml"));
    REQUIRE(bundle.has_value());
    CHECK(bundle->scenario.name == "empty");
    CHECK(bundle->schedule.empty());
}

TEST_CASE("Repository scenarios load with the M2 fields", "[unit][loader][data]") {
    auto empty = simdata::load_scenario(test::repo_path("data/scenarios/empty.toml"));
    REQUIRE(empty.has_value());
    CHECK(empty->soil.saturation_mm == 110.0);
    CHECK_FALSE(empty->crop.has_value());
    CHECK(empty->stress_combination == sim::StressCombination::Minimum);

    auto m1 = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m1/s3_over.toml"));
    REQUIRE(m1.has_value());
    CHECK_FALSE(m1->scenario.crop.has_value());
    CHECK(m1->scenario.soil.water_mm == 45.0);
    CHECK_FALSE(m1->schedule.empty());
}

TEST_CASE("Missing files report an I/O error", "[unit][loader]") {
    auto p = simdata::load_params(test::repo_path("data/does-not-exist.toml"));
    REQUIRE_FALSE(p.has_value());
    CHECK(p.error().code == sim::ErrorCode::Io);
}

// ---- M3 (schema 4) ----

TEST_CASE("M3 params tables, keys and entries are read", "[unit][loader][m3]") {
    auto p = simdata::parse_params_toml(kValidParams, "p.toml");
    REQUIRE(p.has_value());
    CHECK(p->cation_exchange.cec_organic_eq_per_g_C == 0.0015);
    CHECK(p->cation_exchange.exchange_rate_per_hour == 0.3);
    CHECK(p->cation_exchange.ca_retention_factor == 760.0);
    CHECK(p->cation_exchange.buffer_eq_per_pH_per_eq_cec == 0.15);
    CHECK(p->antagonism.ca_curve(1.2) == 0.15);
    CHECK(p->antagonism.mg_curve(1.2) == 0.1);
    CHECK(p->antagonism.min_demand_factor == 0.6);
    CHECK(p->phosphorus.fixation_rate_per_hour == 0.0015);
    CHECK(p->phosphorus.release_rate_per_hour == 0.00005);
    CHECK(p->manganese.retention_ph_curve(4.5) == 450.0);
    CHECK(p->manganese.oxidation_o2_curve.points.size() == 2);
    CHECK(p->manganese.reduction_rate_per_hour == 0.00006);
    CHECK(p->ions.mn_g_per_eq == 27.469);
    CHECK(p->crop.ca_uptake_rate_per_g_root_per_hour == 0.00019);
    CHECK(p->crop.mg_uptake_rate_per_g_root_per_hour == 0.00021);
    CHECK(p->crop.target_Mn_g_per_g == 0.0001);
    CHECK(p->crop.mn_uptake_rate_per_g_root_per_hour == 0.00004);
    CHECK(p->crop.mn_max_uptake_ratio == 30.0);
    CHECK(p->crop.mn_toxicity_curve(30.0) == 0.1);
    CHECK(p->composts[0].mn_g_per_kg == 0.3);
    CHECK(p->fertilizers[0].mn_g_per_kg == 0.3);
}

TEST_CASE("M3 params reject missing, removed and unknown keys", "[unit][loader][m3]") {
    SECTION("missing M3 table is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "[manganese]\n", "[mangan]\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'manganese'"));
    }
    SECTION("missing M3 key in an existing table is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "solver_iterations = 60\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'cation_exchange.solver_iterations'"));
    }
    SECTION("missing antagonism floor is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "min_demand_factor = 0.6\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'antagonism.min_demand_factor'"));
    }
    SECTION("missing crop Mn key is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "mn_max_uptake_ratio = 30.0\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'crop.mn_max_uptake_ratio'"));
    }
    SECTION("missing mn_g_per_kg in a compost entry is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "mn_g_per_kg = 0.3\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'compost[0].mn_g_per_kg'"));
    }
    SECTION("removed M2 solution fraction keys are unknown now") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "[soil_chemistry]\n", "[soil_chemistry]\nk_solution_fraction = 0.03\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'soil_chemistry.k_solution_fraction'"));
    }
    SECTION("removed [leaching] table is unknown now") {
        auto p = simdata::parse_params_toml(std::string(kValidParams) + "[leaching]\nnh4_solution_fraction = 0.1\n",
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'leaching'"));
    }
    SECTION("removed mn_retention_factor is unknown now") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "[cation_exchange]\n", "[cation_exchange]\nmn_retention_factor = 1500.0\n"),
            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'cation_exchange.mn_retention_factor'"));
    }
    SECTION("solver_iterations must be an integer") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "solver_iterations = 60", "solver_iterations = 60.5"),
                                            "p.toml");
        CHECK_FALSE(p.has_value());
    }
    SECTION("params schema 4 (M3) is rejected as an old version") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "schema_version = 7", "schema_version = 4"),
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK(p.error().code == sim::ErrorCode::UnsupportedVersion);
    }
}

TEST_CASE("M3 scenario soil keys are read and required", "[unit][loader][m3]") {
    SECTION("valid") {
        auto doc = simdata::parse_scenario_toml(kValidScenario, "s.toml");
        REQUIRE(doc.has_value());
        CHECK(doc->scenario.soil.cec_mineral_eq_per_m2 == 25.0);
        CHECK(doc->scenario.soil.mn_g_per_m2 == 5.0);
        CHECK(doc->scenario.soil.mn_oxide_g_per_m2 == 100.0);
        CHECK(doc->scenario.soil.p_fixed_g_per_m2 == 150.0);
    }
    for (const char* key : {"cec_mineral_eq_per_m2", "mn_g_per_m2", "mn_oxide_g_per_m2", "p_fixed_g_per_m2"}) {
        INFO(key);
        std::string text(kValidScenario);
        const auto begin = text.find(std::string(key) + " = ");
        REQUIRE(begin != std::string::npos);
        const auto end = text.find('\n', begin);
        text.erase(begin, end - begin + 1);
        auto doc = simdata::parse_scenario_toml(text, "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring(std::string("missing required key 'soil.") + key + "'"));
    }
}

TEST_CASE("Repository params and scenarios carry the M3 fields", "[unit][loader][data][m3]") {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    CHECK(params->schema_version == 7);
    CHECK(params->cation_exchange.solver_iterations == 60);
    CHECK(params->crop.mn_max_uptake_ratio == 30.0);
    bool has_mnso4 = false;
    for (const auto& fertilizer : params->fertilizers) {
        has_mnso4 = has_mnso4 || (fertilizer.id == "manganese_sulfate" && fertilizer.mn_g_per_kg > 0.0);
    }
    CHECK(has_mnso4);

    auto empty = simdata::load_scenario(test::repo_path("data/scenarios/empty.toml"));
    REQUIRE(empty.has_value());
    CHECK(empty->soil.cec_mineral_eq_per_m2 > 0.0);
    CHECK(empty->soil.mn_oxide_g_per_m2 >= 0.0);
}

// ---- M4 (schema 5) ----

TEST_CASE("M4 params tables, arrays and seedling grades are read", "[unit][loader][m4]") {
    auto p = simdata::parse_params_toml(kValidParams, "p.toml");
    REQUIRE(p.has_value());
    CHECK(p->house.layer_resistance_m2K_per_W == 0.13);
    CHECK(p->house.cover_change_window_doy[0] == 91);
    CHECK(p->house.cover_change_window_doy[1] == 243);
    CHECK(p->house.min_cover_layers == 2);
    CHECK(p->house.max_cover_layers == 4);
    REQUIRE(p->house.outside_co2_ppm_by_hour.size() == 24);
    CHECK(p->house.outside_co2_ppm_by_hour[0] == 420.0);
    CHECK(p->house.outside_co2_ppm_by_hour[23] == 416.0);  // 정수로 쓴 항목
    CHECK(p->calendar.day_length_curve.points.size() == 3);
    CHECK(p->phenology.truss_degree_days_to_anthesis == 600.0);
    CHECK(p->fruit.rank_fraction.size() == 4);
    CHECK(p->fruit.rank_anthesis_delay_days[2] == 4.0);  // 정수로 쓴 항목
    CHECK(p->fruit.size_class_bounds_g.size() == 4);
    CHECK(p->fruit.brix_temp_score_curve(1.0) == 1.15);  // 4차: brix_temp_curve 삭제, 같은 자리 점검을 새 곡선으로
    CHECK(p->fruit.rank_abortion_weight.size() == 4);
    CHECK(p->denitrification.H_eq_per_eq_N == -1.0);
    CHECK(p->crop.vegetative_root_fraction == 0.25);
    CHECK(p->crop.cold_fruit_loss_curve(-4.0) == 0.2);
    REQUIRE(p->seedling_grades.size() == 2);
    CHECK(p->seedling_grades[0].id == "A");
    CHECK(p->seedling_grades[0].crown_g_per_plant == 3.0);  // 정수로 쓴 실수 키
    CHECK(p->seedling_grades[0].initial_induction_index == 0.8);
    CHECK(p->seedling_grades[1].id == "C");
}

TEST_CASE("M4 params reject missing, removed and malformed keys", "[unit][loader][m4]") {
    SECTION("removed M1 house keys are unknown now") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "[house]\n", "[house]\nbase_offset_degC = 2.0\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'house.base_offset_degC'"));
    }
    SECTION("removed leaf_allocation_fraction is unknown now") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "[crop]\n", "[crop]\nleaf_allocation_fraction = 0.7\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'crop.leaf_allocation_fraction'"));
    }
    SECTION("missing [[seedling_grade]] is rejected") {
        std::string text = replaced(kValidParams, "[[seedling_grade]]\nid = \"A\"", "[[seedling_gradee]]\nid = \"A\"");
        text = replaced(text, "[[seedling_grade]]\nid = \"C\"", "[[seedling_gradee]]\nid = \"C\"");
        auto p = simdata::parse_params_toml(text, "p.toml");
        REQUIRE_FALSE(p.has_value());
    }
    SECTION("missing key in a seedling grade entry is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "root_g_per_plant = 2.0\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'seedling_grade[0].root_g_per_plant'"));
    }
    SECTION("missing M4 table is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "[fruit]\n", "[fruity]\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'fruit'"));
    }
    SECTION("number array with a non-number element is rejected") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "rank_fraction = [0.07,", "rank_fraction = [\"x\","), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("fruit.rank_fraction[0]"));
    }
    SECTION("number array given as a scalar is rejected") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "size_class_bounds_g = [10.0, 15.0, 20.0, 25.0]", "size_class_bounds_g = 10.0"),
            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("fruit.size_class_bounds_g"));
    }
    SECTION("cover window must be two integers") {
        auto three = simdata::parse_params_toml(
            replaced(kValidParams, "cover_change_window_doy = [91, 243]", "cover_change_window_doy = [91, 243, 300]"),
            "p.toml");
        REQUIRE_FALSE(three.has_value());
        CHECK_THAT(three.error().message, ContainsSubstring("house.cover_change_window_doy"));
        auto floating = simdata::parse_params_toml(
            replaced(kValidParams, "cover_change_window_doy = [91, 243]", "cover_change_window_doy = [91.5, 243]"),
            "p.toml");
        CHECK_FALSE(floating.has_value());
    }
    SECTION("params schema 4 (M3) is rejected as an old version") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "schema_version = 7", "schema_version = 4"),
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK(p.error().code == sim::ErrorCode::UnsupportedVersion);
    }
}

TEST_CASE("M4 scenario cover_layers and crop init are read and required", "[unit][loader][m4]") {
    SECTION("cover_layers is read") {
        auto doc = simdata::parse_scenario_toml(kValidScenario, "s.toml");
        REQUIRE(doc.has_value());
        CHECK(doc->scenario.cover_layers == 3);
    }
    SECTION("missing cover_layers is rejected") {
        auto doc = simdata::parse_scenario_toml(replaced(kValidScenario, "cover_layers = 3\n", ""), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("missing required key 'house.cover_layers'"));
    }
    SECTION("old crop keys are rejected") {
        auto doc = simdata::parse_scenario_toml(
            replaced(kValidScenario, "[difficulty]", "[crop]\nleaf_g_per_m2 = 30.0\nroot_g_per_m2 = 10\n[difficulty]"),
            "s.toml");
        REQUIRE_FALSE(doc.has_value());
    }
}

TEST_CASE("M4 schedule actions set_window, set_heater and set_cover_layers parse", "[unit][loader][schedule][m4]") {
    SECTION("all three actions with repeats") {
        auto doc = simdata::parse_scenario_toml(scenario_with(R"(
[[schedule]]
tick = 6
action = "set_window"
layer = 1
side = "right"
open_fraction = 1.0
repeat_every_ticks = 24
repeat_count = 2
[[schedule]]
tick = 2
action = "set_heater"
on = true
setpoint_degC = 8
[[schedule]]
tick = 4
action = "set_cover_layers"
layers = 2
)"),
                                                "s.toml");
        REQUIRE(doc.has_value());
        REQUIRE(doc->schedule.size() == 4);
        const auto& heater = std::get<sim::SetHeaterCommand>(doc->schedule[0].command);
        CHECK(doc->schedule[0].tick == 2);
        CHECK(heater.on);
        CHECK(heater.setpoint_degC == 8.0);  // 정수로 쓴 실수 키
        const auto& cover = std::get<sim::SetCoverLayersCommand>(doc->schedule[1].command);
        CHECK(doc->schedule[1].tick == 4);
        CHECK(cover.layers == 2);
        const auto& window = std::get<sim::SetWindowCommand>(doc->schedule[2].command);
        CHECK(doc->schedule[2].tick == 6);
        CHECK(window.layer == 1);
        CHECK(window.side == sim::WindowSide::Right);
        CHECK(window.open_fraction == 1.0);
        CHECK(doc->schedule[3].tick == 30);
    }
    SECTION("left side and closing a window") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"set_window\"\nlayer = 0\nside = \"left\"\nopen_fraction = 0\n"),
            "s.toml");
        REQUIRE(doc.has_value());
        const auto& window = std::get<sim::SetWindowCommand>(doc->schedule[0].command);
        CHECK(window.side == sim::WindowSide::Left);
        CHECK(window.open_fraction == 0.0);
    }
    SECTION("bad side string is rejected") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"set_window\"\nlayer = 0\nside = \"north\"\nopen_fraction = 1.0\n"),
            "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("schedule[0].side"));
    }
    SECTION("area on set_window is an unknown key") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"set_window\"\nlayer = 0\nside = \"left\"\n"
                          "open_fraction = 1.0\narea = [0, 0, 1, 1]\n"),
            "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("unknown key 'schedule[0].area'"));
    }
    SECTION("area on set_heater and set_cover_layers is an unknown key") {
        CHECK_FALSE(simdata::parse_scenario_toml(
                        scenario_with("[[schedule]]\ntick = 0\naction = \"set_heater\"\non = true\n"
                                      "setpoint_degC = 5.0\narea = [0, 0, 1, 1]\n"),
                        "s.toml")
                        .has_value());
        CHECK_FALSE(simdata::parse_scenario_toml(
                        scenario_with("[[schedule]]\ntick = 0\naction = \"set_cover_layers\"\nlayers = 3\n"
                                      "area = [0, 0, 1, 1]\n"),
                        "s.toml")
                        .has_value());
    }
    SECTION("missing or mistyped keys are rejected") {
        auto missing = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"set_heater\"\non = true\n"), "s.toml");
        REQUIRE_FALSE(missing.has_value());
        CHECK_THAT(missing.error().message, ContainsSubstring("missing required key 'schedule[0].setpoint_degC'"));
        auto not_bool = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"set_heater\"\non = 1\nsetpoint_degC = 5.0\n"),
            "s.toml");
        CHECK_FALSE(not_bool.has_value());
        auto layers_float = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"set_cover_layers\"\nlayers = 2.5\n"), "s.toml");
        CHECK_FALSE(layers_float.has_value());
    }
}

TEST_CASE("Repository params and scenarios carry the M4 fields", "[unit][loader][data][m4]") {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    CHECK(params->house.outside_co2_ppm_by_hour.size() == 24);
    CHECK(params->seedling_grades.size() >= 3);
    CHECK(params->fruit.rank_fraction.size() == params->fruit.rank_potential_fresh_g.size());

    auto s13 = simdata::load_scenario(test::repo_path("data/scenarios/m4/s13_proper.toml"));
    REQUIRE(s13.has_value());
    CHECK(s13->cover_layers == 3);
    REQUIRE(s13->crop.has_value());
    CHECK(s13->crop->seedling_grade == "B");
    CHECK(s13->crop->plants_per_m2 == 8.0);
}

// ---- M5a (schema 6): 미생물·소독·멀칭·유익균 ----

TEST_CASE("M5a params tables are read", "[unit][loader][m5a]") {
    auto p = simdata::parse_params_toml(kValidParams, "p.toml");
    REQUIRE(p.has_value());
    CHECK(p->schema_version == 7);
    CHECK(p->microbes.capacity_floor_index == 0.01);
    CHECK(p->microbes.moist_heat_curve.points.size() == 2);
    const auto& fus = p->microbes.groups[sim::microbe(sim::MicrobeGroup::Fusarium)];
    CHECK(fus.competition[sim::microbe(sim::MicrobeGroup::Antagonist)] == 0.8);
    CHECK(fus.competition[sim::microbe(sim::MicrobeGroup::Fusarium)] == 0.0);
    CHECK(fus.saprophytic_capacity_index == 0.3);
    CHECK(fus.host_capacity_index == 0.7);
    const auto& ant = p->microbes.groups[sim::microbe(sim::MicrobeGroup::Antagonist)];
    CHECK(ant.competition[sim::microbe(sim::MicrobeGroup::Fusarium)] == 0.3);
    CHECK(ant.competition[sim::microbe(sim::MicrobeGroup::Phytophthora)] == 0.3);
    CHECK(ant.saprophytic_capacity_index == 0.0);  // 병원균이 아니라서 읽지 않아요
    const auto& dec = p->microbes.groups[sim::microbe(sim::MicrobeGroup::Decomposer)];
    CHECK(dec.competition == sim::MicrobeIndices{});
    CHECK(p->soil_disease[static_cast<std::size_t>(sim::SoilDisease::Phytophthora)].onset_infection_index == 0.1);
    CHECK(p->disinfection.till_residue_release_fraction == 0.5);
    CHECK(p->root_damage.fumigant_curve.points.size() == 2);
    REQUIRE(p->mulches.size() == 2);
    CHECK(p->mulches[1].id == "white_film");
    CHECK(p->mulches[1].soil_solar_gain_degC_per_W_per_m2 == -0.004);
    REQUIRE(p->disinfectants.size() == 1);
    CHECK(p->disinfectants[0].kill_rate_per_hour[sim::microbe(sim::MicrobeGroup::NitrifierNob)] == 0.001);
    REQUIRE(p->inoculants.size() == 1);
    CHECK(p->inoculants[0].index_gain_per_g_per_m2[sim::microbe(sim::MicrobeGroup::Antagonist)] == 0.02);
    CHECK(p->inoculants[0].index_gain_per_g_per_m2[sim::microbe(sim::MicrobeGroup::Mycorrhiza)] == 0.0);
    CHECK(p->inoculants[0].C_g_per_kg == 300.0);
}

TEST_CASE("M5a params reject missing, unknown and malformed keys", "[unit][loader][m5a]") {
    SECTION("missing microbes table") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "[microbes]\n", "[microbez]\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
    }
    SECTION("missing group") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "[microbes.mycorrhiza]", "[microbes.myco]"),
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
    }
    SECTION("missing curve key (no hidden default)") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "ph_curve = [[0, 1], [14, 1]]\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("ph_curve"));
    }
    SECTION("unknown id in competition") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "{ fusarium = 0.3, phytophthora = 0.3 }", "{ fusarium = 0.3, rhizoctonia = 0.3 }"),
            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("rhizoctonia"));
    }
    SECTION("competition must not list the group itself") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "{ fusarium = 0.3, phytophthora = 0.3 }", "{ antagonist = 0.3 }"), "p.toml");
        REQUIRE_FALSE(p.has_value());
    }
    SECTION("capacity keys are rejected on non-pathogen groups") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "[microbes.mycorrhiza]\n", "[microbes.mycorrhiza]\nhost_capacity_index = 0.5\n"),
            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("host_capacity_index"));
    }
    SECTION("capacity keys are required on pathogens") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "host_capacity_index = 0.7\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("host_capacity_index"));
    }
    SECTION("unknown common microbes key") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "[microbes]\n", "[microbes]\nspore_factor = 1\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
    }
    SECTION("disinfectant kill_rate needs all seven ids") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, ", phytophthora = 0.015 }", " }"), "p.toml");
        REQUIRE_FALSE(p.has_value());
    }
    SECTION("disinfectant kill_rate rejects unknown ids") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "phytophthora = 0.015 }", "phytophthora = 0.015, rhizoctonia = 0.1 }"), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("rhizoctonia"));
    }
    SECTION("inoculant gain may be partial but not unknown") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "{ antagonist = 0.02 }", "{ antagonist = 0.02, mycorrhiza = 0.01 }"), "p.toml");
        REQUIRE(p.has_value());
        CHECK(p->inoculants[0].index_gain_per_g_per_m2[sim::microbe(sim::MicrobeGroup::Mycorrhiza)] == 0.01);
        auto bad = simdata::parse_params_toml(
            replaced(kValidParams, "{ antagonist = 0.02 }", "{ yeast = 0.02 }"), "p.toml");
        REQUIRE_FALSE(bad.has_value());
    }
    SECTION("missing mulch, disinfectant and inoculant arrays") {
        for (const char* name : {"[[mulch]]", "[[disinfectant]]", "[[inoculant]]"}) {
            std::string text(kValidParams);
            // 배열 이름만 틀리게 바꾸면 필수 배열이 없어요
            std::string bad = text;
            std::size_t pos = 0;
            const std::string from(name);
            const std::string to = from.substr(0, from.size() - 2) + "x]]";
            while ((pos = bad.find(from, pos)) != std::string::npos) {
                bad.replace(pos, from.size(), to);
                pos += to.size();
            }
            REQUIRE_FALSE(simdata::parse_params_toml(bad, "p.toml").has_value());
        }
    }
    SECTION("root_damage needs the M5a curves") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "fumigant_curve = [[0, 0], [1, 0.2]]\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("fumigant_curve"));
    }
    SECTION("schema 5 params are rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "schema_version = 7", "schema_version = 5"),
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK(p.error().code == sim::ErrorCode::UnsupportedVersion);
    }
}

TEST_CASE("M5a scenario microbes section is read and required", "[unit][loader][m5a]") {
    auto doc = simdata::parse_scenario_toml(kValidScenario, "s.toml");
    REQUIRE(doc.has_value());
    const auto& mi = doc->scenario.soil.microbe_index;
    CHECK(mi[sim::microbe(sim::MicrobeGroup::Decomposer)] == 1.0);
    CHECK(mi[sim::microbe(sim::MicrobeGroup::NitrifierNob)] == 0.9);
    CHECK(mi[sim::microbe(sim::MicrobeGroup::Antagonist)] == 0.5);
    CHECK(mi[sim::microbe(sim::MicrobeGroup::Phytophthora)] == 0.25);

    SECTION("missing section") {
        auto bad = simdata::parse_scenario_toml(replaced(kValidScenario, "[microbes]", "[microbez]"), "s.toml");
        REQUIRE_FALSE(bad.has_value());
    }
    SECTION("missing id") {
        auto bad = simdata::parse_scenario_toml(replaced(kValidScenario, "fusarium = 0.0\n", ""), "s.toml");
        REQUIRE_FALSE(bad.has_value());
        CHECK_THAT(bad.error().message, ContainsSubstring("fusarium"));
    }
    SECTION("unknown id") {
        auto bad = simdata::parse_scenario_toml(replaced(kValidScenario, "fusarium = 0.0\n", "fusarium = 0.0\nanthracnose = 0.1\n"),
                                                "s.toml");
        REQUIRE_FALSE(bad.has_value());
    }
    SECTION("schema 5 scenarios are rejected") {
        auto bad = simdata::parse_scenario_toml(replaced(kValidScenario, "schema_version = 7", "schema_version = 5"),
                                                "s.toml");
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().code == sim::ErrorCode::UnsupportedVersion);
    }
}

TEST_CASE("M5a schedule actions set_mulch, apply_disinfectant, add_inoculant and plant parse", "[unit][loader][schedule][m5a]") {
    auto doc = simdata::parse_scenario_toml(scenario_with(R"(
[[schedule]]
tick = 1
action = "set_mulch"
mulch = "clear_film"
[[schedule]]
tick = 2
action = "set_mulch"
area = [0, 0, 2, 3]
mulch = "none"
[[schedule]]
tick = 3
action = "apply_disinfectant"
disinfectant = "dazomet"
g_per_m2 = 30
[[schedule]]
tick = 4
action = "add_inoculant"
inoculant = "bacillus_product"
g_per_m2 = 20.5
repeat_every_ticks = 24
repeat_count = 2
[[schedule]]
tick = 5
action = "plant"
seedling_grade = "B"
plants_per_m2 = 8
)"), "s.toml");
    REQUIRE(doc.has_value());
    REQUIRE(doc->schedule.size() == 6);
    const auto& mulch = std::get<sim::SetMulchCommand>(doc->schedule[0].command);
    CHECK(mulch.mulch_id == "clear_film");
    CHECK(mulch.area == sim::whole_grid(2, 5));
    const auto& partial = std::get<sim::SetMulchCommand>(doc->schedule[1].command);
    CHECK(partial.mulch_id == "none");
    CHECK(partial.area.cols == 3);
    const auto& fumigant = std::get<sim::ApplyDisinfectantCommand>(doc->schedule[2].command);
    CHECK(fumigant.disinfectant_id == "dazomet");
    CHECK(fumigant.g_per_m2 == 30.0);
    const auto& inoc = std::get<sim::AddInoculantCommand>(doc->schedule[3].command);
    CHECK(inoc.inoculant_id == "bacillus_product");
    CHECK(inoc.g_per_m2 == 20.5);
    CHECK(doc->schedule[4].tick == 5);  // 정렬: 4, 5(plant), 28(반복)
    const auto& plant = std::get<sim::PlantCommand>(doc->schedule[4].command);
    CHECK(plant.seedling_grade == "B");
    CHECK(plant.plants_per_m2 == 8.0);
    CHECK(doc->schedule[5].tick == 28);

    SECTION("plant is a whole-house command and rejects area") {
        auto bad = simdata::parse_scenario_toml(scenario_with(R"(
[[schedule]]
tick = 0
action = "plant"
area = [0, 0, 1, 1]
seedling_grade = "B"
plants_per_m2 = 8
)"), "s.toml");
        REQUIRE_FALSE(bad.has_value());
    }
    SECTION("missing fields are rejected") {
        CHECK_FALSE(simdata::parse_scenario_toml(scenario_with("[[schedule]]\ntick = 0\naction = \"set_mulch\"\n"),
                                                 "s.toml").has_value());
        CHECK_FALSE(simdata::parse_scenario_toml(
                        scenario_with("[[schedule]]\ntick = 0\naction = \"apply_disinfectant\"\ndisinfectant = \"dazomet\"\n"),
                        "s.toml").has_value());
        CHECK_FALSE(simdata::parse_scenario_toml(
                        scenario_with("[[schedule]]\ntick = 0\naction = \"plant\"\nseedling_grade = \"B\"\n"),
                        "s.toml").has_value());
    }
}

TEST_CASE("Repository params and M5a scenarios load", "[unit][loader][data][m5a]") {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    CHECK(params->schema_version == 7);
    CHECK(params->mulches.size() == 3);
    CHECK(params->disinfectants.size() == 2);
    CHECK(params->inoculants.size() == 3);
    CHECK(params->disinfectants[0].residue_index_per_g_per_m2 == 1.0 / 30.0);
    CHECK(params->disinfectants[1].residue_index_per_g_per_m2 == 1.0 / 4.0);

    // 모든 시나리오(M1~M5a)가 schema 6으로 읽히고, 세계를 만들 수 있어요.
    const auto root = test::repo_path("data/scenarios");
    std::size_t loaded = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.path().extension() != ".toml") {
            continue;
        }
        auto bundle = simdata::load_scenario_bundle(entry.path());
        INFO(entry.path().string());
        REQUIRE(bundle.has_value());
        if (!bundle.has_value()) {
            continue;
        }
        auto world = sim::World::create(*params, std::move(bundle->scenario));
        REQUIRE(world.has_value());
        ++loaded;
    }
    CHECK(loaded >= 70);

    auto s19 = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m5a/s19_solar_4w.toml"));
    REQUIRE(s19.has_value());
    CHECK(s19->scenario.seed == 20261119);
    CHECK(s19->scenario.soil.microbe_index[sim::microbe(sim::MicrobeGroup::Fusarium)] == 0.5);
    CHECK_FALSE(s19->scenario.crop.has_value());
    bool has_mulch = false;
    for (const auto& c : s19->schedule) {
        has_mulch = has_mulch || std::holds_alternative<sim::SetMulchCommand>(c.command);
    }
    CHECK(has_mulch);
}
