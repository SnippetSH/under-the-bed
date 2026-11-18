// docs/spec/cation-exchange.md + manganese.md "이동성과 분배" — CEC, 빈자리 φ, 평형, 완화 (M3 프로토타입)
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

[[nodiscard]] double non_negative(double v) { return v > 0.0 ? v : 0.0; }

constexpr std::size_t kIonCount = 5;  // NH4, K, Ca, Mg, Mn

struct IonSet {
    std::array<double, kIonCount> total_g{};        // 총량 g/m²
    std::array<double, kIonCount> g_per_eq{};       // 당량 무게
    std::array<double, kIonCount> retention{};      // 세기 w (Mn은 이동성으로 나눈 값)
    std::array<bool, kIonCount> fully_bound{};      // Mn 이동성 ≤ 0: 평형 용액 0
};

IonSet ion_set(const SoilCell& cell, const Params& params) {
    const CationExchangeParams& ce = params.cation_exchange;
    const IonParams& ions = params.ions;
    IonSet s;
    s.total_g = {non_negative(cell.nh4_N_g_per_m2), non_negative(cell.k_g_per_m2), non_negative(cell.ca_g_per_m2),
                 non_negative(cell.mg_g_per_m2), non_negative(cell.mn_g_per_m2)};
    s.g_per_eq = {ions.nh4_N_g_per_eq, ions.k_g_per_eq, ions.ca_g_per_eq, ions.mg_g_per_eq, ions.mn_g_per_eq};
    s.retention = {ce.nh4_retention_factor, ce.k_retention_factor, ce.ca_retention_factor, ce.mg_retention_factor,
                   0.0};
    s.retention[4] = manganese_retention(cell, params.manganese);
    if (!(s.retention[4] > 0.0)) {
        s.retention[4] = 0.0;  // 세기 0: 전부 용액 (교환성 0)
    }
    return s;
}

// Σ 교환성_i(φ) [eq]
double exchangeable_eq(const IonSet& s, double phi) {
    double sum = 0.0;
    for (std::size_t i = 0; i < kIonCount; ++i) {
        const double t_eq = s.total_g[i] / s.g_per_eq[i];
        if (s.fully_bound[i]) {
            sum += phi > 0.0 ? t_eq : 0.0;
            continue;
        }
        const double wphi = s.retention[i] * phi;
        sum += t_eq * wphi / (1.0 + wphi);
    }
    return sum;
}

}  // namespace

double cec_eq_per_m2(const SoilCell& cell, const CationExchangeParams& params) {
    return non_negative(cell.cec_mineral_eq_per_m2) + params.cec_organic_eq_per_g_C * non_negative(cell.stable_C_g_per_m2);
}

double manganese_retention(const SoilCell& cell, const ManganeseParams& params) {
    return params.retention_ph_curve(cell.ph);
}

void redox_manganese(SoilCell& cell, const ManganeseParams& params) {
    const double mn0 = non_negative(cell.mn_g_per_m2);
    const double oxide0 = non_negative(cell.mn_oxide_g_per_m2);
    const double ox_fraction = std::clamp(params.oxidation_rate_per_hour * params.oxidation_ph_curve(cell.ph) *
                                              params.oxidation_o2_curve(cell.aeration_index),
                                          0.0, 1.0);
    const double red_fraction = std::clamp(params.reduction_rate_per_hour * params.reduction_ph_curve(cell.ph) *
                                               params.reduction_o2_curve(cell.aeration_index),
                                           0.0, 1.0);
    const double oxidized = mn0 * ox_fraction;
    const double reduced = oxide0 * red_fraction;
    // 산화: 용액 비례 차감 (산화 전 총량 기준), 환원: 용액으로
    const double kept = mn0 - oxidized;
    double solution = mn0 > 0.0 ? non_negative(cell.mn_solution_g_per_m2) * (kept / mn0) : 0.0;
    cell.mn_g_per_m2 = kept + reduced;
    cell.mn_oxide_g_per_m2 = oxide0 - reduced + oxidized;
    solution += reduced;
    cell.mn_solution_g_per_m2 = std::clamp(solution, 0.0, cell.mn_g_per_m2);
    cell.mn_oxidized_this_tick_g_per_m2 = oxidized;
    cell.mn_reduced_this_tick_g_per_m2 = reduced;
}

