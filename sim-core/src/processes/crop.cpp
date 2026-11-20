// docs/spec/strawberry-minimal.md + M4 strawberry-fruit.md, house-climate.md "저온 피해"
// 작물: 수관·증산 요구, 저온 피해, 생물계절 호출, 스트레스, 총생산·유지 호흡·분배, 과실 진행·품질·수확,
// 흡수(요구량에 관부·과실, 활착 배율), 뿌리 건강도·고사, 낙엽
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "fruit_list.hpp"
#include "sim/calendar.hpp"
#include "sim/processes.hpp"

namespace sim::process {

namespace {

// 단위 환산: 1 W·h = 3600 J = 0.0036 MJ
constexpr double kMJ_per_W_h = 0.0036;

[[nodiscard]] double non_negative(double value) { return std::max(0.0, value); }

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

// 충족도·과잉 지수 (분모 = 요구량, 관부·과실 포함)
NutrientStatus status_from_requirement(const Crop& crop, const NutrientRequirement& required) {
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
        // 과실·꽃마다 손실 판정 (목록 순서대로 난수 1회)
        detail::remove_fruits_if(crop.fruits, [&](const Fruit& fruit) {
            if (biology_rng.next_unit_double() < fruit_loss_fraction) {
                lost_dry_g_per_m2 += non_negative(fruit.dry_g);
                return true;
            }
            return false;
        });
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

// 6b 낙과 (디렉터 결정 12번): p_tick = abortion_rate_per_development_index × dD × curve(크기 충족도) × rank_weight.
// 같은 부족이면 계절(진행 속도)과 무관하게 같은 비율이 떨어져요.
// fruit_dry_g_per_m2: 들어올 때 지금 과실 건물 합, 나갈 때 남은 과실의 건물 합 (목록 순서)
void abort_young_fruits(Crop& crop, SoilCell& cell, const Params& params, double development_increment,
                        Pcg32& biology_rng, Ledger& ledger, double& fruit_dry_g_per_m2) {
    const FruitParams& fp = params.fruit;
    const double base = std::max(0.0, fp.abortion_rate_per_development_index) * std::max(0.0, development_increment) *
                        fp.abortion_satisfaction_curve(crop.size_satisfaction_mean);
    if (!(base > 0.0) || fp.rank_abortion_weight.empty()) {
        return;
    }
    const double before = vegetative_biomass_g_per_m2(crop) + fruit_dry_g_per_m2;
    double lost_dry_g = 0.0;
    double kept_dry_g = 0.0;
    std::uint64_t aborted = 0;
    // 착과된 어린 과실마다 목록 순서대로 난수 1회
    detail::remove_fruits_if(crop.fruits, [&](const Fruit& fruit) {
        if (fruit.set && fruit.development_index < fp.abortion_window_index) {
            const std::size_t rank = std::min<std::size_t>(fruit.rank, fp.rank_abortion_weight.size() - 1);
            const double p = base * fp.rank_abortion_weight[rank];
            if (biology_rng.next_unit_double() < p) {
                if (fruit.truss_index < crop.trusses.size()) {
                    ++crop.trusses[fruit.truss_index].aborted_fruits;
                }
                lost_dry_g += non_negative(fruit.dry_g);
                ++aborted;
                return true;
            }
        }
        kept_dry_g += fruit.dry_g;
        return false;
    });
    fruit_dry_g_per_m2 = kept_dry_g;
    if (aborted > 0) {
        crop.harvest.aborted_fruits += aborted;
        ledger.C_leaf_litter_g += return_litter(crop, cell, params.crop, lost_dry_g, before);
    }
}

// 자동 수확 (strawberry-fruit 6번). 익은 과실(진행도 ≥ 1)이 있을 때만 불러요.
// 딴 과실을 목록에서 지우고 수확 기록·장부·이벤트를 만들어요.
// fruit_dry_g_per_m2: 들어올 때 지금 과실 건물 합, 나갈 때 남은 과실의 건물 합 (목록 순서)
// M5b (foliar-disease 4.4): 딴 과실마다 mildew_prob > 0이면 Foliar 난수 u < p → 흰가루 과실 (정상과·기형과 집계에서 빠지고
// 장부 harvested_mildew에. 건물·양분은 수확 반출로 같이 나가요). foliar_rng가 없으면 판정하지 않아요.
std::optional<FruitHarvestedEvent> harvest_ripe_fruits(Crop& crop, const Params& params, std::uint64_t tick,
                                                       std::uint32_t tile_index, Ledger& ledger,
                                                       double& fruit_dry_g_per_m2, double mildew_prob,
                                                       Pcg32* foliar_rng) {
    const FruitParams& fp = params.fruit;
    const CropParams& cp = params.crop;
    if (!(fp.dry_matter_fraction > 0.0)) {
        return std::nullopt;
    }
    // 반출 비율은 수확 전 포기 전체의 체내량 ÷ 요구량
    const NutrientRequirement required = nutrient_requirement(crop, cp, fruit_dry_g_per_m2);
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
    double kept_dry_g = 0.0;
    std::uint32_t mildew_count = 0;
    detail::remove_fruits_if(crop.fruits, [&](const Fruit& fruit) {
        if (!fruit.set || fruit.development_index < 1.0) {
            kept_dry_g += fruit.dry_g;
            return false;
        }
        const double dry_g = non_negative(fruit.dry_g);
        const double fresh_g = dry_g / fp.dry_matter_fraction;
        harvested_dry_g += dry_g;
        if (mildew_prob > 0.0 && foliar_rng != nullptr && foliar_rng->next_unit_double() < mildew_prob) {
            ++mildew_count;
            ++ledger.harvested_mildew;
            ledger.harvested_mildew_fresh_g += fresh_g;
            return true;
        }
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
        return true;
    });
    fruit_dry_g_per_m2 = kept_dry_g;
    const bool marketable = normal_count + deformed_count > 0;
    if (!marketable && mildew_count == 0) {
        return std::nullopt;
    }
    if (marketable && !record.has_first_harvest) {
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

    if (!marketable) {
        return std::nullopt;  // 흰가루 과실만 딴 틱: 반출은 했지만 수확 이벤트는 없어요
    }
    return FruitHarvestedEvent{
        .tile_index = tile_index,
        .fruits = normal_count,
        .deformed = deformed_count,
        .fresh_g = fresh_total_g,
        .mean_brix = normal_fresh_g > 0.0 ? brix_weighted / normal_fresh_g : 0.0,
        .mean_firmness = normal_fresh_g > 0.0 ? firmness_weighted / normal_fresh_g : 0.0,
    };
}

// M5b 잿빛곰팡이 과실 썩음 (foliar-disease 4.3): 수확·낙과 뒤, 착과된 과실마다 목록 순서대로
// p = fruit_rot_prob × inf_b × fruit_stage_curve(development) > 0이면 Foliar 난수 u < p → 썩음.
// 썩은 과실의 건물·양분은 낙엽 규칙으로 토양에 (C는 C_leaf_litter_g). 아직 피지 않은 봉오리는 대상이 아니에요.
// fruit_dry_g_per_m2: 들어올 때 지금 과실 건물 합, 나갈 때 남은 과실의 건물 합
void rot_botrytis_fruits(Crop& crop, SoilCell& cell, const Params& params, Pcg32* foliar_rng, Ledger& ledger,
                         double& fruit_dry_g_per_m2) {
    const BotrytisParams& bp = params.foliar_disease.botrytis;
    const double base = non_negative(bp.fruit_rot_prob_per_hour) * std::clamp(crop.infection_botrytis_index, 0.0, 1.0);
    if (!(base > 0.0) || foliar_rng == nullptr) {
        return;
    }
    const FruitParams& fp = params.fruit;
    const double before = vegetative_biomass_g_per_m2(crop) + fruit_dry_g_per_m2;
    double lost_dry_g = 0.0;
    double kept_dry_g = 0.0;
    std::uint64_t rotten = 0;
    detail::remove_fruits_if(crop.fruits, [&](const Fruit& fruit) {
        if (fruit.set) {
            const double p = std::min(1.0, base * non_negative(bp.fruit_stage_curve(fruit.development_index)));
            if (p > 0.0 && foliar_rng->next_unit_double() < p) {
                const double dry_g = non_negative(fruit.dry_g);
                lost_dry_g += dry_g;
                ledger.fruit_rot_fresh_g += fp.dry_matter_fraction > 0.0 ? dry_g / fp.dry_matter_fraction : 0.0;
                ++rotten;
                return true;
            }
        }
        kept_dry_g += fruit.dry_g;
        return false;
    });
    fruit_dry_g_per_m2 = kept_dry_g;
    if (rotten > 0) {
        ledger.fruit_rot_count += rotten;
        crop.last_fruit_rot_count += static_cast<double>(rotten);
        ledger.C_leaf_litter_g += return_litter(crop, cell, params.crop, lost_dry_g, before);
    }
}

// 과실 목표 건물 = 최대 생체중 × 건물 비율 × shape(새 진행도 D′) (strawberry-fruit 4번).
// 따라잡기 수요는 목표 − 지금 건물이라 밤에 못 받은 몫도 다음 낮에 다시 요구해요.
double fruit_target_dry_g(const Fruit& fruit, const FruitParams& fp, double development_increment) {
    const double next = std::min(1.0, fruit.development_index + development_increment);
    return fruit.potential_fresh_g * fp.dry_matter_fraction * fp.growth_shape_curve(next);
}

}  // namespace

void return_dead_crop_to_soil(Crop& crop, SoilCell& cell, const CropParams& params, Ledger& ledger) {
    if (crop.present) {
        // 낙엽 규칙(share = 1): 체내 원소 전부를 토양에, 건물 × C 비율 → 이분해성 C
        const double leaf_fruit_g_per_m2 = non_negative(crop.leaf_g_per_m2) + crop.fruit_g_per_m2();
        const double crown_root_g_per_m2 = non_negative(crop.crown_g_per_m2) + non_negative(crop.root_g_per_m2);
        const double dead_g_per_m2 = leaf_fruit_g_per_m2 + crown_root_g_per_m2;
        const double litter_C_g_per_m2 = return_litter(crop, cell, params, dead_g_per_m2, dead_g_per_m2);
        const double leaf_C_g_per_m2 = leaf_fruit_g_per_m2 * params.dry_matter_C_fraction;
        ledger.C_leaf_litter_g += leaf_C_g_per_m2;
        ledger.C_root_litter_g += litter_C_g_per_m2 - leaf_C_g_per_m2;
        // 건물이 0이라 return_litter가 옮기지 않은 원소(있다면)도 토양으로
        cell.labile_N_g_per_m2 += non_negative(crop.plant_N_g_per_m2);
        cell.p_g_per_m2 += non_negative(crop.plant_P_g_per_m2);
        cell.k_g_per_m2 += non_negative(crop.plant_K_g_per_m2);
        cell.k_solution_g_per_m2 += non_negative(crop.plant_K_g_per_m2);
        cell.ca_g_per_m2 += non_negative(crop.plant_Ca_g_per_m2);
        cell.ca_solution_g_per_m2 += non_negative(crop.plant_Ca_g_per_m2);
        cell.mg_g_per_m2 += non_negative(crop.plant_Mg_g_per_m2);
        cell.mg_solution_g_per_m2 += non_negative(crop.plant_Mg_g_per_m2);
        cell.mn_g_per_m2 += non_negative(crop.plant_Mn_g_per_m2);
        cell.mn_solution_g_per_m2 += non_negative(crop.plant_Mn_g_per_m2);
        // 남은 저장 (보통 고사 때 이미 0)
        if (crop.reserve_g_per_m2 > 0.0) {
            const double reserve_C_g_per_m2 = crop.reserve_g_per_m2 * params.dry_matter_C_fraction;
            cell.labile_C_g_per_m2 += reserve_C_g_per_m2;
            ledger.C_root_litter_g += reserve_C_g_per_m2;
        }
    }
    crop = Crop{};
}

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
    return nutrient_requirement(crop, params, crop.fruit_g_per_m2());
}

NutrientRequirement nutrient_requirement(const Crop& crop, const CropParams& params, double fruit_dry_g_per_m2) {
    const double vegetative = vegetative_biomass_g_per_m2(crop);
    return NutrientRequirement{
        .N = params.target_N_g_per_g * vegetative + params.fruit_target_N_g_per_g * fruit_dry_g_per_m2,
        .P = params.target_P_g_per_g * vegetative + params.fruit_target_P_g_per_g * fruit_dry_g_per_m2,
        .K = params.target_K_g_per_g * vegetative + params.fruit_target_K_g_per_g * fruit_dry_g_per_m2,
        .Ca = params.target_Ca_g_per_g * vegetative + params.fruit_target_Ca_g_per_g * fruit_dry_g_per_m2,
        .Mg = params.target_Mg_g_per_g * vegetative + params.fruit_target_Mg_g_per_g * fruit_dry_g_per_m2,
        .Mn = params.target_Mn_g_per_g * vegetative + params.fruit_target_Mn_g_per_g * fruit_dry_g_per_m2,
    };
}

double leaf_area_index(const Crop& crop, const CropParams& params) {
    return non_negative(crop.leaf_g_per_m2) * params.specific_leaf_area_m2_per_g;
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
    return status_from_requirement(crop, nutrient_requirement(crop, params));
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
    const double rounded = whole + (u < wanted - whole ? 1.0 : 0.0);
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
    // 후보 수로 먼저 제한해요 (아주 큰 per_plant도 정수 변환이 넘치지 않게)
    const std::size_t count = rounded >= static_cast<double>(candidates.size())
                                  ? candidates.size()
                                  : static_cast<std::size_t>(rounded);
    if (count == 0) {
        return 0;
    }
    const double before = total_biomass_g_per_m2(crop);
    double lost_dry_g = 0.0;
    std::vector<bool> thinned(crop.fruits.size(), false);
    for (std::size_t k = 0; k < count; ++k) {
        lost_dry_g += non_negative(crop.fruits[candidates[k]].dry_g);
        thinned[candidates[k]] = true;
    }
    std::size_t index = 0;
    detail::remove_fruits_if(crop.fruits, [&](const Fruit&) { return thinned[index++]; });
    crop.harvest.thinned_fruits += count;
    ledger.C_leaf_litter_g += return_litter(crop, cell, params.crop, lost_dry_g, before);
    return count;
}

CropTickResult update_crop(Crop& crop, SoilCell& cell, const Params& params, const NutrientAvailability& available,
                           const CropEnvironment& env, Pcg32& biology_rng, Ledger& ledger, Pcg32* foliar_rng) {
    CropTickResult result;
    if (!crop.present || !crop.alive) {
        return result;
    }
    const CropParams& cp = params.crop;
    const FruitParams& fp = params.fruit;
    const double air_degC = env.house_air_temp_degC;

    // ---- h0. 저온 피해 ----
    apply_cold_damage(crop, cell, cp, air_degC, biology_rng, ledger);

    // 달린 과실 건물 합 (Crop::fruit_g_per_m2()와 같은 목록 순서 합). 꽃(set = false)은 건물이 0이라
    // 꽃 만들기·개화 판정(낙화)은 이 합을 바꾸지 않아요. 이 아래에서 과실 건물이나 목록이 바뀌면
    // 그 단계가 같은 순서로 다시 구해 이 값을 갱신해요 (분배, 수확, 낙과).
    double fruit_dry_g_per_m2 = crop.fruit_g_per_m2();

    // ---- h1. 생물계절 (활착 → 화아분화 → 화방 발달·꽃 만들기) → 개화 판정 ----
    const double growth_factor_establish = update_phenology(crop, params, env, fruit_dry_g_per_m2, biology_rng);
    judge_anthesis(crop, fp, env.house_air_temp_mean_degC, env.tick, biology_rng);

    // ---- h2. 스트레스 계수 (M5a: 균근균 완화, soil-microbes 2번. h5 흡수도 같은 값을 써요) ----
    const WaterSaltFactors water_salt = mycorrhiza_relieved(water_salt_factors(cell, cp), cell, params.microbes);
    const NutrientStatus status = status_from_requirement(crop, nutrient_requirement(crop, cp, fruit_dry_g_per_m2));
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
    // M5a 백색 필름 반사광 (disinfection 1번): 광합성 광량에만 더해요
    const double crop_light_W_per_m2 =
        non_negative(env.inside_solar_W_per_m2) *
        (1.0 + non_negative(env.canopy_reflect_factor) * (1.0 - std::clamp(interception, 0.0, 1.0)));
    const double potential_g_per_m2 = cp.light_use_efficiency_g_per_MJ * crop_light_W_per_m2 * cp.par_fraction *
                                      kMJ_per_W_h * interception * cp.temp_curve(air_degC);
    // M5b (foliar-disease 4.4·4.5): 마지막에 흰가루·응애 피해 배율 (틱 시작 병든 잎 면적·응애 2주 평균). 없으면 정확히 1
    const double gross_g_per_m2 = non_negative(potential_g_per_m2 * cp.co2_growth_curve(env.house_co2_ppm) *
                                               combined_factor * growth_factor_establish) *
                                  foliar_photosynthesis_factor(crop, params.foliar_disease);
    const double respiration_g_per_m2 =
        non_negative((non_negative(crop.leaf_g_per_m2) * cp.maintenance_rate_leaf_per_hour +
                      non_negative(crop.crown_g_per_m2) * cp.maintenance_rate_crown_per_hour +
                      non_negative(crop.root_g_per_m2) * cp.maintenance_rate_root_per_hour +
                      fruit_dry_g_per_m2 * cp.maintenance_rate_fruit_per_hour) *
                     cp.maintenance_temp_curve(air_degC) * cp.maintenance_activity_curve(crop.rgr_mean_per_day));
    const double net_g_per_m2 = gross_g_per_m2 - respiration_g_per_m2;
    // RGR 이동 평균 (strawberry-fruit 4번 유지 호흡 활성 보정): 이번 틱 순생장 ÷ 전체 건물(영양 기관 + 과실 + 저장), 시간당 → 일당(× 24)
    {
        const double total_g_per_m2 =
            vegetative_biomass_g_per_m2(crop) + fruit_dry_g_per_m2 + non_negative(crop.reserve_g_per_m2);
        if (total_g_per_m2 > 0.0) {
            const double rgr_tick_per_day = net_g_per_m2 / total_g_per_m2 * kHoursPerDay;
            crop.rgr_mean_per_day +=
                (rgr_tick_per_day - crop.rgr_mean_per_day) * std::min(1.0, cp.rgr_mean_relax_rate_per_hour);
        }
    }

    // M5b 흰가루병 희석(h6)의 기준: 분배 전 잎 건물
    const double leaf_before_growth_g_per_m2 = non_negative(crop.leaf_g_per_m2);

    // 분배 (strawberry-fruit 4번): 착과된 과실의 따라잡기 수요 = 새 진행도의 목표 건물 − 지금 건물
    const double development_increment = fruit_development_increment(fp, air_degC);
    double fruit_demand_g_per_m2 = 0.0;
    for (const Fruit& fruit : crop.fruits) {
        if (fruit.set) {
            fruit_demand_g_per_m2 += non_negative(fruit_target_dry_g(fruit, fp, development_increment) - fruit.dry_g);
        }
    }
    // 광합성에서 과실로 가는 몫
    const double assimilate_supply_g_per_m2 =
        net_g_per_m2 > 0.0 ? std::min(fruit_demand_g_per_m2, net_g_per_m2 * cp.max_fruit_assimilate_fraction) : 0.0;
    // 저장 탄수화물 (strawberry-fruit 4b) 쓰기 2: 순생장 > 0인 틱에 과실 공급이 수요보다 작으면 모자란 몫을
    // 저장에서 꺼내요 (틱당 저장 × reserve_release_rate_per_hour까지). 저장 → 과실 내부 이동이라 CO2 수지는 그대로.
    double reserve_release_g_per_m2 = 0.0;
    if (net_g_per_m2 > 0.0 && assimilate_supply_g_per_m2 < fruit_demand_g_per_m2) {
        reserve_release_g_per_m2 =
            std::min({fruit_demand_g_per_m2 - assimilate_supply_g_per_m2,
                      non_negative(crop.reserve_g_per_m2) * std::min(1.0, cp.reserve_release_rate_per_hour),
                      non_negative(crop.reserve_g_per_m2)});
        crop.reserve_g_per_m2 = non_negative(crop.reserve_g_per_m2 - reserve_release_g_per_m2);
    }
    const double fruit_supply_g_per_m2 = assimilate_supply_g_per_m2 + reserve_release_g_per_m2;
    const double supply_ratio =
        fruit_demand_g_per_m2 > 0.0 ? std::clamp(fruit_supply_g_per_m2 / fruit_demand_g_per_m2, 0.0, 1.0) : 1.0;
    // 채우기: 광합성만으로 과실이 다 채워진 틱(또는 과실 수요 없음)
    const bool reserve_fill_allowed =
        fruit_demand_g_per_m2 > 0.0 ? assimilate_supply_g_per_m2 >= fruit_demand_g_per_m2 : true;
    if (net_g_per_m2 > 0.0) {
        // 과실 증가 = 수요 × 공급 비율, 같은 순회에서 크기 충족도(5번)의 합도 구해요 (분배 뒤 건물 기준).
        // 크기 충족도(틱) = Σ 건물 ÷ Σ 목표 건물 (착과된 과실, 과실이 없으면 1, 0~1).
        // 순생장 > 0인 틱에만 하우스 기온 이동 평균과 같은 완화 비율로 이동 평균해요.
        double set_dry_g = 0.0;
        double set_target_g = 0.0;
        double all_dry_g = 0.0;
        for (Fruit& fruit : crop.fruits) {
            if (fruit.set) {
                const double target_g = fruit_target_dry_g(fruit, fp, development_increment);
                if (fruit_supply_g_per_m2 > 0.0) {
                    fruit.dry_g += non_negative(target_g - fruit.dry_g) * supply_ratio;
                }
                set_dry_g += non_negative(fruit.dry_g);
                set_target_g += non_negative(target_g);
            }
            all_dry_g += fruit.dry_g;
        }
        fruit_dry_g_per_m2 = all_dry_g;
        const double size_satisfaction = set_target_g > 0.0 ? std::clamp(set_dry_g / set_target_g, 0.0, 1.0) : 1.0;
        crop.size_satisfaction_mean += (size_satisfaction - crop.size_satisfaction_mean) *
                                       std::min(1.0, params.house.temp_mean_relax_rate_per_hour);

        // 나머지(순생장 − 과실 공급)를 잎·관부·뿌리에 vegetative_*_fraction대로
        double rest_g_per_m2 = non_negative(net_g_per_m2 - assimilate_supply_g_per_m2);
        // 저장 채우기: 영양 생장 몫의 reserve_fill_fraction을 저장으로, 상한 = capacity × (관부 + 뿌리)
        if (reserve_fill_allowed && cp.reserve_capacity_fraction > 0.0) {
            const double capacity_g_per_m2 = cp.reserve_capacity_fraction * (non_negative(crop.crown_g_per_m2) +
                                                                             non_negative(crop.root_g_per_m2));
            const double fill_g_per_m2 =
                std::min(rest_g_per_m2 * std::clamp(cp.reserve_fill_fraction, 0.0, 1.0),
                         non_negative(capacity_g_per_m2 - non_negative(crop.reserve_g_per_m2)));
            crop.reserve_g_per_m2 = non_negative(crop.reserve_g_per_m2) + fill_g_per_m2;
            rest_g_per_m2 -= fill_g_per_m2;
        }
        crop.leaf_g_per_m2 += rest_g_per_m2 * cp.vegetative_leaf_fraction;
        crop.crown_g_per_m2 += rest_g_per_m2 * cp.vegetative_crown_fraction;
        crop.root_g_per_m2 += rest_g_per_m2 * cp.vegetative_root_fraction;
    } else if (net_g_per_m2 < 0.0) {
        // 저장 쓰기 1: 부족분을 저장에서 먼저 (strawberry-fruit 4b)
        const double from_reserve_g_per_m2 = std::min(non_negative(crop.reserve_g_per_m2), -net_g_per_m2);
        crop.reserve_g_per_m2 = non_negative(crop.reserve_g_per_m2 - from_reserve_g_per_m2);
        const double deficit_g_per_m2 = non_negative(-net_g_per_m2 - from_reserve_g_per_m2);
        // 남은 부족분을 잎·관부·뿌리에서 건물 비율대로 (과실은 줄이지 않음). 탄소는 공기로(장부 밖), 체내 양분은 그대로.
        const double vegetative = vegetative_biomass_g_per_m2(crop);
        if (vegetative > 0.0 && deficit_g_per_m2 > 0.0) {
            const double keep = 1.0 - std::min(1.0, deficit_g_per_m2 / vegetative);
            crop.leaf_g_per_m2 = non_negative(crop.leaf_g_per_m2) * keep;
            crop.crown_g_per_m2 = non_negative(crop.crown_g_per_m2) * keep;
            crop.root_g_per_m2 = non_negative(crop.root_g_per_m2) * keep;
        }
    }
    crop.last_gross_g_per_m2 = gross_g_per_m2;
    crop.last_respiration_g_per_m2 = respiration_g_per_m2;
    crop.last_growth_g_per_m2 = net_g_per_m2;
    crop.last_supply_ratio = supply_ratio;

    // ---- h4. 과실 진행·품질 적산 → 자동 수확 → 낙과 ----
    {
        const double q_brix = fp.brix_supply_curve(crop.size_satisfaction_mean) *
                              fp.brix_k_curve(status.sufficiency_K) * fp.brix_n_excess_curve(status.excess_N) *
                              fp.brix_water_curve(cell.relative_water());
        const double q_firmness = fp.firmness_ca_curve(status.sufficiency_Ca) * fp.firmness_temp_curve(air_degC) *
                                  fp.firmness_n_excess_curve(status.excess_N);
        // 당도 온도 점수 (주·야간 이중 가중): 낮은 하우스 안 일사 가중, 밤은 시간 가중 (Δt = 1틱).
        // 이번 틱 모든 과실에 같은 값이라 한 번만 계산해요.
        const bool is_day = env.inside_solar_W_per_m2 > fp.day_solar_threshold_W_per_m2;
        const double day_score_weighted =
            is_day ? fp.brix_day_temp_score_curve(air_degC) * env.inside_solar_W_per_m2 : 0.0;
        const double night_score = is_day ? 0.0 : fp.brix_night_temp_score_curve(air_degC);
        bool any_ripe = false;
        for (Fruit& fruit : crop.fruits) {
            if (!fruit.set) {
                continue;
            }
            fruit.development_index = std::min(1.0, fruit.development_index + development_increment);
            fruit.brix_factor_sum += q_brix * development_increment;
            fruit.firmness_factor_sum += q_firmness * development_increment;
            fruit.weight_sum += development_increment;
            if (is_day) {
                fruit.day_score_sum += day_score_weighted;
                fruit.day_weight_sum += env.inside_solar_W_per_m2;
            } else {
                fruit.night_score_sum += night_score;
                fruit.night_weight_sum += 1.0;
            }
            any_ripe = any_ripe || fruit.development_index >= 1.0;
        }
        if (any_ripe) {
            // M5b: 흰가루 과실 확률 = fruit_prob_curve(병든 잎 면적, 틱 시작)
            const double mildew_prob = std::clamp(
                params.foliar_disease.powdery_mildew.fruit_prob_curve(crop.infection_powdery_mildew_index), 0.0, 1.0);
            result.harvested = harvest_ripe_fruits(crop, params, env.tick, env.tile_index, ledger, fruit_dry_g_per_m2,
                                                   mildew_prob, foliar_rng);
        }
        // 6b 낙과: 착과된 어린 과실(진행도 < abortion_window_index)마다 난수 < p 이면 떨어짐 (biology 스트림)
        abort_young_fruits(crop, cell, params, development_increment, biology_rng, ledger, fruit_dry_g_per_m2);
        // M5b 잿빛곰팡이 과실 썩음 (Foliar 스트림, 감염 압력은 틱 시작 값)
        rot_botrytis_fruits(crop, cell, params, foliar_rng, ledger, fruit_dry_g_per_m2);
    }

    // ---- h5. 흡수 (요구량에 관부·과실 포함, 흡수 능력에 활착 배율) ----
    const NutrientRequirement required = nutrient_requirement(crop, cp, fruit_dry_g_per_m2);
    // 근권 온도 배율 (strawberry-minimal 3번, m5-overview 13.1): 모든 원소의 흡수 능력에 곱해요
    const double uptake_temp_factor = cp.uptake_temp_curve(cell.soil_temp_degC);
    const double root_capacity_factor =
        uptake_temp_factor *
        std::min(1.0, cp.uptake_rate_per_g_root_per_hour * non_negative(crop.root_g_per_m2) * crop.root_health) *
        water_salt.water * water_salt.salt * growth_factor_establish;

    // M3 길항 (cation-exchange "흡수 길항"): 흡수 직전 용액 상태로 한 번 계산.
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
        // N 전용 사치 흡수 상한·흡수 속도 (strawberry-minimal 3번, strawberry-fruit 8번)
        const double n_capacity_factor =
            std::min(1.0, cp.n_uptake_rate_per_g_root_per_hour * non_negative(crop.root_g_per_m2) * crop.root_health) *
            water_salt.water * water_salt.salt * growth_factor_establish * uptake_temp_factor *
            cp.n_luxury_uptake_curve(nutrient_ratio(crop.plant_N_g_per_m2, required.N));
        const double uptake_N_g_per_m2 =
            std::min(demand_g_per_m2(crop.plant_N_g_per_m2, required.N, cp.n_luxury_max_ratio, 1.0),
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
         water_salt.salt * crop.antagonism_Ca_factor * growth_factor_establish * uptake_temp_factor);
    const double mg_capacity_g_per_m2 =
        non_negative(available.mg) *
        (std::min(1.0, cp.mg_uptake_rate_per_g_root_per_hour * root_mass_health_g_per_m2) * water_salt.water *
         water_salt.salt * crop.antagonism_Mg_factor * growth_factor_establish * uptake_temp_factor);
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
            water_salt.water * water_salt.salt * growth_factor_establish * uptake_temp_factor *
            cp.mn_uptake_regulation_curve(nutrient_ratio(crop.plant_Mn_g_per_m2, required.Mn));
        ledger.Mn_uptake_g += transfer_from_solution(cell.mn_g_per_m2, cell.mn_solution_g_per_m2,
                                                     crop.plant_Mn_g_per_m2,
                                                     std::min(mn_demand_g_per_m2, mn_capacity_g_per_m2));
    }

