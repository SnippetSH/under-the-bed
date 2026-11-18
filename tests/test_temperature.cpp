// docs/spec/soil-temperature.md "테스트 (단위)"
#include <algorithm>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sim/processes.hpp"
#include "test_helpers.hpp"

using namespace sim;

// 토양 온도는 낮은 쪽에서 하우스 기온으로 단조 수렴한다
TEST_CASE("Soil temperature rises monotonically toward house air temperature", "[unit][temperature]") {
    const Params p = test::test_params();
    SoilCell cell = test::test_cell();
    cell.soil_temp_degC = 5.0;
    const double house = 25.0;
    double prev = cell.soil_temp_degC;
    for (int i = 0; i < 500; ++i) {
        process::relax_soil_temperature(cell, p.soil_temperature, house);
        REQUIRE(cell.soil_temp_degC >= prev);
        REQUIRE(cell.soil_temp_degC <= house);
        prev = cell.soil_temp_degC;
    }
    REQUIRE(cell.soil_temp_degC > house - 0.01);
}

// 토양 온도는 높은 쪽에서 하우스 기온으로 단조 수렴한다
TEST_CASE("Soil temperature falls monotonically toward house air temperature", "[unit][temperature]") {
    const Params p = test::test_params();
    SoilCell cell = test::test_cell();
    cell.soil_temp_degC = 40.0;
    const double house = 10.0;
    double prev = cell.soil_temp_degC;
    for (int i = 0; i < 500; ++i) {
        process::relax_soil_temperature(cell, p.soil_temperature, house);
        REQUIRE(cell.soil_temp_degC <= prev);
        REQUIRE(cell.soil_temp_degC >= house);
        prev = cell.soil_temp_degC;
    }
    REQUIRE(cell.soil_temp_degC < house + 0.01);
}

// M4 (house-climate): 낮 일사가 있으면 평형 하우스 기온이 밤(같은 외기)보다 높고, 밤은 외기 + 지중 열 / U
// (M1 식의 base_offset 테스트를 다중 피복 열수지로 옮김. 기대값의 정의가 바뀐 테스트예요.)
TEST_CASE("Daytime solar raises house air above the night equilibrium", "[unit][temperature]") {
    const Params p = test::test_params();
    const sim::HouseState house = process::initial_house(p.house, 3, WeatherHour{}, 0);
    const WeatherHour day{.outside_air_temp_degC = 15.0, .solar_radiation_W_per_m2 = 600.0, .outside_rh_percent = 50.0};
    const WeatherHour night{.outside_air_temp_degC = 15.0, .solar_radiation_W_per_m2 = 0.0, .outside_rh_percent = 50.0};
    const double night_degC = process::house_equilibrium(p.house, house, night).air_temp_degC;
    REQUIRE(night_degC > night.outside_air_temp_degC);
    REQUIRE(process::house_equilibrium(p.house, house, day).air_temp_degC > night_degC);
}

// M4: 하우스 안 일사 = 일사 × 투과율^겹수
TEST_CASE("Inside solar scales with layer transmittance", "[unit][temperature]") {
    Params p = test::test_params();
    const sim::HouseState house = process::initial_house(p.house, 3, WeatherHour{}, 0);
    const WeatherHour w{.outside_air_temp_degC = 10.0, .solar_radiation_W_per_m2 = 500.0, .outside_rh_percent = 50.0};
    const double base = process::inside_solar_W_per_m2(p.house, house, w);
    REQUIRE(base > 0.0);
    REQUIRE(base < w.solar_radiation_W_per_m2);
    p.house.layer_transmittance_factor *= 0.5;
    REQUIRE(process::inside_solar_W_per_m2(p.house, house, w) == Catch::Approx(base * 0.125));
    p.house.layer_transmittance_factor = 0.0;
    REQUIRE(process::inside_solar_W_per_m2(p.house, house, w) == 0.0);
}

// relax_rate가 1 이상이면 하우스 기온으로 바로 맞춰진다
TEST_CASE("Relax rate of one or more snaps soil to house air temperature", "[unit][temperature]") {
    Params p = test::test_params();
    for (const double rate : {1.0, 3.5}) {
        p.soil_temperature.relax_rate_per_hour = rate;
        SoilCell cell = test::test_cell();
        cell.soil_temp_degC = 12.0;
        process::relax_soil_temperature(cell, p.soil_temperature, 30.0);
        REQUIRE(cell.soil_temp_degC == 30.0);
    }
}

// 극단 온도에서도 NaN과 무한대가 없다
TEST_CASE("Extreme temperatures stay finite", "[unit][temperature][extreme]") {
    const Params p = test::test_params();
    for (const double house : {-60.0, 0.0, 80.0, 1000.0}) {
        for (const double start : {-60.0, 20.0, 90.0}) {
            SoilCell cell = test::test_cell();
            cell.soil_temp_degC = start;
            for (int i = 0; i < 100; ++i) {
                process::relax_soil_temperature(cell, p.soil_temperature, house);
                REQUIRE(std::isfinite(cell.soil_temp_degC));
            }
        }
    }
    const WeatherHour w{.outside_air_temp_degC = -50.0, .solar_radiation_W_per_m2 = 1500.0, .outside_rh_percent = 0.0};
    const sim::HouseState house = process::initial_house(p.house, 3, w, 12);
    REQUIRE(std::isfinite(process::house_equilibrium(p.house, house, w).air_temp_degC));
    REQUIRE(std::isfinite(process::inside_solar_W_per_m2(p.house, house, w)));
}

// 월드 48틱 동안 토양 온도는 하우스 기온 범위 안에 머문다
TEST_CASE("World soil temperature stays within the house air range over 48 ticks", "[unit][temperature][world]") {
    sim::World world = test::make_world();
    double lo = 20.0;  // 초기 토양 온도
    double hi = 20.0;
    for (int i = 0; i < 48; ++i) {
        world.step(1);
        const auto snap = world.snapshot();
        lo = std::min(lo, snap.house_air_temp_degC);
        hi = std::max(hi, snap.house_air_temp_degC);
    }
    const auto snap = world.snapshot();
    for (const SoilCell& cell : snap.soil) {
        REQUIRE(cell.soil_temp_degC >= lo);
        REQUIRE(cell.soil_temp_degC <= hi);
    }
}
