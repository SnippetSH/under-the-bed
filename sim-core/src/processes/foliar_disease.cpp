// docs/spec/foliar-disease.md (M5b) — 질소 과다 감수성, 탄저병·잿빛곰팡이병·흰가루병 감염, 점박이응애·칠레이리응애,
// 모종 병해, 병든 포기 제거, 천적 방사, 하우스 포자 압력. 과실 썩음·흰가루 과실 판정은 crop.cpp h4에 있어요.
#include <algorithm>
#include <cstddef>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

[[nodiscard]] double non_negative(double v) { return v > 0.0 ? v : 0.0; }                       // NaN → 0
[[nodiscard]] double clamp01(double v) { return v > 0.0 ? (v < 1.0 ? v : 1.0) : 0.0; }  // NaN → 0

constexpr std::size_t foliar(FoliarDisease d) { return static_cast<std::size_t>(d); }

// 처음 onset 이상이면 이벤트 (병마다 한 번)
void check_onset(Crop& crop, FoliarDisease disease, double index, double onset_index, std::uint32_t tile_index,
                 FoliarOnsets& onsets) {
    const std::size_t d = foliar(disease);
    if (!crop.foliar_onset_reported[d] && index >= onset_index) {
        crop.foliar_onset_reported[d] = true;
        onsets[d] = FoliarDiseaseOnsetEvent{.tile_index = tile_index, .disease = disease};
    }
}

}  // namespace

double mulch_soil_splash_factor(const SoilCell& cell, const Params& params) {
    const MulchDef* mulch = mulch_of(cell, params);
    return mulch != nullptr ? non_negative(mulch->soil_splash_factor) : 1.0;
}

double mulch_plant_splash_factor(const SoilCell& cell, const Params& params) {
    const MulchDef* mulch = mulch_of(cell, params);
    return mulch != nullptr ? non_negative(mulch->plant_splash_factor) : 1.0;
}

double house_air_changes_per_hour(const HouseParams& params, const HouseState& house) {
    return non_negative(params.infiltration_air_changes_per_hour +
                        params.vent_air_changes_per_hour * house.vent_path_index);
}

void apply_seedling_disease(Crop& crop, const SeedlingGradeDef& grade, const SoilCell& cell, Pcg32& foliar_rng) {
    // 확률이 0이면 난수를 뽑지 않아요 (등급 A + 배경 포자 0이면 Foliar 스트림을 한 번도 쓰지 않음)
    if (grade.latent_anthracnose_fraction > 0.0) {
        const double u1 = foliar_rng.next_unit_double();
        if (u1 < grade.latent_anthracnose_fraction) {
            crop.infection_anthracnose_index = clamp01(grade.latent_anthracnose_index);
        }
    }
    if (grade.spider_mite_fraction > 0.0) {
        const double u2 = foliar_rng.next_unit_double();
        if (u2 < grade.spider_mite_fraction) {
            crop.spider_mite_index = clamp01(grade.spider_mite_start_index);
        }
    }
    crop.infection_powdery_mildew_index = clamp01(grade.powdery_mildew_start_index);
    // 4.1: 정식 때 이동 평균은 그 셀의 그때 무기 N으로 시작
    crop.soil_mineral_N_mean_g_per_m2 = non_negative(cell.nh4_N_g_per_m2) + non_negative(cell.no3_N_g_per_m2);
}

double update_n_susceptibility(Crop& crop, const SoilCell& cell, const FoliarDiseaseParams& params) {
    const double mineral_N = non_negative(cell.nh4_N_g_per_m2) + non_negative(cell.no3_N_g_per_m2);
    crop.soil_mineral_N_mean_g_per_m2 = non_negative(
        crop.soil_mineral_N_mean_g_per_m2 +
        (mineral_N - crop.soil_mineral_N_mean_g_per_m2) * std::min(1.0, non_negative(params.n_mean_relax_rate_per_hour)));
    return non_negative(params.n_susceptibility_curve(crop.soil_mineral_N_mean_g_per_m2));
}

