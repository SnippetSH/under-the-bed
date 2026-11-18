// docs/spec/aeration.md — 통기 지수, 경운
#include <algorithm>

#include "sim/processes.hpp"

namespace sim::process {

double aeration_index(const SoilCell& cell, const AerationParams& params) {
    // 원 지수: 포장용수량 이하면 1, 그 위로는 포화에 가까울수록 0으로
    double raw = 1.0;
    if (cell.water_mm > cell.field_capacity_mm) {
        const double span_mm = cell.saturation_mm - cell.field_capacity_mm;
        raw = span_mm > 0.0 ? (cell.saturation_mm - cell.water_mm) / span_mm : 0.0;
    }
    raw = std::clamp(raw, 0.0, 1.0);

    // 상한: 다짐이 낮추고, 유기 C가 올려요
    const double cap = std::min(1.0, params.compaction_cap_curve(cell.compaction_index) +
                                         params.organic_C_cap_bonus_per_g_C * cell.organic_C_g_per_m2());
    return std::clamp(std::min(raw, cap), 0.0, 1.0);
}

void apply_till(SoilCell& cell, Crop& crop, const AerationParams& params) {
    cell.compaction_index = params.tilled_compaction_index;
    if (crop.present && crop.alive) {
        crop.root_health *= (1.0 - params.till_root_damage_fraction);
    }
}

}  // namespace sim::process
