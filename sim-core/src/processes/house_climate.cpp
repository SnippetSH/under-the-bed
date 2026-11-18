// docs/spec/house-climate.md — 다중 피복·측창·난방 열수지, 열 관성, 이동 평균, CO2 수지
#include <algorithm>
#include <cmath>
#include <cstddef>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

// 단위 환산: 1 W·h = 0.0036 MJ, 1 g = 1000 mg
constexpr double kMJ_per_W_h = 0.0036;
constexpr double kMg_per_g = 1000.0;

[[nodiscard]] double outside_co2_at(const HouseParams& params, int hour) {
    if (params.outside_co2_ppm_by_hour.empty()) {
        return 0.0;
    }
    const auto index = static_cast<std::size_t>(hour) % params.outside_co2_ppm_by_hour.size();
    return params.outside_co2_ppm_by_hour[index];
}

}  // namespace

CoverState cover_state(const HouseParams& params, const HouseState& house) {
    double transmittance = 1.0;
    double resistance_m2K_per_W = 0.0;
    // 직렬 개구부 경로 path = sqrt(n) / sqrt(Σ 1/a_l²), a_l = (왼쪽 + 오른쪽 개방률) / 2. 어느 겹이든 0이면 0
    double inverse_area_sq_sum = 0.0;
    bool any_closed_layer = house.window_open_fraction.empty();
    for (const auto& layer : house.window_open_fraction) {
        const double open_sum = std::max(0.0, layer[0]) + std::max(0.0, layer[1]);
        transmittance *= params.layer_transmittance_factor;
        resistance_m2K_per_W += params.layer_resistance_m2K_per_W *
                                std::max(0.0, 1.0 - params.window_bypass_fraction * open_sum);
        const double area = open_sum / 2.0;
        if (area > 0.0) {
            inverse_area_sq_sum += 1.0 / (area * area);
        } else {
            any_closed_layer = true;
        }
    }
    const double path_index =
        any_closed_layer ? 0.0
                         : std::sqrt(static_cast<double>(house.window_open_fraction.size())) / std::sqrt(inverse_area_sq_sum);
    const double vent = params.vent_conductance_W_per_m2_per_K;
    const double through_cover =
        resistance_m2K_per_W > 0.0 ? std::min(1.0 / resistance_m2K_per_W, vent) : vent;
    return CoverState{
        .transmittance_factor = transmittance,
        .conductance_W_per_m2_per_K = through_cover + params.infiltration_W_per_m2_per_K,
        .vent_W_per_m2_per_K = vent * path_index,
        .path_index = path_index,
    };
}

double inside_solar_W_per_m2(const HouseParams& params, const HouseState& house, const WeatherHour& weather) {
    return std::max(0.0, weather.solar_radiation_W_per_m2) * cover_state(params, house).transmittance_factor;
}

HouseEquilibrium house_equilibrium(const HouseParams& params, const HouseState& house, const WeatherHour& weather) {
    const CoverState cover = cover_state(params, house);
    const double exchange_W_per_m2_per_K = cover.conductance_W_per_m2_per_K + cover.vent_W_per_m2_per_K;
    const double free_gain_W_per_m2 = params.ground_heat_W_per_m2 + std::max(0.0, weather.solar_radiation_W_per_m2) *
                                                                         cover.transmittance_factor *
                                                                         params.solar_heat_gain_fraction;
    if (!(exchange_W_per_m2_per_K > 0.0)) {
        // 검증된 파라미터에서는 일어나지 않아요(침투 > 0). 안전장치: 바깥 기온
        return HouseEquilibrium{.air_temp_degC = weather.outside_air_temp_degC, .heat_W_per_m2 = 0.0};
    }
    const double free_temp_degC = weather.outside_air_temp_degC + free_gain_W_per_m2 / exchange_W_per_m2_per_K;
    double heat_W_per_m2 = 0.0;
    if (house.heater_on) {
        heat_W_per_m2 = std::clamp((house.heater_setpoint_degC - free_temp_degC) * exchange_W_per_m2_per_K, 0.0,
                                   params.heater_capacity_W_per_m2);
    }
    return HouseEquilibrium{
        .air_temp_degC =
            weather.outside_air_temp_degC + (free_gain_W_per_m2 + heat_W_per_m2) / exchange_W_per_m2_per_K,
        .heat_W_per_m2 = heat_W_per_m2,
    };
}

