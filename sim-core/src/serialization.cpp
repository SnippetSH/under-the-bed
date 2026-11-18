// 세이브 형식 (리틀 엔디언)
//
//   magic "FGSV" | format_version u32
//   [정적 입력] params, scenario (토양 초기값, 기상 표 포함)
//   [동적 상태] tick, next_command_seq, pending_commands, tiles(토양 셀 + 작물), house_air_temp, ledger
//
// state_hash()는 동적 상태 부분만 해시해요. 이벤트는 저장하지 않아요.
// 필드를 추가하면 kSaveFormatVersion을 올려요 (M0 = 1, M1 = 2, M2 = 3, 뿌리 고사 = 4, M3 = 5).

#include <array>
#include <format>
#include <utility>

#include "byte_io.hpp"
#include "sim/calendar.hpp"
#include "sim/hash.hpp"
#include "sim/world.hpp"

namespace sim {

namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'F', 'G', 'S', 'V'};
constexpr std::uint32_t kSaveFormatVersion = 5;

constexpr std::size_t kWeatherHourBytes = 24;       // f64 × 3
constexpr std::size_t kCurvePointBytes = 16;        // f64 × 2
constexpr std::size_t kQueuedCommandMinBytes = 12;  // seq u64 + variant index u32
constexpr std::size_t kMinStringBytes = 8;          // 길이 u64

std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

// ---- 공통 조각 ----

void write_curve(detail::ByteWriter& w, const PiecewiseLinear& c) {
    w.u64(c.points.size());
    for (const auto& p : c.points) {
        w.f64(p[0]);
        w.f64(p[1]);
    }
}

void read_curve(detail::ByteReader& r, PiecewiseLinear& c) {
    const std::uint64_t n = r.u64();
    if (!r.ok() || n > r.remaining() / kCurvePointBytes) {
        r.fail();
        return;
    }
    c.points.resize(static_cast<std::size_t>(n));
    for (auto& p : c.points) {
        p[0] = r.f64();
        p[1] = r.f64();
    }
}

void write_area(detail::ByteWriter& w, const TileArea& a) {
    w.i32(a.row);
    w.i32(a.col);
    w.i32(a.rows);
    w.i32(a.cols);
}

TileArea read_area(detail::ByteReader& r) {
    TileArea a;
    a.row = r.i32();
    a.col = r.i32();
    a.rows = r.i32();
    a.cols = r.i32();
    return a;
}

void write_cell(detail::ByteWriter& w, const SoilCell& c) {
    w.f64(c.field_capacity_mm);
    w.f64(c.saturation_mm);
    w.f64(c.base_buffer_eq_per_m2_per_pH);
    w.f64(c.water_mm);
    w.f64(c.drained_mm_this_tick);
    w.f64(c.soil_temp_degC);
    w.f64(c.labile_C_g_per_m2);
    w.f64(c.labile_N_g_per_m2);
    w.f64(c.stable_C_g_per_m2);
    w.f64(c.stable_N_g_per_m2);
    w.f64(c.nh4_N_g_per_m2);
    w.f64(c.no2_N_g_per_m2);
    w.f64(c.no3_N_g_per_m2);
    w.f64(c.ph);
    w.f64(c.compaction_index);
    w.f64(c.aeration_index);
    w.f64(c.k_g_per_m2);
    w.f64(c.ca_g_per_m2);
    w.f64(c.mg_g_per_m2);
    w.f64(c.p_g_per_m2);
    w.f64(c.s_g_per_m2);
    w.f64(c.lime_undissolved_eq_per_m2);
    w.f64(c.lime_ca_fraction);
    w.f64(c.lime_mg_fraction);
    w.f64(c.acid_H_eq_this_tick);
    w.f64(c.pending_H_eq_per_m2);
    w.f64(c.ec_dS_per_m);
    w.f64(c.cec_mineral_eq_per_m2);
    w.f64(c.mn_g_per_m2);
    w.f64(c.mn_oxide_g_per_m2);
    w.f64(c.p_fixed_g_per_m2);
    w.f64(c.nh4_solution_N_g_per_m2);
    w.f64(c.k_solution_g_per_m2);
    w.f64(c.ca_solution_g_per_m2);
    w.f64(c.mg_solution_g_per_m2);
    w.f64(c.mn_solution_g_per_m2);
    w.f64(c.cec_eq_per_m2);
    w.f64(c.exchange_free_site_index);
    w.u8(c.nitrite_alarm ? 1 : 0);
}

