// docs/spec/house-humidity.md (M5b) — 하우스 수증기 수지(음해법), 피복 면 온도, 공기 흐름, 잎 젖음
#include <algorithm>
#include <cmath>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

// 단위 환산: 1 mm (지면 1 m²) = 1000 g, 공기 V m³/m² 기준 g/m³ ↔ mm
constexpr double kG_per_mm = 1000.0;
// 기상 상대습도 % → 0~1
constexpr double kPercent = 100.0;

[[nodiscard]] double non_negative(double value) { return std::max(0.0, value); }

[[nodiscard]] double air_volume_m3_per_m2(const HouseParams& house_params) {
    return non_negative(house_params.air_volume_m3_per_m2);
}

[[nodiscard]] double outside_vapor_g_per_m3(const HumidityParams& params, const WeatherHour& weather) {
    const double rh_fraction = std::clamp(weather.outside_rh_percent / kPercent, 0.0, 1.0);
    return rh_fraction * saturation_vapor_g_per_m3(params, weather.outside_air_temp_degC);
}

// 상대습도 = 수증기 ÷ 포화(하우스 기온), 0~1. 포화가 0 이하(검증된 params에서는 없음)면 0
[[nodiscard]] double relative_humidity_index(const HumidityParams& params, double vapor_g_per_m3, double air_temp_degC) {
    const double sat = saturation_vapor_g_per_m3(params, air_temp_degC);
    if (!(sat > 0.0)) {
        return 0.0;
    }
    return std::clamp(vapor_g_per_m3 / sat, 0.0, 1.0);
}

}  // namespace

double saturation_vapor_g_per_m3(const HumidityParams& params, double temp_degC) {
    return non_negative(params.saturation_vapor_curve(temp_degC));
}

double cover_inner_temp_degC(const HouseParams& house_params, const HumidityParams& params, const HouseState& house,
                             double outside_air_temp_degC) {
    const double resistance_m2K_per_W = cover_state(house_params, house).resistance_m2K_per_W;
    const double frac =
        resistance_m2K_per_W > 0.0
            ? std::clamp(1.0 - params.inner_surface_resistance_m2K_per_W / resistance_m2K_per_W, 0.0, 1.0)
            : 0.0;
    return outside_air_temp_degC + frac * (house.air_temp_degC - outside_air_temp_degC);
}

void init_house_humidity(HouseState& house, const HouseParams& house_params, const HumidityParams& params,
                         const WeatherHour& weather) {
    // 첫 틱 바깥 수증기. 하우스 기온의 포화를 넘지 않게 잘라요 (불변 조건 rh ≤ 1)
    const double vapor = std::min(outside_vapor_g_per_m3(params, weather),
                                  saturation_vapor_g_per_m3(params, house.air_temp_degC));
    house.vapor_g_per_m3 = non_negative(vapor);
    house.rh_index = relative_humidity_index(params, house.vapor_g_per_m3, house.air_temp_degC);
    house.rh_mean_index = house.rh_index;
    house.vapor_source_mm = 0.0;
    house.water_condensed_mm = 0.0;
    house.vapor_vented_mm = 0.0;
    house.vapor_deposited_mm = 0.0;
    house.vapor_initial_mm = house.vapor_g_per_m3 * air_volume_m3_per_m2(house_params) / kG_per_mm;
    house.cover_inner_temp_degC = cover_inner_temp_degC(house_params, params, house, weather.outside_air_temp_degC);
    update_airflow(house, params);
}

void update_airflow(HouseState& house, const HumidityParams& params) {
    const double fan_index = house.fan_on ? params.fan_airflow_index : 0.0;
    house.airflow_index =
        std::clamp(params.vent_airflow_factor * non_negative(house.vent_path_index) + fan_index, 0.0, 1.0);
}