HouseState initial_house(const HouseParams& params, int cover_layers, const WeatherHour& weather, int hour) {
    HouseState house;
    set_cover_layers(house, cover_layers);
    const HouseEquilibrium eq = house_equilibrium(params, house, weather);
    house.air_temp_degC = eq.air_temp_degC;
    house.air_temp_mean_degC = eq.air_temp_degC;
    house.outside_co2_ppm = outside_co2_at(params, hour);
    house.co2_ppm = house.outside_co2_ppm;
    house.inside_solar_W_per_m2 = inside_solar_W_per_m2(params, house, weather);
    house.vent_path_index = cover_state(params, house).path_index;
    return house;
}

void update_house_air(HouseState& house, const HouseParams& params, const WeatherHour& weather, int hour) {
    const HouseEquilibrium eq = house_equilibrium(params, house, weather);
    house.air_temp_degC += (eq.air_temp_degC - house.air_temp_degC) * std::min(1.0, params.air_relax_rate_per_hour);
    house.air_temp_mean_degC +=
        (house.air_temp_degC - house.air_temp_mean_degC) * std::min(1.0, params.temp_mean_relax_rate_per_hour);
    house.heating_power_W_per_m2 = eq.heat_W_per_m2;
    house.heating_energy_MJ_per_m2 += eq.heat_W_per_m2 * kMJ_per_W_h;
    house.outside_co2_ppm = outside_co2_at(params, hour);
    house.inside_solar_W_per_m2 = inside_solar_W_per_m2(params, house, weather);
    house.vent_path_index = cover_state(params, house).path_index;
}

void update_house_co2(HouseState& house, const HouseParams& params, const CropParams& crop_params,
                      double crop_net_dry_matter_g_per_m2, double soil_respired_C_g_per_m2) {
    const double crop_uptake_g_co2_per_m2 = crop_net_dry_matter_g_per_m2 * crop_params.co2_g_per_g_dry_matter;
    // C → CO2 질량비(44/12)는 건물 1 g당 CO2 ÷ 건물 1 g당 C로 얻어요 (1.65 / 0.45 = 44/12, 데이터 일관성)
    const double co2_g_per_g_C = crop_params.dry_matter_C_fraction > 0.0
                                     ? crop_params.co2_g_per_g_dry_matter / crop_params.dry_matter_C_fraction
                                     : 0.0;
    const double soil_release_g_co2_per_m2 = std::max(0.0, soil_respired_C_g_per_m2) * co2_g_per_g_C;
    const double g_per_m2_per_ppm = params.air_volume_m3_per_m2 * params.co2_mg_per_m3_per_ppm / kMg_per_g;
    if (g_per_m2_per_ppm > 0.0) {
        house.co2_ppm += (soil_release_g_co2_per_m2 - crop_uptake_g_co2_per_m2) / g_per_m2_per_ppm;
    }
    const double air_changes =
        params.infiltration_air_changes_per_hour + params.vent_air_changes_per_hour * house.vent_path_index;
    house.co2_ppm += (house.outside_co2_ppm - house.co2_ppm) * std::min(1.0, std::max(0.0, air_changes));
    house.co2_ppm = std::max(house.co2_ppm, params.co2_floor_ppm);
}

void set_cover_layers(HouseState& house, int layers) {
    house.cover_layers = std::max(0, layers);
    house.window_open_fraction.resize(static_cast<std::size_t>(house.cover_layers), std::array<double, 2>{0.0, 0.0});
}

bool in_cover_change_window(const HouseParams& params, int day_of_year) {
    return day_of_year >= params.cover_change_window_doy[0] && day_of_year <= params.cover_change_window_doy[1];
}

}  // namespace sim::process
