// docs/spec/denitrification.md — 과습 토양의 질산 기체 손실 (셀 단계 f1)
#include <algorithm>

#include "sim/processes.hpp"

namespace sim::process {

void denitrify(SoilCell& cell, const DenitrificationParams& params, const IonParams& ions, Ledger& ledger) {
    const double no3_N_g_per_m2 = std::max(0.0, cell.no3_N_g_per_m2);
    if (!(no3_N_g_per_m2 > 0.0)) {
        return;
    }
    const double fraction = std::min(1.0, params.rate_per_hour * params.o2_curve(cell.aeration_index) *
                                              params.temp_curve(cell.soil_temp_degC) *
                                              params.carbon_curve(cell.labile_C_g_per_m2));
    if (!(fraction > 0.0)) {
        return;
    }
    const double lost_N_g_per_m2 = std::min(no3_N_g_per_m2, no3_N_g_per_m2 * fraction);
    cell.no3_N_g_per_m2 = no3_N_g_per_m2 - lost_N_g_per_m2;
    ledger.N_gas_g += lost_N_g_per_m2;
    // 탈질은 H+를 소비해요 (H_eq_per_eq_N < 0). pH 단계(g)가 이번 틱에 소비해요.
    cell.acid_H_eq_this_tick += lost_N_g_per_m2 / ions.no3_N_g_per_eq * params.H_eq_per_eq_N;
}

}  // namespace sim::process
