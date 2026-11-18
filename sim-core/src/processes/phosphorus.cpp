// docs/spec/phosphorus.md (M3) — 인산 고정 (셀 단계 f3, 양이온 교환 다음)
#include <algorithm>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

// 1차 반응의 이번 틱 이동 비율 min(1, 속도 × 계수). 음수·NaN은 0(이동 없음).
[[nodiscard]] double transfer_fraction(double rate_times_factors) {
    if (!(rate_times_factors > 0.0)) {
        return 0.0;
    }
    return std::min(1.0, rate_times_factors);
}

}  // namespace

void fix_phosphorus(SoilCell& cell, const PhosphorusParams& params) {
    // 두 이동량은 같은 틱 시작 값으로 계산해요(순서 의존 없음). 각자 원천 풀로 제한돼서 음수가 안 돼요.
    const double p0_g_per_m2 = std::max(0.0, cell.p_g_per_m2);
    const double fixed0_g_per_m2 = std::max(0.0, cell.p_fixed_g_per_m2);
    const double fixed_g_per_m2 =
        p0_g_per_m2 * transfer_fraction(params.fixation_rate_per_hour * params.fixation_ph_curve(cell.ph));
    const double released_g_per_m2 = fixed0_g_per_m2 * transfer_fraction(params.release_rate_per_hour);
    cell.p_g_per_m2 = (p0_g_per_m2 - fixed_g_per_m2) + released_g_per_m2;
    cell.p_fixed_g_per_m2 = (fixed0_g_per_m2 - released_g_per_m2) + fixed_g_per_m2;
}

}  // namespace sim::process
