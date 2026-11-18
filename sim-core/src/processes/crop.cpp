// docs/spec/strawberry-minimal.md — 딸기 최소 작물: 수관·증산 요구, 스트레스, 생장, 흡수, 뿌리 건강도
#include <algorithm>
#include <array>
#include <cstddef>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

// 단위 환산: 1 W·h = 3600 J = 0.0036 MJ
constexpr double kMJ_per_W_h = 0.0036;

[[nodiscard]] double non_negative(double value) { return std::max(0.0, value); }

[[nodiscard]] double leaf_area_index(const Crop& crop, const CropParams& params) {
    return non_negative(crop.leaf_g_per_m2) * params.specific_leaf_area_m2_per_g;
}

[[nodiscard]] double biomass_g_per_m2(const Crop& crop) { return crop.leaf_g_per_m2 + crop.root_g_per_m2; }

// 체내 양분 / (적정 농도 × 건물량). 요구량이 0이면(건물량 0) 충족된 것으로 봐요 (비율 1).
[[nodiscard]] double nutrient_ratio(double plant_g_per_m2, double target_g_per_g, double biomass) {
    const double required_g_per_m2 = target_g_per_g * biomass;
    if (!(required_g_per_m2 > 0.0)) {
        return 1.0;
    }
    return non_negative(plant_g_per_m2) / required_g_per_m2;
}

// 원소 1개의 흡수량 (g/m²): min(요구량, 흡수 능력)
[[nodiscard]] double element_uptake_g_per_m2(double plant_g_per_m2, double target_g_per_g, double biomass,
                                             double available_g_per_m2, double root_capacity_factor,
                                             const CropParams& params) {
    const double demand_g_per_m2 =
        non_negative(target_g_per_g * params.luxury_uptake_ratio * biomass - plant_g_per_m2);
    const double capacity_g_per_m2 = non_negative(available_g_per_m2) * root_capacity_factor;
    return std::min(demand_g_per_m2, capacity_g_per_m2);
}

// 토양 풀 → 체내 풀 이동. 토양 풀이 가진 양을 넘지 않아요. 실제 이동량을 돌려줘요.
double transfer_g_per_m2(double& soil_pool_g_per_m2, double& plant_pool_g_per_m2, double amount_g_per_m2) {
    const double moved_g_per_m2 = std::min(non_negative(amount_g_per_m2), non_negative(soil_pool_g_per_m2));
    soil_pool_g_per_m2 -= moved_g_per_m2;
    plant_pool_g_per_m2 += moved_g_per_m2;
    return moved_g_per_m2;
}

}  // namespace

Crop plant_crop(const CropInit& init, const CropParams& params) {
    Crop crop;
    crop.present = true;
    crop.alive = true;
    crop.leaf_g_per_m2 = init.leaf_g_per_m2;
    crop.root_g_per_m2 = init.root_g_per_m2;
    crop.root_health = 1.0;
    const double biomass_g_per_m2 = init.leaf_g_per_m2 + init.root_g_per_m2;
    crop.plant_N_g_per_m2 = params.target_N_g_per_g * biomass_g_per_m2;
    crop.plant_P_g_per_m2 = params.target_P_g_per_g * biomass_g_per_m2;
    crop.plant_K_g_per_m2 = params.target_K_g_per_g * biomass_g_per_m2;
    crop.plant_Ca_g_per_m2 = params.target_Ca_g_per_g * biomass_g_per_m2;
    crop.plant_Mg_g_per_m2 = params.target_Mg_g_per_g * biomass_g_per_m2;
    return crop;
}

double canopy_interception(const Crop& crop, const CropParams& params) {
    if (!crop.present || !crop.alive) {
        return 0.0;
    }
    return params.light_interception_curve(leaf_area_index(crop, params));
}

