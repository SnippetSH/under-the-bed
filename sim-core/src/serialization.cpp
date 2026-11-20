// 세이브 형식 (리틀 엔디언)
//
//   magic "FGSV" | format_version u32
//   [정적 입력] params, scenario (토양 초기값, 기상 표 포함)
//   [동적 상태] tick, next_command_seq, house(하우스 상태), biology_rng, disease_rng(M5a), foliar_rng(M5b),
//               pending_commands,
//               tiles(토양 셀 + 작물 + 화방·과실·수확 기록), ledger
//
// state_hash()는 동적 상태 부분만 해시해요. 이벤트는 저장하지 않아요.
// 필드를 추가하면 kSaveFormatVersion을 올려요 (M0 = 1, M1 = 2, M2 = 3, 뿌리 고사 = 4, M3 = 5, M4 = 6, M5a = 7, M5b = 8).

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
constexpr std::uint32_t kSaveFormatVersion = 8;

constexpr std::size_t kWeatherHourBytes = 24;       // f64 × 3
constexpr std::size_t kCurvePointBytes = 16;        // f64 × 2
constexpr std::size_t kQueuedCommandMinBytes = 12;  // seq u64 + variant index u32
constexpr std::size_t kMinStringBytes = 8;          // 길이 u64
constexpr std::size_t kTrussBytes = 73;             // u64 + f64 x 2 + u8 + 진단(u64 x 5 + f64)
constexpr std::size_t kFruitBytes = 106;            // u32 x 2 + u64 + u8 x 2 + f64 x 11
constexpr std::size_t kF64Bytes = 8;
constexpr std::size_t kWindowLayerBytes = 16;  // f64 x 2 (왼쪽·오른쪽 개방률)

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

void write_f64_list(detail::ByteWriter& w, const std::vector<double>& v) {
    w.u64(v.size());
    for (const double x : v) {
        w.f64(x);
    }
}

void read_f64_list(detail::ByteReader& r, std::vector<double>& v) {
    const std::uint64_t n = r.u64();
    if (!r.ok() || n > r.remaining() / kF64Bytes) {
        r.fail();
        return;
    }
    v.resize(static_cast<std::size_t>(n));
    for (double& x : v) {
        x = r.f64();
    }
}

void write_indices(detail::ByteWriter& w, const MicrobeIndices& v) {
    for (const double x : v) {
        w.f64(x);
    }
}

