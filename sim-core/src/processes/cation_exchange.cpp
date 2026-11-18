// docs/spec/cation-exchange.md + manganese.md (M3) — CEC, 빈자리 φ, 평형, 완화, 망간 산화·환원
// ADR-0008 (빈자리 경쟁 + 완화), ADR-0010 (망간 산화물 풀, pH 의존 흡착 세기)
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

[[nodiscard]] double non_negative(double v) { return v > 0.0 ? v : 0.0; }

// 1차 반응의 이번 틱 이동 비율 min(1, 속도 × 계수들). 음수·NaN은 0(이동 없음).
[[nodiscard]] double transfer_fraction(double rate_times_factors) {
    if (!(rate_times_factors > 0.0)) {
        return 0.0;
    }
    return std::min(1.0, rate_times_factors);
}

constexpr std::size_t kIonCount = 5;  // NH4, K, Ca, Mg, Mn (이 순서)

struct IonSet {
    std::array<double, kIonCount> total_g_per_m2{};    // 총량
    std::array<double, kIonCount> g_per_eq{};          // 1 당량의 무게
    std::array<double, kIonCount> retention_factor{};  // 세기 w (Mn은 retention_ph_curve(pH))
};

IonSet ion_set(const SoilCell& cell, const Params& params) {
    const CationExchangeParams& ce = params.cation_exchange;
    const IonParams& ions = params.ions;
    IonSet s;
    s.total_g_per_m2 = {non_negative(cell.nh4_N_g_per_m2), non_negative(cell.k_g_per_m2),
                        non_negative(cell.ca_g_per_m2), non_negative(cell.mg_g_per_m2),
                        non_negative(cell.mn_g_per_m2)};
    s.g_per_eq = {ions.nh4_N_g_per_eq, ions.k_g_per_eq, ions.ca_g_per_eq, ions.mg_g_per_eq, ions.mn_g_per_eq};
    // 세기는 검증에서 > 0이지만, 음수·NaN이 들어와도 식이 발산하지 않도록 0 이상으로 둬요(0이면 전부 용액).
    s.retention_factor = {non_negative(ce.nh4_retention_factor), non_negative(ce.k_retention_factor),
                          non_negative(ce.ca_retention_factor), non_negative(ce.mg_retention_factor),
                          non_negative(manganese_retention_factor(cell, params.manganese))};
    return s;
}

// Σ 교환성_i(φ) [eq/m²] = Σ T_i × w_i·φ / (1 + w_i·φ)
double exchangeable_eq_per_m2(const IonSet& s, double free_site_index) {
    double sum_eq_per_m2 = 0.0;
    for (std::size_t i = 0; i < kIonCount; ++i) {
        const double total_eq_per_m2 = s.total_g_per_m2[i] / s.g_per_eq[i];
        const double w_phi = s.retention_factor[i] * free_site_index;
        sum_eq_per_m2 += total_eq_per_m2 * w_phi / (1.0 + w_phi);
    }
    return sum_eq_per_m2;
}

// 용액 양을 평형값 쪽으로 비율만큼 옮기고 0 ≤ 용액 ≤ 총량으로 제한해요.
void relax_toward(double& solution_g_per_m2, double target_g_per_m2, double total_g_per_m2, double fraction) {
    solution_g_per_m2 += (target_g_per_m2 - solution_g_per_m2) * fraction;
    solution_g_per_m2 = std::clamp(solution_g_per_m2, 0.0, non_negative(total_g_per_m2));
}

}  // namespace

double cec_eq_per_m2(const SoilCell& cell, const CationExchangeParams& params) {
    return non_negative(cell.cec_mineral_eq_per_m2) +
           params.cec_organic_eq_per_g_C * non_negative(cell.stable_C_g_per_m2);
}

double manganese_retention_factor(const SoilCell& cell, const ManganeseParams& params) {
    return params.retention_ph_curve(cell.ph);
}

