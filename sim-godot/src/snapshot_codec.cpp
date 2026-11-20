#include "snapshot_codec.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "sim/processes.hpp"
#include "text.hpp"

namespace simgodot {

namespace {

// 스트레스 요인 이름 (StressFactor 순서). 코어에 공개 이름표가 없어서(m6-overview 2.3은 C1~C3만 승인)
// sim-cli CSV 열 이름(stress_hours_<요인>)과 같은 이름을 여기 둬요. 코어 표가 생기면 그것으로 바꿔요.
constexpr std::array<std::string_view, sim::kStressFactorCount> kStressFactorIds = {
    "water", "salt", "N", "P", "K", "Ca", "Mg", "root", "Mn_deficiency", "Mn_toxicity"};

// ---- 필드 목록 (이름 = 코어 필드 이름). 사전과 열(column) 접근자가 같은 목록을 써요 ----

#define SIM_SOIL_DOUBLE_FIELDS(X)                                                                              \
    X(field_capacity_mm) X(saturation_mm) X(base_buffer_eq_per_m2_per_pH) X(cec_mineral_eq_per_m2) X(water_mm) \
    X(drained_mm_this_tick) X(soil_temp_degC) X(deep_soil_temp_degC) X(labile_C_g_per_m2) X(labile_N_g_per_m2) \
    X(stable_C_g_per_m2) X(stable_N_g_per_m2) X(nh4_N_g_per_m2) X(no2_N_g_per_m2) X(no3_N_g_per_m2) X(ph)      \
    X(compaction_index) X(aeration_index) X(k_g_per_m2) X(ca_g_per_m2) X(mg_g_per_m2) X(p_g_per_m2)            \
    X(s_g_per_m2) X(mn_g_per_m2) X(mn_oxide_g_per_m2) X(p_fixed_g_per_m2) X(nh4_solution_N_g_per_m2)           \
    X(k_solution_g_per_m2) X(ca_solution_g_per_m2) X(mg_solution_g_per_m2) X(mn_solution_g_per_m2)             \
    X(cec_eq_per_m2) X(exchange_free_site_index) X(lime_undissolved_eq_per_m2) X(lime_ca_fraction)             \
    X(lime_mg_fraction) X(acid_H_eq_this_tick) X(pending_H_eq_per_m2) X(ec_dS_per_m)

#define SIM_CROP_DOUBLE_FIELDS(X)                                                                                \
    X(leaf_g_per_m2) X(root_g_per_m2) X(root_health) X(plant_N_g_per_m2) X(plant_P_g_per_m2) X(plant_K_g_per_m2) \
    X(plant_Ca_g_per_m2) X(plant_Mg_g_per_m2) X(plant_Mn_g_per_m2) X(last_growth_g_per_m2)                     \
    X(antagonism_Ca_factor) X(antagonism_Mg_factor) X(crown_g_per_m2) X(plants_per_m2) X(establishment_index)  \
    X(induction_index) X(last_gross_g_per_m2) X(last_respiration_g_per_m2) X(last_supply_ratio)                \
    X(size_satisfaction_mean) X(reserve_g_per_m2) X(rgr_mean_per_day) X(disease_susceptibility_factor)         \
    X(leaf_wetness_index) X(wet_run_hours) X(dry_run_hours) X(canopy_rh_index) X(infection_anthracnose_index)  \
    X(infection_botrytis_index) X(infection_powdery_mildew_index) X(spider_mite_index)                         \
    X(spider_mite_mean_index) X(predator_mite_index) X(soil_mineral_N_mean_g_per_m2)                           \
    X(last_anthracnose_increase_index) X(last_botrytis_increase_index) X(last_powdery_mildew_increase_index)   \
    X(last_mite_predation_index) X(last_fruit_rot_count)

#define SIM_HOUSE_DOUBLE_FIELDS(X)                                                                              \
    X(heater_setpoint_degC) X(air_temp_degC) X(air_temp_mean_degC) X(co2_ppm) X(heating_energy_MJ_per_m2)      \
    X(outside_co2_ppm) X(inside_solar_W_per_m2) X(heating_power_W_per_m2) X(vent_path_index)                   \
    X(vapor_g_per_m3) X(rh_mean_index) X(fan_hours) X(drip_installations) X(vapor_source_mm)                   \
    X(water_condensed_mm) X(vapor_vented_mm) X(vapor_deposited_mm) X(vapor_initial_mm) X(rh_index)             \
    X(airflow_index) X(cover_inner_temp_degC) X(botrytis_spore_index) X(powdery_mildew_spore_index)            \
    X(anthracnose_sporulation_index) X(spider_mite_mean_index) X(inside_solar_mean_W_per_m2)

#define SIM_LEDGER_DOUBLE_FIELDS(X)                                                                             \
    X(water_in_mm) X(water_evaporated_mm) X(water_drained_mm) X(N_in_g) X(N_leached_g) X(N_gas_g) X(C_in_g)    \
    X(C_respired_g) X(water_transpired_mm) X(N_uptake_g) X(P_in_g) X(P_leached_g) X(P_uptake_g) X(K_in_g)      \
    X(K_leached_g) X(K_uptake_g) X(Ca_in_g) X(Ca_leached_g) X(Ca_uptake_g) X(Mg_in_g) X(Mg_leached_g)          \
    X(Mg_uptake_g) X(S_in_g) X(S_leached_g) X(H_eq_net) X(C_root_litter_g) X(Mn_in_g) X(Mn_leached_g)          \
    X(Mn_uptake_g) X(N_harvested_g) X(P_harvested_g) X(K_harvested_g) X(Ca_harvested_g) X(Mg_harvested_g)      \
    X(Mn_harvested_g) X(C_leaf_litter_g) X(N_flush_g) X(Mn_flush_g) X(inoculant_applied_g) X(N_removed_g)      \
    X(P_removed_g) X(K_removed_g) X(Ca_removed_g) X(Mg_removed_g) X(Mn_removed_g) X(dry_matter_removed_g)      \
    X(fruit_rot_fresh_g) X(harvested_mildew_fresh_g) X(predators_released_per_m2)

template <std::size_t N, typename T>
godot::Dictionary keyed(const std::array<std::string_view, N>& ids, const std::array<T, N>& values) {
    godot::Dictionary d;
    for (std::size_t i = 0; i < N; ++i) {
        d[gstr(ids[i])] = values[i];
    }
    return d;
}

godot::Dictionary keyed_vector(const sim::Params& p, const std::vector<double>& values) {
    // 약제 id 순서 (params.disinfectants). 비어 있으면 전부 0
    godot::Dictionary d;
    for (std::size_t i = 0; i < p.disinfectants.size(); ++i) {
        d[gstr(p.disinfectants[i].id)] = i < values.size() ? values[i] : 0.0;
    }
    return d;
}

std::string_view mulch_name(const sim::Params& p, std::int32_t index) {
    if (index < 0 || static_cast<std::size_t>(index) >= p.mulches.size()) {
        return sim::kNoMulchId;
    }
    return p.mulches[static_cast<std::size_t>(index)].id;
}

template <typename Defs>
godot::PackedStringArray ids_of(const Defs& defs) {
    godot::PackedStringArray a;
    for (const auto& d : defs) {
        a.push_back(gstr(d.id));
    }
    return a;
}

godot::Dictionary harvest_dict(const sim::HarvestRecord& h) {
    godot::Dictionary d;
    d["harvested_fruits"] = static_cast<int64_t>(h.harvested_fruits);
    d["harvested_fresh_g"] = h.harvested_fresh_g;
    d["harvested_deformed"] = static_cast<int64_t>(h.harvested_deformed);
    d["harvested_deformed_fresh_g"] = h.harvested_deformed_fresh_g;
    d["harvested_brix_weighted"] = h.harvested_brix_weighted;
    d["harvested_firmness_weighted"] = h.harvested_firmness_weighted;
    godot::PackedInt64Array sizes;
    for (const std::uint64_t c : h.harvested_size_class_counts) {
        sizes.push_back(static_cast<int64_t>(c));
    }
    d["harvested_size_class_counts"] = sizes;
    d["has_first_harvest"] = h.has_first_harvest;
    d["first_harvest_tick"] = static_cast<int64_t>(h.first_harvest_tick);
    d["aborted_fruits"] = static_cast<int64_t>(h.aborted_fruits);
    d["thinned_fruits"] = static_cast<int64_t>(h.thinned_fruits);
    d["harvested_day_score_weighted"] = h.harvested_day_score_weighted;
    d["harvested_night_score_weighted"] = h.harvested_night_score_weighted;
    d["harvested_temp_score_weighted"] = h.harvested_temp_score_weighted;
    return d;
}

}  // namespace

godot::Dictionary tile_visuals(const sim::Snapshot& s, const sim::Params& p, double ripening_min_index) {
    const auto n = static_cast<int64_t>(s.crops.size());
    godot::PackedByteArray present;
    godot::PackedByteArray alive;
    godot::PackedFloat64Array leaf;
    godot::PackedInt32Array flowers;
    godot::PackedInt32Array fruits;
    godot::PackedInt32Array ripening;
    godot::PackedStringArray mulch;
    present.resize(n);
    alive.resize(n);
    leaf.resize(n);
    flowers.resize(n);
    fruits.resize(n);
    ripening.resize(n);
    mulch.resize(n);
    uint8_t* present_w = present.ptrw();
    uint8_t* alive_w = alive.ptrw();
    double* leaf_w = leaf.ptrw();
    int32_t* flowers_w = flowers.ptrw();
    int32_t* fruits_w = fruits.ptrw();
    int32_t* ripening_w = ripening.ptrw();
    for (int64_t i = 0; i < n; ++i) {
        const sim::Crop& c = s.crops[static_cast<std::size_t>(i)];
        present_w[i] = c.present ? 1 : 0;
        alive_w[i] = c.alive ? 1 : 0;
        leaf_w[i] = c.leaf_g_per_m2;
        int32_t open = 0;
        int32_t set = 0;
        int32_t ripe = 0;
        for (const sim::Fruit& f : c.fruits) {
            if (!f.set) {
                ++open;
            } else {
                ++set;
                if (f.development_index >= ripening_min_index) {
                    ++ripe;
                }
            }
        }
        flowers_w[i] = open;
        fruits_w[i] = set;
        ripening_w[i] = ripe;
        mulch.set(i, gstr(mulch_name(p, s.soil[static_cast<std::size_t>(i)].mulch_def_index)));
    }
    godot::Dictionary d;
    d["crop_present"] = present;
    d["crop_alive"] = alive;
    d["leaf_g_per_m2"] = leaf;
    d["open_flower_count"] = flowers;
    d["fruit_count"] = fruits;
    d["ripening_fruit_count"] = ripening;
    d["mulch_id"] = mulch;
    return d;
}

godot::Dictionary house_controls(const sim::Snapshot& s) {
    const sim::HouseState& h = s.house;
    godot::Dictionary d;
    d["cover_layers"] = h.cover_layers;
    // [layer0 left, layer0 right, layer1 left, ...] (layer 0 = 가장 바깥)
    godot::PackedFloat64Array windows;
    for (const auto& layer : h.window_open_fraction) {
        windows.push_back(layer[0]);
        windows.push_back(layer[1]);
    }
    d["window_open_fraction"] = windows;
    d["windows_open_count"] = h.windows_open_count();
    d["heater_on"] = h.heater_on;
    d["heater_setpoint_degC"] = h.heater_setpoint_degC;
    d["fan_on"] = h.fan_on;
    d["drip_installed"] = h.drip_installed;
    return d;
}

godot::Dictionary weather(const sim::Snapshot& s) {
    godot::Dictionary d;
    d["outside_air_temp_degC"] = s.weather.outside_air_temp_degC;
    d["solar_radiation_W_per_m2"] = s.weather.solar_radiation_W_per_m2;
    d["outside_rh_percent"] = s.weather.outside_rh_percent;
    return d;
}

godot::Dictionary harvest_totals(const sim::Snapshot& s) {
    std::uint64_t fruits = 0;
    std::uint64_t deformed = 0;
    double fresh_g = 0.0;
    double deformed_fresh_g = 0.0;
    for (const sim::Crop& c : s.crops) {  // 행 우선 순서 고정 (합 순서가 정해져 있어요)
        fruits += c.harvest.harvested_fruits;
        deformed += c.harvest.harvested_deformed;
        fresh_g += c.harvest.harvested_fresh_g;
        deformed_fresh_g += c.harvest.harvested_deformed_fresh_g;
    }
    godot::Dictionary d;
    d["harvested_fruits"] = static_cast<int64_t>(fruits);
    d["harvested_fresh_g"] = fresh_g;
    d["harvested_deformed"] = static_cast<int64_t>(deformed);
    d["harvested_deformed_fresh_g"] = deformed_fresh_g;
    return d;
}

godot::Dictionary catalog(const sim::Params& p, const sim::Snapshot& s) {
    godot::Dictionary d;
    godot::PackedStringArray actions;
    for (const char* a : {"irrigate", "add_compost", "add_fertilizer", "add_lime", "till", "set_window",
                          "set_heater", "set_cover_layers", "thin_flowers", "set_mulch", "apply_disinfectant",
                          "add_inoculant", "plant", "set_fan", "install_drip", "remove_plant", "release_predators"}) {
        actions.push_back(a);
    }
    d["actions"] = actions;
    d["compost"] = ids_of(p.composts);
    d["fertilizer"] = ids_of(p.fertilizers);
    d["lime"] = ids_of(p.limes);
    godot::PackedStringArray mulches = ids_of(p.mulches);
    mulches.push_back(gstr(sim::kNoMulchId));
    d["mulch"] = mulches;
    d["disinfectant"] = ids_of(p.disinfectants);
    d["inoculant"] = ids_of(p.inoculants);
    d["predator"] = ids_of(p.predators);
    d["seedling_grade"] = ids_of(p.seedling_grades);
    godot::PackedFloat64Array levels;
    for (const double v : p.house.window_open_levels) {
        levels.push_back(v);
    }
    d["window_open_levels"] = levels;
    d["heater_setpoint_min_degC"] = p.house.heater_setpoint_min_degC;
    d["heater_setpoint_max_degC"] = p.house.heater_setpoint_max_degC;
    godot::PackedInt64Array window_doy;
    window_doy.push_back(p.house.cover_change_window_doy[0]);
    window_doy.push_back(p.house.cover_change_window_doy[1]);
    d["cover_change_window_doy"] = window_doy;
    d["min_cover_layers"] = p.house.min_cover_layers;
    d["max_cover_layers"] = p.house.max_cover_layers;
    d["bed_rows"] = s.bed_rows;
    d["bed_cols"] = s.bed_cols;
    return d;
}

godot::Dictionary true_house(const sim::Snapshot& s) {
    const sim::HouseState& h = s.house;
    godot::Dictionary d;
    d["house_air_temp_degC"] = s.house_air_temp_degC;  // Snapshot 값 (마지막으로 처리한 틱)
    d["cover_layers"] = h.cover_layers;
    godot::PackedFloat64Array windows;
    for (const auto& layer : h.window_open_fraction) {
        windows.push_back(layer[0]);
        windows.push_back(layer[1]);
    }
    d["window_open_fraction"] = windows;
    d["heater_on"] = h.heater_on;
    d["fan_on"] = h.fan_on;
    d["drip_installed"] = h.drip_installed;
#define X(name) d[#name] = h.name;
    SIM_HOUSE_DOUBLE_FIELDS(X)
#undef X
    return d;
}

godot::Dictionary true_ledger(const sim::Snapshot& s, const sim::Params& p) {
    const sim::Ledger& l = s.ledger;
    godot::Dictionary d;
#define X(name) d[#name] = l.name;
    SIM_LEDGER_DOUBLE_FIELDS(X)
#undef X
    d["disinfectant_applied_g"] = keyed_vector(p, l.disinfectant_applied_g);
    d["plants_removed"] = static_cast<int64_t>(l.plants_removed);
    d["fruit_rot_count"] = static_cast<int64_t>(l.fruit_rot_count);
    d["harvested_mildew"] = static_cast<int64_t>(l.harvested_mildew);
    return d;
}

godot::Dictionary true_cell(const sim::Snapshot& s, const sim::Params& p, std::size_t index) {
    const sim::SoilCell& c = s.soil[index];
    godot::Dictionary d;
#define X(name) d[#name] = c.name;
    SIM_SOIL_DOUBLE_FIELDS(X)
#undef X
    d["nitrite_alarm"] = c.nitrite_alarm;
    d["overhead_irrigated_this_tick"] = c.overhead_irrigated_this_tick;
    d["mulch_def_index"] = c.mulch_def_index;
    d["mulch_id"] = gstr(mulch_name(p, c.mulch_def_index));
    d["microbe_index"] = keyed(sim::kMicrobeGroupIds, c.microbe_index);
    d["disinfection_kill_index"] = keyed(sim::kMicrobeGroupIds, c.disinfection_kill_index);
    d["fumigant_residue_index"] = keyed_vector(p, c.fumigant_residue_index);
    return d;
}

godot::Dictionary true_crop(const sim::Snapshot& s, const sim::Params& p, std::size_t index) {
    const sim::Crop& c = s.crops[index];
    godot::Dictionary d;
    d["present"] = c.present;
    d["alive"] = c.alive;
#define X(name) d[#name] = c.name;
    SIM_CROP_DOUBLE_FIELDS(X)
#undef X
    d["fruit_g_per_m2"] = c.fruit_g_per_m2();
    d["stress_hours"] = keyed(kStressFactorIds, c.stress_hours);
    d["infection_index"] = keyed(sim::kSoilDiseaseIds, c.infection_index);
    d["disease_onset_reported"] = keyed(sim::kSoilDiseaseIds, c.disease_onset_reported);
    d["foliar_onset_reported"] = keyed(sim::kFoliarDiseaseIds, c.foliar_onset_reported);
    d["truss_count"] = static_cast<int64_t>(c.trusses.size());
    int64_t open = 0;
    int64_t set = 0;
    for (const sim::Fruit& f : c.fruits) {
        (f.set ? set : open) += 1;
    }
    d["open_flower_count"] = open;
    d["fruit_count"] = set;
    d["harvest"] = harvest_dict(c.harvest);
    // 충족도·과잉 지수: CLI와 같은 코어 함수 (작물이 없으면 넣지 않아요)
    if (c.present) {
        const sim::process::NutrientStatus ns = sim::process::nutrient_status(c, p.crop);
        godot::Dictionary n;
        n["sufficiency_N"] = ns.sufficiency_N;
        n["sufficiency_P"] = ns.sufficiency_P;
        n["sufficiency_K"] = ns.sufficiency_K;
        n["sufficiency_Ca"] = ns.sufficiency_Ca;
        n["sufficiency_Mg"] = ns.sufficiency_Mg;
        n["sufficiency_Mn"] = ns.sufficiency_Mn;
        n["excess_N"] = ns.excess_N;
        n["excess_P"] = ns.excess_P;
        n["excess_K"] = ns.excess_K;
        n["excess_Ca"] = ns.excess_Ca;
        n["excess_Mg"] = ns.excess_Mg;
        n["excess_Mn"] = ns.excess_Mn;
        n["plant_Mn_ratio"] = ns.plant_Mn_ratio;
        d["nutrient_status"] = n;
    }
    return d;
}

godot::Array true_fruits(const sim::Snapshot& s, std::size_t index) {
    godot::Array out;
    for (const sim::Fruit& f : s.crops[index].fruits) {
        godot::Dictionary d;
        d["truss_index"] = static_cast<int64_t>(f.truss_index);
        d["rank"] = static_cast<int64_t>(f.rank);
        d["anthesis_tick"] = static_cast<int64_t>(f.anthesis_tick);
        d["set"] = f.set;
        d["pollination_index"] = f.pollination_index;
        d["deformed"] = f.deformed;
        d["potential_fresh_g"] = f.potential_fresh_g;
        d["dry_g"] = f.dry_g;
        d["development_index"] = f.development_index;
        d["brix_factor_sum"] = f.brix_factor_sum;
        d["firmness_factor_sum"] = f.firmness_factor_sum;
        d["weight_sum"] = f.weight_sum;
        d["day_score_sum"] = f.day_score_sum;
        d["day_weight_sum"] = f.day_weight_sum;
        d["night_score_sum"] = f.night_score_sum;
        d["night_weight_sum"] = f.night_weight_sum;
        out.push_back(d);
    }
    return out;
}

bool true_column(const sim::Snapshot& s, const sim::Params& p, const godot::String& kind_g,
                 const godot::String& field_g, godot::PackedFloat64Array& out) {
    const std::string kind = to_utf8(kind_g);
    const std::string field = to_utf8(field_g);
    const std::size_t n = s.soil.size();
    out.resize(static_cast<int64_t>(n));
    double* w = out.ptrw();
    auto fill = [&](auto&& get) {
        for (std::size_t i = 0; i < n; ++i) {
            w[i] = get(i);
        }
        return true;
    };
    auto array_field = [&](std::string_view prefix, std::string& id) {
        if (field.size() > prefix.size() + 1 && field.starts_with(prefix) && field[prefix.size()] == '.') {
            id = field.substr(prefix.size() + 1);
            return true;
        }
        return false;
    };
    std::string id;
    if (kind == "soil") {
#define X(name)                                                       \
    if (field == #name) {                                             \
        return fill([&](std::size_t i) { return s.soil[i].name; });   \
    }
        SIM_SOIL_DOUBLE_FIELDS(X)
#undef X
        if (field == "mulch_def_index") {
            return fill([&](std::size_t i) { return static_cast<double>(s.soil[i].mulch_def_index); });
        }
        if (array_field("microbe_index", id) || array_field("disinfection_kill_index", id)) {
            const bool kill = field.starts_with("disinfection_kill_index");
            for (std::size_t g = 0; g < sim::kMicrobeGroupCount; ++g) {
                if (sim::kMicrobeGroupIds[g] == id) {
                    return fill([&](std::size_t i) {
                        return kill ? s.soil[i].disinfection_kill_index[g] : s.soil[i].microbe_index[g];
                    });
                }
            }
        }
        if (array_field("fumigant_residue_index", id)) {
            for (std::size_t k = 0; k < p.disinfectants.size(); ++k) {
                if (p.disinfectants[k].id == id) {
                    return fill([&](std::size_t i) {
                        const auto& v = s.soil[i].fumigant_residue_index;
                        return k < v.size() ? v[k] : 0.0;
                    });
                }
            }
        }
    } else if (kind == "crop") {
#define X(name)                                                        \
    if (field == #name) {                                              \
        return fill([&](std::size_t i) { return s.crops[i].name; });   \
    }
        SIM_CROP_DOUBLE_FIELDS(X)
#undef X
        if (field == "fruit_g_per_m2") {
            return fill([&](std::size_t i) { return s.crops[i].fruit_g_per_m2(); });
        }
        if (array_field("stress_hours", id)) {
            for (std::size_t k = 0; k < kStressFactorIds.size(); ++k) {
                if (kStressFactorIds[k] == id) {
                    return fill([&](std::size_t i) { return s.crops[i].stress_hours[k]; });
                }
            }
        }
        if (array_field("infection_index", id)) {
            for (std::size_t k = 0; k < sim::kSoilDiseaseCount; ++k) {
                if (sim::kSoilDiseaseIds[k] == id) {
                    return fill([&](std::size_t i) { return s.crops[i].infection_index[k]; });
                }
            }
        }
    }
    out.resize(0);
    return false;
}

}  // namespace simgodot
