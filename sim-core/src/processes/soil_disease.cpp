// docs/spec/soil-microbes.md 3번 (M5a) — 작물 감염 진행(작물 단계 h6, 뿌리 건강도 앞), DiseaseOnset 판정
#include <algorithm>
#include <cstddef>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

[[nodiscard]] double non_negative(double v) { return v > 0.0 ? v : 0.0; }
[[nodiscard]] double clamp01(double v) { return v > 0.0 ? (v < 1.0 ? v : 1.0) : 0.0; }  // NaN → 0

}  // namespace

double infection_pressure(SoilDisease disease, const SoilCell& cell, const Params& params) {
    const auto d = static_cast<std::size_t>(disease);
    const SoilDiseaseParams& dp = params.soil_disease[d];
    const double pathogen = clamp01(cell.microbe_index[microbe(pathogen_of(disease))]);
    const double antagonist = clamp01(cell.microbe_index[microbe(MicrobeGroup::Antagonist)]);
    const double protection =
        1.0 - std::clamp(params.microbes.antagonist_protection_factor * antagonist, 0.0, 1.0);
    return non_negative(pathogen * dp.infection_env_curve_temp(cell.soil_temp_degC) *
                        dp.infection_env_curve_o2(cell.aeration_index) * dp.infection_env_curve_ph(cell.ph) *
                        protection);
}

std::array<std::optional<DiseaseOnsetEvent>, kSoilDiseaseCount> update_soil_infection(Crop& crop,
                                                                                       const SoilCell& cell,
                                                                                       const Params& params,
                                                                                       std::uint32_t tile_index) {
    std::array<std::optional<DiseaseOnsetEvent>, kSoilDiseaseCount> onsets{};
    if (!crop.present || !crop.alive) {
        return onsets;
    }
    for (std::size_t d = 0; d < kSoilDiseaseCount; ++d) {
        const auto disease = static_cast<SoilDisease>(d);
        const SoilDiseaseParams& dp = params.soil_disease[d];
        const double infection = clamp01(crop.infection_index[d]);
        const double increase = std::min(
            1.0, non_negative(dp.infection_rate_per_hour) * infection_pressure(disease, cell, params));
        // 줄지 않음 (증가분 ≥ 0, 1에서 멈춤)
        crop.infection_index[d] = clamp01(infection + increase * (1.0 - infection));
        if (!crop.disease_onset_reported[d] && crop.infection_index[d] >= dp.onset_infection_index) {
            crop.disease_onset_reported[d] = true;
            onsets[d] = DiseaseOnsetEvent{.tile_index = tile_index, .disease = disease};
        }
    }
    return onsets;
}

}  // namespace sim::process
