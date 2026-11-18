// docs/spec/strawberry-minimal.md + M4 strawberry-fruit.md, house-climate.md "저온 피해"
// 작물: 수관·증산 요구, 저온 피해, 생물계절 호출, 스트레스, 총생산·유지 호흡·분배, 과실 진행·품질·수확,
// 흡수(요구량에 관부·과실, 활착 배율), 뿌리 건강도·고사, 낙엽
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

// 단위 환산: 1 W·h = 3600 J = 0.0036 MJ
constexpr double kMJ_per_W_h = 0.0036;

[[nodiscard]] double non_negative(double value) { return std::max(0.0, value); }

[[nodiscard]] double leaf_area_index(const Crop& crop, const CropParams& params) {
    return non_negative(crop.leaf_g_per_m2) * params.specific_leaf_area_m2_per_g;
}

// 체내 양분 / 요구량. 요구량이 0이면(건물량 0) 충족된 것으로 봐요 (비율 1).
[[nodiscard]] double nutrient_ratio(double plant_g_per_m2, double required_g_per_m2) {
    if (!(required_g_per_m2 > 0.0)) {
        return 1.0;
    }
    return non_negative(plant_g_per_m2) / required_g_per_m2;
}

// 수요 = max(0, 요구량 × 상한 배수 × 상한 배율 − 체내 양).
[[nodiscard]] double demand_g_per_m2(double plant_g_per_m2, double required_g_per_m2, double cap_ratio,
                                     double cap_factor) {
    return non_negative(required_g_per_m2 * cap_ratio * cap_factor - plant_g_per_m2);
}

// 토양 풀 → 체내 풀 이동. 토양 풀이 가진 양을 넘지 않아요. 실제 이동량을 돌려줘요.
double transfer_g_per_m2(double& soil_pool_g_per_m2, double& plant_pool_g_per_m2, double amount_g_per_m2) {
    const double moved_g_per_m2 = std::min(non_negative(amount_g_per_m2), non_negative(soil_pool_g_per_m2));
    soil_pool_g_per_m2 -= moved_g_per_m2;
    plant_pool_g_per_m2 += moved_g_per_m2;
    return moved_g_per_m2;
}

// M3: 용액에서 흡수. 총량과 용액에서 같은 양을 빼요. 용액·총량을 넘지 않아요.
double transfer_from_solution(double& soil_total_g_per_m2, double& soil_solution_g_per_m2, double& plant_pool_g_per_m2,
                              double amount_g_per_m2) {
    const double moved_g_per_m2 = std::min({non_negative(amount_g_per_m2), non_negative(soil_solution_g_per_m2),
                                            non_negative(soil_total_g_per_m2)});
    soil_total_g_per_m2 -= moved_g_per_m2;
    soil_solution_g_per_m2 =
        std::clamp(soil_solution_g_per_m2 - moved_g_per_m2, 0.0, non_negative(soil_total_g_per_m2));
    plant_pool_g_per_m2 += moved_g_per_m2;
    return moved_g_per_m2;
}

// 죽은 건물(뿌리 고사·낙엽·언 잎·언 과실)이 토양으로: 체내 양분 × share, C = 건물 × C 비율 → labile_C.
// share = 죽은 건물 / 죽기 전 전체 건물(과실 포함). 양분은 생체량에 고르게 분포한다고 단순화.
// 돌려준 C를 돌려줘요 (호출자가 장부에 기록).
double return_litter(Crop& crop, SoilCell& cell, const CropParams& params, double dead_dry_g_per_m2,
                     double biomass_before_g_per_m2) {
    if (!(dead_dry_g_per_m2 > 0.0) || !(biomass_before_g_per_m2 > 0.0)) {
        return 0.0;
    }
    const double share = std::min(1.0, dead_dry_g_per_m2 / biomass_before_g_per_m2);
    const auto release = [share](double& plant_pool_g_per_m2, double& soil_pool_g_per_m2) {
        const double moved_g_per_m2 = non_negative(plant_pool_g_per_m2) * share;
        plant_pool_g_per_m2 -= moved_g_per_m2;
        soil_pool_g_per_m2 += moved_g_per_m2;
        return moved_g_per_m2;
    };
    release(crop.plant_N_g_per_m2, cell.labile_N_g_per_m2);
    release(crop.plant_P_g_per_m2, cell.p_g_per_m2);
    // M3: 양이온은 총량과 용액에
    cell.k_solution_g_per_m2 += release(crop.plant_K_g_per_m2, cell.k_g_per_m2);
    cell.ca_solution_g_per_m2 += release(crop.plant_Ca_g_per_m2, cell.ca_g_per_m2);
    cell.mg_solution_g_per_m2 += release(crop.plant_Mg_g_per_m2, cell.mg_g_per_m2);
    cell.mn_solution_g_per_m2 += release(crop.plant_Mn_g_per_m2, cell.mn_g_per_m2);
    const double litter_C_g_per_m2 = dead_dry_g_per_m2 * params.dry_matter_C_fraction;
    cell.labile_C_g_per_m2 += litter_C_g_per_m2;
    return litter_C_g_per_m2;
}

