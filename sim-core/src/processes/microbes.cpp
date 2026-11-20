// docs/spec/soil-microbes.md (M5a) — 개체군 갱신(셀 단계 d2), 활동 계수, 균근균 완화, 유익균 투입
#include <algorithm>
#include <cstddef>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

constexpr double kGramsPerKilogram = 1000.0;  // 단위 환산 (담체 g/kg × g → g)

[[nodiscard]] double non_negative(double v) { return v > 0.0 ? v : 0.0; }
[[nodiscard]] double clamp01(double v) { return v > 0.0 ? (v < 1.0 ? v : 1.0) : 0.0; }  // NaN → 0

}  // namespace

MicrobeIndices microbe_suitability(const SoilCell& cell, const MicrobesParams& params) {
    MicrobeIndices suit{};
    const double relative_water = cell.relative_water();
    for (std::size_t g = 0; g < kMicrobeGroupCount; ++g) {
        const MicrobeGroupParams& gp = params.groups[g];
        suit[g] = non_negative(gp.temp_curve(cell.soil_temp_degC) * gp.moisture_curve(relative_water) *
                               gp.ph_curve(cell.ph) * gp.o2_curve(cell.aeration_index));
    }
    return suit;
}

MicrobeIndices microbe_capacity(const SoilCell& cell, const MicrobesParams& params, const MicrobeHost& host) {
    MicrobeIndices capacity{};
    const double labile_C = non_negative(cell.labile_C_g_per_m2);
    capacity[microbe(MicrobeGroup::Decomposer)] = params.decomposer_food_curve(labile_C);
    capacity[microbe(MicrobeGroup::NitrifierAob)] = 1.0;
    capacity[microbe(MicrobeGroup::NitrifierNob)] = 1.0;
    capacity[microbe(MicrobeGroup::Antagonist)] = params.antagonist_food_curve(labile_C);
    // 균근균: 숙주(뿌리, 살아 있는 작물이 없으면 0) × 유효 인산 억제. 유효 P = cell.p_g_per_m2 (phosphorus.md 유효 인산 풀)
    capacity[microbe(MicrobeGroup::Mycorrhiza)] = params.mycorrhiza_host_curve(non_negative(host.root_g_per_m2)) *
                                                  params.mycorrhiza_p_curve(non_negative(cell.p_g_per_m2));
    // 병원균: 부생 + 숙주 × 짝 병의 작물 감염 지수 (살아 있는 작물이 없으면 0)
    for (std::size_t d = 0; d < kSoilDiseaseCount; ++d) {
        const std::size_t g = microbe(pathogen_of(static_cast<SoilDisease>(d)));
        const MicrobeGroupParams& gp = params.groups[g];
        capacity[g] = gp.saprophytic_capacity_index + gp.host_capacity_index * clamp01(host.infection_index[d]);
    }
    // M5b 탄저병균 (foliar-disease 3.2): 부생 + 숙주 × 그 셀 작물의 관부 감염 지수
    {
        const std::size_t g = microbe(MicrobeGroup::Anthracnose);
        const MicrobeGroupParams& gp = params.groups[g];
        capacity[g] =
            gp.saprophytic_capacity_index + gp.host_capacity_index * clamp01(host.anthracnose_infection_index);
    }
    for (double& k : capacity) {
        k = std::max(params.capacity_floor_index, non_negative(k));
    }
    return capacity;
}

MicrobeIndices fumigant_kill_per_hour(const SoilCell& cell, const std::vector<DisinfectantDef>& disinfectants) {
    MicrobeIndices kill{};
    const std::size_t n = std::min(disinfectants.size(), cell.fumigant_residue_index.size());
    for (std::size_t d = 0; d < n; ++d) {
        const double exposure = non_negative(cell.fumigant_residue_index[d]) * fumigant_activation(disinfectants[d], cell);
        for (std::size_t g = 0; g < kMicrobeGroupCount; ++g) {
            kill[g] += non_negative(disinfectants[d].kill_rate_per_hour[g]) * exposure;
        }
    }
    return kill;
}

MicrobeGroupStep microbe_group_step(std::size_t g, const MicrobeIndices& start, const MicrobeIndices& suitability,
                                    const MicrobeIndices& capacity, const MicrobeIndices& fumigant_kill,
                                    const SoilCell& cell, const MicrobesParams& params) {
    const MicrobeGroupParams& gp = params.groups[g];
    const double P = clamp01(start[g]);
    // 비대칭 경쟁: 다른 무리 h가 이 무리 수용력을 α_gh × P_h만큼 차지 (자기 자신은 검증에서 0)
    double occupied = P;
    for (std::size_t h = 0; h < kMicrobeGroupCount; ++h) {
        if (h != g) {
            occupied += non_negative(gp.competition[h]) * clamp01(start[h]);
        }
    }
    const double K = capacity[g];  // microbe_capacity가 하한(> 0)을 적용
    const double crowd = K > 0.0 ? occupied / K : 1.0;
    const double growth = non_negative(gp.growth_rate_per_hour) * non_negative(suitability[g]) * P * (1.0 - crowd);

    const double kill_natural = non_negative(gp.base_death_rate_per_hour);
    const double kill_heat = non_negative(gp.heat_kill_curve(cell.soil_temp_degC) *
                                          params.moist_heat_curve(cell.relative_water()));
    const double kill_fumigant = non_negative(fumigant_kill[g]);
    const double kill_total = kill_natural + kill_heat + kill_fumigant;
    const double killed = P * std::min(1.0, kill_total);

    const double loss = killed + non_negative(-growth);
    const double gain = non_negative(growth) + non_negative(gp.immigration_rate_per_hour) * (1.0 - P);
    MicrobeGroupStep step;
    step.next_index = clamp01(P + gain - std::min(loss, P));
    // 소독(열 + 약제) 몫만 따로 기록 (분모 0이면 0)
    step.disinfection_kill_index =
        kill_total > 0.0 ? clamp01(killed * (kill_heat + kill_fumigant) / kill_total) : 0.0;
    return step;
}