double foliar_photosynthesis_factor(const Crop& crop, const FoliarDiseaseParams& params) {
    const double mildew = clamp01(params.powdery_mildew.photosynthesis_damage_curve(crop.infection_powdery_mildew_index));
    const double mite = clamp01(params.mites.photosynthesis_damage_curve(crop.spider_mite_mean_index));
    return (1.0 - mildew) * (1.0 - mite);
}

void update_foliar_infection(Crop& crop, const SoilCell& cell, const Params& params, const CropEnvironment& env,
                             double n_susceptibility_factor, double leaf_before_growth_g_per_m2,
                             std::uint32_t tile_index, FoliarOnsets& onsets) {
    const FoliarDiseaseParams& fp = params.foliar_disease;
    const double air_degC = env.house_air_temp_degC;
    const double s_N = non_negative(n_susceptibility_factor);
    const double susc = non_negative(crop.disease_susceptibility_factor);
    const double wet_run_hours = non_negative(crop.wet_run_hours);

    // ---- 4.2 탄저병 (관부): 진행 + 흙에서 + 두상 관수 물 튐. 줄지 않아요 ----
    {
        const AnthracnoseParams& ap = fp.anthracnose;
        const double inf = clamp01(crop.infection_anthracnose_index);
        const double T = non_negative(ap.temp_curve(air_degC));
        const double W = non_negative(ap.wetness_curve(wet_run_hours));
        const double antagonist = clamp01(cell.microbe_index[microbe(MicrobeGroup::Antagonist)]);
        const double protection =
            1.0 - std::clamp(params.microbes.antagonist_protection_factor * antagonist, 0.0, 1.0);
        const double soil_path = clamp01(cell.microbe_index[microbe(MicrobeGroup::Anthracnose)]) *
                                 mulch_soil_splash_factor(cell, params) * protection;
        const double plant_path =
            clamp01(env.anthracnose_sporulation_index) * mulch_plant_splash_factor(cell, params);
        const double common = T * s_N * susc;
        // Δ = (progress × inf + contact × W × soil + [두상] splash × (soil + plant)) × common × (1 − inf)
        double coefficient = non_negative(ap.progress_rate_per_hour) * inf +
                             non_negative(ap.contact_rate_per_hour) * W * soil_path;
        if (env.overhead_irrigated) {
            coefficient += non_negative(ap.splash_transfer_index) * (soil_path + plant_path);
        }
        const double increase = std::min(1.0, non_negative(coefficient * common)) * (1.0 - inf);
        crop.infection_anthracnose_index = clamp01(inf + increase);
        crop.last_anthracnose_increase_index = crop.infection_anthracnose_index - inf;
        check_onset(crop, FoliarDisease::Anthracnose, crop.infection_anthracnose_index, ap.onset_infection_index,
                    tile_index, onsets);
    }

    // ---- 4.3 잿빛곰팡이병: 포자(틱 시작) × 온도 × 연속 젖음 → 증가, 그 뒤 감쇠 ----
    {
        const BotrytisParams& bp = fp.botrytis;
        const double inf = clamp01(crop.infection_botrytis_index);
        const double T = non_negative(bp.infection_temp_curve(air_degC));
        const double W = non_negative(bp.wetness_curve(wet_run_hours));
        const double increase =
            std::min(1.0, non_negative(bp.infection_rate_per_hour * clamp01(env.botrytis_spore_index) * T * W * s_N *
                                       susc)) *
            (1.0 - inf);
        const double grown = clamp01(inf + increase);
        crop.last_botrytis_increase_index = grown - inf;
        crop.infection_botrytis_index =
            clamp01(grown - grown * std::min(1.0, non_negative(bp.infection_decay_rate_per_hour)));
        check_onset(crop, FoliarDisease::Botrytis, crop.infection_botrytis_index, bp.onset_infection_index, tile_index,
                    onsets);
    }

    // ---- 4.4 흰가루병: 습한 공기·마른 잎·약광·밀식·어린 잎. 스스로 번짐(+ inf_p), 새 잎 희석 ----
    {
        const PowderyMildewParams& mp = fp.powdery_mildew;
        const double inf = clamp01(crop.infection_powdery_mildew_index);
        const double lai = leaf_area_index(crop, params.crop);
        const double free_water =
            std::clamp(1.0 - mp.free_water_inhibition_factor * clamp01(crop.leaf_wetness_index), 0.0, 1.0);
        const double env_factor = non_negative(mp.temp_curve(air_degC)) * non_negative(mp.rh_curve(crop.canopy_rh_index)) *
                                  free_water * non_negative(mp.light_curve(env.inside_solar_mean_W_per_m2)) *
                                  non_negative(mp.density_curve(lai)) *
                                  non_negative(mp.young_leaf_curve(crop.rgr_mean_per_day));
        const double increase =
            std::min(1.0, non_negative(mp.infection_rate_per_hour * (clamp01(env.powdery_mildew_spore_index) + inf) *
                                       env_factor * s_N * susc)) *
            (1.0 - inf);
        double next = clamp01(inf + increase);
        crop.last_powdery_mildew_increase_index = next - inf;
        // 희석: 이번 틱 잎 건물이 늘었으면 새 잎은 깨끗 (낙엽은 비율 그대로라 따로 할 일 없음)
        const double leaf_now = non_negative(crop.leaf_g_per_m2);
        if (leaf_now > leaf_before_growth_g_per_m2 && leaf_before_growth_g_per_m2 > 0.0) {
            next = clamp01(next * (leaf_before_growth_g_per_m2 / leaf_now));
        }
        crop.infection_powdery_mildew_index = next;
        check_onset(crop, FoliarDisease::PowderyMildew, next, mp.onset_infection_index, tile_index, onsets);
    }
}

