#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include "sim/calendar.hpp"
#include "sim/params.hpp"
#include "sim/scenario.hpp"
#include "sim/world.hpp"

namespace test {

inline std::filesystem::path repo_path(const std::string& relative) {
    return std::filesystem::path(SIM_REPO_DIR) / relative;
}

inline sim::Params test_params() { return sim::Params{.schema_version = sim::kParamsSchemaVersion}; }

// 시간마다 값이 다른 기상 표: 조회 위치가 틀리면 바로 드러나요.
inline sim::Scenario test_scenario(int start_day_of_year = 1, int rows = 2, int cols = 3) {
    sim::Scenario s;
    s.name = "test";
    s.seed = 12345;
    s.start_day_of_year = start_day_of_year;
    s.bed_rows = rows;
    s.bed_cols = cols;
    s.weather_hourly.reserve(sim::kHoursPerYear);
    for (int day = 1; day <= sim::kDaysPerYear; ++day) {
        for (int hour = 0; hour < sim::kHoursPerDay; ++hour) {
            s.weather_hourly.push_back(sim::WeatherHour{
                .outside_air_temp_degC = day + hour / 100.0,
                .solar_radiation_W_per_m2 = hour * 10.0,
                .outside_rh_percent = 50.0,
            });
        }
    }
    return s;
}

inline sim::World make_world(sim::Scenario scenario = test_scenario()) {
    auto world = sim::World::create(test_params(), std::move(scenario));
    if (!world) {
        throw std::runtime_error(world.error().message);
    }
    return std::move(*world);
}

}  // namespace test