void update_microbes(SoilCell& cell, const Params& params, const MicrobeHost& host) {
    const MicrobesParams& mp = params.microbes;
    // 모든 무리를 단계 시작 값으로 계산하고 마지막에 한꺼번에 바꿔요 (무리 순서에 결과가 의존하지 않음)
    const MicrobeIndices start = cell.microbe_index;
    const MicrobeIndices suitability = microbe_suitability(cell, mp);
    const MicrobeIndices capacity = microbe_capacity(cell, mp, host);
    const MicrobeIndices fumigant_kill = fumigant_kill_per_hour(cell, params.disinfectants);
    MicrobeIndices next{};
    MicrobeIndices disinfection_kill{};
    for (std::size_t g = 0; g < kMicrobeGroupCount; ++g) {
        const MicrobeGroupStep step = microbe_group_step(g, start, suitability, capacity, fumigant_kill, cell, mp);
        next[g] = step.next_index;
        disinfection_kill[g] = step.disinfection_kill_index;
    }
    cell.microbe_index = next;
    cell.disinfection_kill_index = disinfection_kill;
    // (M5b, foliar-disease 4.2) 살아 있는 병든 포기의 관부·잔재가 그 칸 흙에 접종원을 쌓아요.
    // host.anthracnose_infection_index는 살아 있는 작물만 (없으면 0 → sporulation_curve(0) = 0). 이웃 칸을 읽지 않아 셀 순서와 무관
    const AnthracnoseParams& ap = params.foliar_disease.anthracnose;
    const double deposit = non_negative(ap.soil_deposit_per_hour) *
                           clamp01(ap.sporulation_curve(clamp01(host.anthracnose_infection_index)));
    const std::size_t ga = microbe(MicrobeGroup::Anthracnose);
    cell.microbe_index[ga] = std::min(1.0, cell.microbe_index[ga] + deposit);
}

double decomposer_activity_factor(const SoilCell& cell, const MicrobesParams& params) {
    return non_negative(params.decomposer_activity_curve(cell.microbe_index[microbe(MicrobeGroup::Decomposer)]));
}

double aob_activity_factor(const SoilCell& cell, const MicrobesParams& params) {
    return non_negative(params.aob_activity_curve(cell.microbe_index[microbe(MicrobeGroup::NitrifierAob)]));
}

double nob_activity_factor(const SoilCell& cell, const MicrobesParams& params) {
    return non_negative(params.nob_activity_curve(cell.microbe_index[microbe(MicrobeGroup::NitrifierNob)]));
}

double mycorrhiza_p_release_factor(const SoilCell& cell, const MicrobesParams& params) {
    return 1.0 + non_negative(params.mycorrhiza_p_release_factor) *
                     clamp01(cell.microbe_index[microbe(MicrobeGroup::Mycorrhiza)]);
}

WaterSaltFactors mycorrhiza_relieved(const WaterSaltFactors& factors, const SoilCell& cell,
                                     const MicrobesParams& params) {
    const double myco = clamp01(cell.microbe_index[microbe(MicrobeGroup::Mycorrhiza)]);
    WaterSaltFactors relieved = factors;
    relieved.water += (1.0 - factors.water) * clamp01(params.mycorrhiza_water_relief_factor) * myco;
    relieved.salt += (1.0 - factors.salt) * clamp01(params.mycorrhiza_salt_relief_factor) * myco;
    return relieved;
}

void apply_inoculant(SoilCell& cell, const InoculantDef& inoculant, double g_per_m2, Ledger& ledger) {
    // 음수·NaN 입력은 무시해요 (명령 검증에서 걸러지지만 장부가 깨지지 않게)
    if (!(g_per_m2 > 0.0)) {
        return;
    }
    for (std::size_t g = 0; g < kMicrobeGroupCount; ++g) {
        const double gain = std::min(1.0, g_per_m2 * non_negative(inoculant.index_gain_per_g_per_m2[g]));
        const double P = clamp01(cell.microbe_index[g]);
        cell.microbe_index[g] = clamp01(P + (1.0 - P) * gain);
    }
    // 담체 유기물 → 이분해성 풀
    const double kg_per_m2 = g_per_m2 / kGramsPerKilogram;
    const double C_g_per_m2 = kg_per_m2 * non_negative(inoculant.C_g_per_kg);
    const double N_g_per_m2 = kg_per_m2 * non_negative(inoculant.N_g_per_kg);
    cell.labile_C_g_per_m2 += C_g_per_m2;
    cell.labile_N_g_per_m2 += N_g_per_m2;
    ledger.C_in_g += C_g_per_m2;
    ledger.N_in_g += N_g_per_m2;
    ledger.inoculant_applied_g += g_per_m2;
}

}  // namespace sim::process