void read_cell(detail::ByteReader& r, SoilCell& c) {
    c.field_capacity_mm = r.f64();
    c.saturation_mm = r.f64();
    c.base_buffer_eq_per_m2_per_pH = r.f64();
    c.water_mm = r.f64();
    c.drained_mm_this_tick = r.f64();
    c.soil_temp_degC = r.f64();
    c.labile_C_g_per_m2 = r.f64();
    c.labile_N_g_per_m2 = r.f64();
    c.stable_C_g_per_m2 = r.f64();
    c.stable_N_g_per_m2 = r.f64();
    c.nh4_N_g_per_m2 = r.f64();
    c.no2_N_g_per_m2 = r.f64();
    c.no3_N_g_per_m2 = r.f64();
    c.ph = r.f64();
    c.compaction_index = r.f64();
    c.aeration_index = r.f64();
    c.k_g_per_m2 = r.f64();
    c.ca_g_per_m2 = r.f64();
    c.mg_g_per_m2 = r.f64();
    c.p_g_per_m2 = r.f64();
    c.s_g_per_m2 = r.f64();
    c.lime_undissolved_eq_per_m2 = r.f64();
    c.lime_ca_fraction = r.f64();
    c.lime_mg_fraction = r.f64();
    c.acid_H_eq_this_tick = r.f64();
    c.pending_H_eq_per_m2 = r.f64();
    c.ec_dS_per_m = r.f64();
    c.cec_mineral_eq_per_m2 = r.f64();
    c.mn_g_per_m2 = r.f64();
    c.mn_oxide_g_per_m2 = r.f64();
    c.p_fixed_g_per_m2 = r.f64();
    c.nh4_solution_N_g_per_m2 = r.f64();
    c.k_solution_g_per_m2 = r.f64();
    c.ca_solution_g_per_m2 = r.f64();
    c.mg_solution_g_per_m2 = r.f64();
    c.mn_solution_g_per_m2 = r.f64();
    c.cec_eq_per_m2 = r.f64();
    c.exchange_free_site_index = r.f64();
    const std::uint8_t alarm = r.u8();
    if (alarm > 1) {
        r.fail();
    }
    c.nitrite_alarm = alarm == 1;
}

void write_crop(detail::ByteWriter& w, const Crop& c) {
    w.u8(c.present ? 1 : 0);
    w.u8(c.alive ? 1 : 0);
    w.f64(c.leaf_g_per_m2);
    w.f64(c.root_g_per_m2);
    w.f64(c.root_health);
    w.f64(c.plant_N_g_per_m2);
    w.f64(c.plant_P_g_per_m2);
    w.f64(c.plant_K_g_per_m2);
    w.f64(c.plant_Ca_g_per_m2);
    w.f64(c.plant_Mg_g_per_m2);
    for (const double hours : c.stress_hours) {
        w.f64(hours);
    }
    w.f64(c.last_growth_g_per_m2);
    w.f64(c.plant_Mn_g_per_m2);
    w.f64(c.antagonism_Ca_factor);
    w.f64(c.antagonism_Mg_factor);
}

void read_crop(detail::ByteReader& r, Crop& c) {
    const std::uint8_t present = r.u8();
    const std::uint8_t alive = r.u8();
    if (present > 1 || alive > 1) {
        r.fail();
    }
    c.present = present == 1;
    c.alive = alive == 1;
    c.leaf_g_per_m2 = r.f64();
    c.root_g_per_m2 = r.f64();
    c.root_health = r.f64();
    c.plant_N_g_per_m2 = r.f64();
    c.plant_P_g_per_m2 = r.f64();
    c.plant_K_g_per_m2 = r.f64();
    c.plant_Ca_g_per_m2 = r.f64();
    c.plant_Mg_g_per_m2 = r.f64();
    for (double& hours : c.stress_hours) {
        hours = r.f64();
    }
    c.last_growth_g_per_m2 = r.f64();
    c.plant_Mn_g_per_m2 = r.f64();
    c.antagonism_Ca_factor = r.f64();
    c.antagonism_Mg_factor = r.f64();
}

// ---- 정적 입력 ----