    // ---- h6. 감염 진행: (M5b) 질소 감수성 → (M5a) 시들음병·역병 → (M5b) 탄저·잿빛곰팡이·흰가루 →
    //      뿌리 건강도 → 뿌리 고사 → 낙엽 → 고사 판정 ----
    const double n_susceptibility = update_n_susceptibility(crop, cell, params.foliar_disease);
    result.disease_onsets = update_soil_infection(crop, cell, params, env.tile_index);
    update_foliar_infection(crop, cell, params, env, n_susceptibility, leaf_before_growth_g_per_m2, env.tile_index,
                            result.foliar_onsets);
    const RootDamageParams& rd = params.root_damage;
    const double effective_nitrite = non_negative(effective_nitrite_g_N_per_m2(cell, params.nitrification));
    // RootDamageCause 순서 그대로 (동률이면 앞선 원인). M5a: 병(감염 지수), 약제(잔류 × 활성의 합). M5b: 관부탄저
    const std::array<double, kRootDamageCauseCount> damage_terms{
        rd.waterlog_curve(cell.aeration_index),
        rd.heat_curve(cell.soil_temp_degC),
        rd.nitrite_curve(effective_nitrite),
        rd.acid_curve(cell.ph),
        rd.fusarium_curve(crop.infection_index[static_cast<std::size_t>(SoilDisease::Fusarium)]),
        rd.phytophthora_curve(crop.infection_index[static_cast<std::size_t>(SoilDisease::Phytophthora)]),
        rd.fumigant_curve(fumigant_exposure_index(cell, params)),
        rd.anthracnose_curve(crop.infection_anthracnose_index),
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
        const double before = vegetative_biomass_g_per_m2(crop) + fruit_dry_g_per_m2;
        const double dead_root_g_per_m2 = std::min(
            non_negative(crop.root_g_per_m2),
            non_negative(crop.root_g_per_m2) * std::min(1.0, damage_per_hour) * cp.root_dieback_per_damage);
        if (dead_root_g_per_m2 > 0.0) {
            // 저장 잃기 (strawberry-fruit 4b): 저장은 관부·뿌리에 있다고 보고, 죽은 뿌리 / (관부 + 뿌리) 비율만큼 토양 이분해성 C로.
            // 저장에는 양분이 없어서 양분 share(before)에는 넣지 않아요.
            const double storage_organs_g_per_m2 =
                non_negative(crop.crown_g_per_m2) + non_negative(crop.root_g_per_m2);
            double lost_reserve_g_per_m2 = 0.0;
            if (storage_organs_g_per_m2 > 0.0 && crop.reserve_g_per_m2 > 0.0) {
                lost_reserve_g_per_m2 =
                    crop.reserve_g_per_m2 * std::min(1.0, dead_root_g_per_m2 / storage_organs_g_per_m2);
                crop.reserve_g_per_m2 = non_negative(crop.reserve_g_per_m2 - lost_reserve_g_per_m2);
            }
            crop.root_g_per_m2 -= dead_root_g_per_m2;
            ledger.C_root_litter_g += return_litter(crop, cell, cp, dead_root_g_per_m2, before);
            const double reserve_C_g_per_m2 = lost_reserve_g_per_m2 * cp.dry_matter_C_fraction;
            cell.labile_C_g_per_m2 += reserve_C_g_per_m2;
            ledger.C_root_litter_g += reserve_C_g_per_m2;
        }
    }

