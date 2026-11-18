// docs/spec/nitrogen.md "용탈" — 배수에 딸려 나가는 NO3-, NO2-, (일부) NH4+
// + docs/spec/soil-chemistry.md "용탈 확장" — K·Ca·Mg·P·S의 용액 부분
#include <algorithm>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

// 풀 중 용액 부분이 배수 비율 f만큼 빠지는 양. 계수는 [0, 1]로 제한해서 풀이 음수가 되지 않아요.
[[nodiscard]] double solution_loss(double pool, double solution_fraction, double f) {
    if (!(pool > 0.0)) {
        return 0.0;
    }
    return pool * (f * std::clamp(solution_fraction, 0.0, 1.0));
}

}  // namespace

void leach(SoilCell& cell, const LeachingParams& params, const SoilChemistryParams& chemistry, Ledger& ledger) {
    const double drained_mm = cell.drained_mm_this_tick;
    if (!(drained_mm > 0.0)) {
        return;
    }
    const double total_mm = cell.water_mm + drained_mm;
    const double f = std::clamp(drained_mm / total_mm, 0.0, 1.0);

    // 질소 (M1 동작 그대로)
    const double nh4_f = f * std::clamp(params.nh4_solution_fraction, 0.0, 1.0);

    const double no3_loss = cell.no3_N_g_per_m2 * f;
    const double no2_loss = cell.no2_N_g_per_m2 * f;
    const double nh4_loss = cell.nh4_N_g_per_m2 * nh4_f;

    cell.no3_N_g_per_m2 -= no3_loss;
    cell.no2_N_g_per_m2 -= no2_loss;
    cell.nh4_N_g_per_m2 -= nh4_loss;
    ledger.N_leached_g += no3_loss + no2_loss + nh4_loss;

    // K·Ca·Mg·P·S: 용액 부분만 (M2)
    const double k_loss = solution_loss(cell.k_g_per_m2, chemistry.k_solution_fraction, f);
    const double ca_loss = solution_loss(cell.ca_g_per_m2, chemistry.ca_solution_fraction, f);
    const double mg_loss = solution_loss(cell.mg_g_per_m2, chemistry.mg_solution_fraction, f);
    const double p_loss = solution_loss(cell.p_g_per_m2, chemistry.p_solution_fraction, f);
    const double s_loss = solution_loss(cell.s_g_per_m2, chemistry.s_solution_fraction, f);

    cell.k_g_per_m2 -= k_loss;
    cell.ca_g_per_m2 -= ca_loss;
    cell.mg_g_per_m2 -= mg_loss;
    cell.p_g_per_m2 -= p_loss;
    cell.s_g_per_m2 -= s_loss;

    ledger.K_leached_g += k_loss;
    ledger.Ca_leached_g += ca_loss;
    ledger.Mg_leached_g += mg_loss;
    ledger.P_leached_g += p_loss;
    ledger.S_leached_g += s_loss;
}

}  // namespace sim::process