void write_params(detail::ByteWriter& w, const Params& p) {
    w.i64(p.schema_version);
    w.f64(p.house.base_offset_degC);
    w.f64(p.house.solar_gain_degC_per_W_per_m2);
    w.f64(p.house.solar_transmittance_factor);
    w.f64(p.soil_water.evap_mm_per_hour_per_W_per_m2);
    w.f64(p.soil_water.evap_base_mm_per_hour);
    write_curve(w, p.soil_water.evap_moisture_curve);
    w.f64(p.soil_water.drainage_rate_per_hour);
    write_curve(w, p.soil_water.drainage_compaction_curve);
    w.f64(p.soil_temperature.relax_rate_per_hour);
    w.f64(p.soil_temperature.heat_degC_per_g_C);
    w.f64(p.organic_matter.labile_decay_rate_per_hour);
    w.f64(p.organic_matter.stable_decay_rate_per_hour);
    w.f64(p.organic_matter.humification_fraction);
    w.f64(p.organic_matter.humus_C_to_N_ratio);
    write_curve(w, p.organic_matter.temp_curve);
    write_curve(w, p.organic_matter.moisture_curve);
    write_curve(w, p.organic_matter.o2_curve);
    w.f64(p.nitrification.step1_rate_per_hour);
    w.f64(p.nitrification.step2_rate_per_hour);
    write_curve(w, p.nitrification.temp_curve);
    write_curve(w, p.nitrification.moisture_curve);
    write_curve(w, p.nitrification.step1_ph_curve);
    write_curve(w, p.nitrification.step2_ph_curve);
    write_curve(w, p.nitrification.ammonia_fraction_ph_curve);
    write_curve(w, p.nitrification.free_ammonia_inhibition_curve);
    w.f64(p.nitrification.nitrite_toxicity_threshold_g_N_per_m2);
    w.f64(p.nitrification.nitrite_alarm_reset_fraction);
    write_curve(w, p.nitrification.step1_o2_curve);
    write_curve(w, p.nitrification.step2_o2_curve);
    write_curve(w, p.nitrification.nitrite_toxicity_ph_curve);
    write_curve(w, p.aeration.compaction_cap_curve);
    w.f64(p.aeration.organic_C_cap_bonus_per_g_C);
    w.f64(p.aeration.compaction_per_mm_irrigation);
    w.f64(p.aeration.tilled_compaction_index);
    w.f64(p.aeration.till_root_damage_fraction);
    w.f64(p.ions.nh4_N_g_per_eq);
    w.f64(p.ions.no3_N_g_per_eq);
    w.f64(p.ions.no2_N_g_per_eq);
    w.f64(p.ions.k_g_per_eq);
    w.f64(p.ions.ca_g_per_eq);
    w.f64(p.ions.mg_g_per_eq);
    w.f64(p.ions.p_g_per_eq);
    w.f64(p.ions.s_g_per_eq);
    w.f64(p.soil_chemistry.p_solution_fraction);
    w.f64(p.soil_chemistry.s_solution_fraction);
    w.f64(p.soil_chemistry.nitrification_H_eq_per_eq_N);
    w.f64(p.soil_chemistry.uptake_H_eq_per_eq_NH4);
    w.f64(p.soil_chemistry.uptake_H_eq_per_eq_NO3);
    w.f64(p.soil_chemistry.organic_C_buffer_eq_per_g_C_per_pH);
    w.f64(p.soil_chemistry.ph_min);
    w.f64(p.soil_chemistry.ph_max);
    w.f64(p.soil_chemistry.lime_dissolution_rate_per_hour);
    write_curve(w, p.soil_chemistry.lime_dissolution_ph_curve);
    write_curve(w, p.soil_chemistry.lime_dissolution_moisture_curve);
    w.f64(p.soil_chemistry.ec_meq_per_L_per_dS_per_m);
    w.f64(p.soil_chemistry.ec_min_water_mm);
    write_curve(w, p.availability.n_ph_curve);
    write_curve(w, p.availability.p_ph_curve);
    write_curve(w, p.availability.k_ph_curve);
    write_curve(w, p.availability.ca_ph_curve);
    write_curve(w, p.availability.mg_ph_curve);
    write_curve(w, p.root_damage.waterlog_curve);
    write_curve(w, p.root_damage.heat_curve);
    write_curve(w, p.root_damage.nitrite_curve);
    write_curve(w, p.root_damage.acid_curve);
    w.f64(p.crop.specific_leaf_area_m2_per_g);
    write_curve(w, p.crop.light_interception_curve);
    w.f64(p.crop.par_fraction);
    w.f64(p.crop.light_use_efficiency_g_per_MJ);
    write_curve(w, p.crop.temp_curve);
    w.f64(p.crop.leaf_allocation_fraction);
    write_curve(w, p.crop.water_stress_curve);
    write_curve(w, p.crop.salt_stress_curve);
    write_curve(w, p.crop.nutrient_stress_curve);
    w.f64(p.crop.target_N_g_per_g);
    w.f64(p.crop.target_P_g_per_g);
    w.f64(p.crop.target_K_g_per_g);
    w.f64(p.crop.target_Ca_g_per_g);
    w.f64(p.crop.target_Mg_g_per_g);
    w.f64(p.crop.luxury_uptake_ratio);
    w.f64(p.crop.uptake_rate_per_g_root_per_hour);
    w.f64(p.crop.transpiration_mm_per_hour_per_W_per_m2);
    w.f64(p.crop.root_recovery_rate_per_hour);
    w.f64(p.crop.death_root_health);
    w.f64(p.crop.root_dieback_per_damage);
    w.f64(p.crop.dry_matter_C_fraction);
    w.f64(p.crop.target_Mn_g_per_g);
    w.f64(p.crop.ca_uptake_rate_per_g_root_per_hour);
    w.f64(p.crop.mg_uptake_rate_per_g_root_per_hour);
    w.f64(p.crop.mn_uptake_rate_per_g_root_per_hour);
    w.f64(p.crop.mn_max_uptake_ratio);
    write_curve(w, p.crop.mn_toxicity_curve);
    w.f64(p.ions.mn_g_per_eq);
    w.f64(p.cation_exchange.cec_organic_eq_per_g_C);
    w.f64(p.cation_exchange.exchange_rate_per_hour);
    w.f64(p.cation_exchange.nh4_retention_factor);
    w.f64(p.cation_exchange.k_retention_factor);
    w.f64(p.cation_exchange.ca_retention_factor);
    w.f64(p.cation_exchange.mg_retention_factor);
    w.i64(p.cation_exchange.solver_iterations);
    w.f64(p.cation_exchange.buffer_eq_per_pH_per_eq_cec);
    write_curve(w, p.antagonism.ca_curve);
    write_curve(w, p.antagonism.mg_curve);
    w.f64(p.antagonism.min_demand_factor);
    w.f64(p.phosphorus.fixation_rate_per_hour);
    write_curve(w, p.phosphorus.fixation_ph_curve);
    w.f64(p.phosphorus.release_rate_per_hour);
    write_curve(w, p.manganese.retention_ph_curve);
    w.f64(p.manganese.oxidation_rate_per_hour);
    write_curve(w, p.manganese.oxidation_ph_curve);
    write_curve(w, p.manganese.oxidation_o2_curve);
    w.f64(p.manganese.reduction_rate_per_hour);
    write_curve(w, p.manganese.reduction_ph_curve);
    write_curve(w, p.manganese.reduction_o2_curve);
    w.u64(p.composts.size());
    for (const auto& c : p.composts) {
        w.str(c.id);
        w.str(c.name);
        w.f64(c.C_g_per_kg);
        w.f64(c.N_g_per_kg);
        w.f64(c.labile_C_fraction);
        w.f64(c.labile_N_fraction);
        w.f64(c.k_g_per_kg);
        w.f64(c.ca_g_per_kg);
        w.f64(c.mg_g_per_kg);
        w.f64(c.p_g_per_kg);
        w.f64(c.s_g_per_kg);
        w.f64(c.mn_g_per_kg);
        w.f64(c.direct_acid_eq_per_kg);
    }
    w.u64(p.fertilizers.size());
    for (const auto& f : p.fertilizers) {
        w.str(f.id);
        w.str(f.name);
        w.f64(f.nh4_N_g_per_kg);
        w.f64(f.no3_N_g_per_kg);
        w.f64(f.k_g_per_kg);
        w.f64(f.ca_g_per_kg);
        w.f64(f.mg_g_per_kg);
        w.f64(f.p_g_per_kg);
        w.f64(f.s_g_per_kg);
        w.f64(f.mn_g_per_kg);
        w.f64(f.direct_acid_eq_per_kg);
    }
    w.u64(p.limes.size());
    for (const auto& l : p.limes) {
        w.str(l.id);
        w.str(l.name);
        w.f64(l.eq_per_kg);
        w.f64(l.ca_fraction);
        w.f64(l.mg_fraction);
    }
}