    // 낙엽 (strawberry-fruit 7번, 뿌리 고사와 같은 규칙)
    {
        const double before = vegetative_biomass_g_per_m2(crop) + fruit_dry_g_per_m2;
        const double dead_leaf_g_per_m2 =
            non_negative(crop.leaf_g_per_m2) * std::min(1.0, non_negative(cp.leaf_senescence_rate_per_hour));
        if (dead_leaf_g_per_m2 > 0.0) {
            crop.leaf_g_per_m2 -= dead_leaf_g_per_m2;
            ledger.C_leaf_litter_g += return_litter(crop, cell, cp, dead_leaf_g_per_m2, before);
        }
    }

    if (crop.root_health < cp.death_root_health) {
        crop.alive = false;
        // 포기 사망 (strawberry-fruit 4b): 저장 전부를 토양 이분해성 C로 (구조 건물은 지금처럼 죽은 포기에 남아요)
        if (crop.reserve_g_per_m2 > 0.0) {
            const double reserve_C_g_per_m2 = crop.reserve_g_per_m2 * cp.dry_matter_C_fraction;
            crop.reserve_g_per_m2 = 0.0;
            cell.labile_C_g_per_m2 += reserve_C_g_per_m2;
            ledger.C_root_litter_g += reserve_C_g_per_m2;
        }
        // M5b (foliar-disease 4.2): 죽은 포기의 병든 관부·잔재는 흙에 남아요 → 접종원 한 번 (뽑은 포기는 RemovePlant라 이 길이 아님)
        {
            const std::size_t g = microbe(MicrobeGroup::Anthracnose);
            const double inf = std::clamp(crop.infection_anthracnose_index, 0.0, 1.0);
            const double lump = non_negative(params.foliar_disease.anthracnose.residue_index) * inf;
            cell.microbe_index[g] = std::min(1.0, std::clamp(cell.microbe_index[g], 0.0, 1.0) + lump);
        }
        // M5b (4.5): 작물이 죽으면 응애·천적 = 0
        crop.spider_mite_index = 0.0;
        crop.predator_mite_index = 0.0;
        result.died =
            PlantDiedEvent{.tile_index = env.tile_index, .cause = static_cast<RootDamageCause>(dominant_cause_index)};
        return result;
    }

    // ---- h7. (M5b) 점박이응애·칠레이리응애 ----
    update_mites(crop, params.foliar_disease, env, env.tile_index, result.foliar_onsets);
    return result;
}

}  // namespace sim::process
