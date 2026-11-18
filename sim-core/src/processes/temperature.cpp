// docs/spec/soil-temperature.md — 하우스 기온, 하우스 안 일사, 토양 온도 완화
#include <algorithm>

#include "sim/processes.hpp"

namespace sim::process {

double house_air_temp_degC(const HouseParams& params, const WeatherHour& weather) {
    return weather.outside_air_temp_degC + params.base_offset_degC +
           params.solar_gain_degC_per_W_per_m2 * weather.solar_radiation_W_per_m2;
}

double inside_solar_W_per_m2(const HouseParams& params, const WeatherHour& weather) {
    return weather.solar_radiation_W_per_m2 * params.solar_transmittance_factor;
}

void relax_soil_temperature(SoilCell& cell, const SoilTemperatureParams& params, double house_air_temp_degC) {
    const double fraction = std::min(1.0, params.relax_rate_per_hour);
    cell.soil_temp_degC += (house_air_temp_degC - cell.soil_temp_degC) * fraction;
}

}  // namespace sim::process