void redox_manganese(SoilCell& cell, const ManganeseParams& params) {
    // 두 이동량은 같은 틱 시작 값으로 계산해요(순서 의존 없음). 각자 원천 풀로 제한돼요.
    const double mn0_g_per_m2 = non_negative(cell.mn_g_per_m2);
    const double oxide0_g_per_m2 = non_negative(cell.mn_oxide_g_per_m2);
    const double oxidation_fraction =
        transfer_fraction(params.oxidation_rate_per_hour * params.oxidation_ph_curve(cell.ph) *
                          params.oxidation_o2_curve(cell.aeration_index));
    const double reduction_fraction =
        transfer_fraction(params.reduction_rate_per_hour * params.reduction_ph_curve(cell.ph) *
                          params.reduction_o2_curve(cell.aeration_index));
    const double oxidized_g_per_m2 = mn0_g_per_m2 * oxidation_fraction;
    const double reduced_g_per_m2 = oxide0_g_per_m2 * reduction_fraction;

    // 산화: 유효태 총량에서 빼고 용액은 비례 차감. 환원: 용액으로 들어가요.
    const double kept_g_per_m2 = mn0_g_per_m2 - oxidized_g_per_m2;
    double solution_g_per_m2 =
        mn0_g_per_m2 > 0.0 ? non_negative(cell.mn_solution_g_per_m2) * (kept_g_per_m2 / mn0_g_per_m2) : 0.0;
    solution_g_per_m2 += reduced_g_per_m2;
    cell.mn_g_per_m2 = kept_g_per_m2 + reduced_g_per_m2;
    cell.mn_oxide_g_per_m2 = (oxide0_g_per_m2 - reduced_g_per_m2) + oxidized_g_per_m2;
    cell.mn_solution_g_per_m2 = std::clamp(solution_g_per_m2, 0.0, cell.mn_g_per_m2);
}

