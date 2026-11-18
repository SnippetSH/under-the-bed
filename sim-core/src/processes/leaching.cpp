// docs/spec/nitrogen.md "용탈" — 배수에 딸려 나가는 NO3-, NO2-, (일부) NH4+
#include <algorithm>

#include "sim/processes.hpp"

namespace sim::process {

void leach(SoilCell& cell, const LeachingParams& params, Ledger& ledger) {
    const double drained_mm = cell.drained_mm_this_tick;
    if (!(drained_mm > 0.0)) {
        return;
    }
    const double total_mm = cell.water_mm + drained_mm;
    const double f = std::clamp(drained_mm / total_mm, 0.0, 1.0);
    const double nh4_f = f * std::clamp(params.nh4_solution_fraction, 0.0, 1.0);

    const double no3_loss = cell.no3_N_g_per_m2 * f;
    const double no2_loss = cell.no2_N_g_per_m2 * f;
    const double nh4_loss = cell.nh4_N_g_per_m2 * nh4_f;

    cell.no3_N_g_per_m2 -= no3_loss;
    cell.no2_N_g_per_m2 -= no2_loss;
    cell.nh4_N_g_per_m2 -= nh4_loss;
    ledger.N_leached_g += no3_loss + no2_loss + nh4_loss;
}

}  // namespace sim::process
