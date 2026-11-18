// docs/spec/phosphorus.md — 인산 고정 (셀 단계 f3, M3 프로토타입)
#include <algorithm>

#include "sim/processes.hpp"

namespace sim::process {

void fix_phosphorus(SoilCell& cell, const PhosphorusParams& params) {
    const double p0 = std::max(0.0, cell.p_g_per_m2);
    const double fixed0 = std::max(0.0, cell.p_fixed_g_per_m2);
    const double fix_fraction = std::clamp(params.fixation_rate_per_hour * params.fixation_ph_curve(cell.ph), 0.0, 1.0);
    const double release_fraction = std::clamp(params.release_rate_per_hour, 0.0, 1.0);
    const double fixed = p0 * fix_fraction;
    const double released = fixed0 * release_fraction;
    cell.p_g_per_m2 = p0 + released - fixed;
    cell.p_fixed_g_per_m2 = fixed0 + fixed - released;
}

}  // namespace sim::process