void read_params(detail::ByteReader& r, Params& p) {
    p.schema_version = r.i64();
    p.house.base_offset_degC = r.f64();
    p.house.solar_gain_degC_per_W_per_m2 = r.f64();
    p.house.solar_transmittance_factor = r.f64();
    p.soil_water.evap_mm_per_hour_per_W_per_m2 = r.f64();
    p.soil_water.evap_base_mm_per_hour = r.f64();
    read_curve(r, p.soil_water.evap_moisture_curve);
    p.soil_water.drainage_rate_per_hour = r.f64();
    read_curve(r, p.soil_water.drainage_compaction_curve);
    p.soil_temperature.relax_rate_per_hour = r.f64();
    p.soil_temperature.heat_degC_per_g_C = r.f64();
    p.organic_matter.labile_decay_rate_per_hour = r.f64();
    p.organic_matter.stable_decay_rate_per_hour = r.f64();
    p.organic_matter.humification_fraction = r.f64();
    p.organic_matter.humus_C_to_N_ratio = r.f64();
    read_curve(r, p.organic_matter.temp_curve);
    read_curve(r, p.organic_matter.moisture_curve);
    read_curve(r, p.organic_matter.o2_curve);
    p.nitrification.step1_rate_per_hour = r.f64();
    p.nitrification.step2_rate_per_hour = r.f64();
    read_curve(r, p.nitrification.temp_curve);
    read_curve(r, p.nitrification.moisture_curve);
    read_curve(r, p.nitrification.step1_ph_curve);
    read_curve(r, p.nitrification.step2_ph_curve);
    read_curve(r, p.nitrification.ammonia_fraction_ph_curve);
    read_curve(r, p.nitrification.free_ammonia_inhibition_curve);
    p.nitrification.nitrite_toxicity_threshold_g_N_per_m2 = r.f64();
    p.nitrification.nitrite_alarm_reset_fraction = r.f64();
    read_curve(r, p.nitrification.step1_o2_curve);
    read_curve(r, p.nitrification.step2_o2_curve);
    read_curve(r, p.nitrification.nitrite_toxicity_ph_curve);
    read_curve(r, p.aeration.compaction_cap_curve);
    p.aeration.organic_C_cap_bonus_per_g_C = r.f64();
    p.aeration.compaction_per_mm_irrigation = r.f64();
    p.aeration.tilled_compaction_index = r.f64();
    p.aeration.till_root_damage_fraction = r.f64();
    p.ions.nh4_N_g_per_eq = r.f64();
    p.ions.no3_N_g_per_eq = r.f64();
    p.ions.no2_N_g_per_eq = r.f64();
    p.ions.k_g_per_eq = r.f64();
    p.ions.ca_g_per_eq = r.f64();
    p.ions.mg_g_per_eq = r.f64();
    p.ions.p_g_per_eq = r.f64();
    p.ions.s_g_per_eq = r.f64();
    p.soil_chemistry.p_solution_fraction = r.f64();
    p.soil_chemistry.s_solution_fraction = r.f64();
    p.soil_chemistry.nitrification_H_eq_per_eq_N = r.f64();
    p.soil_chemistry.uptake_H_eq_per_eq_NH4 = r.f64();
    p.soil_chemistry.uptake_H_eq_per_eq_NO3 = r.f64();
    p.soil_chemistry.organic_C_buffer_eq_per_g_C_per_pH = r.f64();
    p.soil_chemistry.ph_min = r.f64();
    p.soil_chemistry.ph_max = r.f64();
    p.soil_chemistry.lime_dissolution_rate_per_hour = r.f64();
    read_curve(r, p.soil_chemistry.lime_dissolution_ph_curve);
    read_curve(r, p.soil_chemistry.lime_dissolution_moisture_curve);
    p.soil_chemistry.ec_meq_per_L_per_dS_per_m = r.f64();
    p.soil_chemistry.ec_min_water_mm = r.f64();
    read_curve(r, p.availability.n_ph_curve);
    read_curve(r, p.availability.p_ph_curve);
    read_curve(r, p.availability.k_ph_curve);
    read_curve(r, p.availability.ca_ph_curve);
    read_curve(r, p.availability.mg_ph_curve);
    read_curve(r, p.root_damage.waterlog_curve);
    read_curve(r, p.root_damage.heat_curve);
    read_curve(r, p.root_damage.nitrite_curve);
    read_curve(r, p.root_damage.acid_curve);
    p.crop.specific_leaf_area_m2_per_g = r.f64();
    read_curve(r, p.crop.light_interception_curve);
    p.crop.par_fraction = r.f64();
    p.crop.light_use_efficiency_g_per_MJ = r.f64();
    read_curve(r, p.crop.temp_curve);
    p.crop.leaf_allocation_fraction = r.f64();
    read_curve(r, p.crop.water_stress_curve);
    read_curve(r, p.crop.salt_stress_curve);
    read_curve(r, p.crop.nutrient_stress_curve);
    p.crop.target_N_g_per_g = r.f64();
    p.crop.target_P_g_per_g = r.f64();
    p.crop.target_K_g_per_g = r.f64();
    p.crop.target_Ca_g_per_g = r.f64();
    p.crop.target_Mg_g_per_g = r.f64();
    p.crop.luxury_uptake_ratio = r.f64();
    p.crop.uptake_rate_per_g_root_per_hour = r.f64();
    p.crop.transpiration_mm_per_hour_per_W_per_m2 = r.f64();
    p.crop.root_recovery_rate_per_hour = r.f64();
    p.crop.death_root_health = r.f64();
    p.crop.root_dieback_per_damage = r.f64();
    p.crop.dry_matter_C_fraction = r.f64();
    p.crop.target_Mn_g_per_g = r.f64();
    p.crop.ca_uptake_rate_per_g_root_per_hour = r.f64();
    p.crop.mg_uptake_rate_per_g_root_per_hour = r.f64();
    p.crop.mn_uptake_rate_per_g_root_per_hour = r.f64();
    p.crop.mn_max_uptake_ratio = r.f64();
    read_curve(r, p.crop.mn_toxicity_curve);
    p.ions.mn_g_per_eq = r.f64();
    p.cation_exchange.cec_organic_eq_per_g_C = r.f64();
    p.cation_exchange.exchange_rate_per_hour = r.f64();
    p.cation_exchange.nh4_retention_factor = r.f64();
    p.cation_exchange.k_retention_factor = r.f64();
    p.cation_exchange.ca_retention_factor = r.f64();
    p.cation_exchange.mg_retention_factor = r.f64();
    p.cation_exchange.solver_iterations = r.i64();
    p.cation_exchange.buffer_eq_per_pH_per_eq_cec = r.f64();
    read_curve(r, p.antagonism.ca_curve);
    read_curve(r, p.antagonism.mg_curve);
    p.antagonism.min_demand_factor = r.f64();
    p.phosphorus.fixation_rate_per_hour = r.f64();
    read_curve(r, p.phosphorus.fixation_ph_curve);
    p.phosphorus.release_rate_per_hour = r.f64();
    read_curve(r, p.manganese.retention_ph_curve);
    p.manganese.oxidation_rate_per_hour = r.f64();
    read_curve(r, p.manganese.oxidation_ph_curve);
    read_curve(r, p.manganese.oxidation_o2_curve);
    p.manganese.reduction_rate_per_hour = r.f64();
    read_curve(r, p.manganese.reduction_ph_curve);
    read_curve(r, p.manganese.reduction_o2_curve);
    const std::uint64_t composts_count = r.u64();
    if (!r.ok() || composts_count > r.remaining() / (2 * kMinStringBytes)) {
        r.fail();
        return;
    }
    p.composts.resize(static_cast<std::size_t>(composts_count));
    for (auto& c : p.composts) {
        c.id = r.str();
        c.name = r.str();
        c.C_g_per_kg = r.f64();
        c.N_g_per_kg = r.f64();
        c.labile_C_fraction = r.f64();
        c.labile_N_fraction = r.f64();
        c.k_g_per_kg = r.f64();
        c.ca_g_per_kg = r.f64();
        c.mg_g_per_kg = r.f64();
        c.p_g_per_kg = r.f64();
        c.s_g_per_kg = r.f64();
        c.mn_g_per_kg = r.f64();
        c.direct_acid_eq_per_kg = r.f64();
    }
    const std::uint64_t fertilizers_count = r.u64();
    if (!r.ok() || fertilizers_count > r.remaining() / (2 * kMinStringBytes)) {
        r.fail();
        return;
    }
    p.fertilizers.resize(static_cast<std::size_t>(fertilizers_count));
    for (auto& f : p.fertilizers) {
        f.id = r.str();
        f.name = r.str();
        f.nh4_N_g_per_kg = r.f64();
        f.no3_N_g_per_kg = r.f64();
        f.k_g_per_kg = r.f64();
        f.ca_g_per_kg = r.f64();
        f.mg_g_per_kg = r.f64();
        f.p_g_per_kg = r.f64();
        f.s_g_per_kg = r.f64();
        f.mn_g_per_kg = r.f64();
        f.direct_acid_eq_per_kg = r.f64();
    }
    const std::uint64_t limes_count = r.u64();
    if (!r.ok() || limes_count > r.remaining() / (2 * kMinStringBytes)) {
        r.fail();
        return;
    }
    p.limes.resize(static_cast<std::size_t>(limes_count));
    for (auto& l : p.limes) {
        l.id = r.str();
        l.name = r.str();
        l.eq_per_kg = r.f64();
        l.ca_fraction = r.f64();
        l.mg_fraction = r.f64();
    }
}