WaterSaltFactors water_salt_factors(const SoilCell& cell, const CropParams& params) {
    return WaterSaltFactors{
        .water = params.water_stress_curve(cell.relative_water()),
        .salt = params.salt_stress_curve(cell.ec_dS_per_m),
    };
}

double transpiration_demand_mm(const Crop& crop, const CropParams& params, double inside_solar_W_per_m2,
                               const WaterSaltFactors& factors) {
    if (!crop.present || !crop.alive) {
        return 0.0;
    }
    return params.transpiration_mm_per_hour_per_W_per_m2 * non_negative(inside_solar_W_per_m2) *
           canopy_interception(crop, params) * std::min(factors.water, factors.salt);
}

NutrientStatus nutrient_status(const Crop& crop, const CropParams& params) {
    if (!crop.present) {
        return {};
    }
    const double biomass = biomass_g_per_m2(crop);
    const double ratio_N = nutrient_ratio(crop.plant_N_g_per_m2, params.target_N_g_per_g, biomass);
    const double ratio_P = nutrient_ratio(crop.plant_P_g_per_m2, params.target_P_g_per_g, biomass);
    const double ratio_K = nutrient_ratio(crop.plant_K_g_per_m2, params.target_K_g_per_g, biomass);
    const double ratio_Ca = nutrient_ratio(crop.plant_Ca_g_per_m2, params.target_Ca_g_per_g, biomass);
    const double ratio_Mg = nutrient_ratio(crop.plant_Mg_g_per_m2, params.target_Mg_g_per_g, biomass);
    return NutrientStatus{
        .sufficiency_N = std::min(1.0, ratio_N),
        .sufficiency_P = std::min(1.0, ratio_P),
        .sufficiency_K = std::min(1.0, ratio_K),
        .sufficiency_Ca = std::min(1.0, ratio_Ca),
        .sufficiency_Mg = std::min(1.0, ratio_Mg),
        .excess_N = non_negative(ratio_N - 1.0),
        .excess_P = non_negative(ratio_P - 1.0),
        .excess_K = non_negative(ratio_K - 1.0),
        .excess_Ca = non_negative(ratio_Ca - 1.0),
        .excess_Mg = non_negative(ratio_Mg - 1.0),
    };
}