ExchangeEquilibrium exchange_equilibrium(const SoilCell& cell, const Params& params) {
    const IonSet s = ion_set(cell, params);
    ExchangeEquilibrium eq;
    const double cec = cec_eq_per_m2(cell, params.cation_exchange);
    eq.cec_eq_per_m2 = cec;

    double total_eq_per_m2 = 0.0;
    for (std::size_t i = 0; i < kIonCount; ++i) {
        total_eq_per_m2 += s.total_g_per_m2[i] / s.g_per_eq[i];
    }

    if (!(cec > 0.0)) {
        // 예외: 자리가 없으면 φ = 0, 전부 용액
        eq.exchange_free_site_index = 0.0;
        eq.nh4_solution_N_g_per_m2 = s.total_g_per_m2[0];
        eq.k_solution_g_per_m2 = s.total_g_per_m2[1];
        eq.ca_solution_g_per_m2 = s.total_g_per_m2[2];
        eq.mg_solution_g_per_m2 = s.total_g_per_m2[3];
        eq.mn_solution_g_per_m2 = s.total_g_per_m2[4];
        return eq;
    }

    double free_site_index = 1.0;  // 예외: 양이온이 없으면 φ = 1
    if (total_eq_per_m2 > 0.0) {
        // f(φ) = cec × (1 − φ) − Σ 교환성(φ)는 φ에 대해 감소해요. f(0) = cec > 0, f(1) < 0.
        // 이분법을 solver_iterations회 고정 반복해요(결정론).
        double lo = 0.0;
        double hi = 1.0;
        for (std::int64_t it = 0; it < params.cation_exchange.solver_iterations; ++it) {
            const double mid = (lo + hi) / 2.0;
            const double residual_eq_per_m2 = cec * (1.0 - mid) - exchangeable_eq_per_m2(s, mid);
            if (residual_eq_per_m2 > 0.0) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        free_site_index = (lo + hi) / 2.0;
    }
    eq.exchange_free_site_index = std::clamp(free_site_index, 0.0, 1.0);

    // 평형 용액 = T_i / (1 + w_i·φ) (당량 → g 환산은 같은 비율이라 g로 바로 계산해요)
    std::array<double, kIonCount> solution_g_per_m2{};
    for (std::size_t i = 0; i < kIonCount; ++i) {
        solution_g_per_m2[i] = std::clamp(
            s.total_g_per_m2[i] / (1.0 + s.retention_factor[i] * eq.exchange_free_site_index), 0.0,
            s.total_g_per_m2[i]);
    }
    eq.nh4_solution_N_g_per_m2 = solution_g_per_m2[0];
    eq.k_solution_g_per_m2 = solution_g_per_m2[1];
    eq.ca_solution_g_per_m2 = solution_g_per_m2[2];
    eq.mg_solution_g_per_m2 = solution_g_per_m2[3];
    eq.mn_solution_g_per_m2 = solution_g_per_m2[4];
    return eq;
}

void update_cation_exchange(SoilCell& cell, const Params& params) {
    redox_manganese(cell, params.manganese);  // ADR-0010: 산화·환원이 교환 계산 앞
    const ExchangeEquilibrium eq = exchange_equilibrium(cell, params);
    cell.cec_eq_per_m2 = eq.cec_eq_per_m2;
    cell.exchange_free_site_index = eq.exchange_free_site_index;
    const double fraction = transfer_fraction(params.cation_exchange.exchange_rate_per_hour);
    relax_toward(cell.nh4_solution_N_g_per_m2, eq.nh4_solution_N_g_per_m2, cell.nh4_N_g_per_m2, fraction);
    relax_toward(cell.k_solution_g_per_m2, eq.k_solution_g_per_m2, cell.k_g_per_m2, fraction);
    relax_toward(cell.ca_solution_g_per_m2, eq.ca_solution_g_per_m2, cell.ca_g_per_m2, fraction);
    relax_toward(cell.mg_solution_g_per_m2, eq.mg_solution_g_per_m2, cell.mg_g_per_m2, fraction);
    relax_toward(cell.mn_solution_g_per_m2, eq.mn_solution_g_per_m2, cell.mn_g_per_m2, fraction);
}

void set_solution_to_equilibrium(SoilCell& cell, const Params& params) {
    const ExchangeEquilibrium eq = exchange_equilibrium(cell, params);
    cell.cec_eq_per_m2 = eq.cec_eq_per_m2;
    cell.exchange_free_site_index = eq.exchange_free_site_index;
    cell.nh4_solution_N_g_per_m2 = eq.nh4_solution_N_g_per_m2;
    cell.k_solution_g_per_m2 = eq.k_solution_g_per_m2;
    cell.ca_solution_g_per_m2 = eq.ca_solution_g_per_m2;
    cell.mg_solution_g_per_m2 = eq.mg_solution_g_per_m2;
    cell.mn_solution_g_per_m2 = eq.mn_solution_g_per_m2;
}

void sync_nh4_solution(SoilCell& cell, double old_nh4_N_g_per_m2) {
    const double new_total_g_per_m2 = non_negative(cell.nh4_N_g_per_m2);
    const double old_total_g_per_m2 = non_negative(old_nh4_N_g_per_m2);
    if (new_total_g_per_m2 >= old_total_g_per_m2) {
        cell.nh4_solution_N_g_per_m2 += new_total_g_per_m2 - old_total_g_per_m2;  // 무기화 → 용액
    } else {
        // 비례 차감 (옛 총량 > 새 총량 ≥ 0이라 옛 총량 > 0)
        cell.nh4_solution_N_g_per_m2 *= new_total_g_per_m2 / old_total_g_per_m2;
    }
    cell.nh4_solution_N_g_per_m2 = std::clamp(cell.nh4_solution_N_g_per_m2, 0.0, new_total_g_per_m2);
}

}  // namespace sim::process