void update_mites(Crop& crop, const FoliarDiseaseParams& params, const CropEnvironment& env, std::uint32_t tile_index,
                  FoliarOnsets& onsets) {
    const MiteParams& mp = params.mites;
    const double M = clamp01(crop.spider_mite_index);
    const double D = clamp01(crop.predator_mite_index);
    const double air_degC = env.house_air_temp_degC;
    const double rh_mean = env.house_rh_mean_index;
    const double r_m = non_negative(mp.mite_growth_curve_per_hour(air_degC)) * non_negative(mp.mite_rh_curve(rh_mean));
    const double T_d = non_negative(mp.predator_temp_curve(air_degC));
    const double H = clamp01(mp.predator_hatch_curve(rh_mean));
    const double a = non_negative(mp.attack_rate_per_hour);
    const double ah_M = a * non_negative(mp.handling_time_hours) * M;
    // Holling II: 이번 틱 먹힌 양(M을 넘지 않음)과 천적의 배부름 0~1
    const double predation = std::min(M, non_negative(a * M * D * T_d / (1.0 + ah_M)));
    const double satiation = ah_M / (1.0 + ah_M);
    const double house_mean = clamp01(env.house_spider_mite_mean_index);

    const double M_next =
        clamp01(M + r_m * M * (1.0 - M) - predation + non_negative(mp.mite_spread_rate_per_hour) * (house_mean - M));
    // 기본·굶주림 사망 × 기온 배율 (15.3 항목 33): 추우면 대사가 느려 덜 죽어요
    const double death_temp = non_negative(mp.predator_death_temp_curve(air_degC));
    const double death =
        std::min(1.0, death_temp * (non_negative(mp.predator_death_rate_per_hour) +
                                    non_negative(mp.predator_starvation_death_per_hour) * (1.0 - satiation)));
    const double D_next = clamp01(D + non_negative(mp.conversion_factor) * predation * H - D * death);
    crop.spider_mite_index = M_next;
    crop.predator_mite_index = D_next;
    crop.last_mite_predation_index = predation;
    crop.spider_mite_mean_index =
        clamp01(crop.spider_mite_mean_index +
                (M_next - crop.spider_mite_mean_index) * std::min(1.0, non_negative(params.mite_mean_relax_rate_per_hour)));
    check_onset(crop, FoliarDisease::SpiderMite, M_next, mp.mite_onset_index, tile_index, onsets);
}