// h0 저온 피해 (house-climate "저온 피해"): 잎은 낙엽 규칙으로, 과실·꽃은 개체마다 난수, 안 핀 화방의 꽃은 비율로
void apply_cold_damage(Crop& crop, SoilCell& cell, const CropParams& params, double air_degC, Pcg32& biology_rng,
                       Ledger& ledger) {
    const double leaf_loss_fraction = std::min(1.0, non_negative(params.cold_leaf_damage_curve(air_degC)));
    if (leaf_loss_fraction > 0.0) {
        const double before = total_biomass_g_per_m2(crop);
        const double dead_leaf_g_per_m2 = non_negative(crop.leaf_g_per_m2) * leaf_loss_fraction;
        crop.leaf_g_per_m2 -= dead_leaf_g_per_m2;
        ledger.C_leaf_litter_g += return_litter(crop, cell, params, dead_leaf_g_per_m2, before);
    }
    const double fruit_loss_fraction = std::min(1.0, non_negative(params.cold_fruit_loss_curve(air_degC)));
    if (fruit_loss_fraction > 0.0) {
        for (Truss& truss : crop.trusses) {
            if (!truss.opened) {
                truss.flowers_per_m2 *= 1.0 - fruit_loss_fraction;
            }
        }
        const double before = total_biomass_g_per_m2(crop);
        double lost_dry_g_per_m2 = 0.0;
        for (Fruit& fruit : crop.fruits) {
            if (biology_rng.next_unit_double() < fruit_loss_fraction) {
                lost_dry_g_per_m2 += non_negative(fruit.dry_g);
                fruit.dry_g = 0.0;
                fruit.weight_sum = -1.0;  // 제거 표시
            }
        }
        std::erase_if(crop.fruits, [](const Fruit& f) { return f.weight_sum < 0.0; });
        // 언 과실은 버림: 낙엽처럼 토양에 (C는 C_leaf_litter_g, strawberry-fruit "보존·불변 조건")
        ledger.C_leaf_litter_g += return_litter(crop, cell, params, lost_dry_g_per_m2, before);
    }
}

// 당도 온도 점수: S̄_day, S̄_night, K_temp (한쪽 가중치 합이 0이면 다른 쪽만, 둘 다 0이면 1)
struct TempScores {
    double day = 0.0;
    double night = 0.0;
    double k_temp = 1.0;
};
TempScores temp_scores(const Fruit& fruit, const FruitParams& fp) {
    TempScores s;
    const bool has_day = fruit.day_weight_sum > 0.0;
    const bool has_night = fruit.night_weight_sum > 0.0;
    s.day = has_day ? fruit.day_score_sum / fruit.day_weight_sum : 0.0;
    s.night = has_night ? fruit.night_score_sum / fruit.night_weight_sum : 0.0;
    if (has_day && has_night) {
        s.k_temp = fp.brix_day_weight * s.day + fp.brix_night_weight * s.night;
    } else if (has_day) {
        s.k_temp = s.day;
    } else if (has_night) {
        s.k_temp = s.night;
    }
    return s;
}

// 6b 낙과
// 5차 (디렉터 승인): p_tick = abortion_rate_per_development_index × dD × curve(크기 충족도) × rank_weight.
// 같은 부족이면 계절(진행 속도)과 무관하게 같은 비율이 떨어져요.
void abort_young_fruits(Crop& crop, SoilCell& cell, const Params& params, double development_increment,
                        Pcg32& biology_rng, Ledger& ledger) {
    const FruitParams& fp = params.fruit;
    const double base = std::max(0.0, fp.abortion_rate_per_development_index) * std::max(0.0, development_increment) *
                        fp.abortion_satisfaction_curve(crop.size_satisfaction_mean);
    if (!(base > 0.0)) {
        return;
    }
    const double before = total_biomass_g_per_m2(crop);
    double lost_dry_g = 0.0;
    std::uint64_t aborted = 0;
    for (Fruit& fruit : crop.fruits) {
        if (!fruit.set || fruit.development_index >= fp.abortion_window_index) {
            continue;
        }
        const std::size_t rank = std::min<std::size_t>(fruit.rank, fp.rank_abortion_weight.size() - 1);
        const double p = base * fp.rank_abortion_weight[rank];
        if (biology_rng.next_unit_double() < p) {
            if (fruit.truss_index < crop.trusses.size()) {
                ++crop.trusses[fruit.truss_index].aborted_fruits;
            }
            lost_dry_g += non_negative(fruit.dry_g);
            fruit.dry_g = 0.0;
            fruit.weight_sum = -1.0;  // 제거 표시
            ++aborted;
        }
    }
    if (aborted > 0) {
        std::erase_if(crop.fruits, [](const Fruit& f) { return f.weight_sum < 0.0; });
        crop.harvest.aborted_fruits += aborted;
        ledger.C_leaf_litter_g += return_litter(crop, cell, params.crop, lost_dry_g, before);
    }
}