void update_house_vapor(HouseState& house, const HouseParams& house_params, const HumidityParams& params,
                        const WeatherHour& weather, double source_mm) {
    const double volume_m3_per_m2 = air_volume_m3_per_m2(house_params);
    // 이번 틱 원천 S (g/m³). 공기 부피가 0이면(검증된 params에서는 없음) 원천을 받지 않아요
    const double source_g_per_m3 =
        volume_m3_per_m2 > 0.0 ? non_negative(source_mm) * kG_per_mm / volume_m3_per_m2 : 0.0;
    const double v = non_negative(house.vapor_g_per_m3);
    const double v_out = outside_vapor_g_per_m3(params, weather);
    house.cover_inner_temp_degC = cover_inner_temp_degC(house_params, params, house, weather.outside_air_temp_degC);
    const double v_c = saturation_vapor_g_per_m3(params, house.cover_inner_temp_degC);
    const double ach = non_negative(house_params.infiltration_air_changes_per_hour +
                                    house_params.vent_air_changes_per_hour * house.vent_path_index);
    const double k = non_negative(params.condensation_rate_per_hour);

    // 음해법 (4.2): 원천·환기·결로를 한 번에 풀어요. 지수 함수 없음 (ADR-0004)
    const double numerator = v + source_g_per_m3 + ach * v_out;
    const double v1 = numerator / (1.0 + ach);
    double v_new = v1;
    double condensed_g_per_m3 = 0.0;
    if (v1 > v_c) {
        v_new = (numerator + k * v_c) / (1.0 + ach + k);
        condensed_g_per_m3 = k * (v_new - v_c);
    }
    const double vented_g_per_m3 = ach * (v_new - v_out);  // 음수 = 바깥에서 들어옴
    const double sat_air = saturation_vapor_g_per_m3(params, house.air_temp_degC);
    const double deposited_g_per_m3 = non_negative(v_new - sat_air);
    v_new -= deposited_g_per_m3;

    house.vapor_g_per_m3 = non_negative(v_new);
    house.rh_index = relative_humidity_index(params, house.vapor_g_per_m3, house.air_temp_degC);
    house.rh_mean_index += (house.rh_index - house.rh_mean_index) * std::min(1.0, non_negative(params.rh_mean_relax_rate_per_hour));

    const double mm_per_g_per_m3 = volume_m3_per_m2 / kG_per_mm;
    house.vapor_source_mm += source_g_per_m3 * mm_per_g_per_m3;
    house.water_condensed_mm += condensed_g_per_m3 * mm_per_g_per_m3;
    house.vapor_vented_mm += vented_g_per_m3 * mm_per_g_per_m3;
    house.vapor_deposited_mm += deposited_g_per_m3 * mm_per_g_per_m3;
}

void update_leaf_wetness(Crop& crop, const CropParams& crop_params, const HumidityParams& params,
                         double house_rh_index, double airflow_index, double inside_solar_W_per_m2, bool overhead) {
    if (!crop.present || !crop.alive) {
        crop.leaf_wetness_index = 0.0;
        crop.wet_run_hours = 0.0;
        crop.dry_run_hours = 0.0;
        crop.canopy_rh_index = 0.0;
        return;
    }
    const double airflow = std::clamp(airflow_index, 0.0, 1.0);
    const double solar = non_negative(inside_solar_W_per_m2);
    const double lai_fraction = params.canopy_lai_ref > 0.0
                                    ? std::min(1.0, leaf_area_index(crop, crop_params) / params.canopy_lai_ref)
                                    : 0.0;
    const double canopy_rh = std::clamp(
        std::clamp(house_rh_index, 0.0, 1.0) + params.canopy_rh_boost_index * lai_fraction * (1.0 - airflow), 0.0, 1.0);
    crop.canopy_rh_index = canopy_rh;

    double w = std::clamp(crop.leaf_wetness_index, 0.0, 1.0);
    if (overhead) {
        w = std::max(w, params.overhead_wet_index);
    } else if (canopy_rh >= params.dew_rh_index && solar <= params.dew_solar_limit_W_per_m2) {
        w += std::min(1.0, non_negative(params.wetting_rate_per_hour)) * (1.0 - w);
    } else {
        const double drying_fraction =
            // 일사 항은 습도 곡선과 무관 (15.3 항목 2) (햇빛 받은 잎은 공기보다 따뜻해 포화 공기에서도 마름)
            std::min(1.0, non_negative(params.drying_rate_per_hour) *
                              ((1.0 + params.airflow_drying_factor * airflow) *
                                   non_negative(params.drying_rh_curve(canopy_rh)) +
                               params.drying_solar_factor_per_W_per_m2 * solar));
        w -= w * non_negative(drying_fraction);
    }
    crop.leaf_wetness_index = std::clamp(w, 0.0, 1.0);

    if (crop.leaf_wetness_index >= params.wet_threshold_index) {
        crop.wet_run_hours += 1.0;
        crop.dry_run_hours = 0.0;
    } else {
        crop.dry_run_hours += 1.0;
        if (crop.dry_run_hours >= params.wetness_break_hours) {
            crop.wet_run_hours = 0.0;
        }
    }
}

}  // namespace sim::process