void add_predator_search_sums(PredatorSearchSums& sums, const Crop& crop) {
    if (!crop.present || !crop.alive) {
        return;
    }
    sums.predator_sum += clamp01(crop.predator_mite_index);
    sums.mite_sum += clamp01(crop.spider_mite_index);
    ++sums.live_crops;
}

void apply_predator_search(Crop& crop, const PredatorSearchSums& sums, const MiteParams& params) {
    if (!crop.present || !crop.alive || sums.live_crops == 0) {
        return;
    }
    const double n = static_cast<double>(sums.live_crops);
    const double D_mean = clamp01(sums.predator_sum / n);
    const double M_mean = clamp01(sums.mite_sum / n);
    const double D = clamp01(crop.predator_mite_index);
    // 목표가 1을 넘으면 잘라요 (그 틱은 천적 합이 조금 줄 수 있음)
    const double target = M_mean > 0.0 ? clamp01(D_mean * clamp01(crop.spider_mite_index) / M_mean) : D_mean;
    crop.predator_mite_index = clamp01(D + clamp01(params.predator_search_rate_per_hour) * (target - D));
}

void reset_foliar_tick_values(Crop& crop) {
    crop.last_anthracnose_increase_index = 0.0;
    crop.last_botrytis_increase_index = 0.0;
    crop.last_powdery_mildew_increase_index = 0.0;
    crop.last_mite_predation_index = 0.0;
    crop.last_fruit_rot_count = 0.0;
}

void remove_plant(Crop& crop, Ledger& ledger) {
    if (!crop.present) {
        return;
    }
    ledger.N_removed_g += non_negative(crop.plant_N_g_per_m2);
    ledger.P_removed_g += non_negative(crop.plant_P_g_per_m2);
    ledger.K_removed_g += non_negative(crop.plant_K_g_per_m2);
    ledger.Ca_removed_g += non_negative(crop.plant_Ca_g_per_m2);
    ledger.Mg_removed_g += non_negative(crop.plant_Mg_g_per_m2);
    ledger.Mn_removed_g += non_negative(crop.plant_Mn_g_per_m2);
    ledger.dry_matter_removed_g += total_biomass_g_per_m2(crop) + non_negative(crop.reserve_g_per_m2);
    ++ledger.plants_removed;
    // 빈 칸으로 남아요 (보식 없음). 수확 기록은 작기 리포트를 위해 남겨요
    HarvestRecord harvest = std::move(crop.harvest);
    crop = Crop{};
    crop.harvest = std::move(harvest);
}

void release_predators(Crop& crop, const PredatorDef& predator, double per_m2) {
    if (!crop.present || !crop.alive || !(per_m2 > 0.0)) {
        return;
    }
    crop.predator_mite_index =
        std::min(1.0, clamp01(crop.predator_mite_index) + per_m2 * non_negative(predator.index_per_individual_per_m2));
}

void add_foliar_house_sources(FoliarHouseSources& sources, const Crop& crop, const Params& params,
                              double house_air_temp_degC) {
    const FoliarDiseaseParams& fp = params.foliar_disease;
    // 이번 틱 썩은 과실은 그 틱에 죽은 포기여도 원천에 넣어요 (썩음은 고사 판정 앞 h4)
    sources.botrytis_source_sum +=
        non_negative(fp.botrytis.spore_source_per_rotten_fruit_per_hour) * non_negative(crop.last_fruit_rot_count);
    if (!crop.present || !crop.alive) {
        return;
    }
    ++sources.live_crops;
    sources.anthracnose_sporulation_sum +=
        clamp01(fp.anthracnose.sporulation_curve(crop.infection_anthracnose_index));
    const bool wet = crop.leaf_wetness_index >= params.humidity.wet_threshold_index;
    if (wet) {
        sources.botrytis_source_sum += non_negative(fp.botrytis.spore_source_per_infection_per_hour) *
                                       clamp01(crop.infection_botrytis_index) *
                                       non_negative(fp.botrytis.sporulation_temp_curve(house_air_temp_degC));
    }
    sources.powdery_mildew_source_sum +=
        non_negative(fp.powdery_mildew.spore_source_per_infection_per_hour) * clamp01(crop.infection_powdery_mildew_index);
    sources.spider_mite_sum += clamp01(crop.spider_mite_index);
}