void write_scenario(detail::ByteWriter& w, const Scenario& s) {
    w.str(s.name);
    w.u64(s.seed);
    w.i32(s.start_day_of_year);
    w.i32(s.bed_rows);
    w.i32(s.bed_cols);

    const SoilInit& soil = s.soil;
    w.f64(soil.field_capacity_mm);
    w.f64(soil.water_mm);
    w.f64(soil.soil_temp_degC);
    w.f64(soil.ph);
    w.f64(soil.nh4_N_g_per_m2);
    w.f64(soil.no2_N_g_per_m2);
    w.f64(soil.no3_N_g_per_m2);
    w.f64(soil.labile_C_g_per_m2);
    w.f64(soil.labile_N_g_per_m2);
    w.f64(soil.stable_C_g_per_m2);
    w.f64(soil.stable_N_g_per_m2);
    w.f64(soil.saturation_mm);
    w.f64(soil.compaction_index);
    w.f64(soil.base_buffer_eq_per_m2_per_pH);
    w.f64(soil.k_g_per_m2);
    w.f64(soil.ca_g_per_m2);
    w.f64(soil.mg_g_per_m2);
    w.f64(soil.p_g_per_m2);
    w.f64(soil.s_g_per_m2);
    w.f64(soil.cec_mineral_eq_per_m2);
    w.f64(soil.mn_g_per_m2);
    w.f64(soil.mn_oxide_g_per_m2);
    w.f64(soil.p_fixed_g_per_m2);

    w.u8(s.crop.has_value() ? 1 : 0);
    if (s.crop) {
        w.f64(s.crop->leaf_g_per_m2);
        w.f64(s.crop->root_g_per_m2);
    }
    w.u8(static_cast<std::uint8_t>(s.stress_combination));

    w.u64(s.weather_hourly.size());
    for (const WeatherHour& h : s.weather_hourly) {
        w.f64(h.outside_air_temp_degC);
        w.f64(h.solar_radiation_W_per_m2);
        w.f64(h.outside_rh_percent);
    }
}