void read_indices(detail::ByteReader& r, MicrobeIndices& v) {
    for (double& x : v) {
        x = r.f64();
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
    // M5a
    write_indices(w, c.microbe_index);
    w.i32(c.mulch_def_index);
    write_f64_list(w, c.fumigant_residue_index);
    write_indices(w, c.disinfection_kill_index);
    w.f64(c.deep_soil_temp_degC);  // M5a 프로토타입 3차
    w.u8(c.overhead_irrigated_this_tick ? 1 : 0);  // M5b
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
    // M5a (멀칭 인덱스·잔류 개수는 read_dynamic이 params와 맞춰 검사)
    read_indices(r, c.microbe_index);
    c.mulch_def_index = r.i32();
    read_f64_list(r, c.fumigant_residue_index);
    read_indices(r, c.disinfection_kill_index);
    c.deep_soil_temp_degC = r.f64();  // M5a 프로토타입 3차
    const std::uint8_t overhead = r.u8();  // M5b
    if (overhead > 1) {
        r.fail();
    }
    c.overhead_irrigated_this_tick = overhead == 1;
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

    // M4
    w.f64(c.crown_g_per_m2);
    w.f64(c.plants_per_m2);
    w.f64(c.establishment_index);
    w.f64(c.induction_index);
    w.f64(c.last_gross_g_per_m2);
    w.f64(c.last_respiration_g_per_m2);
    w.f64(c.last_supply_ratio);
    w.f64(c.size_satisfaction_mean);
    w.f64(c.reserve_g_per_m2);
    w.f64(c.rgr_mean_per_day);
    w.u64(c.trusses.size());
    for (const Truss& t : c.trusses) {
        w.u64(t.initiated_tick);
        w.f64(t.development_index);
        w.f64(t.flowers_per_m2);
        w.u8(t.opened ? 1 : 0);
        w.u64(t.opened_tick);
        w.u64(t.harvested_fruits);
        w.f64(t.harvested_fresh_g);
        w.u64(t.aborted_fruits);
        w.u64(t.first_harvest_tick);
        w.u64(t.last_harvest_tick);
    }
    w.u64(c.fruits.size());
    for (const Fruit& f : c.fruits) {
        w.u32(f.truss_index);
        w.u32(f.rank);
        w.u64(f.anthesis_tick);
        w.u8(f.set ? 1 : 0);
        w.f64(f.pollination_index);
        w.u8(f.deformed ? 1 : 0);
        w.f64(f.potential_fresh_g);
        w.f64(f.dry_g);
        w.f64(f.development_index);
        w.f64(f.brix_factor_sum);
        w.f64(f.firmness_factor_sum);
        w.f64(f.weight_sum);
        w.f64(f.day_score_sum);
        w.f64(f.day_weight_sum);
        w.f64(f.night_score_sum);
        w.f64(f.night_weight_sum);
    }
    const HarvestRecord& h = c.harvest;
    w.u64(h.harvested_fruits);
    w.f64(h.harvested_fresh_g);
    w.u64(h.harvested_deformed);
    w.f64(h.harvested_deformed_fresh_g);
    w.f64(h.harvested_brix_weighted);
    w.f64(h.harvested_firmness_weighted);
    w.u64(h.harvested_size_class_counts.size());
    for (const std::uint64_t n : h.harvested_size_class_counts) {
        w.u64(n);
    }
    w.u8(h.has_first_harvest ? 1 : 0);
    w.u64(h.first_harvest_tick);
    w.u64(h.aborted_fruits);
    w.u64(h.thinned_fruits);
    w.f64(h.harvested_day_score_weighted);
    w.f64(h.harvested_night_score_weighted);
    w.f64(h.harvested_temp_score_weighted);
    // M5a
    for (const double x : c.infection_index) {
        w.f64(x);
    }
    for (const bool reported : c.disease_onset_reported) {
        w.u8(reported ? 1 : 0);
    }
    w.f64(c.disease_susceptibility_factor);  // M5a 프로토타입 3차
    // M5b 잎 젖음 (house-humidity 2장)
    w.f64(c.leaf_wetness_index);
    w.f64(c.wet_run_hours);
    w.f64(c.dry_run_hours);
    w.f64(c.canopy_rh_index);
    // M5b 지상부 병해충 (foliar-disease 2장)
    w.f64(c.infection_anthracnose_index);
    w.f64(c.infection_botrytis_index);
    w.f64(c.infection_powdery_mildew_index);
    w.f64(c.spider_mite_index);
    w.f64(c.spider_mite_mean_index);
    w.f64(c.predator_mite_index);
    w.f64(c.soil_mineral_N_mean_g_per_m2);
    for (const bool reported : c.foliar_onset_reported) {
        w.u8(reported ? 1 : 0);
    }
    w.f64(c.last_anthracnose_increase_index);
    w.f64(c.last_botrytis_increase_index);
    w.f64(c.last_powdery_mildew_increase_index);
    w.f64(c.last_mite_predation_index);
    w.f64(c.last_fruit_rot_count);
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

    // M4
    c.crown_g_per_m2 = r.f64();
    c.plants_per_m2 = r.f64();
    c.establishment_index = r.f64();
    c.induction_index = r.f64();
    c.last_gross_g_per_m2 = r.f64();
    c.last_respiration_g_per_m2 = r.f64();
    c.last_supply_ratio = r.f64();
    c.size_satisfaction_mean = r.f64();
    c.reserve_g_per_m2 = r.f64();
    c.rgr_mean_per_day = r.f64();

    const std::uint64_t truss_count = r.u64();
    if (!r.ok() || truss_count > r.remaining() / kTrussBytes) {
        r.fail();
        return;
    }
    c.trusses.assign(static_cast<std::size_t>(truss_count), Truss{});
    for (Truss& t : c.trusses) {
        t.initiated_tick = r.u64();
        t.development_index = r.f64();
        t.flowers_per_m2 = r.f64();
        const std::uint8_t opened = r.u8();
        if (opened > 1) {
            r.fail();
        }
        t.opened = opened == 1;
        t.opened_tick = r.u64();
        t.harvested_fruits = r.u64();
        t.harvested_fresh_g = r.f64();
        t.aborted_fruits = r.u64();
        t.first_harvest_tick = r.u64();
        t.last_harvest_tick = r.u64();
    }

    const std::uint64_t fruit_count = r.u64();
    if (!r.ok() || fruit_count > r.remaining() / kFruitBytes) {
        r.fail();
        return;
    }
    c.fruits.assign(static_cast<std::size_t>(fruit_count), Fruit{});
    for (Fruit& f : c.fruits) {
        f.truss_index = r.u32();
        f.rank = r.u32();
        f.anthesis_tick = r.u64();
        const std::uint8_t set = r.u8();
        f.pollination_index = r.f64();
        const std::uint8_t deformed = r.u8();
        if (set > 1 || deformed > 1) {
            r.fail();
        }
        f.set = set == 1;
        f.deformed = deformed == 1;
        f.potential_fresh_g = r.f64();
        f.dry_g = r.f64();
        f.development_index = r.f64();
        f.brix_factor_sum = r.f64();
        f.firmness_factor_sum = r.f64();
        f.weight_sum = r.f64();
        f.day_score_sum = r.f64();
        f.day_weight_sum = r.f64();
        f.night_score_sum = r.f64();
        f.night_weight_sum = r.f64();
    }

    HarvestRecord& h = c.harvest;
    h.harvested_fruits = r.u64();
    h.harvested_fresh_g = r.f64();
    h.harvested_deformed = r.u64();
    h.harvested_deformed_fresh_g = r.f64();
    h.harvested_brix_weighted = r.f64();
    h.harvested_firmness_weighted = r.f64();
    const std::uint64_t class_count = r.u64();
    if (!r.ok() || class_count > r.remaining() / kF64Bytes) {
        r.fail();
        return;
    }
    h.harvested_size_class_counts.assign(static_cast<std::size_t>(class_count), 0);
    for (std::uint64_t& n : h.harvested_size_class_counts) {
        n = r.u64();
    }
    const std::uint8_t has_first = r.u8();
    if (has_first > 1) {
        r.fail();
    }
    h.has_first_harvest = has_first == 1;
    h.first_harvest_tick = r.u64();
    h.aborted_fruits = r.u64();
    h.thinned_fruits = r.u64();
    h.harvested_day_score_weighted = r.f64();
    h.harvested_night_score_weighted = r.f64();
    h.harvested_temp_score_weighted = r.f64();
    // M5a
    for (double& x : c.infection_index) {
        x = r.f64();
    }
    for (bool& reported : c.disease_onset_reported) {
        const std::uint8_t flag = r.u8();
        if (flag > 1) {
            r.fail();
        }
        reported = flag == 1;
    }
    c.disease_susceptibility_factor = r.f64();  // M5a 프로토타입 3차
    // M5b 잎 젖음 (house-humidity 2장)
    c.leaf_wetness_index = r.f64();
    c.wet_run_hours = r.f64();
    c.dry_run_hours = r.f64();
    c.canopy_rh_index = r.f64();
    // M5b 지상부 병해충
    c.infection_anthracnose_index = r.f64();
    c.infection_botrytis_index = r.f64();
    c.infection_powdery_mildew_index = r.f64();
    c.spider_mite_index = r.f64();
    c.spider_mite_mean_index = r.f64();
    c.predator_mite_index = r.f64();
    c.soil_mineral_N_mean_g_per_m2 = r.f64();
    for (bool& reported : c.foliar_onset_reported) {
        const std::uint8_t flag = r.u8();
        if (flag > 1) {
            r.fail();
        }
        reported = flag == 1;
    }
    c.last_anthracnose_increase_index = r.f64();
    c.last_botrytis_increase_index = r.f64();
    c.last_powdery_mildew_increase_index = r.f64();
    c.last_mite_predation_index = r.f64();
    c.last_fruit_rot_count = r.f64();
}

// 세이브의 작물 구조 검사: 과위·화방 인덱스가 범위 안이고, 꽃(set = false)은 건물이 0이고,
// 크기 구간 개수가 params와 맞는지. 시뮬레이션이 이 불변 조건에 기대어 인덱스를 써요.
bool crop_structure_is_valid(const Crop& crop, const Params& params) {
    if (!crop.present) {
        return crop.trusses.empty() && crop.fruits.empty();
    }
    if (crop.harvest.harvested_size_class_counts.size() != params.fruit.size_class_bounds_g.size() + 1) {
        return false;
    }
    const std::size_t ranks = params.fruit.rank_fraction.size();
    for (const Fruit& fruit : crop.fruits) {
        if (fruit.rank >= ranks || fruit.truss_index >= crop.trusses.size()) {
            return false;
        }
        if (!fruit.set && fruit.dry_g != 0.0) {
            return false;
        }
    }
    return true;
}

bool is_window_open_level(const HouseParams& params, double open_fraction) {
    for (const double level : params.window_open_levels) {
        if (open_fraction == level) {
            return true;
        }
    }
    return false;
}

// ---- 정적 입력 ----

void write_params(detail::ByteWriter& w, const Params& p) {
    w.i64(p.schema_version);
    w.f64(p.soil_water.evap_mm_per_hour_per_W_per_m2);
    w.f64(p.soil_water.evap_base_mm_per_hour);
    write_curve(w, p.soil_water.evap_moisture_curve);
    w.f64(p.soil_water.drainage_rate_per_hour);
    write_curve(w, p.soil_water.drainage_compaction_curve);
    w.f64(p.soil_temperature.relax_rate_per_hour);
    w.f64(p.soil_temperature.heat_degC_per_g_C);
    w.f64(p.soil_temperature.evap_cooling_degC_per_mm);
    w.f64(p.soil_temperature.deep_coupling_fraction);
    w.f64(p.soil_temperature.deep_relax_rate_per_hour);
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

    // ---- M4 (append) ----
    const HouseParams& h = p.house;
    w.f64(h.layer_transmittance_factor);
    w.f64(h.layer_resistance_m2K_per_W);
    w.f64(h.infiltration_W_per_m2_per_K);
    w.f64(h.window_bypass_fraction);
    w.f64(h.vent_conductance_W_per_m2_per_K);
    w.f64(h.solar_heat_gain_fraction);
    w.f64(h.ground_heat_W_per_m2);
    w.f64(h.air_relax_rate_per_hour);
    w.f64(h.heater_capacity_W_per_m2);
    w.f64(h.heater_setpoint_min_degC);
    w.f64(h.heater_setpoint_max_degC);
    w.f64(h.temp_mean_relax_rate_per_hour);
    w.i64(h.cover_change_window_doy[0]);
    w.i64(h.cover_change_window_doy[1]);
    w.i64(h.min_cover_layers);
    w.i64(h.max_cover_layers);
    write_f64_list(w, h.outside_co2_ppm_by_hour);
    write_f64_list(w, h.window_open_levels);
    w.f64(h.air_volume_m3_per_m2);
    w.f64(h.co2_mg_per_m3_per_ppm);
    w.f64(h.infiltration_air_changes_per_hour);
    w.f64(h.vent_air_changes_per_hour);
    w.f64(h.co2_floor_ppm);

    write_curve(w, p.calendar.day_length_curve);

    const PhenologyParams& ph = p.phenology;
    w.f64(ph.establishment_rate_per_hour);
    write_curve(w, ph.establishment_temp_curve);
    write_curve(w, ph.establishment_growth_curve);
    w.f64(ph.induction_rate_per_hour);
    write_curve(w, ph.induction_day_length_curve);
    write_curve(w, ph.induction_temp_curve);
    write_curve(w, ph.induction_nitrogen_curve);
    w.f64(ph.truss_base_temp_degC);
    w.f64(ph.truss_degree_days_to_anthesis);
    w.f64(ph.induction_spread_index);
    write_curve(w, ph.flowers_per_truss_curve);

    const FruitParams& fr = p.fruit;
    write_f64_list(w, fr.rank_fraction);
    write_f64_list(w, fr.rank_anthesis_delay_days);
    w.f64(fr.anthesis_jitter_days);
    write_f64_list(w, fr.rank_potential_fresh_g);
    w.f64(fr.size_variation_fraction);
    write_curve(w, fr.pollination_mean_curve);
    w.f64(fr.pollination_spread);
    w.f64(fr.set_threshold);
    w.f64(fr.deformed_threshold);
    write_curve(w, fr.pollination_size_curve);
    w.f64(fr.dry_matter_fraction);
    w.f64(fr.base_temp_degC);
    w.f64(fr.degree_days_to_harvest);
    write_curve(w, fr.growth_shape_curve);
    write_f64_list(w, fr.size_class_bounds_g);
    w.f64(fr.base_brix);
    write_curve(w, fr.brix_supply_curve);
    write_curve(w, fr.brix_k_curve);
    write_curve(w, fr.brix_n_excess_curve);
    write_curve(w, fr.brix_water_curve);
    write_curve(w, fr.firmness_ca_curve);
    write_curve(w, fr.firmness_temp_curve);
    write_curve(w, fr.firmness_n_excess_curve);
    w.f64(fr.abortion_rate_per_development_index);
    w.f64(fr.abortion_window_index);
    write_curve(w, fr.abortion_satisfaction_curve);
    write_f64_list(w, fr.rank_abortion_weight);
    w.f64(fr.thinnable_max_index);
    write_curve(w, fr.brix_day_temp_score_curve);
    write_curve(w, fr.brix_night_temp_score_curve);
    w.f64(fr.brix_day_weight);
    w.f64(fr.brix_night_weight);
    write_curve(w, fr.brix_temp_score_curve);
    w.f64(fr.day_solar_threshold_W_per_m2);

    const DenitrificationParams& dn = p.denitrification;
    w.f64(dn.rate_per_hour);
    write_curve(w, dn.o2_curve);
    write_curve(w, dn.temp_curve);
    write_curve(w, dn.carbon_curve);
    w.f64(dn.H_eq_per_eq_N);

    const CropParams& c4 = p.crop;
    w.f64(c4.vegetative_leaf_fraction);
    w.f64(c4.vegetative_crown_fraction);
    w.f64(c4.vegetative_root_fraction);
    w.f64(c4.max_fruit_assimilate_fraction);
    w.f64(c4.maintenance_rate_leaf_per_hour);
    w.f64(c4.maintenance_rate_crown_per_hour);
    w.f64(c4.maintenance_rate_root_per_hour);
    w.f64(c4.maintenance_rate_fruit_per_hour);
    write_curve(w, c4.maintenance_temp_curve);
    w.f64(c4.leaf_senescence_rate_per_hour);
    w.f64(c4.fruit_target_N_g_per_g);
    w.f64(c4.fruit_target_P_g_per_g);
    w.f64(c4.fruit_target_K_g_per_g);
    w.f64(c4.fruit_target_Ca_g_per_g);
    w.f64(c4.fruit_target_Mg_g_per_g);
    w.f64(c4.fruit_target_Mn_g_per_g);
    w.f64(c4.co2_g_per_g_dry_matter);
    write_curve(w, c4.co2_growth_curve);
    write_curve(w, c4.cold_leaf_damage_curve);
    write_curve(w, c4.cold_fruit_loss_curve);
    w.f64(c4.n_luxury_max_ratio);
    write_curve(w, c4.n_luxury_uptake_curve);
    write_curve(w, c4.mn_uptake_regulation_curve);
    write_curve(w, c4.uptake_temp_curve);
    w.f64(c4.n_uptake_rate_per_g_root_per_hour);
    w.f64(c4.reserve_capacity_fraction);
    w.f64(c4.reserve_fill_fraction);
    w.f64(c4.reserve_release_rate_per_hour);
    write_curve(w, c4.maintenance_activity_curve);
    w.f64(c4.rgr_mean_relax_rate_per_hour);

    w.u64(p.seedling_grades.size());
    for (const SeedlingGradeDef& g : p.seedling_grades) {
        w.str(g.id);
        w.f64(g.leaf_g_per_plant);
        w.f64(g.crown_g_per_plant);
        w.f64(g.root_g_per_plant);
        w.f64(g.initial_induction_index);
    }

    // ---- M5a (append) ----
    write_curve(w, p.root_damage.fusarium_curve);
    write_curve(w, p.root_damage.phytophthora_curve);
    write_curve(w, p.root_damage.fumigant_curve);
    const MicrobesParams& m = p.microbes;
    w.f64(m.capacity_floor_index);
    write_curve(w, m.decomposer_activity_curve);
    write_curve(w, m.aob_activity_curve);
    write_curve(w, m.nob_activity_curve);
    w.f64(m.antagonist_protection_factor);
    w.f64(m.mycorrhiza_p_release_factor);
    w.f64(m.mycorrhiza_water_relief_factor);
    w.f64(m.mycorrhiza_salt_relief_factor);
    w.f64(m.pathogen_spread_index);
    w.f64(m.susceptibility_spread_index);
    write_curve(w, m.moist_heat_curve);
    write_curve(w, m.decomposer_food_curve);
    write_curve(w, m.antagonist_food_curve);
    write_curve(w, m.mycorrhiza_host_curve);
    write_curve(w, m.mycorrhiza_p_curve);
    for (const MicrobeGroupParams& gp : m.groups) {
        w.f64(gp.growth_rate_per_hour);
        write_curve(w, gp.temp_curve);
        write_curve(w, gp.moisture_curve);
        write_curve(w, gp.ph_curve);
        write_curve(w, gp.o2_curve);
        w.f64(gp.base_death_rate_per_hour);
        write_curve(w, gp.heat_kill_curve);
        w.f64(gp.immigration_rate_per_hour);
        write_indices(w, gp.competition);
        w.f64(gp.saprophytic_capacity_index);
        w.f64(gp.host_capacity_index);
    }
    for (const SoilDiseaseParams& dp : p.soil_disease) {
        w.f64(dp.infection_rate_per_hour);
        write_curve(w, dp.infection_env_curve_temp);
        write_curve(w, dp.infection_env_curve_o2);
        write_curve(w, dp.infection_env_curve_ph);
        w.f64(dp.onset_infection_index);
    }
    w.f64(p.disinfection.flush_N_g_per_m2_per_index);
    w.f64(p.disinfection.flush_C_to_N_ratio);
    w.f64(p.disinfection.mn_reduction_per_index);
    w.f64(p.disinfection.till_residue_release_fraction);
    // M5b [humidity]
    {
        const HumidityParams& hum = p.humidity;
        write_curve(w, hum.saturation_vapor_curve);
        w.f64(hum.inner_surface_resistance_m2K_per_W);
        w.f64(hum.condensation_rate_per_hour);
        w.f64(hum.rh_mean_relax_rate_per_hour);
        w.f64(hum.vent_airflow_factor);
        w.f64(hum.fan_airflow_index);
        w.f64(hum.canopy_rh_boost_index);
        w.f64(hum.canopy_lai_ref);
        w.f64(hum.dew_rh_index);
        w.f64(hum.dew_solar_limit_W_per_m2);
        w.f64(hum.wetting_rate_per_hour);
        w.f64(hum.drying_rate_per_hour);
        w.f64(hum.airflow_drying_factor);
        write_curve(w, hum.drying_rh_curve);
        w.f64(hum.drying_solar_factor_per_W_per_m2);
        w.f64(hum.overhead_wet_index);
        w.f64(hum.wet_threshold_index);
        w.f64(hum.wetness_break_hours);
    }
    w.u64(p.mulches.size());
    for (const MulchDef& d : p.mulches) {
        w.str(d.id);
        w.str(d.name);
        w.f64(d.soil_solar_gain_degC_per_W_per_m2);
        w.f64(d.evaporation_factor);
        w.f64(d.fumigant_retention_factor);
        w.f64(d.canopy_reflect_factor);
    }
    w.u64(p.disinfectants.size());
    for (const DisinfectantDef& d : p.disinfectants) {
        w.str(d.id);
        w.str(d.name);
        w.f64(d.residue_index_per_g_per_m2);
        write_indices(w, d.kill_rate_per_hour);
        w.f64(d.decay_rate_per_hour);
        write_curve(w, d.decay_temp_curve);
        write_curve(w, d.activation_moisture_curve);
    }
    w.u64(p.inoculants.size());
    for (const InoculantDef& d : p.inoculants) {
        w.str(d.id);
        w.str(d.name);
        write_indices(w, d.index_gain_per_g_per_m2);
        w.f64(d.C_g_per_kg);
        w.f64(d.N_g_per_kg);
    }
    // ---- M5b 지상부 병해충 (append, foliar-disease 8장) ----
    write_curve(w, p.root_damage.anthracnose_curve);
    for (const SeedlingGradeDef& g : p.seedling_grades) {
        w.f64(g.latent_anthracnose_fraction);
        w.f64(g.latent_anthracnose_index);
        w.f64(g.spider_mite_fraction);
        w.f64(g.spider_mite_start_index);
        w.f64(g.powdery_mildew_start_index);
    }
    for (const MulchDef& d : p.mulches) {
        w.f64(d.soil_splash_factor);
        w.f64(d.plant_splash_factor);
    }
    const FoliarDiseaseParams& fd = p.foliar_disease;
    write_curve(w, fd.n_susceptibility_curve);
    w.f64(fd.n_mean_relax_rate_per_hour);
    w.f64(fd.mite_mean_relax_rate_per_hour);
    const AnthracnoseParams& an = fd.anthracnose;
    write_curve(w, an.temp_curve);
    write_curve(w, an.wetness_curve);
    w.f64(an.contact_rate_per_hour);
    w.f64(an.progress_rate_per_hour);
    w.f64(an.splash_transfer_index);
    write_curve(w, an.sporulation_curve);
    w.f64(an.onset_infection_index);
    w.f64(an.soil_deposit_per_hour);
    w.f64(an.residue_index);
    const BotrytisParams& bo = fd.botrytis;
    write_curve(w, bo.infection_temp_curve);
    write_curve(w, bo.wetness_curve);
    w.f64(bo.infection_rate_per_hour);
    w.f64(bo.infection_decay_rate_per_hour);
    write_curve(w, bo.sporulation_temp_curve);
    w.f64(bo.spore_source_per_infection_per_hour);
    w.f64(bo.spore_source_per_rotten_fruit_per_hour);
    w.f64(bo.spore_decay_rate_per_hour);
    w.f64(bo.spore_vent_fraction);
    w.f64(bo.fruit_rot_prob_per_hour);
    write_curve(w, bo.fruit_stage_curve);
    w.f64(bo.onset_infection_index);
    const PowderyMildewParams& pm = fd.powdery_mildew;
    write_curve(w, pm.temp_curve);
    write_curve(w, pm.rh_curve);
    w.f64(pm.free_water_inhibition_factor);
    write_curve(w, pm.light_curve);
    write_curve(w, pm.density_curve);
    write_curve(w, pm.young_leaf_curve);
    w.f64(pm.infection_rate_per_hour);
    w.f64(pm.spore_source_per_infection_per_hour);
    w.f64(pm.spore_decay_rate_per_hour);
    w.f64(pm.spore_vent_fraction);
    write_curve(w, pm.photosynthesis_damage_curve);
    write_curve(w, pm.fruit_prob_curve);
    w.f64(pm.onset_infection_index);
    const MiteParams& mi = fd.mites;
    write_curve(w, mi.mite_growth_curve_per_hour);
    write_curve(w, mi.mite_rh_curve);
    w.f64(mi.mite_spread_rate_per_hour);
    write_curve(w, mi.photosynthesis_damage_curve);
    write_curve(w, mi.predator_temp_curve);
    write_curve(w, mi.predator_hatch_curve);
    w.f64(mi.attack_rate_per_hour);
    w.f64(mi.handling_time_hours);
    w.f64(mi.conversion_factor);
    w.f64(mi.predator_death_rate_per_hour);
    w.f64(mi.predator_starvation_death_per_hour);
    write_curve(w, mi.predator_death_temp_curve);
    w.f64(mi.predator_search_rate_per_hour);
    w.f64(mi.mite_onset_index);
    w.u64(p.predators.size());
    for (const PredatorDef& d : p.predators) {
        w.str(d.id);
        w.str(d.name);
        w.f64(d.index_per_individual_per_m2);
    }
}

void read_params(detail::ByteReader& r, Params& p) {
    p.schema_version = r.i64();
    p.soil_water.evap_mm_per_hour_per_W_per_m2 = r.f64();
    p.soil_water.evap_base_mm_per_hour = r.f64();
    read_curve(r, p.soil_water.evap_moisture_curve);
    p.soil_water.drainage_rate_per_hour = r.f64();
    read_curve(r, p.soil_water.drainage_compaction_curve);
    p.soil_temperature.relax_rate_per_hour = r.f64();
    p.soil_temperature.heat_degC_per_g_C = r.f64();
    p.soil_temperature.evap_cooling_degC_per_mm = r.f64();
    p.soil_temperature.deep_coupling_fraction = r.f64();
    p.soil_temperature.deep_relax_rate_per_hour = r.f64();
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

    // ---- M4 (append) ----
    HouseParams& h = p.house;
    h.layer_transmittance_factor = r.f64();
    h.layer_resistance_m2K_per_W = r.f64();
    h.infiltration_W_per_m2_per_K = r.f64();
    h.window_bypass_fraction = r.f64();
    h.vent_conductance_W_per_m2_per_K = r.f64();
    h.solar_heat_gain_fraction = r.f64();
    h.ground_heat_W_per_m2 = r.f64();
    h.air_relax_rate_per_hour = r.f64();
    h.heater_capacity_W_per_m2 = r.f64();
    h.heater_setpoint_min_degC = r.f64();
    h.heater_setpoint_max_degC = r.f64();
    h.temp_mean_relax_rate_per_hour = r.f64();
    h.cover_change_window_doy[0] = r.i64();
    h.cover_change_window_doy[1] = r.i64();
    h.min_cover_layers = r.i64();
    h.max_cover_layers = r.i64();
    read_f64_list(r, h.outside_co2_ppm_by_hour);
    read_f64_list(r, h.window_open_levels);
    h.air_volume_m3_per_m2 = r.f64();
    h.co2_mg_per_m3_per_ppm = r.f64();
    h.infiltration_air_changes_per_hour = r.f64();
    h.vent_air_changes_per_hour = r.f64();
    h.co2_floor_ppm = r.f64();

    read_curve(r, p.calendar.day_length_curve);

    PhenologyParams& ph = p.phenology;
    ph.establishment_rate_per_hour = r.f64();
    read_curve(r, ph.establishment_temp_curve);
    read_curve(r, ph.establishment_growth_curve);
    ph.induction_rate_per_hour = r.f64();
    read_curve(r, ph.induction_day_length_curve);
    read_curve(r, ph.induction_temp_curve);
    read_curve(r, ph.induction_nitrogen_curve);
    ph.truss_base_temp_degC = r.f64();
    ph.truss_degree_days_to_anthesis = r.f64();
    ph.induction_spread_index = r.f64();
    read_curve(r, ph.flowers_per_truss_curve);

    FruitParams& fr = p.fruit;
    read_f64_list(r, fr.rank_fraction);
    read_f64_list(r, fr.rank_anthesis_delay_days);
    fr.anthesis_jitter_days = r.f64();
    read_f64_list(r, fr.rank_potential_fresh_g);
    fr.size_variation_fraction = r.f64();
    read_curve(r, fr.pollination_mean_curve);
    fr.pollination_spread = r.f64();
    fr.set_threshold = r.f64();
    fr.deformed_threshold = r.f64();
    read_curve(r, fr.pollination_size_curve);
    fr.dry_matter_fraction = r.f64();
    fr.base_temp_degC = r.f64();
    fr.degree_days_to_harvest = r.f64();
    read_curve(r, fr.growth_shape_curve);
    read_f64_list(r, fr.size_class_bounds_g);
    fr.base_brix = r.f64();
    read_curve(r, fr.brix_supply_curve);
    read_curve(r, fr.brix_k_curve);
    read_curve(r, fr.brix_n_excess_curve);
    read_curve(r, fr.brix_water_curve);
    read_curve(r, fr.firmness_ca_curve);
    read_curve(r, fr.firmness_temp_curve);
    read_curve(r, fr.firmness_n_excess_curve);
    fr.abortion_rate_per_development_index = r.f64();
    fr.abortion_window_index = r.f64();
    read_curve(r, fr.abortion_satisfaction_curve);
    read_f64_list(r, fr.rank_abortion_weight);
    fr.thinnable_max_index = r.f64();
    read_curve(r, fr.brix_day_temp_score_curve);
    read_curve(r, fr.brix_night_temp_score_curve);
    fr.brix_day_weight = r.f64();
    fr.brix_night_weight = r.f64();
    read_curve(r, fr.brix_temp_score_curve);
    fr.day_solar_threshold_W_per_m2 = r.f64();

    DenitrificationParams& dn = p.denitrification;
    dn.rate_per_hour = r.f64();
    read_curve(r, dn.o2_curve);
    read_curve(r, dn.temp_curve);
    read_curve(r, dn.carbon_curve);
    dn.H_eq_per_eq_N = r.f64();

    CropParams& c4 = p.crop;
    c4.vegetative_leaf_fraction = r.f64();
    c4.vegetative_crown_fraction = r.f64();
    c4.vegetative_root_fraction = r.f64();
    c4.max_fruit_assimilate_fraction = r.f64();
    c4.maintenance_rate_leaf_per_hour = r.f64();
    c4.maintenance_rate_crown_per_hour = r.f64();
    c4.maintenance_rate_root_per_hour = r.f64();
    c4.maintenance_rate_fruit_per_hour = r.f64();
    read_curve(r, c4.maintenance_temp_curve);
    c4.leaf_senescence_rate_per_hour = r.f64();
    c4.fruit_target_N_g_per_g = r.f64();
    c4.fruit_target_P_g_per_g = r.f64();
    c4.fruit_target_K_g_per_g = r.f64();
    c4.fruit_target_Ca_g_per_g = r.f64();
    c4.fruit_target_Mg_g_per_g = r.f64();
    c4.fruit_target_Mn_g_per_g = r.f64();
    c4.co2_g_per_g_dry_matter = r.f64();
    read_curve(r, c4.co2_growth_curve);
    read_curve(r, c4.cold_leaf_damage_curve);
    read_curve(r, c4.cold_fruit_loss_curve);
    c4.n_luxury_max_ratio = r.f64();
    read_curve(r, c4.n_luxury_uptake_curve);
    read_curve(r, c4.mn_uptake_regulation_curve);
    read_curve(r, c4.uptake_temp_curve);
    c4.n_uptake_rate_per_g_root_per_hour = r.f64();
    c4.reserve_capacity_fraction = r.f64();
    c4.reserve_fill_fraction = r.f64();
    c4.reserve_release_rate_per_hour = r.f64();
    read_curve(r, c4.maintenance_activity_curve);
    c4.rgr_mean_relax_rate_per_hour = r.f64();

    const std::uint64_t grade_count = r.u64();
    if (!r.ok() || grade_count > r.remaining() / (kMinStringBytes + 4 * kF64Bytes)) {
        r.fail();
        return;
    }
    p.seedling_grades.resize(static_cast<std::size_t>(grade_count));
    for (SeedlingGradeDef& g : p.seedling_grades) {
        g.id = r.str();
        g.leaf_g_per_plant = r.f64();
        g.crown_g_per_plant = r.f64();
        g.root_g_per_plant = r.f64();
        g.initial_induction_index = r.f64();
    }

    // ---- M5a (append) ----
    read_curve(r, p.root_damage.fusarium_curve);
    read_curve(r, p.root_damage.phytophthora_curve);
    read_curve(r, p.root_damage.fumigant_curve);
    MicrobesParams& m = p.microbes;
    m.capacity_floor_index = r.f64();
    read_curve(r, m.decomposer_activity_curve);
    read_curve(r, m.aob_activity_curve);
    read_curve(r, m.nob_activity_curve);
    m.antagonist_protection_factor = r.f64();
    m.mycorrhiza_p_release_factor = r.f64();
    m.mycorrhiza_water_relief_factor = r.f64();
    m.mycorrhiza_salt_relief_factor = r.f64();
    m.pathogen_spread_index = r.f64();
    m.susceptibility_spread_index = r.f64();
    read_curve(r, m.moist_heat_curve);
    read_curve(r, m.decomposer_food_curve);
    read_curve(r, m.antagonist_food_curve);
    read_curve(r, m.mycorrhiza_host_curve);
    read_curve(r, m.mycorrhiza_p_curve);
    for (MicrobeGroupParams& gp : m.groups) {
        gp.growth_rate_per_hour = r.f64();
        read_curve(r, gp.temp_curve);
        read_curve(r, gp.moisture_curve);
        read_curve(r, gp.ph_curve);
        read_curve(r, gp.o2_curve);
        gp.base_death_rate_per_hour = r.f64();
        read_curve(r, gp.heat_kill_curve);
        gp.immigration_rate_per_hour = r.f64();
        read_indices(r, gp.competition);
        gp.saprophytic_capacity_index = r.f64();
        gp.host_capacity_index = r.f64();
    }
    for (SoilDiseaseParams& dp : p.soil_disease) {
        dp.infection_rate_per_hour = r.f64();
        read_curve(r, dp.infection_env_curve_temp);
        read_curve(r, dp.infection_env_curve_o2);
        read_curve(r, dp.infection_env_curve_ph);
        dp.onset_infection_index = r.f64();
    }
    p.disinfection.flush_N_g_per_m2_per_index = r.f64();
    p.disinfection.flush_C_to_N_ratio = r.f64();
    p.disinfection.mn_reduction_per_index = r.f64();
    p.disinfection.till_residue_release_fraction = r.f64();
    // M5b [humidity]
    {
        HumidityParams& hum = p.humidity;
        read_curve(r, hum.saturation_vapor_curve);
        hum.inner_surface_resistance_m2K_per_W = r.f64();
        hum.condensation_rate_per_hour = r.f64();
        hum.rh_mean_relax_rate_per_hour = r.f64();
        hum.vent_airflow_factor = r.f64();
        hum.fan_airflow_index = r.f64();
        hum.canopy_rh_boost_index = r.f64();
        hum.canopy_lai_ref = r.f64();
        hum.dew_rh_index = r.f64();
        hum.dew_solar_limit_W_per_m2 = r.f64();
        hum.wetting_rate_per_hour = r.f64();
        hum.drying_rate_per_hour = r.f64();
        hum.airflow_drying_factor = r.f64();
        read_curve(r, hum.drying_rh_curve);
        hum.drying_solar_factor_per_W_per_m2 = r.f64();
        hum.overhead_wet_index = r.f64();
        hum.wet_threshold_index = r.f64();
        hum.wetness_break_hours = r.f64();
    }
    const std::uint64_t mulch_count = r.u64();
    if (!r.ok() || mulch_count > r.remaining() / (2 * kMinStringBytes + 4 * kF64Bytes)) {
        r.fail();
        return;
    }
    p.mulches.resize(static_cast<std::size_t>(mulch_count));
    for (MulchDef& d : p.mulches) {
        d.id = r.str();
        d.name = r.str();
        d.soil_solar_gain_degC_per_W_per_m2 = r.f64();
        d.evaporation_factor = r.f64();
        d.fumigant_retention_factor = r.f64();
        d.canopy_reflect_factor = r.f64();
    }
    const std::uint64_t disinfectant_count = r.u64();
    if (!r.ok() || disinfectant_count > r.remaining() / (2 * kMinStringBytes + (kMicrobeGroupCount + 2) * kF64Bytes)) {
        r.fail();
        return;
    }
    p.disinfectants.resize(static_cast<std::size_t>(disinfectant_count));
    for (DisinfectantDef& d : p.disinfectants) {
        d.id = r.str();
        d.name = r.str();
        d.residue_index_per_g_per_m2 = r.f64();
        read_indices(r, d.kill_rate_per_hour);
        d.decay_rate_per_hour = r.f64();
        read_curve(r, d.decay_temp_curve);
        read_curve(r, d.activation_moisture_curve);
    }
    const std::uint64_t inoculant_count = r.u64();
    if (!r.ok() || inoculant_count > r.remaining() / (2 * kMinStringBytes + (kMicrobeGroupCount + 2) * kF64Bytes)) {
        r.fail();
        return;
    }
    p.inoculants.resize(static_cast<std::size_t>(inoculant_count));
    for (InoculantDef& d : p.inoculants) {
        d.id = r.str();
        d.name = r.str();
        read_indices(r, d.index_gain_per_g_per_m2);
        d.C_g_per_kg = r.f64();
        d.N_g_per_kg = r.f64();
    }
    // ---- M5b 지상부 병해충 (append) ----
    read_curve(r, p.root_damage.anthracnose_curve);
    for (SeedlingGradeDef& g : p.seedling_grades) {
        g.latent_anthracnose_fraction = r.f64();
        g.latent_anthracnose_index = r.f64();
        g.spider_mite_fraction = r.f64();
        g.spider_mite_start_index = r.f64();
        g.powdery_mildew_start_index = r.f64();
    }
    for (MulchDef& d : p.mulches) {
        d.soil_splash_factor = r.f64();
        d.plant_splash_factor = r.f64();
    }
    FoliarDiseaseParams& fd = p.foliar_disease;
    read_curve(r, fd.n_susceptibility_curve);
    fd.n_mean_relax_rate_per_hour = r.f64();
    fd.mite_mean_relax_rate_per_hour = r.f64();
    AnthracnoseParams& an = fd.anthracnose;
    read_curve(r, an.temp_curve);
    read_curve(r, an.wetness_curve);
    an.contact_rate_per_hour = r.f64();
    an.progress_rate_per_hour = r.f64();
    an.splash_transfer_index = r.f64();
    read_curve(r, an.sporulation_curve);
    an.onset_infection_index = r.f64();
    an.soil_deposit_per_hour = r.f64();
    an.residue_index = r.f64();
    BotrytisParams& bo = fd.botrytis;
    read_curve(r, bo.infection_temp_curve);
    read_curve(r, bo.wetness_curve);
    bo.infection_rate_per_hour = r.f64();
    bo.infection_decay_rate_per_hour = r.f64();
    read_curve(r, bo.sporulation_temp_curve);
    bo.spore_source_per_infection_per_hour = r.f64();
    bo.spore_source_per_rotten_fruit_per_hour = r.f64();
    bo.spore_decay_rate_per_hour = r.f64();
    bo.spore_vent_fraction = r.f64();
    bo.fruit_rot_prob_per_hour = r.f64();
    read_curve(r, bo.fruit_stage_curve);
    bo.onset_infection_index = r.f64();
    PowderyMildewParams& pm = fd.powdery_mildew;
    read_curve(r, pm.temp_curve);
    read_curve(r, pm.rh_curve);
    pm.free_water_inhibition_factor = r.f64();
    read_curve(r, pm.light_curve);
    read_curve(r, pm.density_curve);
    read_curve(r, pm.young_leaf_curve);
    pm.infection_rate_per_hour = r.f64();
    pm.spore_source_per_infection_per_hour = r.f64();
    pm.spore_decay_rate_per_hour = r.f64();
    pm.spore_vent_fraction = r.f64();
    read_curve(r, pm.photosynthesis_damage_curve);
    read_curve(r, pm.fruit_prob_curve);
    pm.onset_infection_index = r.f64();
    MiteParams& mi = fd.mites;
    read_curve(r, mi.mite_growth_curve_per_hour);
    read_curve(r, mi.mite_rh_curve);
    mi.mite_spread_rate_per_hour = r.f64();
    read_curve(r, mi.photosynthesis_damage_curve);
    read_curve(r, mi.predator_temp_curve);
    read_curve(r, mi.predator_hatch_curve);
    mi.attack_rate_per_hour = r.f64();
    mi.handling_time_hours = r.f64();
    mi.conversion_factor = r.f64();
    mi.predator_death_rate_per_hour = r.f64();
    mi.predator_starvation_death_per_hour = r.f64();
    read_curve(r, mi.predator_death_temp_curve);
    mi.predator_search_rate_per_hour = r.f64();
    mi.mite_onset_index = r.f64();
    const std::uint64_t predator_count = r.u64();
    if (!r.ok() || predator_count > r.remaining() / (2 * kMinStringBytes + kF64Bytes)) {
        r.fail();
        return;
    }
    p.predators.resize(static_cast<std::size_t>(predator_count));
    for (PredatorDef& d : p.predators) {
        d.id = r.str();
        d.name = r.str();
        d.index_per_individual_per_m2 = r.f64();
    }
}

void write_scenario(detail::ByteWriter& w, const Scenario& s) {
    w.str(s.name);
    w.u64(s.seed);
    w.i32(s.start_day_of_year);
    w.i32(s.bed_rows);
    w.i32(s.bed_cols);
    w.i32(s.cover_layers);

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
    write_indices(w, soil.microbe_index);  // M5a

    w.u8(s.crop.has_value() ? 1 : 0);
    if (s.crop) {
        w.str(s.crop->seedling_grade);
        w.f64(s.crop->plants_per_m2);
        w.f64(s.crop->initial_establishment_index);
    }
    w.u8(static_cast<std::uint8_t>(s.stress_combination));
    // M5b
    w.u8(s.drip_installed ? 1 : 0);
    w.f64(s.foliar.botrytis_background_index);
    w.f64(s.foliar.powdery_mildew_background_index);

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
    s.cover_layers = r.i32();

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
    read_indices(r, soil.microbe_index);  // M5a

    const std::uint8_t has_crop = r.u8();
    if (has_crop > 1) {
        r.fail();
    }
    if (has_crop == 1) {
        CropInit crop;
        crop.seedling_grade = r.str();
        crop.plants_per_m2 = r.f64();
        crop.initial_establishment_index = r.f64();
        s.crop = crop;
    }
    const std::uint8_t stress_combination = r.u8();
    if (stress_combination > static_cast<std::uint8_t>(StressCombination::Product)) {
        r.fail();
    }
    s.stress_combination = static_cast<StressCombination>(stress_combination);
    // M5b
    const std::uint8_t drip = r.u8();
    if (drip > 1) {
        r.fail();
    }
    s.drip_installed = drip == 1;
    s.foliar.botrytis_background_index = r.f64();
    s.foliar.powdery_mildew_background_index = r.f64();

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
        void operator()(const SetWindowCommand& c) const {
            w.i32(c.layer);
            w.u8(static_cast<std::uint8_t>(c.side));
            w.f64(c.open_fraction);
        }
        void operator()(const SetHeaterCommand& c) const {
            w.u8(c.on ? 1 : 0);
            w.f64(c.setpoint_degC);
        }
        void operator()(const SetCoverLayersCommand& c) const { w.i32(c.layers); }
        void operator()(const ThinFlowersCommand& c) const {
            write_area(w, c.area);
            w.f64(c.per_plant);
        }
        // M5a
        void operator()(const SetMulchCommand& c) const {
            write_area(w, c.area);
            w.str(c.mulch_id);
        }
        void operator()(const ApplyDisinfectantCommand& c) const {
            write_area(w, c.area);
            w.str(c.disinfectant_id);
            w.f64(c.g_per_m2);
        }
        void operator()(const AddInoculantCommand& c) const {
            write_area(w, c.area);
            w.str(c.inoculant_id);
            w.f64(c.g_per_m2);
        }
        void operator()(const PlantCommand& c) const {
            w.str(c.seedling_grade);
            w.f64(c.plants_per_m2);
        }
        // M5b
        void operator()(const SetFanCommand& c) const { w.u8(c.on ? 1 : 0); }
        void operator()(const RemovePlantCommand& c) const { write_area(w, c.area); }
        void operator()(const ReleasePredatorsCommand& c) const {
            write_area(w, c.area);
            w.str(c.predator_id);
            w.f64(c.per_m2);
        }
        void operator()(const InstallDripCommand&) const {}
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
        case 6: {
            SetWindowCommand c;
            c.layer = r.i32();
            const std::uint8_t side = r.u8();
            c.open_fraction = r.f64();
            if (side > static_cast<std::uint8_t>(WindowSide::Right)) {
                return fail(ErrorCode::InvalidData, "saved window command has an invalid side");
            }
            c.side = static_cast<WindowSide>(side);
            return c;
        }
        case 7: {
            SetHeaterCommand c;
            const std::uint8_t on = r.u8();
            if (on > 1) {
                return fail(ErrorCode::InvalidData, "saved heater command has an invalid flag");
            }
            c.on = on == 1;
            c.setpoint_degC = r.f64();
            return c;
        }
        case 8: {
            SetCoverLayersCommand c;
            c.layers = r.i32();
            return c;
        }
        case 9: {
            ThinFlowersCommand c;
            c.area = read_area(r);
            c.per_plant = r.f64();
            return c;
        }
        // M5a
        case 10: {
            SetMulchCommand c;
            c.area = read_area(r);
            c.mulch_id = r.str();
            return c;
        }
        case 11: {
            ApplyDisinfectantCommand c;
            c.area = read_area(r);
            c.disinfectant_id = r.str();
            c.g_per_m2 = r.f64();
            return c;
        }
        case 12: {
            AddInoculantCommand c;
            c.area = read_area(r);
            c.inoculant_id = r.str();
            c.g_per_m2 = r.f64();
            return c;
        }
        case 13: {
            PlantCommand c;
            c.seedling_grade = r.str();
            c.plants_per_m2 = r.f64();
            return c;
        }
        // M5b
        case 14: {
            SetFanCommand c;
            const std::uint8_t on = r.u8();
            if (on > 1) {
                return fail(ErrorCode::InvalidData, "saved fan command has an invalid flag");
            }
            c.on = on == 1;
            return c;
        }
        case 15: {
            RemovePlantCommand c;
            c.area = read_area(r);
            return c;
        }
        case 16: {
            ReleasePredatorsCommand c;
            c.area = read_area(r);
            c.predator_id = r.str();
            c.per_m2 = r.f64();
            return c;
        }
        case 17:
            return InstallDripCommand{};
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

    // 하우스는 명령 검증(cover_layers)이 의존하므로 대기 명령보다 앞에 둬요.
    w.i32(house_.cover_layers);
    w.u64(house_.window_open_fraction.size());
    for (const auto& layer : house_.window_open_fraction) {
        w.f64(layer[0]);
        w.f64(layer[1]);
    }
    w.u8(house_.heater_on ? 1 : 0);
    w.f64(house_.heater_setpoint_degC);
    w.f64(house_.air_temp_degC);
    w.f64(house_.air_temp_mean_degC);
    w.f64(house_.co2_ppm);
    w.f64(house_.heating_energy_MJ_per_m2);
    w.f64(house_.outside_co2_ppm);
    w.f64(house_.inside_solar_W_per_m2);
    w.f64(house_.heating_power_W_per_m2);
    w.f64(house_.vent_path_index);
    // M5b 하우스 습도 (house-humidity 2장): 상태, 장부, 마지막 틱 파생값
    w.f64(house_.vapor_g_per_m3);
    w.f64(house_.rh_mean_index);
    w.u8(house_.fan_on ? 1 : 0);
    w.f64(house_.vapor_source_mm);
    w.f64(house_.water_condensed_mm);
    w.f64(house_.vapor_vented_mm);
    w.f64(house_.vapor_deposited_mm);
    w.f64(house_.vapor_initial_mm);
    w.f64(house_.rh_index);
    w.f64(house_.airflow_index);
    w.f64(house_.cover_inner_temp_degC);
    // M5b 점적·팬 장부, 지상부 병해충 하우스 값
    w.u8(house_.drip_installed ? 1 : 0);
    w.f64(house_.fan_hours);
    w.f64(house_.drip_installations);
    w.f64(house_.botrytis_spore_index);
    w.f64(house_.powdery_mildew_spore_index);
    w.f64(house_.anthracnose_sporulation_index);
    w.f64(house_.spider_mite_mean_index);
    w.f64(house_.inside_solar_mean_W_per_m2);

    w.u64(biology_rng_.raw_state());
    w.u64(biology_rng_.raw_increment());
    w.u64(disease_rng_.raw_state());
    w.u64(disease_rng_.raw_increment());
    w.u64(foliar_rng_.raw_state());  // M5b (Disease 스트림 다음)
    w.u64(foliar_rng_.raw_increment());

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
    w.f64(ledger_.N_harvested_g);
    w.f64(ledger_.P_harvested_g);
    w.f64(ledger_.K_harvested_g);
    w.f64(ledger_.Ca_harvested_g);
    w.f64(ledger_.Mg_harvested_g);
    w.f64(ledger_.Mn_harvested_g);
    w.f64(ledger_.C_leaf_litter_g);
    // M5a
    w.f64(ledger_.N_flush_g);
    w.f64(ledger_.Mn_flush_g);
    write_f64_list(w, ledger_.disinfectant_applied_g);
    w.f64(ledger_.inoculant_applied_g);
    // M5b
    w.f64(ledger_.N_removed_g);
    w.f64(ledger_.P_removed_g);
    w.f64(ledger_.K_removed_g);
    w.f64(ledger_.Ca_removed_g);
    w.f64(ledger_.Mg_removed_g);
    w.f64(ledger_.Mn_removed_g);
    w.f64(ledger_.dry_matter_removed_g);
    w.u64(ledger_.plants_removed);
    w.u64(ledger_.fruit_rot_count);
    w.f64(ledger_.fruit_rot_fresh_g);
    w.u64(ledger_.harvested_mildew);
    w.f64(ledger_.harvested_mildew_fresh_g);
    w.f64(ledger_.predators_released_per_m2);
}

std::expected<void, Error> World::read_dynamic(detail::ByteReader& r) {
    tick_ = r.u64();
    next_command_seq_ = r.u64();

    // 하우스 상태 (대기 명령 검증이 cover_layers를 읽어요)
    house_.cover_layers = r.i32();
    const std::uint64_t window_layers = r.u64();
    if (!r.ok() || window_layers > r.remaining() / kWindowLayerBytes) {
        return fail(ErrorCode::Truncated, "save data ends inside the house state");
    }
    if (house_.cover_layers < params_.house.min_cover_layers || house_.cover_layers > params_.house.max_cover_layers ||
        window_layers != static_cast<std::uint64_t>(house_.cover_layers)) {
        return fail(ErrorCode::InvalidData, "house cover layers are inconsistent with params or window state");
    }
    house_.window_open_fraction.assign(static_cast<std::size_t>(window_layers), std::array<double, 2>{0.0, 0.0});
    for (auto& layer : house_.window_open_fraction) {
        const double left = r.f64();
        const double right = r.f64();
        if (!is_window_open_level(params_.house, left) || !is_window_open_level(params_.house, right)) {
            return fail(ErrorCode::InvalidData, "house window open fraction is not an allowed level");
        }
        layer = {left, right};
    }
    const std::uint8_t heater_on = r.u8();
    if (heater_on > 1) {
        return fail(ErrorCode::InvalidData, "house heater flag is invalid");
    }
    house_.heater_on = heater_on == 1;
    house_.heater_setpoint_degC = r.f64();
    house_.air_temp_degC = r.f64();
    house_.air_temp_mean_degC = r.f64();
    house_.co2_ppm = r.f64();
    house_.heating_energy_MJ_per_m2 = r.f64();
    house_.outside_co2_ppm = r.f64();
    house_.inside_solar_W_per_m2 = r.f64();
    house_.heating_power_W_per_m2 = r.f64();
    house_.vent_path_index = r.f64();
    // M5b 하우스 습도
    house_.vapor_g_per_m3 = r.f64();
    house_.rh_mean_index = r.f64();
    {
        const std::uint8_t fan_on = r.u8();
        if (fan_on > 1) {
            return fail(ErrorCode::InvalidData, "house fan flag is invalid");
        }
        house_.fan_on = fan_on == 1;
    }
    house_.vapor_source_mm = r.f64();
    house_.water_condensed_mm = r.f64();
    house_.vapor_vented_mm = r.f64();
    house_.vapor_deposited_mm = r.f64();
    house_.vapor_initial_mm = r.f64();
    house_.rh_index = r.f64();
    house_.airflow_index = r.f64();
    house_.cover_inner_temp_degC = r.f64();
    {
        const std::uint8_t drip = r.u8();
        if (drip > 1) {
            return fail(ErrorCode::InvalidData, "house drip flag is invalid");
        }
        house_.drip_installed = drip == 1;
    }
    house_.fan_hours = r.f64();
    house_.drip_installations = r.f64();
    house_.botrytis_spore_index = r.f64();
    house_.powdery_mildew_spore_index = r.f64();
    house_.anthracnose_sporulation_index = r.f64();
    house_.spider_mite_mean_index = r.f64();
    house_.inside_solar_mean_W_per_m2 = r.f64();

    const std::uint64_t rng_state = r.u64();
    const std::uint64_t rng_increment = r.u64();
    if (!r.ok()) {
        return fail(ErrorCode::Truncated, "save data ends inside the house state");
    }
    biology_rng_ = Pcg32::from_raw(rng_state, rng_increment);
    {
        const std::uint64_t disease_state = r.u64();
        const std::uint64_t disease_increment = r.u64();
        if (!r.ok()) {
            return fail(ErrorCode::Truncated, "save data ends inside the disease rng");
        }
        disease_rng_ = Pcg32::from_raw(disease_state, disease_increment);
        const std::uint64_t foliar_state = r.u64();
        const std::uint64_t foliar_increment = r.u64();
        if (!r.ok()) {
            return fail(ErrorCode::Truncated, "save data ends inside the foliar rng");
        }
        foliar_rng_ = Pcg32::from_raw(foliar_state, foliar_increment);
    }

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
        // 명령 검증은 타일을 읽은 뒤에 해요 (M5a Plant는 저장된 작물 상태로 검증)
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
        if (r.ok() && !crop_structure_is_valid(tile.crop, params_)) {
            return fail(ErrorCode::InvalidData, "saved crop has inconsistent trusses, fruits or harvest record");
        }
        // M5a: 멀칭 인덱스는 kNoMulch 또는 params.mulches 범위, 약제 잔류 개수는 params.disinfectants와 같아야 해요
        if (r.ok() && (tile.soil.mulch_def_index < kNoMulch ||
                       (tile.soil.mulch_def_index >= 0 &&
                        static_cast<std::size_t>(tile.soil.mulch_def_index) >= params_.mulches.size()) ||
                       tile.soil.fumigant_residue_index.size() != params_.disinfectants.size())) {
            return fail(ErrorCode::InvalidData, "saved soil cell mulch or fumigant residue does not match params");
        }
    }
    if (r.ok()) {
        for (const QueuedCommand& queued : pending_commands_) {
            if (auto valid = validate_command(queued.command); !valid) {
                return fail(ErrorCode::InvalidData,
                            std::format("saved command is invalid: {}", valid.error().message));
            }
        }
    }

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
    ledger_.N_harvested_g = r.f64();
    ledger_.P_harvested_g = r.f64();
    ledger_.K_harvested_g = r.f64();
    ledger_.Ca_harvested_g = r.f64();
    ledger_.Mg_harvested_g = r.f64();
    ledger_.Mn_harvested_g = r.f64();
    ledger_.C_leaf_litter_g = r.f64();
    // M5a
    ledger_.N_flush_g = r.f64();
    ledger_.Mn_flush_g = r.f64();
    read_f64_list(r, ledger_.disinfectant_applied_g);
    ledger_.inoculant_applied_g = r.f64();
    // M5b
    ledger_.N_removed_g = r.f64();
    ledger_.P_removed_g = r.f64();
    ledger_.K_removed_g = r.f64();
    ledger_.Ca_removed_g = r.f64();
    ledger_.Mg_removed_g = r.f64();
    ledger_.Mn_removed_g = r.f64();
    ledger_.dry_matter_removed_g = r.f64();
    ledger_.plants_removed = r.u64();
    ledger_.fruit_rot_count = r.u64();
    ledger_.fruit_rot_fresh_g = r.f64();
    ledger_.harvested_mildew = r.u64();
    ledger_.harvested_mildew_fresh_g = r.f64();
    ledger_.predators_released_per_m2 = r.f64();
    if (r.ok() && !ledger_.disinfectant_applied_g.empty() &&
        ledger_.disinfectant_applied_g.size() != params_.disinfectants.size()) {
        return fail(ErrorCode::InvalidData, "saved disinfectant ledger does not match params disinfectants");
    }

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