// 자동 수확 (strawberry-fruit 6번). 딴 과실을 목록에서 지우고 수확 기록·장부·이벤트를 만들어요.
std::optional<FruitHarvestedEvent> harvest_ripe_fruits(Crop& crop, const Params& params, std::uint64_t tick,
                                                       std::uint32_t tile_index, Ledger& ledger) {
    const FruitParams& fp = params.fruit;
    const CropParams& cp = params.crop;
    bool any_ripe = false;
    for (const Fruit& fruit : crop.fruits) {
        any_ripe = any_ripe || (fruit.set && fruit.development_index >= 1.0);
    }
    if (!any_ripe || !(fp.dry_matter_fraction > 0.0)) {
        return std::nullopt;
    }
    // 반출 비율은 수확 전 포기 전체의 체내량 ÷ 요구량
    const NutrientRequirement required = nutrient_requirement(crop, cp);
    std::uint32_t normal_count = 0;
    std::uint32_t deformed_count = 0;
    double harvested_dry_g = 0.0;
    double fresh_total_g = 0.0;
    double normal_fresh_g = 0.0;
    double brix_weighted = 0.0;
    double firmness_weighted = 0.0;
    HarvestRecord& record = crop.harvest;
    if (record.harvested_size_class_counts.size() != fp.size_class_bounds_g.size() + 1) {
        record.harvested_size_class_counts.resize(fp.size_class_bounds_g.size() + 1, 0);
    }
    for (Fruit& fruit : crop.fruits) {
        if (!fruit.set || fruit.development_index < 1.0) {
            continue;
        }
        const double dry_g = non_negative(fruit.dry_g);
        const double fresh_g = dry_g / fp.dry_matter_fraction;
        harvested_dry_g += dry_g;
        if (fruit.truss_index < crop.trusses.size()) {
            Truss& truss = crop.trusses[fruit.truss_index];
            if (truss.harvested_fruits == 0) {
                truss.first_harvest_tick = tick;
            }
            ++truss.harvested_fruits;
            truss.harvested_fresh_g += fresh_g;
            truss.last_harvest_tick = tick;
        }
        fresh_total_g += fresh_g;
        if (fruit.deformed) {
            ++deformed_count;
            ++record.harvested_deformed;
            record.harvested_deformed_fresh_g += fresh_g;
        } else {
            const TempScores scores = temp_scores(fruit, fp);
            const double brix = fruit.weight_sum > 0.0 ? fp.base_brix * fruit.brix_factor_sum / fruit.weight_sum *
                                                             fp.brix_temp_score_curve(scores.k_temp)
                                                       : 0.0;
            record.harvested_day_score_weighted += scores.day * fresh_g;
            record.harvested_night_score_weighted += scores.night * fresh_g;
            record.harvested_temp_score_weighted += scores.k_temp * fresh_g;
            const double firmness = fruit.weight_sum > 0.0 ? fruit.firmness_factor_sum / fruit.weight_sum : 0.0;
            ++normal_count;
            ++record.harvested_fruits;
            record.harvested_fresh_g += fresh_g;
            record.harvested_brix_weighted += brix * fresh_g;
            record.harvested_firmness_weighted += firmness * fresh_g;
            ++record.harvested_size_class_counts[size_class_index(fp, fresh_g)];
            normal_fresh_g += fresh_g;
            brix_weighted += brix * fresh_g;
            firmness_weighted += firmness * fresh_g;
        }
        fruit.weight_sum = -1.0;  // 제거 표시 (set인 과실은 weight_sum ≥ 0)
    }
    std::erase_if(crop.fruits, [](const Fruit& f) { return f.weight_sum < 0.0; });
    if (!record.has_first_harvest) {
        record.has_first_harvest = true;
        record.first_harvest_tick = tick;
    }
    // 양분 반출: min(plant_X, 수확 건물 × fruit_target_X × min(1, ratio_X))
    const auto export_nutrient = [harvested_dry_g](double& plant_pool_g_per_m2, double fruit_target_g_per_g,
                                                    double required_g_per_m2, double& ledger_g) {
        const double ratio = std::min(1.0, nutrient_ratio(plant_pool_g_per_m2, required_g_per_m2));
        const double moved = std::min(non_negative(plant_pool_g_per_m2), harvested_dry_g * fruit_target_g_per_g * ratio);
        plant_pool_g_per_m2 -= moved;
        ledger_g += moved;
    };
    export_nutrient(crop.plant_N_g_per_m2, cp.fruit_target_N_g_per_g, required.N, ledger.N_harvested_g);
    export_nutrient(crop.plant_P_g_per_m2, cp.fruit_target_P_g_per_g, required.P, ledger.P_harvested_g);
    export_nutrient(crop.plant_K_g_per_m2, cp.fruit_target_K_g_per_g, required.K, ledger.K_harvested_g);
    export_nutrient(crop.plant_Ca_g_per_m2, cp.fruit_target_Ca_g_per_g, required.Ca, ledger.Ca_harvested_g);
    export_nutrient(crop.plant_Mg_g_per_m2, cp.fruit_target_Mg_g_per_g, required.Mg, ledger.Mg_harvested_g);
    export_nutrient(crop.plant_Mn_g_per_m2, cp.fruit_target_Mn_g_per_g, required.Mn, ledger.Mn_harvested_g);

    return FruitHarvestedEvent{
        .tile_index = tile_index,
        .fruits = normal_count,
        .deformed = deformed_count,
        .fresh_g = fresh_total_g,
        .mean_brix = normal_fresh_g > 0.0 ? brix_weighted / normal_fresh_g : 0.0,
        .mean_firmness = normal_fresh_g > 0.0 ? firmness_weighted / normal_fresh_g : 0.0,
    };
}