void read_scenario(detail::ByteReader& r, Scenario& s) {
    s.name = r.str();
    s.seed = r.u64();
    s.start_day_of_year = r.i32();
    s.bed_rows = r.i32();
    s.bed_cols = r.i32();

    SoilInit& soil = s.soil;
    soil.field_capacity_mm = r.f64();
    soil.water_mm = r.f64();
    soil.soil_temp_degC = r.f64();
    soil.ph = r.f64();
    soil.nh4_N_g_per_m2 = r.f64();
    soil.no2_N_g_per_m2 = r.f64();
    soil.no3_N_g_per_m2 = r.f64();
    soil.labile_C_g_per_m2 = r.f64();
    soil.labile_N_g_per_m2 = r.f64();
    soil.stable_C_g_per_m2 = r.f64();
    soil.stable_N_g_per_m2 = r.f64();
    soil.saturation_mm = r.f64();
    soil.compaction_index = r.f64();
    soil.base_buffer_eq_per_m2_per_pH = r.f64();
    soil.k_g_per_m2 = r.f64();
    soil.ca_g_per_m2 = r.f64();
    soil.mg_g_per_m2 = r.f64();
    soil.p_g_per_m2 = r.f64();
    soil.s_g_per_m2 = r.f64();
    soil.cec_mineral_eq_per_m2 = r.f64();
    soil.mn_g_per_m2 = r.f64();
    soil.mn_oxide_g_per_m2 = r.f64();
    soil.p_fixed_g_per_m2 = r.f64();

    const std::uint8_t has_crop = r.u8();
    if (has_crop > 1) {
        r.fail();
    }
    if (has_crop == 1) {
        CropInit crop;
        crop.leaf_g_per_m2 = r.f64();
        crop.root_g_per_m2 = r.f64();
        s.crop = crop;
    }
    const std::uint8_t stress_combination = r.u8();
    if (stress_combination > static_cast<std::uint8_t>(StressCombination::Product)) {
        r.fail();
    }
    s.stress_combination = static_cast<StressCombination>(stress_combination);

    const std::uint64_t weather_count = r.u64();
    if (!r.ok() || weather_count > r.remaining() / kWeatherHourBytes) {
        r.fail();
        return;
    }
    s.weather_hourly.resize(static_cast<std::size_t>(weather_count));
    for (WeatherHour& h : s.weather_hourly) {
        h.outside_air_temp_degC = r.f64();
        h.solar_radiation_W_per_m2 = r.f64();
        h.outside_rh_percent = r.f64();
    }
}

