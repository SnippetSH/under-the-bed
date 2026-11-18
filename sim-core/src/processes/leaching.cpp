// docs/spec/nitrogen.md "용탈" + soil-chemistry "용탈 확장" + M3 cation-exchange (용액 상태 기준)
#include <algorithm>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

[[nodiscard]] double solution_loss(double pool, double solution_fraction, double f) {
    if (!(pool > 0.0)) {
        return 0.0;
    }
    return pool * (f * std::clamp(solution_fraction, 0.0, 1.0));
}

// 용액 상태 × f를 총량·용액에서 함께 빼고 이동량을 돌려줘요.
double leach_solution(double& total, double& solution, double f) {
    const double loss = std::min(std::max(0.0, solution) * f, std::max(0.0, total));
    total -= loss;
    solution = std::clamp(solution - loss, 0.0, std::max(0.0, total));
    return loss;
}

}  // namespace

void leach(SoilCell& cell, const Params& params, Ledger& ledger) {
    const SoilChemistryParams& chemistry = params.soil_chemistry;
    const double drained_mm = cell.drained_mm_this_tick;
    if (!(drained_mm > 0.0)) {
        return;
    }
    const double total_mm = cell.water_mm + drained_mm;
    const double f = std::clamp(drained_mm / total_mm, 0.0, 1.0);

    const double no3_loss = cell.no3_N_g_per_m2 * f;
    const double no2_loss = cell.no2_N_g_per_m2 * f;
    cell.no3_N_g_per_m2 -= no3_loss;
    cell.no2_N_g_per_m2 -= no2_loss;
    const double nh4_loss = leach_solution(cell.nh4_N_g_per_m2, cell.nh4_solution_N_g_per_m2, f);
    ledger.N_leached_g += no3_loss + no2_loss + nh4_loss;

    ledger.K_leached_g += leach_solution(cell.k_g_per_m2, cell.k_solution_g_per_m2, f);
    ledger.Ca_leached_g += leach_solution(cell.ca_g_per_m2, cell.ca_solution_g_per_m2, f);
    ledger.Mg_leached_g += leach_solution(cell.mg_g_per_m2, cell.mg_solution_g_per_m2, f);
    ledger.Mn_leached_g += leach_solution(cell.mn_g_per_m2, cell.mn_solution_g_per_m2, f);

    const double p_loss = solution_loss(cell.p_g_per_m2, chemistry.p_solution_fraction, f);
    const double s_loss = solution_loss(cell.s_g_per_m2, chemistry.s_solution_fraction, f);
    cell.p_g_per_m2 -= p_loss;
    cell.s_g_per_m2 -= s_loss;
    ledger.P_leached_g += p_loss;
    ledger.S_leached_g += s_loss;
}

}  // namespace sim::process