ExchangeEquilibrium exchange_equilibrium(const SoilCell& cell, const Params& params) {
    const IonSet s = ion_set(cell, params);
    ExchangeEquilibrium eq;
    const double cec = cec_eq_per_m2(cell, params.cation_exchange);
    eq.cec_eq_per_m2 = cec;

    double total_eq = 0.0;
    for (std::size_t i = 0; i < kIonCount; ++i) {
        total_eq += s.total_g[i] / s.g_per_eq[i];
    }

    double phi = 0.0;
    if (!(cec > 0.0)) {
        phi = 0.0;  // 자리 없음: 전부 용액
    } else if (!(total_eq > 0.0)) {
        phi = 1.0;
    } else {
        // f(φ) = cec(1−φ) − Σ교환성(φ): φ에 대해 감소. f(0) = cec > 0, f(1) ≤ 0
        double lo = 0.0;
        double hi = 1.0;
        for (std::int64_t it = 0; it < params.cation_exchange.solver_iterations; ++it) {
            const double mid = 0.5 * (lo + hi);
            const double f = cec * (1.0 - mid) - exchangeable_eq(s, mid);
            if (f > 0.0) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        phi = 0.5 * (lo + hi);
    }
    eq.free_site_index = std::clamp(phi, 0.0, 1.0);

    std::array<double, kIonCount> sol{};
    for (std::size_t i = 0; i < kIonCount; ++i) {
        if (!(cec > 0.0)) {
            sol[i] = s.total_g[i];
        } else if (s.fully_bound[i]) {
            sol[i] = eq.free_site_index > 0.0 ? 0.0 : s.total_g[i];
        } else {
            sol[i] = s.total_g[i] / (1.0 + s.retention[i] * eq.free_site_index);
        }
        sol[i] = std::clamp(sol[i], 0.0, s.total_g[i]);
    }
    eq.nh4_N = sol[0];
    eq.k = sol[1];
    eq.ca = sol[2];
    eq.mg = sol[3];
    eq.mn = sol[4];
    return eq;
}

namespace {
void relax(double& solution, double target, double total, double fraction) {
    solution += (target - solution) * fraction;
    solution = std::clamp(solution, 0.0, non_negative(total));
}
}  // namespace

void update_cation_exchange(SoilCell& cell, const Params& params) {
    redox_manganese(cell, params.manganese);  // ADR-0010: 산화·환원이 교환 계산 앞
    const ExchangeEquilibrium eq = exchange_equilibrium(cell, params);
    cell.cec_eq_per_m2 = eq.cec_eq_per_m2;
    cell.exchange_free_site_index = eq.free_site_index;
    const double fraction = std::clamp(params.cation_exchange.exchange_rate_per_hour, 0.0, 1.0);
    relax(cell.nh4_solution_N_g_per_m2, eq.nh4_N, cell.nh4_N_g_per_m2, fraction);
    relax(cell.k_solution_g_per_m2, eq.k, cell.k_g_per_m2, fraction);
    relax(cell.ca_solution_g_per_m2, eq.ca, cell.ca_g_per_m2, fraction);
    relax(cell.mg_solution_g_per_m2, eq.mg, cell.mg_g_per_m2, fraction);
    relax(cell.mn_solution_g_per_m2, eq.mn, cell.mn_g_per_m2, fraction);
}

void set_solution_to_equilibrium(SoilCell& cell, const Params& params) {
    const ExchangeEquilibrium eq = exchange_equilibrium(cell, params);
    cell.cec_eq_per_m2 = eq.cec_eq_per_m2;
    cell.exchange_free_site_index = eq.free_site_index;
    cell.nh4_solution_N_g_per_m2 = eq.nh4_N;
    cell.k_solution_g_per_m2 = eq.k;
    cell.ca_solution_g_per_m2 = eq.ca;
    cell.mg_solution_g_per_m2 = eq.mg;
    cell.mn_solution_g_per_m2 = eq.mn;
}

void sync_nh4_solution(SoilCell& cell, double old_nh4_N_g_per_m2) {
    const double new_total = non_negative(cell.nh4_N_g_per_m2);
    const double old_total = non_negative(old_nh4_N_g_per_m2);
    if (new_total >= old_total) {
        cell.nh4_solution_N_g_per_m2 += new_total - old_total;  // 무기화 → 용액
    } else if (old_total > 0.0) {
        cell.nh4_solution_N_g_per_m2 *= new_total / old_total;  // 비례 차감
    } else {
        cell.nh4_solution_N_g_per_m2 = 0.0;
    }
    cell.nh4_solution_N_g_per_m2 = std::clamp(cell.nh4_solution_N_g_per_m2, 0.0, new_total);
}

}  // namespace sim::process
