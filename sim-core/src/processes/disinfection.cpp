// docs/spec/disinfection.md (M5a) — 멀칭(증발·지온·반사광), 약제 잔류(d1)·활성, 경운 방출,
// 소독 뒤 무기화·망간 환원(d3), 약제 투입
#include <algorithm>
#include <cstddef>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

[[nodiscard]] double non_negative(double v) { return v > 0.0 ? v : 0.0; }

// 1차 반응의 이번 틱 이동 비율 min(1, 속도 × 계수). 음수·NaN은 0(이동 없음).
[[nodiscard]] double transfer_fraction(double rate_times_factors) {
    if (!(rate_times_factors > 0.0)) {
        return 0.0;
    }
    return std::min(1.0, rate_times_factors);
}

}  // namespace

const MulchDef* mulch_of(const SoilCell& cell, const Params& params) {
    if (cell.mulch_def_index < 0) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(cell.mulch_def_index);
    return index < params.mulches.size() ? &params.mulches[index] : nullptr;
}

double mulch_evaporation_factor(const SoilCell& cell, const Params& params) {
    const MulchDef* mulch = mulch_of(cell, params);
    return mulch != nullptr ? non_negative(mulch->evaporation_factor) : 1.0;
}

double soil_relax_target_degC(const SoilCell& cell, const Params& params, double house_air_temp_degC,
                              double inside_solar_W_per_m2, double canopy_interception) {
    const MulchDef* mulch = mulch_of(cell, params);
    if (mulch == nullptr) {
        return house_air_temp_degC;  // 멀칭 없음: M1~M4 식 그대로
    }
    const double exposed_fraction = 1.0 - std::clamp(canopy_interception, 0.0, 1.0);
    return house_air_temp_degC +
           mulch->soil_solar_gain_degC_per_W_per_m2 * non_negative(inside_solar_W_per_m2) * exposed_fraction;
}

double mulch_canopy_reflect_factor(const SoilCell& cell, const Params& params) {
    const MulchDef* mulch = mulch_of(cell, params);
    return mulch != nullptr ? non_negative(mulch->canopy_reflect_factor) : 0.0;
}

double fumigant_activation(const DisinfectantDef& disinfectant, const SoilCell& cell) {
    return non_negative(disinfectant.activation_moisture_curve(cell.relative_water()));
}

double fumigant_exposure_index(const SoilCell& cell, const Params& params) {
    double exposure = 0.0;
    const std::size_t n = std::min(params.disinfectants.size(), cell.fumigant_residue_index.size());
    for (std::size_t d = 0; d < n; ++d) {
        exposure += non_negative(cell.fumigant_residue_index[d]) * fumigant_activation(params.disinfectants[d], cell);
    }
    return exposure;
}

void decay_fumigant(SoilCell& cell, const Params& params) {
    const MulchDef* mulch = mulch_of(cell, params);
    const double retention = mulch != nullptr ? non_negative(mulch->fumigant_retention_factor) : 1.0;
    const std::size_t n = std::min(params.disinfectants.size(), cell.fumigant_residue_index.size());
    for (std::size_t d = 0; d < n; ++d) {
        const DisinfectantDef& def = params.disinfectants[d];
        const double residue = non_negative(cell.fumigant_residue_index[d]);
        const double fraction =
            transfer_fraction(def.decay_rate_per_hour * def.decay_temp_curve(cell.soil_temp_degC) * retention);
        cell.fumigant_residue_index[d] = non_negative(residue - residue * fraction);
    }
}

void release_fumigant_on_till(SoilCell& cell, const DisinfectionParams& params) {
    const double keep = 1.0 - std::clamp(params.till_residue_release_fraction, 0.0, 1.0);
    for (double& residue : cell.fumigant_residue_index) {
        residue = non_negative(residue) * keep;
    }
}

void apply_disinfectant(SoilCell& cell, std::size_t disinfectant_index, const DisinfectantDef& disinfectant,
                        double g_per_m2, Ledger& ledger) {
    if (!(g_per_m2 > 0.0) || disinfectant_index >= cell.fumigant_residue_index.size()) {
        return;
    }
    cell.fumigant_residue_index[disinfectant_index] += g_per_m2 * non_negative(disinfectant.residue_index_per_g_per_m2);
    if (disinfectant_index < ledger.disinfectant_applied_g.size()) {
        ledger.disinfectant_applied_g[disinfectant_index] += g_per_m2;
    }
}

void apply_disinfection_flush(SoilCell& cell, const DisinfectionParams& params, Ledger& ledger) {
    const double k = std::clamp(cell.disinfection_kill_index[microbe(MicrobeGroup::Decomposer)], 0.0, 1.0);
    if (!(k > 0.0)) {
        return;
    }
    // 4. 무기화 증가: 난분해성 유기 N → NH4+ (용액으로), 같은 C:N로 난분해성 → 이분해성 C
    const double stable_N = non_negative(cell.stable_N_g_per_m2);
    const double stable_C = non_negative(cell.stable_C_g_per_m2);
    const double flush_N = std::min(stable_N, k * non_negative(params.flush_N_g_per_m2_per_index));
    const double flush_C = std::min(stable_C, flush_N * non_negative(params.flush_C_to_N_ratio));
    if (flush_N > 0.0) {
        const double nh4_before = cell.nh4_N_g_per_m2;
        cell.stable_N_g_per_m2 = stable_N - flush_N;
        cell.nh4_N_g_per_m2 = non_negative(cell.nh4_N_g_per_m2) + flush_N;
        sync_nh4_solution(cell, nh4_before);  // 증가분 → 용액
        ledger.N_flush_g += flush_N;
    }
    if (flush_C > 0.0) {
        cell.stable_C_g_per_m2 = stable_C - flush_C;
        cell.labile_C_g_per_m2 = non_negative(cell.labile_C_g_per_m2) + flush_C;
    }
    // 5. 망간 산화물 환원 → 유효태 (용액으로, manganese "환원")
    const double oxide = non_negative(cell.mn_oxide_g_per_m2);
    const double mn_reduced = oxide * transfer_fraction(k * params.mn_reduction_per_index);
    if (mn_reduced > 0.0) {
        cell.mn_oxide_g_per_m2 = oxide - mn_reduced;
        cell.mn_g_per_m2 = non_negative(cell.mn_g_per_m2) + mn_reduced;
        cell.mn_solution_g_per_m2 =
            std::clamp(non_negative(cell.mn_solution_g_per_m2) + mn_reduced, 0.0, cell.mn_g_per_m2);
        ledger.Mn_flush_g += mn_reduced;
    }
}

}  // namespace sim::process
