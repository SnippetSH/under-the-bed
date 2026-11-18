#include "validation.hpp"

#include <cmath>
#include <format>
#include <set>
#include <string>
#include <utility>

#include "sim/calendar.hpp"
#include "sim/curve.hpp"

namespace sim::validation {

namespace {

std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

// validate_curve(점 2개 이상, x 엄격 오름차순, y ≥ 0) 다음에 쓰는 y 범위 검사
std::expected<void, Error> curve_y_at_most_one(const PiecewiseLinear& curve, std::string_view name) {
    for (const auto& point : curve.points) {
        if (!(point[1] <= 1.0)) {
            return fail(ErrorCode::InvalidArgument, std::format("curve '{}' y values must be within 0..1", name));
        }
    }
    return {};
}

std::expected<void, Error> curve_y_positive(const PiecewiseLinear& curve, std::string_view name) {
    for (const auto& point : curve.points) {
        if (!(point[1] > 0.0)) {
            return fail(ErrorCode::InvalidArgument, std::format("curve '{}' y values must be > 0", name));
        }
    }
    return {};
}

// 곡선 y가 0~1 계수·비율인 경우
std::expected<void, Error> factor_curve(const PiecewiseLinear& curve, std::string_view name) {
    SIM_CHECK(validate_curve(curve, name));
    return curve_y_at_most_one(curve, name);
}

constexpr std::int64_t kMaxCoverLayers = 64;  // 창 상태 배열 크기 상한 (검증용 안전장치)

}  // namespace

std::expected<void, Error> require(bool ok, std::string_view what) {
    if (!ok) {
        return fail(ErrorCode::InvalidArgument, std::string(what));
    }
    return {};
}

std::expected<void, Error> non_negative(double v, std::string_view name) {
    return require(std::isfinite(v) && v >= 0.0, std::format("{} must be finite and >= 0", name));
}

std::expected<void, Error> fraction(double v, std::string_view name) {
    return require(std::isfinite(v) && v >= 0.0 && v <= 1.0, std::format("{} must be within 0..1", name));
}

std::expected<void, Error> positive(double v, std::string_view name) {
    return require(std::isfinite(v) && v > 0.0, std::format("{} must be finite and > 0", name));
}

std::expected<void, Error> finite(double v, std::string_view name) {
    return require(std::isfinite(v), std::format("{} must be finite", name));
}

std::expected<void, Error> validate_params(const Params& p) {
    if (p.schema_version != kParamsSchemaVersion) {
        return fail(ErrorCode::UnsupportedVersion,
                    std::format("params schema_version {} is not supported (expected {})", p.schema_version,
                                kParamsSchemaVersion));
    }

    SIM_CHECK(non_negative(p.soil_water.evap_mm_per_hour_per_W_per_m2, "soil_water.evap_mm_per_hour_per_W_per_m2"));
    SIM_CHECK(non_negative(p.soil_water.evap_base_mm_per_hour, "soil_water.evap_base_mm_per_hour"));
    SIM_CHECK(validate_curve(p.soil_water.evap_moisture_curve, "soil_water.evap_moisture_curve"));
    SIM_CHECK(non_negative(p.soil_water.drainage_rate_per_hour, "soil_water.drainage_rate_per_hour"));

    SIM_CHECK(non_negative(p.soil_temperature.relax_rate_per_hour, "soil_temperature.relax_rate_per_hour"));
    SIM_CHECK(non_negative(p.soil_temperature.heat_degC_per_g_C, "soil_temperature.heat_degC_per_g_C"));

    const OrganicMatterParams& om = p.organic_matter;
    SIM_CHECK(non_negative(om.labile_decay_rate_per_hour, "organic_matter.labile_decay_rate_per_hour"));
    SIM_CHECK(non_negative(om.stable_decay_rate_per_hour, "organic_matter.stable_decay_rate_per_hour"));
    SIM_CHECK(fraction(om.humification_fraction, "organic_matter.humification_fraction"));
    SIM_CHECK(positive(om.humus_C_to_N_ratio, "organic_matter.humus_C_to_N_ratio"));
    SIM_CHECK(validate_curve(om.temp_curve, "organic_matter.temp_curve"));
    SIM_CHECK(validate_curve(om.moisture_curve, "organic_matter.moisture_curve"));
    SIM_CHECK(validate_curve(om.o2_curve, "organic_matter.o2_curve"));
    SIM_CHECK(validate_curve(p.soil_water.drainage_compaction_curve, "soil_water.drainage_compaction_curve"));

    const NitrificationParams& nit = p.nitrification;
    SIM_CHECK(non_negative(nit.step1_rate_per_hour, "nitrification.step1_rate_per_hour"));
    SIM_CHECK(non_negative(nit.step2_rate_per_hour, "nitrification.step2_rate_per_hour"));
    SIM_CHECK(validate_curve(nit.temp_curve, "nitrification.temp_curve"));
    SIM_CHECK(validate_curve(nit.moisture_curve, "nitrification.moisture_curve"));
    SIM_CHECK(validate_curve(nit.step1_ph_curve, "nitrification.step1_ph_curve"));
    SIM_CHECK(validate_curve(nit.step2_ph_curve, "nitrification.step2_ph_curve"));
    SIM_CHECK(validate_curve(nit.ammonia_fraction_ph_curve, "nitrification.ammonia_fraction_ph_curve"));
    SIM_CHECK(validate_curve(nit.free_ammonia_inhibition_curve, "nitrification.free_ammonia_inhibition_curve"));
    SIM_CHECK(positive(nit.nitrite_toxicity_threshold_g_N_per_m2, "nitrification.nitrite_toxicity_threshold_g_N_per_m2"));
    SIM_CHECK(fraction(nit.nitrite_alarm_reset_fraction, "nitrification.nitrite_alarm_reset_fraction"));
    SIM_CHECK(validate_curve(nit.step1_o2_curve, "nitrification.step1_o2_curve"));
    SIM_CHECK(validate_curve(nit.step2_o2_curve, "nitrification.step2_o2_curve"));
    SIM_CHECK(validate_curve(nit.nitrite_toxicity_ph_curve, "nitrification.nitrite_toxicity_ph_curve"));

    // M3: cation-exchange, phosphorus, manganese
    const CationExchangeParams& ce = p.cation_exchange;
    SIM_CHECK(non_negative(ce.cec_organic_eq_per_g_C, "cation_exchange.cec_organic_eq_per_g_C"));
    SIM_CHECK(non_negative(ce.exchange_rate_per_hour, "cation_exchange.exchange_rate_per_hour"));
    SIM_CHECK(positive(ce.nh4_retention_factor, "cation_exchange.nh4_retention_factor"));
    SIM_CHECK(positive(ce.k_retention_factor, "cation_exchange.k_retention_factor"));
    SIM_CHECK(positive(ce.ca_retention_factor, "cation_exchange.ca_retention_factor"));
    SIM_CHECK(positive(ce.mg_retention_factor, "cation_exchange.mg_retention_factor"));
    SIM_CHECK(require(ce.solver_iterations >= 1 && ce.solver_iterations <= 200,
                      "cation_exchange.solver_iterations must be 1..200"));
    SIM_CHECK(non_negative(ce.buffer_eq_per_pH_per_eq_cec, "cation_exchange.buffer_eq_per_pH_per_eq_cec"));
    // 길항 배율은 흡수 능력·수요 상한을 줄이기만 해요 (0~1)
    SIM_CHECK(validate_curve(p.antagonism.ca_curve, "antagonism.ca_curve"));
    SIM_CHECK(curve_y_at_most_one(p.antagonism.ca_curve, "antagonism.ca_curve"));
    SIM_CHECK(validate_curve(p.antagonism.mg_curve, "antagonism.mg_curve"));
    SIM_CHECK(curve_y_at_most_one(p.antagonism.mg_curve, "antagonism.mg_curve"));
    SIM_CHECK(fraction(p.antagonism.min_demand_factor, "antagonism.min_demand_factor"));
    SIM_CHECK(non_negative(p.phosphorus.fixation_rate_per_hour, "phosphorus.fixation_rate_per_hour"));
    SIM_CHECK(validate_curve(p.phosphorus.fixation_ph_curve, "phosphorus.fixation_ph_curve"));
    SIM_CHECK(non_negative(p.phosphorus.release_rate_per_hour, "phosphorus.release_rate_per_hour"));
    const ManganeseParams& mn = p.manganese;
    // w_Mn는 다른 양이온 세기(> 0)와 같은 뜻이라 > 0
    SIM_CHECK(validate_curve(mn.retention_ph_curve, "manganese.retention_ph_curve"));
    SIM_CHECK(curve_y_positive(mn.retention_ph_curve, "manganese.retention_ph_curve"));
    SIM_CHECK(non_negative(mn.oxidation_rate_per_hour, "manganese.oxidation_rate_per_hour"));
    SIM_CHECK(validate_curve(mn.oxidation_ph_curve, "manganese.oxidation_ph_curve"));
    SIM_CHECK(validate_curve(mn.oxidation_o2_curve, "manganese.oxidation_o2_curve"));
    SIM_CHECK(non_negative(mn.reduction_rate_per_hour, "manganese.reduction_rate_per_hour"));
    SIM_CHECK(validate_curve(mn.reduction_ph_curve, "manganese.reduction_ph_curve"));
    SIM_CHECK(validate_curve(mn.reduction_o2_curve, "manganese.reduction_o2_curve"));
    // 작물 망간 (manganese.md)
    SIM_CHECK(positive(p.crop.target_Mn_g_per_g, "crop.target_Mn_g_per_g"));
    SIM_CHECK(non_negative(p.crop.mn_uptake_rate_per_g_root_per_hour, "crop.mn_uptake_rate_per_g_root_per_hour"));
    SIM_CHECK(require(std::isfinite(p.crop.mn_max_uptake_ratio) && p.crop.mn_max_uptake_ratio >= 1.0,
                      "crop.mn_max_uptake_ratio must be finite and >= 1"));
    SIM_CHECK(validate_curve(p.crop.mn_toxicity_curve, "crop.mn_toxicity_curve"));
    SIM_CHECK(curve_y_at_most_one(p.crop.mn_toxicity_curve, "crop.mn_toxicity_curve"));  // y = 생장 계수

    const AerationParams& aer = p.aeration;
    SIM_CHECK(validate_curve(aer.compaction_cap_curve, "aeration.compaction_cap_curve"));
    SIM_CHECK(non_negative(aer.organic_C_cap_bonus_per_g_C, "aeration.organic_C_cap_bonus_per_g_C"));
    SIM_CHECK(non_negative(aer.compaction_per_mm_irrigation, "aeration.compaction_per_mm_irrigation"));
    SIM_CHECK(fraction(aer.tilled_compaction_index, "aeration.tilled_compaction_index"));
    SIM_CHECK(fraction(aer.till_root_damage_fraction, "aeration.till_root_damage_fraction"));

    const IonParams& ion = p.ions;
    SIM_CHECK(positive(ion.nh4_N_g_per_eq, "ions.nh4_N_g_per_eq"));
    SIM_CHECK(positive(ion.no3_N_g_per_eq, "ions.no3_N_g_per_eq"));
    SIM_CHECK(positive(ion.no2_N_g_per_eq, "ions.no2_N_g_per_eq"));
    SIM_CHECK(positive(ion.k_g_per_eq, "ions.k_g_per_eq"));
    SIM_CHECK(positive(ion.ca_g_per_eq, "ions.ca_g_per_eq"));
    SIM_CHECK(positive(ion.mg_g_per_eq, "ions.mg_g_per_eq"));
    SIM_CHECK(positive(ion.p_g_per_eq, "ions.p_g_per_eq"));
    SIM_CHECK(positive(ion.s_g_per_eq, "ions.s_g_per_eq"));
    SIM_CHECK(positive(ion.mn_g_per_eq, "ions.mn_g_per_eq"));

    const SoilChemistryParams& chem = p.soil_chemistry;
    SIM_CHECK(fraction(chem.p_solution_fraction, "soil_chemistry.p_solution_fraction"));
    SIM_CHECK(fraction(chem.s_solution_fraction, "soil_chemistry.s_solution_fraction"));
    SIM_CHECK(finite(chem.nitrification_H_eq_per_eq_N, "soil_chemistry.nitrification_H_eq_per_eq_N"));
    SIM_CHECK(finite(chem.uptake_H_eq_per_eq_NH4, "soil_chemistry.uptake_H_eq_per_eq_NH4"));
    SIM_CHECK(finite(chem.uptake_H_eq_per_eq_NO3, "soil_chemistry.uptake_H_eq_per_eq_NO3"));
    SIM_CHECK(non_negative(chem.organic_C_buffer_eq_per_g_C_per_pH, "soil_chemistry.organic_C_buffer_eq_per_g_C_per_pH"));
    SIM_CHECK(require(std::isfinite(chem.ph_min) && std::isfinite(chem.ph_max) && chem.ph_min >= 0.0 &&
                          chem.ph_min < chem.ph_max && chem.ph_max <= 14.0,
                      "soil_chemistry ph_min/ph_max must satisfy 0 <= ph_min < ph_max <= 14"));
    SIM_CHECK(non_negative(chem.lime_dissolution_rate_per_hour, "soil_chemistry.lime_dissolution_rate_per_hour"));
    SIM_CHECK(validate_curve(chem.lime_dissolution_ph_curve, "soil_chemistry.lime_dissolution_ph_curve"));
    SIM_CHECK(validate_curve(chem.lime_dissolution_moisture_curve, "soil_chemistry.lime_dissolution_moisture_curve"));
    SIM_CHECK(positive(chem.ec_meq_per_L_per_dS_per_m, "soil_chemistry.ec_meq_per_L_per_dS_per_m"));
    SIM_CHECK(positive(chem.ec_min_water_mm, "soil_chemistry.ec_min_water_mm"));

    const AvailabilityParams& av = p.availability;
    SIM_CHECK(validate_curve(av.n_ph_curve, "availability.n_ph_curve"));
    SIM_CHECK(validate_curve(av.p_ph_curve, "availability.p_ph_curve"));
    SIM_CHECK(validate_curve(av.k_ph_curve, "availability.k_ph_curve"));
    SIM_CHECK(validate_curve(av.ca_ph_curve, "availability.ca_ph_curve"));
    SIM_CHECK(validate_curve(av.mg_ph_curve, "availability.mg_ph_curve"));

    const RootDamageParams& rd = p.root_damage;
    SIM_CHECK(validate_curve(rd.waterlog_curve, "root_damage.waterlog_curve"));
    SIM_CHECK(validate_curve(rd.heat_curve, "root_damage.heat_curve"));
    SIM_CHECK(validate_curve(rd.nitrite_curve, "root_damage.nitrite_curve"));
    SIM_CHECK(validate_curve(rd.acid_curve, "root_damage.acid_curve"));

    const CropParams& crop = p.crop;
    SIM_CHECK(positive(crop.specific_leaf_area_m2_per_g, "crop.specific_leaf_area_m2_per_g"));
    SIM_CHECK(validate_curve(crop.light_interception_curve, "crop.light_interception_curve"));
    SIM_CHECK(fraction(crop.par_fraction, "crop.par_fraction"));
    SIM_CHECK(positive(crop.light_use_efficiency_g_per_MJ, "crop.light_use_efficiency_g_per_MJ"));
    SIM_CHECK(validate_curve(crop.temp_curve, "crop.temp_curve"));
    SIM_CHECK(validate_curve(crop.water_stress_curve, "crop.water_stress_curve"));
    SIM_CHECK(validate_curve(crop.salt_stress_curve, "crop.salt_stress_curve"));
    SIM_CHECK(validate_curve(crop.nutrient_stress_curve, "crop.nutrient_stress_curve"));
    SIM_CHECK(positive(crop.target_N_g_per_g, "crop.target_N_g_per_g"));
    SIM_CHECK(positive(crop.target_P_g_per_g, "crop.target_P_g_per_g"));
    SIM_CHECK(positive(crop.target_K_g_per_g, "crop.target_K_g_per_g"));
    SIM_CHECK(positive(crop.target_Ca_g_per_g, "crop.target_Ca_g_per_g"));
    SIM_CHECK(positive(crop.target_Mg_g_per_g, "crop.target_Mg_g_per_g"));
    SIM_CHECK(require(std::isfinite(crop.luxury_uptake_ratio) && crop.luxury_uptake_ratio >= 1.0,
                      "crop.luxury_uptake_ratio must be finite and >= 1"));
    SIM_CHECK(non_negative(crop.uptake_rate_per_g_root_per_hour, "crop.uptake_rate_per_g_root_per_hour"));
    SIM_CHECK(non_negative(crop.ca_uptake_rate_per_g_root_per_hour, "crop.ca_uptake_rate_per_g_root_per_hour"));
    SIM_CHECK(non_negative(crop.mg_uptake_rate_per_g_root_per_hour, "crop.mg_uptake_rate_per_g_root_per_hour"));
    SIM_CHECK(non_negative(crop.transpiration_mm_per_hour_per_W_per_m2, "crop.transpiration_mm_per_hour_per_W_per_m2"));
    SIM_CHECK(non_negative(crop.root_recovery_rate_per_hour, "crop.root_recovery_rate_per_hour"));
    SIM_CHECK(fraction(crop.death_root_health, "crop.death_root_health"));
    SIM_CHECK(fraction(crop.root_dieback_per_damage, "crop.root_dieback_per_damage"));
    SIM_CHECK(fraction(crop.dry_matter_C_fraction, "crop.dry_matter_C_fraction"));

    // ---- M4 ----
    const HouseParams& hs = p.house;
    SIM_CHECK(fraction(hs.layer_transmittance_factor, "house.layer_transmittance_factor"));
    SIM_CHECK(positive(hs.layer_resistance_m2K_per_W, "house.layer_resistance_m2K_per_W"));
    SIM_CHECK(non_negative(hs.infiltration_W_per_m2_per_K, "house.infiltration_W_per_m2_per_K"));
    SIM_CHECK(fraction(hs.window_bypass_fraction, "house.window_bypass_fraction"));
    SIM_CHECK(non_negative(hs.vent_conductance_W_per_m2_per_K, "house.vent_conductance_W_per_m2_per_K"));
    SIM_CHECK(fraction(hs.solar_heat_gain_fraction, "house.solar_heat_gain_fraction"));
    SIM_CHECK(non_negative(hs.ground_heat_W_per_m2, "house.ground_heat_W_per_m2"));
    SIM_CHECK(fraction(hs.air_relax_rate_per_hour, "house.air_relax_rate_per_hour"));
    SIM_CHECK(non_negative(hs.heater_capacity_W_per_m2, "house.heater_capacity_W_per_m2"));
    SIM_CHECK(finite(hs.heater_setpoint_min_degC, "house.heater_setpoint_min_degC"));
    SIM_CHECK(finite(hs.heater_setpoint_max_degC, "house.heater_setpoint_max_degC"));
    SIM_CHECK(require(hs.heater_setpoint_min_degC <= hs.heater_setpoint_max_degC,
                      "house.heater_setpoint_min_degC must be <= heater_setpoint_max_degC"));
    SIM_CHECK(fraction(hs.temp_mean_relax_rate_per_hour, "house.temp_mean_relax_rate_per_hour"));
    SIM_CHECK(require(hs.cover_change_window_doy[0] >= 1 && hs.cover_change_window_doy[1] <= kDaysPerYear &&
                          hs.cover_change_window_doy[0] <= hs.cover_change_window_doy[1],
                      "house.cover_change_window_doy must satisfy 1 <= start <= end <= 365"));
    SIM_CHECK(require(hs.min_cover_layers >= 1 && hs.min_cover_layers <= hs.max_cover_layers &&
                          hs.max_cover_layers <= kMaxCoverLayers,
                      "house cover layers must satisfy 1 <= min_cover_layers <= max_cover_layers <= 64"));
    SIM_CHECK(require(hs.outside_co2_ppm_by_hour.size() == static_cast<std::size_t>(kHoursPerDay),
                      "house.outside_co2_ppm_by_hour must have exactly 24 entries"));
    SIM_CHECK(require(!hs.window_open_levels.empty(), "house.window_open_levels must not be empty"));
    for (std::size_t i = 0; i < hs.window_open_levels.size(); ++i) {
        SIM_CHECK(fraction(hs.window_open_levels[i], "house.window_open_levels entry"));
        SIM_CHECK(require(i == 0 || hs.window_open_levels[i] > hs.window_open_levels[i - 1],
                          "house.window_open_levels must be strictly ascending"));
    }
    SIM_CHECK(require(hs.window_open_levels.front() == 0.0, "house.window_open_levels must start with 0 (closed)"));
    for (const double ppm : hs.outside_co2_ppm_by_hour) {
        SIM_CHECK(positive(ppm, "house.outside_co2_ppm_by_hour entry"));
    }
    SIM_CHECK(positive(hs.air_volume_m3_per_m2, "house.air_volume_m3_per_m2"));
    SIM_CHECK(positive(hs.co2_mg_per_m3_per_ppm, "house.co2_mg_per_m3_per_ppm"));
    SIM_CHECK(non_negative(hs.infiltration_air_changes_per_hour, "house.infiltration_air_changes_per_hour"));
    SIM_CHECK(non_negative(hs.vent_air_changes_per_hour, "house.vent_air_changes_per_hour"));
    SIM_CHECK(positive(hs.co2_floor_ppm, "house.co2_floor_ppm"));

    SIM_CHECK(validate_curve(p.calendar.day_length_curve, "calendar.day_length_curve"));
    for (const auto& point : p.calendar.day_length_curve.points) {
        SIM_CHECK(require(point[1] > 0.0 && point[1] <= 24.0, "calendar.day_length_curve y must be within (0, 24]"));
    }

    const PhenologyParams& ph = p.phenology;
    SIM_CHECK(non_negative(ph.establishment_rate_per_hour, "phenology.establishment_rate_per_hour"));
    SIM_CHECK(factor_curve(ph.establishment_temp_curve, "phenology.establishment_temp_curve"));
    SIM_CHECK(factor_curve(ph.establishment_growth_curve, "phenology.establishment_growth_curve"));
    SIM_CHECK(non_negative(ph.induction_rate_per_hour, "phenology.induction_rate_per_hour"));
    SIM_CHECK(factor_curve(ph.induction_day_length_curve, "phenology.induction_day_length_curve"));
    SIM_CHECK(factor_curve(ph.induction_temp_curve, "phenology.induction_temp_curve"));
    SIM_CHECK(factor_curve(ph.induction_nitrogen_curve, "phenology.induction_nitrogen_curve"));
    SIM_CHECK(finite(ph.truss_base_temp_degC, "phenology.truss_base_temp_degC"));
    SIM_CHECK(positive(ph.truss_degree_days_to_anthesis, "phenology.truss_degree_days_to_anthesis"));
    SIM_CHECK(validate_curve(ph.flowers_per_truss_curve, "phenology.flowers_per_truss_curve"));

    std::set<std::string, std::less<>> grade_ids;
    for (const SeedlingGradeDef& g : p.seedling_grades) {
        SIM_CHECK(require(!g.id.empty() && grade_ids.insert(g.id).second,
                          std::format("seedling grade id '{}' must be non-empty and unique", g.id)));
        SIM_CHECK(non_negative(g.leaf_g_per_plant, std::format("seedling grade '{}' leaf_g_per_plant", g.id)));
        SIM_CHECK(non_negative(g.crown_g_per_plant, std::format("seedling grade '{}' crown_g_per_plant", g.id)));
        SIM_CHECK(non_negative(g.root_g_per_plant, std::format("seedling grade '{}' root_g_per_plant", g.id)));
        SIM_CHECK(require(g.leaf_g_per_plant + g.crown_g_per_plant + g.root_g_per_plant > 0.0,
                          std::format("seedling grade '{}' leaf + crown + root must be > 0", g.id)));
        SIM_CHECK(require(std::isfinite(g.initial_induction_index) && g.initial_induction_index >= 0.0 &&
                              g.initial_induction_index <= 1.0,  // 1 = 분화 완료 모종 (활착 직후 첫 화방)
                          std::format("seedling grade '{}' initial_induction_index must be within [0, 1]", g.id)));
    }

    const FruitParams& fr = p.fruit;
    SIM_CHECK(require(!fr.rank_fraction.empty() && fr.rank_fraction.size() == fr.rank_anthesis_delay_days.size() &&
                          fr.rank_fraction.size() == fr.rank_potential_fresh_g.size(),
                      "fruit rank arrays must be non-empty and have equal length"));
    double rank_fraction_sum = 0.0;
    for (const double f : fr.rank_fraction) {
        SIM_CHECK(fraction(f, "fruit.rank_fraction entry"));
        rank_fraction_sum += f;
    }
    SIM_CHECK(require(std::abs(rank_fraction_sum - 1.0) <= 1e-9, "fruit.rank_fraction must sum to 1"));
    SIM_CHECK(non_negative(fr.anthesis_jitter_days, "fruit.anthesis_jitter_days"));
    for (const double d : fr.rank_anthesis_delay_days) {
        SIM_CHECK(non_negative(d, "fruit.rank_anthesis_delay_days entry"));
    }
    for (const double g : fr.rank_potential_fresh_g) {
        SIM_CHECK(positive(g, "fruit.rank_potential_fresh_g entry"));
    }
    SIM_CHECK(require(std::isfinite(fr.size_variation_fraction) && fr.size_variation_fraction >= 0.0 &&
                          fr.size_variation_fraction < 1.0,
                      "fruit.size_variation_fraction must be within [0, 1)"));
    SIM_CHECK(factor_curve(fr.pollination_mean_curve, "fruit.pollination_mean_curve"));
    SIM_CHECK(fraction(fr.pollination_spread, "fruit.pollination_spread"));
    SIM_CHECK(fraction(fr.set_threshold, "fruit.set_threshold"));
    SIM_CHECK(fraction(fr.deformed_threshold, "fruit.deformed_threshold"));
    SIM_CHECK(require(fr.set_threshold <= fr.deformed_threshold, "fruit.set_threshold must be <= deformed_threshold"));
    SIM_CHECK(validate_curve(fr.pollination_size_curve, "fruit.pollination_size_curve"));
    SIM_CHECK(curve_y_positive(fr.pollination_size_curve, "fruit.pollination_size_curve"));
    SIM_CHECK(require(std::isfinite(fr.dry_matter_fraction) && fr.dry_matter_fraction > 0.0 &&
                          fr.dry_matter_fraction <= 1.0,
                      "fruit.dry_matter_fraction must be within (0, 1]"));
    SIM_CHECK(finite(fr.base_temp_degC, "fruit.base_temp_degC"));
    SIM_CHECK(positive(fr.degree_days_to_harvest, "fruit.degree_days_to_harvest"));
    SIM_CHECK(factor_curve(fr.growth_shape_curve, "fruit.growth_shape_curve"));
    SIM_CHECK(require(!fr.size_class_bounds_g.empty(), "fruit.size_class_bounds_g must not be empty"));
    for (std::size_t i = 0; i < fr.size_class_bounds_g.size(); ++i) {
        SIM_CHECK(positive(fr.size_class_bounds_g[i], "fruit.size_class_bounds_g entry"));
        SIM_CHECK(require(i == 0 || fr.size_class_bounds_g[i] > fr.size_class_bounds_g[i - 1],
                          "fruit.size_class_bounds_g must be strictly ascending"));
    }
    SIM_CHECK(positive(fr.base_brix, "fruit.base_brix"));
    SIM_CHECK(validate_curve(fr.brix_supply_curve, "fruit.brix_supply_curve"));
    SIM_CHECK(validate_curve(fr.brix_k_curve, "fruit.brix_k_curve"));
    SIM_CHECK(validate_curve(fr.brix_n_excess_curve, "fruit.brix_n_excess_curve"));
    SIM_CHECK(validate_curve(fr.brix_water_curve, "fruit.brix_water_curve"));
    SIM_CHECK(factor_curve(fr.firmness_ca_curve, "fruit.firmness_ca_curve"));
    SIM_CHECK(factor_curve(fr.firmness_temp_curve, "fruit.firmness_temp_curve"));
    SIM_CHECK(factor_curve(fr.firmness_n_excess_curve, "fruit.firmness_n_excess_curve"));
    // 6b 낙과, 6c 적화·적과, 당도 온도 주·야간
    SIM_CHECK(non_negative(fr.abortion_rate_per_development_index, "fruit.abortion_rate_per_development_index"));
    SIM_CHECK(fraction(fr.abortion_window_index, "fruit.abortion_window_index"));
    SIM_CHECK(factor_curve(fr.abortion_satisfaction_curve, "fruit.abortion_satisfaction_curve"));
    SIM_CHECK(require(fr.rank_abortion_weight.size() == fr.rank_fraction.size(),
                      "fruit.rank_abortion_weight must have one entry per rank"));
    for (const double v : fr.rank_abortion_weight) {
        SIM_CHECK(non_negative(v, "fruit.rank_abortion_weight entry"));
    }
    SIM_CHECK(fraction(fr.thinnable_max_index, "fruit.thinnable_max_index"));
    SIM_CHECK(factor_curve(fr.brix_day_temp_score_curve, "fruit.brix_day_temp_score_curve"));
    SIM_CHECK(factor_curve(fr.brix_night_temp_score_curve, "fruit.brix_night_temp_score_curve"));
    SIM_CHECK(fraction(fr.brix_day_weight, "fruit.brix_day_weight"));
    SIM_CHECK(fraction(fr.brix_night_weight, "fruit.brix_night_weight"));
    SIM_CHECK(require(std::abs(fr.brix_day_weight + fr.brix_night_weight - 1.0) <= 1e-9,
                      "fruit.brix_day_weight + brix_night_weight must be 1"));
    SIM_CHECK(validate_curve(fr.brix_temp_score_curve, "fruit.brix_temp_score_curve"));
    SIM_CHECK(non_negative(fr.day_solar_threshold_W_per_m2, "fruit.day_solar_threshold_W_per_m2"));

    const DenitrificationParams& dn = p.denitrification;
    SIM_CHECK(non_negative(dn.rate_per_hour, "denitrification.rate_per_hour"));
    SIM_CHECK(factor_curve(dn.o2_curve, "denitrification.o2_curve"));
    SIM_CHECK(factor_curve(dn.temp_curve, "denitrification.temp_curve"));
    SIM_CHECK(factor_curve(dn.carbon_curve, "denitrification.carbon_curve"));
    SIM_CHECK(finite(dn.H_eq_per_eq_N, "denitrification.H_eq_per_eq_N"));

    SIM_CHECK(fraction(crop.vegetative_leaf_fraction, "crop.vegetative_leaf_fraction"));
    SIM_CHECK(fraction(crop.vegetative_crown_fraction, "crop.vegetative_crown_fraction"));
    SIM_CHECK(fraction(crop.vegetative_root_fraction, "crop.vegetative_root_fraction"));
    SIM_CHECK(require(std::abs(crop.vegetative_leaf_fraction + crop.vegetative_crown_fraction +
                               crop.vegetative_root_fraction - 1.0) <= 1e-9,
                      "crop vegetative_*_fraction values must sum to 1"));
    SIM_CHECK(fraction(crop.max_fruit_assimilate_fraction, "crop.max_fruit_assimilate_fraction"));
    SIM_CHECK(non_negative(crop.maintenance_rate_leaf_per_hour, "crop.maintenance_rate_leaf_per_hour"));
    SIM_CHECK(non_negative(crop.maintenance_rate_crown_per_hour, "crop.maintenance_rate_crown_per_hour"));
    SIM_CHECK(non_negative(crop.maintenance_rate_root_per_hour, "crop.maintenance_rate_root_per_hour"));
    SIM_CHECK(non_negative(crop.maintenance_rate_fruit_per_hour, "crop.maintenance_rate_fruit_per_hour"));
    SIM_CHECK(validate_curve(crop.maintenance_temp_curve, "crop.maintenance_temp_curve"));
    SIM_CHECK(non_negative(crop.leaf_senescence_rate_per_hour, "crop.leaf_senescence_rate_per_hour"));
    SIM_CHECK(positive(crop.fruit_target_N_g_per_g, "crop.fruit_target_N_g_per_g"));
    SIM_CHECK(positive(crop.fruit_target_P_g_per_g, "crop.fruit_target_P_g_per_g"));
    SIM_CHECK(positive(crop.fruit_target_K_g_per_g, "crop.fruit_target_K_g_per_g"));
    SIM_CHECK(positive(crop.fruit_target_Ca_g_per_g, "crop.fruit_target_Ca_g_per_g"));
    SIM_CHECK(positive(crop.fruit_target_Mg_g_per_g, "crop.fruit_target_Mg_g_per_g"));
    SIM_CHECK(positive(crop.fruit_target_Mn_g_per_g, "crop.fruit_target_Mn_g_per_g"));
    SIM_CHECK(positive(crop.co2_g_per_g_dry_matter, "crop.co2_g_per_g_dry_matter"));
    SIM_CHECK(validate_curve(crop.co2_growth_curve, "crop.co2_growth_curve"));
    SIM_CHECK(factor_curve(crop.cold_leaf_damage_curve, "crop.cold_leaf_damage_curve"));
    SIM_CHECK(factor_curve(crop.cold_fruit_loss_curve, "crop.cold_fruit_loss_curve"));
    SIM_CHECK(require(std::isfinite(crop.n_luxury_uptake_ratio) && crop.n_luxury_uptake_ratio >= 1.0,
                      "crop.n_luxury_uptake_ratio must be finite and >= 1"));
    SIM_CHECK(non_negative(crop.n_uptake_rate_per_g_root_per_hour, "crop.n_uptake_rate_per_g_root_per_hour"));

    std::set<std::string, std::less<>> compost_ids;
    for (const CompostDef& c : p.composts) {
        SIM_CHECK(require(!c.id.empty() && compost_ids.insert(c.id).second,
                          std::format("compost id '{}' must be non-empty and unique", c.id)));
        SIM_CHECK(non_negative(c.C_g_per_kg, std::format("compost '{}' C_g_per_kg", c.id)));
        SIM_CHECK(non_negative(c.N_g_per_kg, std::format("compost '{}' N_g_per_kg", c.id)));
        SIM_CHECK(fraction(c.labile_C_fraction, std::format("compost '{}' labile_C_fraction", c.id)));
        SIM_CHECK(fraction(c.labile_N_fraction, std::format("compost '{}' labile_N_fraction", c.id)));
        SIM_CHECK(non_negative(c.k_g_per_kg, std::format("compost '{}' k_g_per_kg", c.id)));
        SIM_CHECK(non_negative(c.ca_g_per_kg, std::format("compost '{}' ca_g_per_kg", c.id)));
        SIM_CHECK(non_negative(c.mg_g_per_kg, std::format("compost '{}' mg_g_per_kg", c.id)));
        SIM_CHECK(non_negative(c.p_g_per_kg, std::format("compost '{}' p_g_per_kg", c.id)));
        SIM_CHECK(non_negative(c.s_g_per_kg, std::format("compost '{}' s_g_per_kg", c.id)));
        SIM_CHECK(non_negative(c.mn_g_per_kg, std::format("compost '{}' mn_g_per_kg", c.id)));
        SIM_CHECK(finite(c.direct_acid_eq_per_kg, std::format("compost '{}' direct_acid_eq_per_kg", c.id)));
    }
    std::set<std::string, std::less<>> fertilizer_ids;
    for (const FertilizerDef& f : p.fertilizers) {
        SIM_CHECK(require(!f.id.empty() && fertilizer_ids.insert(f.id).second,
                          std::format("fertilizer id '{}' must be non-empty and unique", f.id)));
        SIM_CHECK(non_negative(f.nh4_N_g_per_kg, std::format("fertilizer '{}' nh4_N_g_per_kg", f.id)));
        SIM_CHECK(non_negative(f.no3_N_g_per_kg, std::format("fertilizer '{}' no3_N_g_per_kg", f.id)));
        SIM_CHECK(non_negative(f.k_g_per_kg, std::format("fertilizer '{}' k_g_per_kg", f.id)));
        SIM_CHECK(non_negative(f.ca_g_per_kg, std::format("fertilizer '{}' ca_g_per_kg", f.id)));
        SIM_CHECK(non_negative(f.mg_g_per_kg, std::format("fertilizer '{}' mg_g_per_kg", f.id)));
        SIM_CHECK(non_negative(f.p_g_per_kg, std::format("fertilizer '{}' p_g_per_kg", f.id)));
        SIM_CHECK(non_negative(f.s_g_per_kg, std::format("fertilizer '{}' s_g_per_kg", f.id)));
        SIM_CHECK(non_negative(f.mn_g_per_kg, std::format("fertilizer '{}' mn_g_per_kg", f.id)));
        SIM_CHECK(finite(f.direct_acid_eq_per_kg, std::format("fertilizer '{}' direct_acid_eq_per_kg", f.id)));
    }
    std::set<std::string, std::less<>> lime_ids;
    for (const LimeDef& l : p.limes) {
        SIM_CHECK(require(!l.id.empty() && lime_ids.insert(l.id).second,
                          std::format("lime id '{}' must be non-empty and unique", l.id)));
        SIM_CHECK(non_negative(l.eq_per_kg, std::format("lime '{}' eq_per_kg", l.id)));
        SIM_CHECK(fraction(l.ca_fraction, std::format("lime '{}' ca_fraction", l.id)));
        SIM_CHECK(fraction(l.mg_fraction, std::format("lime '{}' mg_fraction", l.id)));
        SIM_CHECK(require(l.ca_fraction + l.mg_fraction <= 1.0,
                          std::format("lime '{}' ca_fraction + mg_fraction must be <= 1", l.id)));
    }
    return {};
}

std::expected<void, Error> validate_scenario(const Scenario& s) {
    if (s.start_day_of_year < 1 || s.start_day_of_year > kDaysPerYear) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("start_day_of_year must be 1..{}, got {}", kDaysPerYear, s.start_day_of_year));
    }
    if (s.bed_rows <= 0 || s.bed_cols <= 0) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("bed grid must be at least 1x1, got {}x{}", s.bed_rows, s.bed_cols));
    }

    SIM_CHECK(require(s.cover_layers >= 1 && s.cover_layers <= kMaxCoverLayers,
                      std::format("cover_layers must be within 1..{}, got {}", kMaxCoverLayers, s.cover_layers)));

    const SoilInit& soil = s.soil;
    SIM_CHECK(positive(soil.field_capacity_mm, "soil.field_capacity_mm"));
    SIM_CHECK(non_negative(soil.water_mm, "soil.water_mm"));
    SIM_CHECK(finite(soil.soil_temp_degC, "soil.soil_temp_degC"));
    SIM_CHECK(require(std::isfinite(soil.ph) && soil.ph >= 0.0 && soil.ph <= 14.0, "soil.ph must be within 0..14"));
    SIM_CHECK(non_negative(soil.nh4_N_g_per_m2, "soil.nh4_N_g_per_m2"));
    SIM_CHECK(non_negative(soil.no2_N_g_per_m2, "soil.no2_N_g_per_m2"));
    SIM_CHECK(non_negative(soil.no3_N_g_per_m2, "soil.no3_N_g_per_m2"));
    SIM_CHECK(non_negative(soil.labile_C_g_per_m2, "soil.labile_C_g_per_m2"));
    SIM_CHECK(non_negative(soil.labile_N_g_per_m2, "soil.labile_N_g_per_m2"));
    SIM_CHECK(non_negative(soil.stable_C_g_per_m2, "soil.stable_C_g_per_m2"));
    SIM_CHECK(non_negative(soil.stable_N_g_per_m2, "soil.stable_N_g_per_m2"));
    SIM_CHECK(require(std::isfinite(soil.saturation_mm) && soil.saturation_mm > soil.field_capacity_mm,
                      "soil.saturation_mm must be finite and > soil.field_capacity_mm"));
    SIM_CHECK(fraction(soil.compaction_index, "soil.compaction_index"));
    SIM_CHECK(positive(soil.base_buffer_eq_per_m2_per_pH, "soil.base_buffer_eq_per_m2_per_pH"));
    SIM_CHECK(non_negative(soil.k_g_per_m2, "soil.k_g_per_m2"));
    SIM_CHECK(non_negative(soil.ca_g_per_m2, "soil.ca_g_per_m2"));
    SIM_CHECK(non_negative(soil.mg_g_per_m2, "soil.mg_g_per_m2"));
    SIM_CHECK(non_negative(soil.p_g_per_m2, "soil.p_g_per_m2"));
    SIM_CHECK(non_negative(soil.s_g_per_m2, "soil.s_g_per_m2"));
    SIM_CHECK(positive(soil.cec_mineral_eq_per_m2, "soil.cec_mineral_eq_per_m2"));
    SIM_CHECK(non_negative(soil.mn_g_per_m2, "soil.mn_g_per_m2"));
    SIM_CHECK(non_negative(soil.mn_oxide_g_per_m2, "soil.mn_oxide_g_per_m2"));
    SIM_CHECK(non_negative(soil.p_fixed_g_per_m2, "soil.p_fixed_g_per_m2"));

    if (s.crop) {
        SIM_CHECK(require(!s.crop->seedling_grade.empty(), "crop.seedling_grade must not be empty"));
        SIM_CHECK(positive(s.crop->plants_per_m2, "crop.plants_per_m2"));
        SIM_CHECK(fraction(s.crop->initial_establishment_index, "crop.initial_establishment_index"));
    }
    SIM_CHECK(require(s.stress_combination == StressCombination::Minimum ||
                          s.stress_combination == StressCombination::Product,
                      "stress_combination is not a known value"));

    if (s.weather_hourly.size() != kHoursPerYear) {
        return fail(ErrorCode::InvalidArgument, std::format("weather_hourly must have {} entries, got {}",
                                                            kHoursPerYear, s.weather_hourly.size()));
    }
    for (std::size_t i = 0; i < s.weather_hourly.size(); ++i) {
        const WeatherHour& w = s.weather_hourly[i];
        if (!std::isfinite(w.outside_air_temp_degC) || !std::isfinite(w.solar_radiation_W_per_m2) ||
            !std::isfinite(w.outside_rh_percent)) {
            return fail(ErrorCode::InvalidArgument, std::format("weather_hourly[{}] has a non-finite value", i));
        }
        if (w.solar_radiation_W_per_m2 < 0.0) {
            return fail(ErrorCode::InvalidArgument, std::format("weather_hourly[{}] solar radiation is negative", i));
        }
        if (w.outside_rh_percent < 0.0 || w.outside_rh_percent > 100.0) {
            return fail(ErrorCode::InvalidArgument,
                        std::format("weather_hourly[{}] relative humidity is outside 0..100", i));
        }
    }
    return {};
}

std::expected<void, Error> validate_scenario_against_params(const Scenario& s, const Params& p) {
    if (s.crop) {
        bool found = false;
        for (const SeedlingGradeDef& g : p.seedling_grades) {
            if (g.id == s.crop->seedling_grade) {
                found = true;
                break;
            }
        }
        SIM_CHECK(require(found, std::format("crop.seedling_grade '{}' is not defined in params", s.crop->seedling_grade)));
    }
    SIM_CHECK(require(s.cover_layers >= p.house.min_cover_layers && s.cover_layers <= p.house.max_cover_layers,
                      std::format("cover_layers {} is outside params range {}..{}", s.cover_layers,
                                  p.house.min_cover_layers, p.house.max_cover_layers)));
    return {};
}

}  // namespace sim::validation
