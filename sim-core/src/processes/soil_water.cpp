// docs/spec/soil-water.md — 관수, 증발, 배수
#include <algorithm>

#include "sim/processes.hpp"

namespace sim::process {

void apply_irrigation(SoilCell& cell, double amount_mm, Ledger& ledger) {
    // 음수·NaN 입력은 무시해요 (물이 줄거나 장부가 깨지지 않도록)
    if (!(amount_mm > 0.0)) {
        return;
    }
    cell.water_mm += amount_mm;
    ledger.water_in_mm += amount_mm;
}

void update_soil_water(SoilCell& cell, const SoilWaterParams& params, double inside_solar_W_per_m2,
                       Ledger& ledger) {
    // 증발: 잠재 증발 × 수분 계수, 보유 수분을 넘지 않아요
    const double solar = std::max(0.0, inside_solar_W_per_m2);
    const double potential_mm =
        std::max(0.0, params.evap_mm_per_hour_per_W_per_m2 * solar + params.evap_base_mm_per_hour);
    const double relative_water = cell.field_capacity_mm > 0.0 ? cell.water_mm / cell.field_capacity_mm : 0.0;
    const double evaporated_mm =
        std::clamp(potential_mm * params.evap_moisture_curve(relative_water), 0.0, std::max(0.0, cell.water_mm));
    cell.water_mm -= evaporated_mm;
    ledger.water_evaporated_mm += evaporated_mm;

    // 배수: 포장용수량 초과분의 일정 비율
    const double excess_mm = std::max(0.0, cell.water_mm - cell.field_capacity_mm);
    const double drained_mm = std::min(excess_mm, excess_mm * std::clamp(params.drainage_rate_per_hour, 0.0, 1.0));
    cell.water_mm -= drained_mm;
    cell.drained_mm_this_tick = drained_mm;
    ledger.water_drained_mm += drained_mm;
}

}  // namespace sim::process
