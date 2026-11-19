// docs/spec/soil-temperature.md — 토양 온도 완화 (M4: 하우스 기온·일사는 house_climate.cpp)
#include <algorithm>

#include "sim/processes.hpp"

namespace sim::process {

double soil_relax_blend_target_degC(const SoilCell& cell, const SoilTemperatureParams& params,
                                    double air_target_degC) {
    // M5a 프로토타입 3차 (깊은 흙 온도 항): 목표 = (1 − f) × (하우스 기온 + 멀칭 지온 이득) + f × 깊은 흙 온도.
    // f = 0이면 (1 − 0) × x + 0 × 깊은 흙 = x 로 이전 식과 비트 단위로 같아요.
    const double f = std::clamp(params.deep_coupling_fraction, 0.0, 1.0);
    return (1.0 - f) * air_target_degC + f * cell.deep_soil_temp_degC;
}

void relax_soil_temperature(SoilCell& cell, const SoilTemperatureParams& params, double house_air_temp_degC,
                            double evaporated_mm) {
    const double target_degC = soil_relax_blend_target_degC(cell, params, house_air_temp_degC);
    const double fraction = std::min(1.0, params.relax_rate_per_hour);
    cell.soil_temp_degC += (target_degC - cell.soil_temp_degC) * fraction;
    // 증발 냉각 (soil-temperature): 완화 뒤
    cell.soil_temp_degC -= std::max(0.0, evaporated_mm) * params.evap_cooling_degC_per_mm;
    // 깊은 흙 온도: 이번 틱 근권 갱신(완화 + 증발 냉각) 뒤에 근권 온도를 따라가요 (M5a 프로토타입 3차).
    // 분해 발열(셀 단계 e)은 이 뒤라서 다음 틱에 반영돼요
    const double deep_fraction = std::clamp(params.deep_relax_rate_per_hour, 0.0, 1.0);
    cell.deep_soil_temp_degC += (cell.soil_temp_degC - cell.deep_soil_temp_degC) * deep_fraction;
}

}  // namespace sim::process
