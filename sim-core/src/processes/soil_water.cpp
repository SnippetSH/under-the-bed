// docs/spec/soil-water.md — 관수, 증발, 배수
#include <algorithm>

#include "sim/processes.hpp"

namespace sim::process {

void apply_irrigation(SoilCell& cell, double amount_mm, const AerationParams& aeration, Ledger& ledger) {
    // 음수·NaN 입력은 무시해요 (물이 줄거나 장부가 깨지지 않도록)
    if (!(amount_mm > 0.0)) {
        return;
    }
    cell.water_mm += amount_mm;
    ledger.water_in_mm += amount_mm;
    // 관수는 흙을 다져요 (1에서 멈춤)
    cell.compaction_index = std::min(1.0, cell.compaction_index + amount_mm * aeration.compaction_per_mm_irrigation);
}

void update_soil_water(SoilCell& cell, const SoilWaterParams& params, double inside_solar_W_per_m2,
                       double canopy_interception, double transpiration_demand_mm, Ledger& ledger) {
    // 증발: 잠재 증발 × 수분 계수 × (1 − 수관 차광), 보유 수분을 넘지 않아요
    const double solar = std::max(0.0, inside_solar_W_per_m2);
    const double potential_mm =
        std::max(0.0, params.evap_mm_per_hour_per_W_per_m2 * solar + params.evap_base_mm_per_hour);
    const double relative_water = cell.field_capacity_mm > 0.0 ? cell.water_mm / cell.field_capacity_mm : 0.0;
    const double shade_factor = 1.0 - std::clamp(canopy_interception, 0.0, 1.0);
    const double evaporated_mm = std::clamp(potential_mm * params.evap_moisture_curve(relative_water) * shade_factor,
                                            0.0, std::max(0.0, cell.water_mm));
    cell.water_mm -= evaporated_mm;
    ledger.water_evaporated_mm += evaporated_mm;

    // 증산: 요구량, 남은 수분을 넘지 않아요 (음수·NaN 요구는 0)
    const double demand_mm = transpiration_demand_mm > 0.0 ? transpiration_demand_mm : 0.0;
    const double transpired_mm = std::min(demand_mm, std::max(0.0, cell.water_mm));
    cell.water_mm -= transpired_mm;
    ledger.water_transpired_mm += transpired_mm;

    // 포화 초과분은 그 틱에 전부 배수
    double drained_mm = 0.0;
    if (cell.water_mm > cell.saturation_mm) {
        drained_mm = cell.water_mm - cell.saturation_mm;
        cell.water_mm -= drained_mm;
    }

    // 포장용수량 초과분은 배수율 × 다짐 배율만큼
    const double excess_mm = std::max(0.0, cell.water_mm - cell.field_capacity_mm);
    const double rate = std::clamp(params.drainage_rate_per_hour * params.drainage_compaction_curve(cell.compaction_index),
                                   0.0, 1.0);
    const double field_drained_mm = std::min(excess_mm, excess_mm * rate);
    cell.water_mm -= field_drained_mm;
    drained_mm += field_drained_mm;

    cell.drained_mm_this_tick = drained_mm;
    ledger.water_drained_mm += drained_mm;
}

}  // namespace sim::process
