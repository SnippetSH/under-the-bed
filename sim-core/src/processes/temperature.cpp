// docs/spec/soil-temperature.md — 토양 온도 완화 (M4: 하우스 기온·일사는 house_climate.cpp)
#include <algorithm>

#include "sim/processes.hpp"

namespace sim::process {

void relax_soil_temperature(SoilCell& cell, const SoilTemperatureParams& params, double house_air_temp_degC) {
    const double fraction = std::min(1.0, params.relax_rate_per_hour);
    cell.soil_temp_degC += (house_air_temp_degC - cell.soil_temp_degC) * fraction;
}

}  // namespace sim::process