// (C) 따라잡기 수요: 이번 틱 진행도에서의 목표 건물 − 현재 건물 (밤에 못 받은 몫도 다음 낮에 채워요)
double fruit_catch_up_demand_g(const Fruit& fruit, const FruitParams& fp, double development_increment) {
    const double next = std::min(1.0, fruit.development_index + development_increment);
    return non_negative(fruit.potential_fresh_g * fp.dry_matter_fraction * fp.growth_shape_curve(next) - fruit.dry_g);
}

}  // namespace

const SeedlingGradeDef* find_seedling_grade(const Params& params, std::string_view id) {
    for (const SeedlingGradeDef& grade : params.seedling_grades) {
        if (grade.id == id) {
            return &grade;
        }
    }
    return nullptr;
}

Crop plant_crop(const SeedlingGradeDef& grade, double plants_per_m2, const Params& params) {
    Crop crop;
    crop.present = true;
    crop.alive = true;
    crop.plants_per_m2 = plants_per_m2;
    crop.leaf_g_per_m2 = grade.leaf_g_per_plant * plants_per_m2;
    crop.crown_g_per_m2 = grade.crown_g_per_plant * plants_per_m2;
    crop.root_g_per_m2 = grade.root_g_per_plant * plants_per_m2;
    crop.induction_index = grade.initial_induction_index;
    crop.establishment_index = 0.0;
    crop.root_health = 1.0;
    const NutrientRequirement required = nutrient_requirement(crop, params.crop);
    crop.plant_N_g_per_m2 = required.N;
    crop.plant_P_g_per_m2 = required.P;
    crop.plant_K_g_per_m2 = required.K;
    crop.plant_Ca_g_per_m2 = required.Ca;
    crop.plant_Mg_g_per_m2 = required.Mg;
    crop.plant_Mn_g_per_m2 = required.Mn;
    crop.harvest.harvested_size_class_counts.assign(params.fruit.size_class_bounds_g.size() + 1, 0);
    return crop;
}

double vegetative_biomass_g_per_m2(const Crop& crop) {
    return non_negative(crop.leaf_g_per_m2) + non_negative(crop.crown_g_per_m2) + non_negative(crop.root_g_per_m2);
}

double total_biomass_g_per_m2(const Crop& crop) { return vegetative_biomass_g_per_m2(crop) + crop.fruit_g_per_m2(); }

NutrientRequirement nutrient_requirement(const Crop& crop, const CropParams& params) {
    const double vegetative = vegetative_biomass_g_per_m2(crop);
    const double fruit = crop.fruit_g_per_m2();
    return NutrientRequirement{
        .N = params.target_N_g_per_g * vegetative + params.fruit_target_N_g_per_g * fruit,
        .P = params.target_P_g_per_g * vegetative + params.fruit_target_P_g_per_g * fruit,
        .K = params.target_K_g_per_g * vegetative + params.fruit_target_K_g_per_g * fruit,
        .Ca = params.target_Ca_g_per_g * vegetative + params.fruit_target_Ca_g_per_g * fruit,
        .Mg = params.target_Mg_g_per_g * vegetative + params.fruit_target_Mg_g_per_g * fruit,
        .Mn = params.target_Mn_g_per_g * vegetative + params.fruit_target_Mn_g_per_g * fruit,
    };
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
    const NutrientRequirement required = nutrient_requirement(crop, params);
    const double ratio_N = nutrient_ratio(crop.plant_N_g_per_m2, required.N);
    const double ratio_P = nutrient_ratio(crop.plant_P_g_per_m2, required.P);
    const double ratio_K = nutrient_ratio(crop.plant_K_g_per_m2, required.K);
    const double ratio_Ca = nutrient_ratio(crop.plant_Ca_g_per_m2, required.Ca);
    const double ratio_Mg = nutrient_ratio(crop.plant_Mg_g_per_m2, required.Mg);
    const double ratio_Mn = nutrient_ratio(crop.plant_Mn_g_per_m2, required.Mn);
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
        .sufficiency_Mn = std::min(1.0, ratio_Mn),
        .excess_Mn = non_negative(ratio_Mn - 1.0),
        .plant_Mn_ratio = ratio_Mn,
    };
}