// ---- 명령 ----

void write_command(detail::ByteWriter& w, const Command& command) {
    w.u32(static_cast<std::uint32_t>(command.index()));
    struct Visitor {
        detail::ByteWriter& w;
        void operator()(const NoopCommand&) const {}
        void operator()(const IrrigateCommand& c) const {
            write_area(w, c.area);
            w.f64(c.amount_mm);
        }
        void operator()(const AddCompostCommand& c) const {
            write_area(w, c.area);
            w.str(c.compost_id);
            w.f64(c.kg_per_m2);
        }
        void operator()(const AddFertilizerCommand& c) const {
            write_area(w, c.area);
            w.str(c.fertilizer_id);
            w.f64(c.g_per_m2);
        }
        void operator()(const AddLimeCommand& c) const {
            write_area(w, c.area);
            w.str(c.lime_id);
            w.f64(c.g_per_m2);
        }
        void operator()(const TillCommand& c) const { write_area(w, c.area); }
    };
    std::visit(Visitor{w}, command);
}

std::expected<Command, Error> read_command(detail::ByteReader& r) {
    const std::uint32_t index = r.u32();
    switch (index) {
        case 0:
            return NoopCommand{};
        case 1: {
            IrrigateCommand c;
            c.area = read_area(r);
            c.amount_mm = r.f64();
            return c;
        }
        case 2: {
            AddCompostCommand c;
            c.area = read_area(r);
            c.compost_id = r.str();
            c.kg_per_m2 = r.f64();
            return c;
        }
        case 3: {
            AddFertilizerCommand c;
            c.area = read_area(r);
            c.fertilizer_id = r.str();
            c.g_per_m2 = r.f64();
            return c;
        }
        case 4: {
            AddLimeCommand c;
            c.area = read_area(r);
            c.lime_id = r.str();
            c.g_per_m2 = r.f64();
            return c;
        }
        case 5: {
            TillCommand c;
            c.area = read_area(r);
            return c;
        }
        default:
            if (!r.ok()) {
                return fail(ErrorCode::Truncated, "save data ends inside the command queue");
            }
            return fail(ErrorCode::InvalidData, std::format("unknown command type index {}", index));
    }
}

}  // namespace

void World::write_dynamic(detail::ByteWriter& w) const {
    w.u64(tick_);
    w.u64(next_command_seq_);

    w.u64(pending_commands_.size());
    for (const QueuedCommand& queued : pending_commands_) {
        w.u64(queued.seq);
        write_command(w, queued.command);
    }

    w.u64(tiles_.size());
    for (const Tile& tile : tiles_) {
        write_cell(w, tile.soil);
        write_crop(w, tile.crop);
    }

    w.f64(house_air_temp_degC_);

    w.f64(ledger_.water_in_mm);
    w.f64(ledger_.water_evaporated_mm);
    w.f64(ledger_.water_drained_mm);
    w.f64(ledger_.N_in_g);
    w.f64(ledger_.N_leached_g);
    w.f64(ledger_.N_gas_g);
    w.f64(ledger_.C_in_g);
    w.f64(ledger_.C_respired_g);
    w.f64(ledger_.water_transpired_mm);
    w.f64(ledger_.N_uptake_g);
    w.f64(ledger_.P_in_g);
    w.f64(ledger_.P_leached_g);
    w.f64(ledger_.P_uptake_g);
    w.f64(ledger_.K_in_g);
    w.f64(ledger_.K_leached_g);
    w.f64(ledger_.K_uptake_g);
    w.f64(ledger_.Ca_in_g);
    w.f64(ledger_.Ca_leached_g);
    w.f64(ledger_.Ca_uptake_g);
    w.f64(ledger_.Mg_in_g);
    w.f64(ledger_.Mg_leached_g);
    w.f64(ledger_.Mg_uptake_g);
    w.f64(ledger_.S_in_g);
    w.f64(ledger_.S_leached_g);
    w.f64(ledger_.H_eq_net);
    w.f64(ledger_.C_root_litter_g);
    w.f64(ledger_.Mn_in_g);
    w.f64(ledger_.Mn_leached_g);
    w.f64(ledger_.Mn_uptake_g);
}

