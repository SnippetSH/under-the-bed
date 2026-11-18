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

// M1 명세(docs/spec/*.md) 표의 초기값과 같은 파라미터. data/params.toml과 별개인 테스트 고정값이에요.
// 단위 테스트는 이 값에 의존하지 않고 방향만 검증하는 것을 원칙으로 해요.
inline sim::Params test_params() {
    sim::Params p;
    p.schema_version = sim::kParamsSchemaVersion;

    p.house = {.base_offset_degC = 2.0, .solar_gain_degC_per_W_per_m2 = 0.015, .solar_transmittance_factor = 0.7};

    p.soil_water = {
        .evap_mm_per_hour_per_W_per_m2 = 0.0004,
        .evap_base_mm_per_hour = 0.005,
        .evap_moisture_curve = {{{{0.0, 0.0}}, {{0.3, 0.15}}, {{0.7, 0.8}}, {{1.0, 1.0}}}},
        .drainage_rate_per_hour = 0.3,
    };

    p.soil_temperature = {.relax_rate_per_hour = 0.04, .heat_degC_per_g_C = 0.4};

    p.organic_matter = {
        .labile_decay_rate_per_hour = 0.004,
        .stable_decay_rate_per_hour = 0.00002,
        .humification_fraction = 0.3,
        .humus_C_to_N_ratio = 10.0,
        .temp_curve = {{{{0, 0.05}}, {{10, 0.25}}, {{20, 0.55}}, {{30, 0.85}}, {{37, 1.0}}, {{45, 1.0}},
                        {{55, 0.5}}, {{65, 0.1}}}},
        .moisture_curve = {{{{0, 0}}, {{0.2, 0.1}}, {{0.6, 0.8}}, {{0.9, 1.0}}, {{2.0, 1.0}}}},
    };

    p.nitrification = {
        .step1_rate_per_hour = 0.01,
        .step2_rate_per_hour = 0.08,
        .temp_curve = {{{{0, 0}}, {{5, 0.1}}, {{15, 0.5}}, {{25, 0.9}}, {{32, 1.0}}, {{40, 0.6}}, {{50, 0.1}}}},
        .moisture_curve = {{{{0, 0}}, {{0.3, 0.2}}, {{0.6, 0.8}}, {{0.9, 1.0}}, {{2.0, 1.0}}}},
        .step1_ph_curve = {{{{4.5, 0.1}}, {{5.5, 0.5}}, {{6.5, 1.0}}, {{8.0, 1.0}}, {{9.0, 0.6}}}},
        .step2_ph_curve = {{{{4.5, 0.1}}, {{5.5, 0.5}}, {{6.5, 1.0}}, {{7.2, 0.9}}, {{8.0, 0.35}}, {{9.0, 0.1}}}},
        .ammonia_fraction_ph_curve = {{{{6, 0.0006}}, {{7, 0.0056}}, {{7.5, 0.0175}}, {{8, 0.053}}, {{8.5, 0.15}},
                                       {{9, 0.36}}, {{10, 0.85}}}},
        .free_ammonia_inhibition_curve = {{{{0, 1}}, {{0.05, 0.9}}, {{0.3, 0.4}}, {{1.0, 0.1}}, {{3.0, 0.05}}}},
        .nitrite_toxicity_threshold_g_N_per_m2 = 2.0,
        .nitrite_alarm_reset_fraction = 0.8,
    };

    p.leaching = {.nh4_solution_fraction = 0.1};

    p.composts = {
        {.id = "immature_manure", .name = "미숙 우분 퇴비", .C_g_per_kg = 180, .N_g_per_kg = 12,
         .labile_C_fraction = 0.6, .labile_N_fraction = 0.7},
        {.id = "mature_manure", .name = "숙성 우분 퇴비", .C_g_per_kg = 150, .N_g_per_kg = 12,
         .labile_C_fraction = 0.1, .labile_N_fraction = 0.1},
        {.id = "bark_compost", .name = "수피·톱밥 퇴비", .C_g_per_kg = 250, .N_g_per_kg = 5,
         .labile_C_fraction = 0.4, .labile_N_fraction = 0.3},
    };
    p.fertilizers = {
        {.id = "ammonium_sulfate", .name = "황산암모늄", .nh4_N_g_per_kg = 210, .no3_N_g_per_kg = 0},
        {.id = "urea", .name = "요소", .nh4_N_g_per_kg = 460, .no3_N_g_per_kg = 0},
        {.id = "calcium_nitrate", .name = "질산칼슘", .nh4_N_g_per_kg = 11, .no3_N_g_per_kg = 144},
    };
    return p;
}

// m1-overview.md "시나리오 TOML" 예시와 같은 균일 토양
inline sim::SoilInit test_soil() {
    return sim::SoilInit{
        .field_capacity_mm = 60.0,
        .water_mm = 45.0,
        .soil_temp_degC = 20.0,
        .ph = 6.5,
        .nh4_N_g_per_m2 = 2.0,
        .no2_N_g_per_m2 = 0.0,
        .no3_N_g_per_m2 = 10.0,
        .labile_C_g_per_m2 = 50.0,
        .labile_N_g_per_m2 = 5.0,
        .stable_C_g_per_m2 = 3600.0,
        .stable_N_g_per_m2 = 360.0,
    };
}

// test_soil()로 만든 셀 하나
inline sim::SoilCell test_cell() {
    const sim::SoilInit s = test_soil();
    return sim::SoilCell{
        .field_capacity_mm = s.field_capacity_mm,
        .water_mm = s.water_mm,
        .drained_mm_this_tick = 0.0,
        .soil_temp_degC = s.soil_temp_degC,
        .labile_C_g_per_m2 = s.labile_C_g_per_m2,
        .labile_N_g_per_m2 = s.labile_N_g_per_m2,
        .stable_C_g_per_m2 = s.stable_C_g_per_m2,
        .stable_N_g_per_m2 = s.stable_N_g_per_m2,
        .nh4_N_g_per_m2 = s.nh4_N_g_per_m2,
        .no2_N_g_per_m2 = s.no2_N_g_per_m2,
        .no3_N_g_per_m2 = s.no3_N_g_per_m2,
        .ph = s.ph,
        .nitrite_alarm = false,
    };
}

// 시간마다 값이 다른 기상 표: 조회 위치가 틀리면 바로 드러나요.
inline sim::Scenario test_scenario(int start_day_of_year = 1, int rows = 2, int cols = 3) {
    sim::Scenario s;
    s.name = "test";
    s.seed = 12345;
    s.start_day_of_year = start_day_of_year;
    s.bed_rows = rows;
    s.bed_cols = cols;
    s.soil = test_soil();
    s.weather_hourly.reserve(sim::kHoursPerYear);
    for (int day = 1; day <= sim::kDaysPerYear; ++day) {
        for (int hour = 0; hour < sim::kHoursPerDay; ++hour) {
            s.weather_hourly.push_back(sim::WeatherHour{
                .outside_air_temp_degC = day / 20.0 + hour / 100.0,
                .solar_radiation_W_per_m2 = (hour >= 6 && hour < 18) ? hour * 10.0 : 0.0,
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

// enqueue 결과를 바로 꺼내는 테스트용 도우미 (실패하면 예외)
inline std::uint64_t must_enqueue(sim::World& world, sim::Command command) {
    auto seq = world.enqueue(std::move(command));
    if (!seq) {
        throw std::runtime_error(seq.error().message);
    }
    return *seq;
}

}  // namespace test