std::optional<PlantDiedEvent> update_crop(Crop& crop, SoilCell& cell, const Params& params,
                                          const NutrientAvailability& available, double inside_solar_W_per_m2,
                                          double house_air_temp_degC, StressCombination mode,
                                          std::uint32_t tile_index, Ledger& ledger) {
    if (!crop.present || !crop.alive) {
        return std::nullopt;
    }
    const CropParams& cp = params.crop;

    // ---- 1. 스트레스 계수 ----
    const WaterSaltFactors water_salt = water_salt_factors(cell, cp);
    const NutrientStatus status = nutrient_status(crop, cp);
    // StressFactor 순서 그대로 (동률이면 앞선 요인을 기록)
    const std::array<double, kStressFactorCount> factors{
        water_salt.water,
        water_salt.salt,
        cp.nutrient_stress_curve(status.sufficiency_N),
        cp.nutrient_stress_curve(status.sufficiency_P),
        cp.nutrient_stress_curve(status.sufficiency_K),
        cp.nutrient_stress_curve(status.sufficiency_Ca),
        cp.nutrient_stress_curve(status.sufficiency_Mg),
        crop.root_health,
    };
    std::size_t limiting_index = 0;
    for (std::size_t i = 1; i < factors.size(); ++i) {
        if (factors[i] < factors[limiting_index]) {
            limiting_index = i;
        }
    }
    double combined_factor = 1.0;
    if (mode == StressCombination::Product) {
        for (const double factor : factors) {
            combined_factor *= factor;
        }
    } else {
        combined_factor = factors[limiting_index];
    }
    combined_factor = non_negative(combined_factor);
    if (combined_factor < 1.0) {
        crop.stress_hours[limiting_index] += 1.0;
    }

    // ---- 2. 생장 ----
    const double interception = canopy_interception(crop, cp);
    const double potential_growth_g_per_m2 = cp.light_use_efficiency_g_per_MJ * non_negative(inside_solar_W_per_m2) *
                                             cp.par_fraction * kMJ_per_W_h * interception *
                                             cp.temp_curve(house_air_temp_degC);
    const double growth_g_per_m2 = non_negative(potential_growth_g_per_m2 * combined_factor);
    crop.leaf_g_per_m2 += growth_g_per_m2 * cp.leaf_allocation_fraction;
    crop.root_g_per_m2 += growth_g_per_m2 * (1.0 - cp.leaf_allocation_fraction);
    crop.last_growth_g_per_m2 = growth_g_per_m2;

    // ---- 3. 흡수 (생장 후 건물량 기준) ----
    const double biomass = biomass_g_per_m2(crop);
    const double root_capacity_factor =
        std::min(1.0, cp.uptake_rate_per_g_root_per_hour * non_negative(crop.root_g_per_m2) * crop.root_health) *
        water_salt.water * water_salt.salt;

    // N: NH4+와 NO3- 유효량 비율대로 나눠 가져감
    {
        const double available_nh4_g_per_m2 = non_negative(available.nh4_N);
        const double available_no3_g_per_m2 = non_negative(available.no3_N);
        const double available_N_g_per_m2 = available_nh4_g_per_m2 + available_no3_g_per_m2;
        const double uptake_N_g_per_m2 = element_uptake_g_per_m2(crop.plant_N_g_per_m2, cp.target_N_g_per_g, biomass,
                                                                 available_N_g_per_m2, root_capacity_factor, cp);
        double nh4_uptake_g_per_m2 = 0.0;
        double no3_uptake_g_per_m2 = 0.0;
        if (available_N_g_per_m2 > 0.0) {
            nh4_uptake_g_per_m2 = transfer_g_per_m2(
                cell.nh4_N_g_per_m2, crop.plant_N_g_per_m2,
                uptake_N_g_per_m2 * (available_nh4_g_per_m2 / available_N_g_per_m2));
            no3_uptake_g_per_m2 = transfer_g_per_m2(
                cell.no3_N_g_per_m2, crop.plant_N_g_per_m2,
                uptake_N_g_per_m2 * (available_no3_g_per_m2 / available_N_g_per_m2));
        }
        ledger.N_uptake_g += nh4_uptake_g_per_m2 + no3_uptake_g_per_m2;
        // NH4+ 흡수는 H+, NO3- 흡수는 OH- 방출 (다음 틱 pH 단계가 소비)
        const double nh4_uptake_eq_per_m2 = nh4_uptake_g_per_m2 / params.ions.nh4_N_g_per_eq;
        const double no3_uptake_eq_per_m2 = no3_uptake_g_per_m2 / params.ions.no3_N_g_per_eq;
        cell.pending_H_eq_per_m2 += nh4_uptake_eq_per_m2 * params.soil_chemistry.uptake_H_eq_per_eq_NH4 +
                                    no3_uptake_eq_per_m2 * params.soil_chemistry.uptake_H_eq_per_eq_NO3;
    }
    ledger.P_uptake_g += transfer_g_per_m2(
        cell.p_g_per_m2, crop.plant_P_g_per_m2,
        element_uptake_g_per_m2(crop.plant_P_g_per_m2, cp.target_P_g_per_g, biomass, available.p,
                                root_capacity_factor, cp));
    ledger.K_uptake_g += transfer_g_per_m2(
        cell.k_g_per_m2, crop.plant_K_g_per_m2,
        element_uptake_g_per_m2(crop.plant_K_g_per_m2, cp.target_K_g_per_g, biomass, available.k,
                                root_capacity_factor, cp));
    ledger.Ca_uptake_g += transfer_g_per_m2(
        cell.ca_g_per_m2, crop.plant_Ca_g_per_m2,
        element_uptake_g_per_m2(crop.plant_Ca_g_per_m2, cp.target_Ca_g_per_g, biomass, available.ca,
                                root_capacity_factor, cp));
    ledger.Mg_uptake_g += transfer_g_per_m2(
        cell.mg_g_per_m2, crop.plant_Mg_g_per_m2,
        element_uptake_g_per_m2(crop.plant_Mg_g_per_m2, cp.target_Mg_g_per_g, biomass, available.mg,
                                root_capacity_factor, cp));

    // ---- 4. 뿌리 건강도 ----
    const RootDamageParams& rd = params.root_damage;
    // 아질산 경보와 같은 정의를 써요 (nitrification.cpp)
    const double effective_nitrite = non_negative(effective_nitrite_g_N_per_m2(cell, params.nitrification));
    // RootDamageCause 순서 그대로 (동률이면 앞선 원인)
    const std::array<double, 4> damage_terms{
        rd.waterlog_curve(cell.aeration_index),
        rd.heat_curve(cell.soil_temp_degC),
        rd.nitrite_curve(effective_nitrite),
        rd.acid_curve(cell.ph),
    };
    double damage_per_hour = 0.0;
    std::size_t dominant_cause_index = 0;
    for (std::size_t i = 0; i < damage_terms.size(); ++i) {
        damage_per_hour += damage_terms[i];
        if (damage_terms[i] > damage_terms[dominant_cause_index]) {
            dominant_cause_index = i;
        }
    }
    if (damage_per_hour > 0.0) {
        crop.root_health *= 1.0 - std::min(1.0, damage_per_hour);
    } else {
        crop.root_health += cp.root_recovery_rate_per_hour * (1.0 - crop.root_health);
    }
    crop.root_health = std::clamp(crop.root_health, 0.0, 1.0);

    // ---- 4b. 뿌리 고사: 손상 비율만큼 뿌리 건물이 죽어 양분과 C가 토양으로 (계 안의 이동) ----
    if (damage_per_hour > 0.0) {
        const double pre_death_biomass = biomass_g_per_m2(crop);
        const double dead_root_g_per_m2 = std::min(
            non_negative(crop.root_g_per_m2),
            non_negative(crop.root_g_per_m2) * std::min(1.0, damage_per_hour) * cp.root_dieback_per_damage);
        if (dead_root_g_per_m2 > 0.0 && pre_death_biomass > 0.0) {
            const double share = dead_root_g_per_m2 / pre_death_biomass;
            crop.root_g_per_m2 -= dead_root_g_per_m2;
            const auto release = [share](double& plant_pool_g_per_m2, double& soil_pool_g_per_m2) {
                const double moved_g_per_m2 = non_negative(plant_pool_g_per_m2) * share;
                plant_pool_g_per_m2 -= moved_g_per_m2;
                soil_pool_g_per_m2 += moved_g_per_m2;
            };
            release(crop.plant_N_g_per_m2, cell.labile_N_g_per_m2);
            release(crop.plant_P_g_per_m2, cell.p_g_per_m2);
            release(crop.plant_K_g_per_m2, cell.k_g_per_m2);
            release(crop.plant_Ca_g_per_m2, cell.ca_g_per_m2);
            release(crop.plant_Mg_g_per_m2, cell.mg_g_per_m2);
            const double litter_C_g_per_m2 = dead_root_g_per_m2 * cp.dry_matter_C_fraction;
            cell.labile_C_g_per_m2 += litter_C_g_per_m2;
            ledger.C_root_litter_g += litter_C_g_per_m2;
        }
    }

    if (crop.root_health < cp.death_root_health) {
        crop.alive = false;
        return PlantDiedEvent{.tile_index = tile_index,
                              .cause = static_cast<RootDamageCause>(dominant_cause_index)};
    }
    return std::nullopt;
}

}  // namespace sim::process