namespace {

// L = min(1, decay + vent × ach)
double spore_loss_index(double decay_rate, double vent_fraction, double ach) {
    return std::min(1.0, non_negative(decay_rate) + non_negative(vent_fraction) * non_negative(ach));
}

// (M5b, 15.3 항목 12) 배경 원천 bg × L ÷ (1 − bg): 하우스 원천이 0이면 평형이 정확히 배경값 (bg < 1은 검증)
double background_spore_source(double background_index, double loss) {
    const double bg = clamp01(background_index);
    return bg < 1.0 ? bg * loss / (1.0 - bg) : 0.0;
}

// spore += source × (1 − spore) − spore × L
double next_spore_index(double spore, double source, double loss) {
    const double s = clamp01(spore);
    return clamp01(s + non_negative(source) * (1.0 - s) - s * loss);
}

void set_house_means(HouseState& house, const FoliarHouseSources& sources, std::size_t cells) {
    const double n = cells > 0 ? static_cast<double>(cells) : 1.0;
    house.anthracnose_sporulation_index = clamp01(sources.anthracnose_sporulation_sum / n);
    house.spider_mite_mean_index =
        sources.live_crops > 0 ? clamp01(sources.spider_mite_sum / static_cast<double>(sources.live_crops)) : 0.0;
}

}  // namespace

void update_foliar_house(HouseState& house, const Params& params, const FoliarInit& foliar,
                         const FoliarHouseSources& sources, std::size_t cells) {
    const FoliarDiseaseParams& fp = params.foliar_disease;
    const double n = cells > 0 ? static_cast<double>(cells) : 1.0;
    const double ach = house_air_changes_per_hour(params.house, house);
    // 배경 원천 bg × L ÷ (1 − bg): 하우스 원천이 없으면 평형 = 배경값 (닫힘·환기 모두)
    const double botrytis_loss =
        spore_loss_index(fp.botrytis.spore_decay_rate_per_hour, fp.botrytis.spore_vent_fraction, ach);
    const double mildew_loss =
        spore_loss_index(fp.powdery_mildew.spore_decay_rate_per_hour, fp.powdery_mildew.spore_vent_fraction, ach);
    const double botrytis_source =
        sources.botrytis_source_sum / n + background_spore_source(foliar.botrytis_background_index, botrytis_loss);
    const double mildew_source = sources.powdery_mildew_source_sum / n +
                                 background_spore_source(foliar.powdery_mildew_background_index, mildew_loss);
    house.botrytis_spore_index = next_spore_index(house.botrytis_spore_index, botrytis_source, botrytis_loss);
    house.powdery_mildew_spore_index = next_spore_index(house.powdery_mildew_spore_index, mildew_source, mildew_loss);
    set_house_means(house, sources, cells);
    // 하우스 안 일사 하루 이동 평균 (기온 이동 평균과 같은 완화식, foliar-disease 4.4)
    house.inside_solar_mean_W_per_m2 +=
        (house.inside_solar_W_per_m2 - house.inside_solar_mean_W_per_m2) *
        std::min(1.0, non_negative(params.house.temp_mean_relax_rate_per_hour));
}

void init_foliar_house(HouseState& house, const Params& params, const FoliarInit& foliar,
                       const FoliarHouseSources& sources, std::size_t cells) {
    (void)params;
    house.botrytis_spore_index = clamp01(foliar.botrytis_background_index);
    house.powdery_mildew_spore_index = clamp01(foliar.powdery_mildew_background_index);
    set_house_means(house, sources, cells);
    house.inside_solar_mean_W_per_m2 = non_negative(house.inside_solar_W_per_m2);
}

}  // namespace sim::process
