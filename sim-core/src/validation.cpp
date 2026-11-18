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

    SIM_CHECK(finite(p.house.base_offset_degC, "house.base_offset_degC"));
    SIM_CHECK(non_negative(p.house.solar_gain_degC_per_W_per_m2, "house.solar_gain_degC_per_W_per_m2"));
    SIM_CHECK(fraction(p.house.solar_transmittance_factor, "house.solar_transmittance_factor"));

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

    SIM_CHECK(fraction(p.leaching.nh4_solution_fraction, "leaching.nh4_solution_fraction"));

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

    const SoilChemistryParams& chem = p.soil_chemistry;
    SIM_CHECK(fraction(chem.k_solution_fraction, "soil_chemistry.k_solution_fraction"));
    SIM_CHECK(fraction(chem.ca_solution_fraction, "soil_chemistry.ca_solution_fraction"));
    SIM_CHECK(fraction(chem.mg_solution_fraction, "soil_chemistry.mg_solution_fraction"));
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
    SIM_CHECK(fraction(crop.leaf_allocation_fraction, "crop.leaf_allocation_fraction"));
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
    SIM_CHECK(non_negative(crop.transpiration_mm_per_hour_per_W_per_m2, "crop.transpiration_mm_per_hour_per_W_per_m2"));
    SIM_CHECK(non_negative(crop.root_recovery_rate_per_hour, "crop.root_recovery_rate_per_hour"));
    SIM_CHECK(fraction(crop.death_root_health, "crop.death_root_health"));
    SIM_CHECK(fraction(crop.root_dieback_per_damage, "crop.root_dieback_per_damage"));
    SIM_CHECK(fraction(crop.dry_matter_C_fraction, "crop.dry_matter_C_fraction"));

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

    if (s.crop) {
        SIM_CHECK(non_negative(s.crop->leaf_g_per_m2, "crop.leaf_g_per_m2"));
        SIM_CHECK(non_negative(s.crop->root_g_per_m2, "crop.root_g_per_m2"));
        SIM_CHECK(require(s.crop->leaf_g_per_m2 + s.crop->root_g_per_m2 > 0.0,
                          "crop leaf + root biomass must be > 0"));
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

}  // namespace sim::validation