std::uint64_t thin_flowers(Crop& crop, SoilCell& cell, const Params& params, double per_plant, Pcg32& biology_rng,
                           Ledger& ledger) {
    if (!crop.present || !crop.alive) {
        return 0;
    }
    const FruitParams& fp = params.fruit;
    const double wanted = std::max(0.0, per_plant) * std::max(0.0, crop.plants_per_m2);
    const double whole = std::floor(wanted);
    const double u = biology_rng.next_unit_double();  // 소수부 난수 반올림 (항상 1회)
    auto count = static_cast<std::uint64_t>(whole) + (u < wanted - whole ? 1u : 0u);
    // 후보: 꽃(아직 안 핀 것) + 진행도 < thinnable_max_index 인 어린 과실
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < crop.fruits.size(); ++i) {
        const Fruit& f = crop.fruits[i];
        if (!f.set || f.development_index < fp.thinnable_max_index) {
            candidates.push_back(i);
        }
    }
    // 과위가 높은 것부터, 같은 과위는 늦게 핀 것부터 (같으면 목록 뒤쪽부터)
    std::stable_sort(candidates.begin(), candidates.end(), [&](std::size_t a, std::size_t b) {
        const Fruit& fa = crop.fruits[a];
        const Fruit& fb = crop.fruits[b];
        if (fa.rank != fb.rank) {
            return fa.rank > fb.rank;
        }
        if (fa.anthesis_tick != fb.anthesis_tick) {
            return fa.anthesis_tick > fb.anthesis_tick;
        }
        return a > b;
    });
    count = std::min<std::uint64_t>(count, candidates.size());
    if (count == 0) {
        return 0;
    }
    const double before = total_biomass_g_per_m2(crop);
    double lost_dry_g = 0.0;
    for (std::uint64_t k = 0; k < count; ++k) {
        Fruit& f = crop.fruits[candidates[k]];
        lost_dry_g += non_negative(f.dry_g);
        f.dry_g = 0.0;
        f.set = false;
        f.weight_sum = -1.0;  // 제거 표시
    }
    std::erase_if(crop.fruits, [](const Fruit& f) { return f.weight_sum < 0.0; });
    crop.harvest.thinned_fruits += count;
    ledger.C_leaf_litter_g += return_litter(crop, cell, params.crop, lost_dry_g, before);
    return count;
}