std::expected<void, Error> World::read_dynamic(detail::ByteReader& r) {
    tick_ = r.u64();
    next_command_seq_ = r.u64();

    const std::uint64_t pending_count = r.u64();
    if (!r.ok() || pending_count > r.remaining() / kQueuedCommandMinBytes) {
        return fail(ErrorCode::Truncated, "save data ends inside the command queue");
    }
    pending_commands_.clear();
    pending_commands_.reserve(static_cast<std::size_t>(pending_count));
    for (std::uint64_t i = 0; i < pending_count; ++i) {
        const std::uint64_t seq = r.u64();
        auto command = read_command(r);
        if (!command) {
            return std::unexpected(std::move(command.error()));
        }
        if (!r.ok()) {
            return fail(ErrorCode::Truncated, "save data ends inside the command queue");
        }
        const bool ordered = pending_commands_.empty() || seq > pending_commands_.back().seq;
        if (!ordered || seq >= next_command_seq_) {
            return fail(ErrorCode::InvalidData, "command sequence numbers are inconsistent");
        }
        if (auto valid = validate_command(*command); !valid) {
            return fail(ErrorCode::InvalidData, std::format("saved command is invalid: {}", valid.error().message));
        }
        pending_commands_.push_back(QueuedCommand{seq, std::move(*command)});
    }

    const std::uint64_t tile_count = r.u64();
    if (r.ok() && tile_count != tiles_.size()) {
        return fail(ErrorCode::InvalidData,
                    std::format("tile count {} does not match grid size {}", tile_count, tiles_.size()));
    }
    for (Tile& tile : tiles_) {
        read_cell(r, tile.soil);
        read_crop(r, tile.crop);
    }

    house_air_temp_degC_ = r.f64();

    ledger_.water_in_mm = r.f64();
    ledger_.water_evaporated_mm = r.f64();
    ledger_.water_drained_mm = r.f64();
    ledger_.N_in_g = r.f64();
    ledger_.N_leached_g = r.f64();
    ledger_.N_gas_g = r.f64();
    ledger_.C_in_g = r.f64();
    ledger_.C_respired_g = r.f64();
    ledger_.water_transpired_mm = r.f64();
    ledger_.N_uptake_g = r.f64();
    ledger_.P_in_g = r.f64();
    ledger_.P_leached_g = r.f64();
    ledger_.P_uptake_g = r.f64();
    ledger_.K_in_g = r.f64();
    ledger_.K_leached_g = r.f64();
    ledger_.K_uptake_g = r.f64();
    ledger_.Ca_in_g = r.f64();
    ledger_.Ca_leached_g = r.f64();
    ledger_.Ca_uptake_g = r.f64();
    ledger_.Mg_in_g = r.f64();
    ledger_.Mg_leached_g = r.f64();
    ledger_.Mg_uptake_g = r.f64();
    ledger_.S_in_g = r.f64();
    ledger_.S_leached_g = r.f64();
    ledger_.H_eq_net = r.f64();
    ledger_.C_root_litter_g = r.f64();
    ledger_.Mn_in_g = r.f64();
    ledger_.Mn_leached_g = r.f64();
    ledger_.Mn_uptake_g = r.f64();

    if (!r.ok()) {
        return fail(ErrorCode::Truncated, "save data ends inside the dynamic state");
    }
    return {};
}

std::vector<std::byte> World::serialize() const {
    detail::ByteWriter w;
    for (const std::uint8_t c : kMagic) {
        w.u8(c);
    }
    w.u32(kSaveFormatVersion);
    write_params(w, params_);
    write_scenario(w, scenario_);
    write_dynamic(w);
    return w.take();
}

std::expected<World, Error> World::deserialize(std::span<const std::byte> bytes) {
    detail::ByteReader r(bytes);

    for (const std::uint8_t expected : kMagic) {
        if (r.u8() != expected) {
            return fail(ErrorCode::InvalidData, "not a save file (magic mismatch)");
        }
    }
    const std::uint32_t version = r.u32();
    if (!r.ok()) {
        return fail(ErrorCode::Truncated, "save data ends inside the header");
    }
    if (version != kSaveFormatVersion) {
        return fail(ErrorCode::UnsupportedVersion,
                    std::format("save format version {} is not supported (expected {})", version, kSaveFormatVersion));
    }

    Params params;
    Scenario scenario;
    read_params(r, params);
    if (r.ok()) {
        read_scenario(r, scenario);
    }
    if (!r.ok()) {
        return fail(ErrorCode::Truncated, "save data ends inside the static inputs");
    }

    auto world = World::create(std::move(params), std::move(scenario));
    if (!world) {
        return std::unexpected(std::move(world.error()));
    }
    if (auto loaded = world->read_dynamic(r); !loaded) {
        return std::unexpected(std::move(loaded.error()));
    }
    if (!r.at_end()) {
        return fail(ErrorCode::InvalidData, std::format("{} unexpected trailing bytes in save data", r.remaining()));
    }
    return world;
}

std::uint64_t World::state_hash() const {
    detail::ByteWriter w;
    write_dynamic(w);
    return fnv1a64(w.bytes());
}

}  // namespace sim