CropTickResult update_crop(Crop& crop, SoilCell& cell, const Params& params, const NutrientAvailability& available,
                           const CropEnvironment& env, Pcg32& biology_rng, Ledger& ledger) {
    CropTickResult result;
    if (!crop.present || !crop.alive) {
        return result;
    }
    const CropParams& cp = params.crop;
    const FruitParams& fp = params.fruit;
    const double air_degC = env.house_air_temp_degC;

    // ---- h0. 저온 피해 ----
    apply_cold_damage(crop, cell, cp, air_degC, biology_rng, ledger);

    // ---- h1. 생물계절 (활착 → 화아분화 → 화방 발달·꽃 만들기) → 개화 판정 ----
    const double growth_factor_establish = update_phenology(crop, params, env, biology_rng);
    judge_anthesis(crop, fp, env.house_air_temp_mean_degC, env.tick, biology_rng);

    // ---- h2. 스트레스 계수 ----
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
        cp.nutrient_stress_curve(status.sufficiency_Mn),
        cp.mn_toxicity_curve(status.plant_Mn_ratio),
    };
    std::size_t limiting_index = 0;
    for (std::size_t i = 1; i < factors.size(); ++i) {
        if (factors[i] < factors[limiting_index]) {
            limiting_index = i;
        }
    }
    double combined_factor = 1.0;
    if (env.mode == StressCombination::Product) {
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

    // ---- h3. 총생산 − 유지 호흡 → 분배 ----
    const double interception = canopy_interception(crop, cp);
    const double potential_g_per_m2 = cp.light_use_efficiency_g_per_MJ * non_negative(env.inside_solar_W_per_m2) *
                                      cp.par_fraction * kMJ_per_W_h * interception * cp.temp_curve(air_degC);
    const double gross_g_per_m2 = non_negative(potential_g_per_m2 * cp.co2_growth_curve(env.house_co2_ppm) *
                                               combined_factor * growth_factor_establish);
    const double fruit_dry_g_per_m2 = crop.fruit_g_per_m2();
    const double respiration_g_per_m2 =
        non_negative((non_negative(crop.leaf_g_per_m2) * cp.maintenance_rate_leaf_per_hour +
                      non_negative(crop.crown_g_per_m2) * cp.maintenance_rate_crown_per_hour +
                      non_negative(crop.root_g_per_m2) * cp.maintenance_rate_root_per_hour +
                      fruit_dry_g_per_m2 * cp.maintenance_rate_fruit_per_hour) *
                     cp.maintenance_temp_curve(air_degC));
    const double net_g_per_m2 = gross_g_per_m2 - respiration_g_per_m2;

    // 과실 잠재 증가 (착과된 과실, 이번 틱 진행도 증가분 기준)
    const double development_increment = fruit_development_increment(fp, air_degC);
    double fruit_demand_g_per_m2 = 0.0;
    for (const Fruit& fruit : crop.fruits) {
        if (!fruit.set) {
            continue;
        }
        fruit_demand_g_per_m2 += fruit_catch_up_demand_g(fruit, fp, development_increment);
    }
    const double fruit_supply_g_per_m2 =
        net_g_per_m2 > 0.0 ? std::min(fruit_demand_g_per_m2, net_g_per_m2 * cp.max_fruit_assimilate_fraction) : 0.0;
    const double supply_ratio =
        fruit_demand_g_per_m2 > 0.0 ? std::clamp(fruit_supply_g_per_m2 / fruit_demand_g_per_m2, 0.0, 1.0) : 1.0;
    if (fruit_supply_g_per_m2 > 0.0) {
        for (Fruit& fruit : crop.fruits) {
            if (!fruit.set) {
                continue;
            }
            fruit.dry_g += fruit_catch_up_demand_g(fruit, fp, development_increment) * supply_ratio;
        }
    }
    if (net_g_per_m2 > 0.0) {
        const double rest_g_per_m2 = non_negative(net_g_per_m2 - fruit_supply_g_per_m2);
        crop.leaf_g_per_m2 += rest_g_per_m2 * cp.vegetative_leaf_fraction;
        crop.crown_g_per_m2 += rest_g_per_m2 * cp.vegetative_crown_fraction;
        crop.root_g_per_m2 += rest_g_per_m2 * cp.vegetative_root_fraction;
    } else if (net_g_per_m2 < 0.0) {
        // 부족분을 잎·관부·뿌리에서 건물 비율대로 (과실은 줄이지 않음). 탄소는 공기로(장부 밖), 체내 양분은 그대로.
        const double vegetative = vegetative_biomass_g_per_m2(crop);
        if (vegetative > 0.0) {
            const double keep = 1.0 - std::min(1.0, -net_g_per_m2 / vegetative);
            crop.leaf_g_per_m2 = non_negative(crop.leaf_g_per_m2) * keep;
            crop.crown_g_per_m2 = non_negative(crop.crown_g_per_m2) * keep;
            crop.root_g_per_m2 = non_negative(crop.root_g_per_m2) * keep;
        }
    }
    crop.last_gross_g_per_m2 = gross_g_per_m2;
    crop.last_respiration_g_per_m2 = respiration_g_per_m2;
    crop.last_growth_g_per_m2 = net_g_per_m2;
    crop.last_supply_ratio = supply_ratio;
    // (P2) 당도용 크기 충족도: 착과된 과실의 Σ건물 ÷ Σ(최대 생체중 × 건물 비율 × shape(이번 틱 진행도)), 과실 없으면 1.
    //      순생산 > 0인 틱에만, 하우스 기온 이동 평균과 같은 완화 비율로 이동 평균
    if (net_g_per_m2 > 0.0) {
        double fruit_dry_g = 0.0;
        double fruit_target_g = 0.0;
        for (const Fruit& fruit : crop.fruits) {
            if (!fruit.set) {
                continue;
            }
            const double next = std::min(1.0, fruit.development_index + development_increment);
            fruit_dry_g += non_negative(fruit.dry_g);
            fruit_target_g += non_negative(fruit.potential_fresh_g * fp.dry_matter_fraction * fp.growth_shape_curve(next));
        }
        const double size_satisfaction = fruit_target_g > 0.0 ? std::clamp(fruit_dry_g / fruit_target_g, 0.0, 1.0) : 1.0;
        crop.size_satisfaction_mean += (size_satisfaction - crop.size_satisfaction_mean) *
                                       std::min(1.0, params.house.temp_mean_relax_rate_per_hour);
    }

    // ---- h4. 과실 진행·품질 적산 → 자동 수확 ----
    {
        const double q_brix = fp.brix_supply_curve(crop.size_satisfaction_mean) *
                              fp.brix_k_curve(status.sufficiency_K) * fp.brix_n_excess_curve(status.excess_N) *
                              fp.brix_water_curve(cell.relative_water());
        const double q_firmness = fp.firmness_ca_curve(status.sufficiency_Ca) * fp.firmness_temp_curve(air_degC) *
                                  fp.firmness_n_excess_curve(status.excess_N);
        for (Fruit& fruit : crop.fruits) {
            if (!fruit.set) {
                continue;
            }
            fruit.development_index = std::min(1.0, fruit.development_index + development_increment);
            fruit.brix_factor_sum += q_brix * development_increment;
            fruit.firmness_factor_sum += q_firmness * development_increment;
            fruit.weight_sum += development_increment;
            // 당도 온도 점수 (주·야간 이중 가중): 낮은 일사 가중, 밤은 시간 가중 (Δt = 1틱)
            if (env.inside_solar_W_per_m2 > fp.day_solar_threshold_W_per_m2) {
                fruit.day_score_sum += fp.brix_day_temp_score_curve(air_degC) * env.inside_solar_W_per_m2;
                fruit.day_weight_sum += env.inside_solar_W_per_m2;
            } else {
                fruit.night_score_sum += fp.brix_night_temp_score_curve(air_degC);
                fruit.night_weight_sum += 1.0;
            }
        }
        result.harvested = harvest_ripe_fruits(crop, params, env.tick, env.tile_index, ledger);
        // 6b 낙과: 착과된 어린 과실(진행도 < abortion_window_index)마다 난수 < p 이면 떨어짐 (biology 스트림)
        abort_young_fruits(crop, cell, params, development_increment, biology_rng, ledger);
    }

    // ---- h5. 흡수 (요구량에 관부·과실 포함, 흡수 능력에 활착 배율) ----
    const NutrientRequirement required = nutrient_requirement(crop, cp);
    const double root_capacity_factor =
        std::min(1.0, cp.uptake_rate_per_g_root_per_hour * non_negative(crop.root_g_per_m2) * crop.root_health) *
        water_salt.water * water_salt.salt * growth_factor_establish;

    // M3 길항 (cation-exchange "흡수 길항"): h 시작 시점 용액 상태로 한 번 계산.
    {
        const IonParams& ions = params.ions;
        const double monovalent_eq = non_negative(cell.k_solution_g_per_m2) / ions.k_g_per_eq +
                                     non_negative(cell.nh4_solution_N_g_per_m2) / ions.nh4_N_g_per_eq;
        const double divalent_eq = non_negative(cell.ca_solution_g_per_m2) / ions.ca_g_per_eq +
                                   non_negative(cell.mg_solution_g_per_m2) / ions.mg_g_per_eq;
        if (divalent_eq > 0.0) {
            const double antagonism_index = monovalent_eq / divalent_eq;
            crop.antagonism_Ca_factor = params.antagonism.ca_curve(antagonism_index);
            crop.antagonism_Mg_factor = params.antagonism.mg_curve(antagonism_index);
        } else {
            crop.antagonism_Ca_factor = 1.0;
            crop.antagonism_Mg_factor = 1.0;
        }
    }

    // N: NH4+와 NO3- 유효량 비율대로 나눠 가져감
    {
        const double available_nh4_g_per_m2 = non_negative(available.nh4_N);
        const double available_no3_g_per_m2 = non_negative(available.no3_N);
        const double available_N_g_per_m2 = available_nh4_g_per_m2 + available_no3_g_per_m2;
        // (E) N 전용 사치 흡수 상한·흡수 속도
        const double n_capacity_factor =
            std::min(1.0, cp.n_uptake_rate_per_g_root_per_hour * non_negative(crop.root_g_per_m2) * crop.root_health) *
            water_salt.water * water_salt.salt * growth_factor_establish;
        const double uptake_N_g_per_m2 =
            std::min(demand_g_per_m2(crop.plant_N_g_per_m2, required.N, cp.n_luxury_uptake_ratio, 1.0),
                     available_N_g_per_m2 * n_capacity_factor);
        double nh4_uptake_g_per_m2 = 0.0;
        double no3_uptake_g_per_m2 = 0.0;
        if (available_N_g_per_m2 > 0.0) {
            nh4_uptake_g_per_m2 = transfer_from_solution(
                cell.nh4_N_g_per_m2, cell.nh4_solution_N_g_per_m2, crop.plant_N_g_per_m2,
                uptake_N_g_per_m2 * (available_nh4_g_per_m2 / available_N_g_per_m2));
            no3_uptake_g_per_m2 =
                transfer_g_per_m2(cell.no3_N_g_per_m2, crop.plant_N_g_per_m2,
                                  uptake_N_g_per_m2 * (available_no3_g_per_m2 / available_N_g_per_m2));
        }
        ledger.N_uptake_g += nh4_uptake_g_per_m2 + no3_uptake_g_per_m2;
        const double nh4_uptake_eq_per_m2 = nh4_uptake_g_per_m2 / params.ions.nh4_N_g_per_eq;
        const double no3_uptake_eq_per_m2 = no3_uptake_g_per_m2 / params.ions.no3_N_g_per_eq;
        cell.pending_H_eq_per_m2 += nh4_uptake_eq_per_m2 * params.soil_chemistry.uptake_H_eq_per_eq_NH4 +
                                    no3_uptake_eq_per_m2 * params.soil_chemistry.uptake_H_eq_per_eq_NO3;
    }
    ledger.P_uptake_g += transfer_g_per_m2(
        cell.p_g_per_m2, crop.plant_P_g_per_m2,
        std::min(demand_g_per_m2(crop.plant_P_g_per_m2, required.P, cp.luxury_uptake_ratio, 1.0),
                 non_negative(available.p) * root_capacity_factor));
    ledger.K_uptake_g += transfer_from_solution(
        cell.k_g_per_m2, cell.k_solution_g_per_m2, crop.plant_K_g_per_m2,
        std::min(demand_g_per_m2(crop.plant_K_g_per_m2, required.K, cp.luxury_uptake_ratio, 1.0),
                 non_negative(available.k) * root_capacity_factor));

    // Ca·Mg (M3): 전용 흡수 속도, 길항 배율을 흡수 능력과 수요 상한에 (수요 쪽만 하한)
    const double root_mass_health_g_per_m2 = non_negative(crop.root_g_per_m2) * crop.root_health;
    const double ca_capacity_g_per_m2 =
        non_negative(available.ca) *
        (std::min(1.0, cp.ca_uptake_rate_per_g_root_per_hour * root_mass_health_g_per_m2) * water_salt.water *
         water_salt.salt * crop.antagonism_Ca_factor * growth_factor_establish);
    const double mg_capacity_g_per_m2 =
        non_negative(available.mg) *
        (std::min(1.0, cp.mg_uptake_rate_per_g_root_per_hour * root_mass_health_g_per_m2) * water_salt.water *
         water_salt.salt * crop.antagonism_Mg_factor * growth_factor_establish);
    const double ca_demand_g_per_m2 =
        demand_g_per_m2(crop.plant_Ca_g_per_m2, required.Ca, cp.luxury_uptake_ratio,
                        std::max(params.antagonism.min_demand_factor, crop.antagonism_Ca_factor));
    const double mg_demand_g_per_m2 =
        demand_g_per_m2(crop.plant_Mg_g_per_m2, required.Mg, cp.luxury_uptake_ratio,
                        std::max(params.antagonism.min_demand_factor, crop.antagonism_Mg_factor));
    ledger.Ca_uptake_g += transfer_from_solution(cell.ca_g_per_m2, cell.ca_solution_g_per_m2, crop.plant_Ca_g_per_m2,
                                                 std::min(ca_demand_g_per_m2, ca_capacity_g_per_m2));
    ledger.Mg_uptake_g += transfer_from_solution(cell.mg_g_per_m2, cell.mg_solution_g_per_m2, crop.plant_Mg_g_per_m2,
                                                 std::min(mg_demand_g_per_m2, mg_capacity_g_per_m2));

    // Mn (manganese.md "흡수"): 전용 흡수 속도, 높은 수요 상한, 길항 없음
    {
        const double mn_demand_g_per_m2 = demand_g_per_m2(crop.plant_Mn_g_per_m2, required.Mn, cp.mn_max_uptake_ratio, 1.0);
        const double mn_capacity_g_per_m2 =
            non_negative(available.mn) *
            std::min(1.0, cp.mn_uptake_rate_per_g_root_per_hour * non_negative(crop.root_g_per_m2) *
                              crop.root_health) *
            water_salt.water * water_salt.salt * growth_factor_establish;
        ledger.Mn_uptake_g += transfer_from_solution(cell.mn_g_per_m2, cell.mn_solution_g_per_m2,
                                                     crop.plant_Mn_g_per_m2,
                                                     std::min(mn_demand_g_per_m2, mn_capacity_g_per_m2));
    }

    // ---- h6. 뿌리 건강도 → 뿌리 고사 → 낙엽 → 고사 판정 ----
    const RootDamageParams& rd = params.root_damage;
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

    if (damage_per_hour > 0.0) {
        const double before = total_biomass_g_per_m2(crop);
        const double dead_root_g_per_m2 = std::min(
            non_negative(crop.root_g_per_m2),
            non_negative(crop.root_g_per_m2) * std::min(1.0, damage_per_hour) * cp.root_dieback_per_damage);
        if (dead_root_g_per_m2 > 0.0) {
            crop.root_g_per_m2 -= dead_root_g_per_m2;
            ledger.C_root_litter_g += return_litter(crop, cell, cp, dead_root_g_per_m2, before);
        }
    }

    // 낙엽 (strawberry-fruit 7번, 뿌리 고사와 같은 규칙)
    {
        const double before = total_biomass_g_per_m2(crop);
        const double dead_leaf_g_per_m2 =
            non_negative(crop.leaf_g_per_m2) * std::min(1.0, non_negative(cp.leaf_senescence_rate_per_hour));
        if (dead_leaf_g_per_m2 > 0.0) {
            crop.leaf_g_per_m2 -= dead_leaf_g_per_m2;
            ledger.C_leaf_litter_g += return_litter(crop, cell, cp, dead_leaf_g_per_m2, before);
        }
    }

    if (crop.root_health < cp.death_root_health) {
        crop.alive = false;
        result.died =
            PlantDiedEvent{.tile_index = env.tile_index, .cause = static_cast<RootDamageCause>(dominant_cause_index)};
    }
    return result;
}

}  // namespace sim::process
