// M4 단위 테스트: 하우스 기후 (docs/spec/house-climate.md "테스트 (단위)")
// 다중 피복·측창(단계 개방, 직렬 개구부)·난방·열 관성·이동 평균·CO2 수지·저온 피해·명령 검증
//
// 통합 성격의 전 작기 보존·극한·결정론 테스트는 test_m4_conservation.cpp, test_m4_determinism.cpp에 있어요.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "test_helpers.hpp"

namespace {

using Catch::Approx;
using Catch::Matchers::WithinRel;
using sim::WeatherHour;
namespace process = sim::process;

sim::HouseState house_with(const sim::Params& p, int layers) {
    return process::initial_house(p.house, layers, WeatherHour{}, 0);
}

void open_all(sim::HouseState& h, bool both_sides = true, double fraction = 1.0) {
    for (auto& layer : h.window_open_fraction) {
        layer[0] = fraction;
        layer[1] = both_sides ? fraction : 0.0;
    }
}

double teq(const sim::Params& p, const sim::HouseState& h, double out_degC, double solar) {
    return process::house_equilibrium(p.house, h, WeatherHour{out_degC, solar, 50.0}).air_temp_degC;
}

constexpr double kKwh_MJ_per_W_h = 0.0036;

}  // namespace

// ---- 피복과 열수지 ----

// 같은 날씨, 2중 대 4중: 밤에는 4중이 따뜻하고, 한낮에는 4중의 하우스 안 일사가 작음
TEST_CASE("House four layers are warmer at night and darker at noon than two", "[unit][m4][house]") {
    const sim::Params p = test::test_params();
    const auto two = house_with(p, 2);
    const auto four = house_with(p, 4);
    CHECK(teq(p, four, -5.0, 0.0) > teq(p, two, -5.0, 0.0));
    const WeatherHour noon{5.0, 600.0, 50.0};
    CHECK(process::inside_solar_W_per_m2(p.house, four, noon) < process::inside_solar_W_per_m2(p.house, two, noon));
    // 하우스 안 일사 = 일사 x 투과율^겹수
    CHECK(process::inside_solar_W_per_m2(p.house, two, noon) == Approx(600.0 * std::pow(0.9, 2)));
    CHECK(process::inside_solar_W_per_m2(p.house, four, noon) == Approx(600.0 * std::pow(0.9, 4)));
}

// 3중, 창 닫힘, 난방 없음, 1월 맑은 한낮: 약 20~25 °C
// (2026-11-18 프로토타입 점검: 일사 이득을 0.063 -> 0.18로 올려 M1 식(바깥 + 2 + 0.015 x 일사)과 같다는 이전 기대는 버림.
//  이전 테스트는 닫힌 3중이 M1 식과 0.5 °C 안에서 같다고 확인했어요.)
TEST_CASE("House three closed layers at sunny January noon reach about 20 to 25 degrees", "[unit][m4][house]") {
    const sim::Params p = test::test_params();
    const auto three = house_with(p, 3);
    // 합성 기상 1월 한낮: 바깥 약 2 °C, 일사 약 420 W/m² (data/weather/synthetic_year.csv 27일 12시)
    const WeatherHour january_noon{2.2, 422.2, 47.8};
    const double eq = process::house_equilibrium(p.house, three, january_noon).air_temp_degC;
    CHECK(eq > 20.0);
    CHECK(eq < 25.0);
    // 식 그대로: T_eq = 바깥 + (지중 열 + 일사 x 투과율 x 이득) / (U + V), U = 1 / (3 x 저항) + 침투, V = 0 (창 닫힘)
    const double tau = std::pow(p.house.layer_transmittance_factor, 3);
    const double u = 1.0 / (3.0 * p.house.layer_resistance_m2K_per_W) + p.house.infiltration_W_per_m2_per_K;
    const double expected =
        2.2 + (p.house.ground_heat_W_per_m2 + 422.2 * tau * p.house.solar_heat_gain_fraction) / u;
    CHECK(eq == Approx(expected));
    // 닫힌 3중은 M1 식보다 한낮에 훨씬 더워요 (그래서 M1~M3 시나리오에 한낮 환기 일정이 들어가요)
    CHECK(eq > 2.2 + 2.0 + 0.015 * 422.2 + 5.0);
    // 밤(일사 0)은 바깥보다 조금만 따뜻해요: 지중 열만
    CHECK(teq(p, three, -5.0, 0.0) == Approx(-5.0 + p.house.ground_heat_W_per_m2 / u));
}

// 피복 상태 식: 투과율, 저항 합, U, 환기 경로, 직렬 개구부 (house-climate "피복과 창")
TEST_CASE("House cover state follows the series orifice formulas", "[unit][m4][house]") {
    const sim::Params p = test::test_params();
    {
        const auto closed = process::cover_state(p.house, house_with(p, 3));
        CHECK(closed.transmittance_factor == Approx(std::pow(0.9, 3)));
        CHECK(closed.conductance_W_per_m2_per_K == Approx(1.0 / (3.0 * 0.13) + 0.5));
        CHECK(closed.vent_W_per_m2_per_K == 0.0);
        CHECK(closed.path_index == 0.0);
    }
    {
        // 모든 겹 양쪽 100%: 겹 단열 0 -> U = 환기 전도도 + 침투, 경로 1 -> V = 환기 전도도
        auto h = house_with(p, 3);
        open_all(h);
        const auto all = process::cover_state(p.house, h);
        CHECK(all.path_index == Approx(1.0));
        CHECK(all.conductance_W_per_m2_per_K == Approx(p.house.vent_conductance_W_per_m2_per_K + 0.5));
        CHECK(all.vent_W_per_m2_per_K == Approx(p.house.vent_conductance_W_per_m2_per_K));
    }
    {
        // 바깥 겹만 양쪽 열림: 그 겹의 단열만 사라지고 바람은 안 통해요 (다른 겹이 닫혀 path = 0)
        auto h = house_with(p, 3);
        h.window_open_fraction[0] = {1.0, 1.0};
        const auto outer = process::cover_state(p.house, h);
        CHECK(outer.path_index == 0.0);
        CHECK(outer.vent_W_per_m2_per_K == 0.0);
        CHECK(outer.conductance_W_per_m2_per_K == Approx(1.0 / (2.0 * 0.13) + 0.5));
    }
    {
        // 겹마다 개방률이 다르면: a = (1, 0.5, 0.5) -> path = sqrt(3) / sqrt(1 + 4 + 4)
        auto h = house_with(p, 3);
        h.window_open_fraction[0] = {1.0, 1.0};
        h.window_open_fraction[1] = {0.5, 0.5};
        h.window_open_fraction[2] = {0.5, 0.5};
        CHECK(process::cover_state(p.house, h).path_index == Approx(std::sqrt(3.0) / std::sqrt(9.0)));
        // 한쪽만 열어도 a는 (왼 + 오른) / 2
        auto side = house_with(p, 2);
        side.window_open_fraction[0] = {1.0, 0.0};
        side.window_open_fraction[1] = {1.0, 0.0};
        CHECK(process::cover_state(p.house, side).path_index == Approx(0.5));
    }
    {
        // 모든 겹 a = x면 path = x (단계 개방 0.3, 0.5, 0.7)
        for (const double x : {0.3, 0.5, 0.7}) {
            auto h = house_with(p, 4);
            open_all(h, true, x);
            CHECK(process::cover_state(p.house, h).path_index == Approx(x));
        }
    }
    {
        // 구현 규칙 10: window_bypass_fraction이 0.5를 넘어도 겹의 저항 항은 0에서 멈춰요 (음수 저항 없음)
        sim::Params q = p;
        q.house.window_bypass_fraction = 0.8;
        auto h = house_with(q, 3);
        open_all(h);
        const auto cover = process::cover_state(q.house, h);
        CHECK(cover.conductance_W_per_m2_per_K == Approx(q.house.vent_conductance_W_per_m2_per_K + 0.5));
        CHECK(cover.conductance_W_per_m2_per_K > 0.0);
    }
}

// 닫힘 대 모든 겹 양쪽 열림: 열린 쪽이 많이 시원함. 바깥 겹만: 차이 작음. 한쪽 대 양쪽: 양쪽이 더 시원함
TEST_CASE("House windows cool the house: all open both sides much cooler, outer only small", "[unit][m4][house]") {
    const sim::Params p = test::test_params();
    const auto closed = house_with(p, 3);
    auto all_open = closed;
    open_all(all_open);
    auto one_side = closed;
    open_all(one_side, false);
    auto outer = closed;
    outer.window_open_fraction[0] = {1.0, 1.0};
    const double t_closed = teq(p, closed, 20.0, 700.0);
    CHECK(teq(p, all_open, 20.0, 700.0) < t_closed - 5.0);
    CHECK(teq(p, all_open, 20.0, 700.0) < teq(p, one_side, 20.0, 700.0));
    CHECK(t_closed - teq(p, outer, 20.0, 700.0) < t_closed - teq(p, all_open, 20.0, 700.0));
    CHECK(t_closed - teq(p, outer, 20.0, 700.0) > 0.0);
    // 바깥 겹만 연 쪽의 차이(약 9 °C)는 모든 겹을 연 차이(약 30 °C)의 절반보다 작아요
    CHECK(t_closed - teq(p, outer, 20.0, 700.0) < 0.5 * (t_closed - teq(p, all_open, 20.0, 700.0)));
}

// 6차: 모든 겹 한쪽 30% 대 100% (직렬 개구부 경로 0.15 대 0.5), 개방률 0.4는 거부
TEST_CASE("House one side at 30 percent vents less than at 100 percent and 0.4 is rejected",
          "[unit][m4][house]") {
    const sim::Params p = test::test_params();
    auto h30 = house_with(p, 3);
    open_all(h30, false, 0.3);
    auto h100 = house_with(p, 3);
    open_all(h100, false, 1.0);
    CHECK(process::cover_state(p.house, h30).path_index == Approx(0.15));
    CHECK(process::cover_state(p.house, h100).path_index == Approx(0.5));
    CHECK(teq(p, h30, 2.0, 400.0) > teq(p, h100, 2.0, 400.0));
    for (auto* h : {&h30, &h100}) {
        h->co2_ppm = 200.0;
        process::update_house_air(*h, p.house, WeatherHour{2.0, 400.0, 50.0}, 12);
        process::update_house_co2(*h, p.house, p.crop, 0.0, 0.0);
    }
    CHECK(h30.co2_ppm <= h100.co2_ppm);
    CHECK(h30.vent_path_index == Approx(0.15));
}

// 단계 개방: 허용 단계는 0, 0.3, 0.5, 0.7, 1.0만, 그 밖은 enqueue에서 거부
TEST_CASE("House window open levels are staged and other fractions are rejected at enqueue", "[unit][m4][house][world]") {
    sim::World world = test::make_world();
    for (const double level : {0.0, 0.3, 0.5, 0.7, 1.0}) {
        INFO("level " << level);
        CHECK(world.enqueue(sim::SetWindowCommand{.layer = 0, .side = sim::WindowSide::Left, .open_fraction = level}));
    }
    for (const double bad : {0.4, 0.2, 0.9, -0.1, 1.1}) {
        INFO("rejected " << bad);
        CHECK_FALSE(world.enqueue(sim::SetWindowCommand{.layer = 0, .side = sim::WindowSide::Left, .open_fraction = bad}));
    }
    // 존재하지 않는 겹의 창은 거부 (3겹: layer 3, -1)
    CHECK_FALSE(world.enqueue(sim::SetWindowCommand{.layer = 3, .side = sim::WindowSide::Left, .open_fraction = 1.0}));
    CHECK_FALSE(world.enqueue(sim::SetWindowCommand{.layer = -1, .side = sim::WindowSide::Right, .open_fraction = 1.0}));
    CHECK(world.enqueue(sim::SetWindowCommand{.layer = 2, .side = sim::WindowSide::Right, .open_fraction = 0.7}));
    // 적용: 다음 틱부터 상태가 바뀌고 유지돼요 (마지막으로 들어온 명령이 이겨요)
    world.step(1);
    const auto snap = world.snapshot();
    CHECK(snap.house.window_open_fraction[0][0] == 1.0);  // 같은 칸에 0 .. 1.0 순서로 넣었고 마지막이 1.0
    CHECK(snap.house.window_open_fraction[2][1] == 0.7);
    world.step(5);
    CHECK(world.snapshot().house.window_open_fraction[2][1] == 0.7);
}

// ---- 난방 ----

// 난방 켜짐, 설정 8 °C, 바깥 -10 °C: 기온 = 설정. 창 전부 열림: 용량 한계(기온 < 설정, 출력 = 용량). 에너지 증가
TEST_CASE("House heater holds the setpoint and is capacity limited with windows open", "[unit][m4][house]") {
    const sim::Params p = test::test_params();
    auto h = house_with(p, 3);
    h.heater_on = true;
    h.heater_setpoint_degC = 8.0;
    const auto eq = process::house_equilibrium(p.house, h, WeatherHour{-10.0, 0.0, 50.0});
    CHECK(eq.air_temp_degC == Approx(8.0));
    CHECK(eq.heat_W_per_m2 > 0.0);
    // 난방 출력 = (설정 - 열 없는 기온) x (U + V)
    const auto cover = process::cover_state(p.house, h);
    const double free_temp = -10.0 + p.house.ground_heat_W_per_m2 / cover.conductance_W_per_m2_per_K;
    CHECK(eq.heat_W_per_m2 == Approx((8.0 - free_temp) * cover.conductance_W_per_m2_per_K));
    open_all(h);
    const auto open_eq = process::house_equilibrium(p.house, h, WeatherHour{-10.0, 0.0, 50.0});
    CHECK(open_eq.air_temp_degC < 8.0);
    CHECK(open_eq.heat_W_per_m2 == Approx(p.house.heater_capacity_W_per_m2));
    double energy_before = h.heating_energy_MJ_per_m2;
    process::update_house_air(h, p.house, WeatherHour{-10.0, 0.0, 50.0}, 3);
    CHECK(h.heating_energy_MJ_per_m2 > energy_before);
    CHECK(h.heating_energy_MJ_per_m2 - energy_before == Approx(p.house.heater_capacity_W_per_m2 * kKwh_MJ_per_W_h));
    CHECK(h.heating_power_W_per_m2 == Approx(p.house.heater_capacity_W_per_m2));
}

// 난방이 꺼져 있거나 이미 따뜻하면 출력 0, 난방 에너지는 줄지 않음
TEST_CASE("House heater is silent when off or already warm and energy never decreases", "[unit][m4][house]") {
    const sim::Params p = test::test_params();
    auto h = house_with(p, 3);
    const WeatherHour cold{-10.0, 0.0, 50.0};
    CHECK(process::house_equilibrium(p.house, h, cold).heat_W_per_m2 == 0.0);  // 꺼짐
    h.heater_on = true;
    h.heater_setpoint_degC = 0.0;
    const WeatherHour warm{20.0, 0.0, 50.0};  // 열 없는 기온이 이미 설정보다 높음
    CHECK(process::house_equilibrium(p.house, h, warm).heat_W_per_m2 == 0.0);
    double last = h.heating_energy_MJ_per_m2;
    for (int hour = 0; hour < 48; ++hour) {
        process::update_house_air(h, p.house, hour < 24 ? cold : warm, hour % 24);
        CHECK(h.heating_energy_MJ_per_m2 >= last);
        last = h.heating_energy_MJ_per_m2;
    }
    CHECK(last > 0.0);
}

// ---- 열 관성과 이동 평균, 월드 생성 시 초기 상태 ----

// 기온은 평형값을 따라가는 시간당 비율(air_relax_rate)로 움직이고, 이동 평균은 기온을 따라가요
TEST_CASE("House air follows equilibrium with thermal inertia and the mean follows air", "[unit][m4][house]") {
    const sim::Params p = test::test_params();
    auto h = house_with(p, 3);
    const WeatherHour cold{-10.0, 0.0, 50.0};
    const double start_air = h.air_temp_degC;
    const double start_mean = h.air_temp_mean_degC;
    const double eq = process::house_equilibrium(p.house, h, cold).air_temp_degC;
    REQUIRE(eq < start_air);
    process::update_house_air(h, p.house, cold, 0);
    const double expected_air = start_air + (eq - start_air) * p.house.air_relax_rate_per_hour;
    CHECK(h.air_temp_degC == Approx(expected_air));
    CHECK(h.air_temp_degC > eq);  // 한 번에 평형까지 가지 못해요
    CHECK(h.air_temp_mean_degC == Approx(start_mean + (expected_air - start_mean) * p.house.temp_mean_relax_rate_per_hour));
    // 오래 두면 평형에 수렴하고 이동 평균도 따라와요
    for (int i = 0; i < 400; ++i) {
        process::update_house_air(h, p.house, cold, i % 24);
    }
    CHECK(h.air_temp_degC == Approx(eq).margin(1e-6));
    CHECK(h.air_temp_mean_degC == Approx(eq).margin(1e-3));
}

// 월드 생성: 겹수, 창 닫힘, 난방 꺼짐, 기온 = 첫 틱 평형, 이동 평균 = 기온, CO2 = 첫 틱 시각 바깥 값
TEST_CASE("House initial state is closed unheated at equilibrium with outside CO2", "[unit][m4][house]") {
    const sim::Params p = test::test_params();
    const WeatherHour weather{5.0, 300.0, 50.0};
    const auto h = process::initial_house(p.house, 3, weather, 5);
    CHECK(h.cover_layers == 3);
    REQUIRE(h.window_open_fraction.size() == 3);
    CHECK(h.windows_open_count() == 0);
    CHECK_FALSE(h.heater_on);
    CHECK(h.air_temp_degC == Approx(process::house_equilibrium(p.house, h, weather).air_temp_degC));
    CHECK(h.air_temp_mean_degC == h.air_temp_degC);
    CHECK(h.outside_co2_ppm == p.house.outside_co2_ppm_by_hour[5]);
    CHECK(h.co2_ppm == h.outside_co2_ppm);
    CHECK(h.heating_energy_MJ_per_m2 == 0.0);
}

// ---- CO2 ----

// 바깥 CO2 표: 24개, 5시·6시가 가장 높고 15시가 가장 낮음
TEST_CASE("House outside CO2 table peaks at dawn and dips mid afternoon", "[unit][m4][house][co2]") {
    const sim::Params p = test::test_params();
    const auto& t = p.house.outside_co2_ppm_by_hour;
    REQUIRE(t.size() == 24);
    const auto max_it = std::max_element(t.begin(), t.end());
    const auto min_it = std::min_element(t.begin(), t.end());
    CHECK((max_it - t.begin() == 5 || max_it - t.begin() == 6));
    CHECK(min_it - t.begin() == 15);
}

// 맑은 낮 창 닫힘 대 양쪽 열림: 닫힌 쪽 CO2가 낮고 총생산 배율이 작음. 밤 닫힘: 바깥보다 높아짐.
// 같은 조건에서 이분해성 C(토양 호흡)가 많으면 감소폭이 작음. 극단 흡수: 하한, 유한
TEST_CASE("House CO2 drops when closed by day, rises above outside at night, never below floor",
          "[unit][m4][house][co2]") {
    const sim::Params p = test::test_params();
    auto closed = house_with(p, 3);
    auto vented = closed;
    open_all(vented);
    for (auto* h : {&closed, &vented}) {
        process::update_house_air(*h, p.house, WeatherHour{10.0, 500.0, 50.0}, 12);
        process::update_house_co2(*h, p.house, p.crop, 1.0, 0.0);  // 순흡수 1 g 건물/m²/h
    }
    CHECK(closed.co2_ppm < vented.co2_ppm);
    CHECK(p.crop.co2_growth_curve(closed.co2_ppm) < p.crop.co2_growth_curve(vented.co2_ppm));
    // 밤: 호흡(음수 순흡수) + 토양 호흡 -> 바깥보다 높아짐
    auto night = house_with(p, 3);
    process::update_house_air(night, p.house, WeatherHour{5.0, 0.0, 50.0}, 2);
    process::update_house_co2(night, p.house, p.crop, -0.1, 0.05);
    CHECK(night.co2_ppm > night.outside_co2_ppm);
    // 이분해성 C가 많으면(토양 호흡 큼) 감소폭이 작음
    auto rich = house_with(p, 3);
    auto poor = rich;
    process::update_house_co2(rich, p.house, p.crop, 1.0, 0.3);
    process::update_house_co2(poor, p.house, p.crop, 1.0, 0.0);
    CHECK(rich.co2_ppm > poor.co2_ppm);
    // 극단 흡수: 하한, 유한
    auto extreme = house_with(p, 3);
    process::update_house_co2(extreme, p.house, p.crop, 1.0e6, 0.0);
    CHECK(std::isfinite(extreme.co2_ppm));
    CHECK(extreme.co2_ppm >= p.house.co2_floor_ppm);
}

// CO2 수지 식 그대로 (house-climate 3번): 작물 흡수와 토양 호흡을 질량으로 더하고, 환기 횟수만큼 바깥 값으로 섞음
TEST_CASE("House CO2 balance follows the mass formula and the ventilation mixing", "[unit][m4][house][co2]") {
    const sim::Params p = test::test_params();
    auto h = house_with(p, 3);  // 창 닫힘: 환기 경로 0 -> 침투 환기만
    process::update_house_air(h, p.house, WeatherHour{10.0, 0.0, 50.0}, 12);
    const double outside = p.house.outside_co2_ppm_by_hour[12];
    REQUIRE(h.outside_co2_ppm == outside);
    h.co2_ppm = 400.0;
    const double crop_net = 1.0;  // g 건물/m²/h
    const double soil_C = 0.1;    // g C/m²/h
    const double mass_per_ppm = p.house.air_volume_m3_per_m2 * p.house.co2_mg_per_m3_per_ppm / 1000.0;
    const double crop_uptake_g = crop_net * p.crop.co2_g_per_g_dry_matter;
    const double soil_release_g = soil_C * 44.0 / 12.0;
    double expected = 400.0 + (soil_release_g - crop_uptake_g) / mass_per_ppm;
    expected += (outside - expected) * p.house.infiltration_air_changes_per_hour;
    process::update_house_co2(h, p.house, p.crop, crop_net, soil_C);
    CHECK(h.co2_ppm == Approx(expected).epsilon(1e-9));
    // 창을 열면 환기 횟수가 늘어 바깥 값에 더 가까워요
    auto open = house_with(p, 3);
    open_all(open);
    process::update_house_air(open, p.house, WeatherHour{10.0, 0.0, 50.0}, 12);
    open.co2_ppm = 400.0;
    process::update_house_co2(open, p.house, p.crop, crop_net, soil_C);
    CHECK(std::abs(open.co2_ppm - outside) < std::abs(h.co2_ppm - outside));
}

// 총생산 = M2 잠재 생장 x co2_growth_curve(하우스 CO2) x ...: CO2가 낮으면 같은 조건의 총생산이 작음
TEST_CASE("Crop gross production scales with the CO2 growth curve", "[unit][m4][house][co2]") {
    const sim::Params p = test::test_params();
    const auto gross_at = [&](double co2_ppm) {
        sim::SoilCell cell = test::test_cell();
        sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
        sim::Ledger ledger;
        sim::Pcg32 rng = test::biology_rng(1);
        auto env = test::crop_env(500.0, 20.0);
        env.house_co2_ppm = co2_ppm;
        (void)process::update_crop(crop, cell, p, process::NutrientAvailability{}, env, rng, ledger);
        return crop.last_gross_g_per_m2;
    };
    const double reference = gross_at(400.0);
    REQUIRE(reference > 0.0);
    CHECK(gross_at(200.0) == Approx(reference * p.crop.co2_growth_curve(200.0)));
    CHECK(gross_at(100.0) == Approx(reference * p.crop.co2_growth_curve(100.0)));
    CHECK(gross_at(800.0) == Approx(reference * p.crop.co2_growth_curve(800.0)));
    CHECK(gross_at(200.0) < reference);
    CHECK(gross_at(800.0) > reference);
    CHECK(gross_at(p.house.co2_floor_ppm) == 0.0);  // 하한(50 ppm)에서는 광합성 0
}

// ---- 명령: 난방, 피복 ----

TEST_CASE("House heater command is validated against the setpoint range even when switched off",
          "[unit][m4][house][world]") {
    sim::World world = test::make_world();
    CHECK_FALSE(world.enqueue(sim::SetHeaterCommand{.on = true, .setpoint_degC = 99.0}));
    CHECK_FALSE(world.enqueue(sim::SetHeaterCommand{.on = true, .setpoint_degC = -1.0}));
    CHECK_FALSE(world.enqueue(sim::SetHeaterCommand{.on = true, .setpoint_degC = 30.5}));
    // 구현 규칙 9: on = false여도 범위 검사
    CHECK_FALSE(world.enqueue(sim::SetHeaterCommand{.on = false, .setpoint_degC = 99.0}));
    CHECK(world.enqueue(sim::SetHeaterCommand{.on = true, .setpoint_degC = 0.0}));
    CHECK(world.enqueue(sim::SetHeaterCommand{.on = true, .setpoint_degC = 30.0}));
    CHECK(world.enqueue(sim::SetHeaterCommand{.on = false, .setpoint_degC = 8.0}));
    world.step(1);
    const auto snap = world.snapshot();
    CHECK_FALSE(snap.house.heater_on);  // 마지막 명령
    CHECK(snap.house.heater_setpoint_degC == 8.0);
}

// SetCoverLayers: 휴작기에는 받고(3 -> 4: 겹수 4, 새 겹 창 닫힘, 밤 기온 상승·투과율 감소), 작기 중에는 거부
TEST_CASE("House cover layers can change only in the off season", "[unit][m4][house][world]") {
    // 6월(day 160) 시작: 휴작기
    sim::World june = test::make_world(test::test_scenario(160));
    CHECK_FALSE(june.enqueue(sim::SetCoverLayersCommand{.layers = 1}));  // 범위 밖 (2~4)
    CHECK_FALSE(june.enqueue(sim::SetCoverLayersCommand{.layers = 5}));
    const double night_before = teq(test::test_params(), june.snapshot().house, 0.0, 0.0);
    const double solar_before = process::inside_solar_W_per_m2(test::test_params().house, june.snapshot().house,
                                                               WeatherHour{0.0, 600.0, 50.0});
    REQUIRE(june.enqueue(sim::SetCoverLayersCommand{.layers = 4}));
    june.step(1);
    const auto snap = june.snapshot();
    CHECK(snap.house.cover_layers == 4);
    CHECK(snap.house.window_open_fraction.size() == 4);
    CHECK(snap.house.windows_open_count() == 0);  // 새 겹의 창은 닫힘
    CHECK(teq(test::test_params(), snap.house, 0.0, 0.0) > night_before);
    CHECK(process::inside_solar_W_per_m2(test::test_params().house, snap.house, WeatherHour{0.0, 600.0, 50.0}) <
          solar_before);
    CHECK(june.enqueue(sim::SetWindowCommand{.layer = 3, .side = sim::WindowSide::Right, .open_fraction = 1.0}));
    // 11월(day 320): 작기 중 -> 거부
    sim::World november = test::make_world(test::test_scenario(320));
    CHECK_FALSE(november.enqueue(sim::SetCoverLayersCommand{.layers = 4}));
}

// 휴작기 경계: day 91과 243은 포함, 90과 244는 밖. 명령이 적용될 틱의 날짜로 판정
TEST_CASE("House cover change window includes both end days and uses the application day",
          "[unit][m4][house][world]") {
    const sim::Params p = test::test_params();
    CHECK_FALSE(process::in_cover_change_window(p.house, 90));
    CHECK(process::in_cover_change_window(p.house, 91));
    CHECK(process::in_cover_change_window(p.house, 243));
    CHECK_FALSE(process::in_cover_change_window(p.house, 244));
    sim::World edge = test::make_world(test::test_scenario(242));
    CHECK(edge.enqueue(sim::SetCoverLayersCommand{.layers = 2}));
    edge.step(24);  // 다음 날(day 243): 아직 안
    CHECK(edge.enqueue(sim::SetCoverLayersCommand{.layers = 2}));
    edge.step(24);  // day 244
    CHECK_FALSE(edge.enqueue(sim::SetCoverLayersCommand{.layers = 3}));
}

// 구현 규칙 1: 겹을 줄이면 가장 안쪽 겹부터 없어지고 바깥 겹은 남아요. 늘리면 새 겹이 안쪽에, 창은 닫힘
TEST_CASE("House reducing cover layers drops the innermost layer first", "[unit][m4][house][world]") {
    sim::World world = test::make_world(test::test_scenario(160));
    test::must_enqueue(world, sim::SetWindowCommand{.layer = 0, .side = sim::WindowSide::Left, .open_fraction = 0.3});
    test::must_enqueue(world, sim::SetWindowCommand{.layer = 1, .side = sim::WindowSide::Right, .open_fraction = 0.5});
    test::must_enqueue(world, sim::SetWindowCommand{.layer = 2, .side = sim::WindowSide::Left, .open_fraction = 1.0});
    world.step(1);
    REQUIRE(world.snapshot().house.cover_layers == 3);
    test::must_enqueue(world, sim::SetCoverLayersCommand{.layers = 2});
    world.step(1);
    auto snap = world.snapshot();
    REQUIRE(snap.house.window_open_fraction.size() == 2);
    CHECK(snap.house.window_open_fraction[0][0] == 0.3);  // 바깥 겹은 남아요
    CHECK(snap.house.window_open_fraction[1][1] == 0.5);
    // 다시 늘리면 새 겹은 안쪽에 닫힘으로
    test::must_enqueue(world, sim::SetCoverLayersCommand{.layers = 4});
    world.step(1);
    snap = world.snapshot();
    REQUIRE(snap.house.window_open_fraction.size() == 4);
    CHECK(snap.house.window_open_fraction[0][0] == 0.3);
    CHECK(snap.house.window_open_fraction[1][1] == 0.5);
    CHECK(snap.house.window_open_fraction[2] == std::array<double, 2>{0.0, 0.0});
    CHECK(snap.house.window_open_fraction[3] == std::array<double, 2>{0.0, 0.0});
}

// 구현 규칙 2: 창 명령은 명령 시점의 겹 수로 검증하고, 같은 틱에 앞선 SetCoverLayers가 없앤 겹의 창 명령은 적용 때 무시
TEST_CASE("House window command for a layer removed in the same tick is ignored", "[unit][m4][house][world]") {
    sim::World world = test::make_world(test::test_scenario(160));
    // 명령 시점에는 3겹이라 layer 2는 유효해요
    test::must_enqueue(world, sim::SetCoverLayersCommand{.layers = 2});
    REQUIRE(world.enqueue(sim::SetWindowCommand{.layer = 2, .side = sim::WindowSide::Left, .open_fraction = 1.0}));
    world.step(1);
    const auto snap = world.snapshot();
    CHECK(snap.house.cover_layers == 2);
    CHECK(snap.house.window_open_fraction.size() == 2);
    CHECK(snap.house.windows_open_count() == 0);
    // 반대로, 같은 틱에 늘릴 겹의 창은 명령 시점에 아직 없어서 거부
    CHECK_FALSE(world.enqueue(sim::SetWindowCommand{.layer = 3, .side = sim::WindowSide::Left, .open_fraction = 1.0}));
    test::must_enqueue(world, sim::SetCoverLayersCommand{.layers = 4});
    CHECK_FALSE(world.enqueue(sim::SetWindowCommand{.layer = 3, .side = sim::WindowSide::Left, .open_fraction = 1.0}));
}

// ---- 저온 피해 (셀 단계 h0) ----

namespace {

struct Totals {
    double N, P, K, Ca, Mg;
};

Totals soil_plus_plant(const sim::SoilCell& cell, const sim::Crop& crop) {
    return {.N = cell.nh4_N_g_per_m2 + cell.no2_N_g_per_m2 + cell.no3_N_g_per_m2 + cell.labile_N_g_per_m2 +
                 crop.plant_N_g_per_m2,
            .P = cell.p_g_per_m2 + crop.plant_P_g_per_m2,
            .K = cell.k_g_per_m2 + crop.plant_K_g_per_m2,
            .Ca = cell.ca_g_per_m2 + crop.plant_Ca_g_per_m2,
            .Mg = cell.mg_g_per_m2 + crop.plant_Mg_g_per_m2};
}

// 어린 과실이 많은 작물 (착과됨, 진행도 0.5라 낙과 구간 밖) + 아직 안 핀 화방 하나
sim::Crop crop_with_fruits_and_truss(const sim::Params& p, int fruits) {
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    for (int k = 0; k < fruits; ++k) {
        sim::Fruit f;
        f.rank = static_cast<std::uint32_t>(k % 5);
        f.set = true;
        f.potential_fresh_g = 15.0;
        f.development_index = 0.5;
        f.dry_g = 0.5;
        crop.fruits.push_back(f);
    }
    sim::Truss truss;
    truss.flowers_per_m2 = 20.0;
    crop.trusses.push_back(truss);
    return crop;
}

}  // namespace

// 하우스 기온 -3 °C: 잎·과실 손실, 손실분이 토양으로 (원소·C 보존). 기온이 안 낮으면 손실 없음
TEST_CASE("House cold at minus 3 degrees kills leaf and fruit and returns them to the soil", "[unit][m4][house][cold][conservation]") {
    const sim::Params p = test::quiet_params();  // 호흡·낙엽을 꺼서 저온 손실만 봐요
    const double leaf_loss = p.crop.cold_leaf_damage_curve(-3.0);
    const double fruit_loss = p.crop.cold_fruit_loss_curve(-3.0);
    REQUIRE(leaf_loss == Approx(0.015));
    REQUIRE(fruit_loss == Approx(0.125));

    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = crop_with_fruits_and_truss(p, 3000);
    const sim::Crop before = crop;
    const sim::SoilCell cell_before = cell;
    const Totals initial = soil_plus_plant(cell, crop);
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(11);
    (void)process::update_crop(crop, cell, p, process::NutrientAvailability{}, test::crop_env(0.0, -3.0), rng, ledger);

    // 잎: 식 그대로
    CHECK(crop.leaf_g_per_m2 == Approx(before.leaf_g_per_m2 * (1.0 - leaf_loss)));
    // 안 핀 화방의 꽃: 같은 비율
    CHECK(crop.trusses[0].flowers_per_m2 == Approx(20.0 * (1.0 - fruit_loss)));
    // 과실: 개체마다 난수 판정, 기대 375개 (표준편차 약 18개)
    const double lost_fruits = static_cast<double>(before.fruits.size() - crop.fruits.size());
    CHECK(std::abs(lost_fruits - 3000.0 * fruit_loss) < 5.0 * std::sqrt(3000.0 * fruit_loss * (1.0 - fruit_loss)));
    CHECK(lost_fruits > 0.0);
    // 손실분은 토양으로: 건물 C가 낙엽 C 장부와 이분해성 C에 같은 양으로
    const double lost_dry = (before.leaf_g_per_m2 - crop.leaf_g_per_m2) + (before.fruit_g_per_m2() - crop.fruit_g_per_m2());
    CHECK(ledger.C_leaf_litter_g == Approx(lost_dry * p.crop.dry_matter_C_fraction).epsilon(1e-9));
    CHECK(cell.labile_C_g_per_m2 - cell_before.labile_C_g_per_m2 == Approx(ledger.C_leaf_litter_g).epsilon(1e-9));
    // 원소 보존
    const Totals now = soil_plus_plant(cell, crop);
    CHECK(now.N == Approx(initial.N).epsilon(1e-12));
    CHECK(now.P == Approx(initial.P).epsilon(1e-12));
    CHECK(now.K == Approx(initial.K).epsilon(1e-12));
    CHECK(now.Ca == Approx(initial.Ca).epsilon(1e-12));
    CHECK(now.Mg == Approx(initial.Mg).epsilon(1e-12));
    CHECK(crop.plant_N_g_per_m2 < before.plant_N_g_per_m2);  // 체내 양분이 줄고 토양에 돌아감
    CHECK(cell.labile_N_g_per_m2 > cell_before.labile_N_g_per_m2);
    CHECK(ledger.N_harvested_g == 0.0);  // 수확이 아니라 낙엽 규칙

    // 하우스 기온 -0.5 °C: 잎만 아주 조금, 과실은 손실 0. 5 °C: 아무 손실 없음
    for (const double air : {-0.5, 5.0}) {
        INFO("air " << air);
        sim::SoilCell mild_cell = test::test_cell();
        sim::Crop mild = crop_with_fruits_and_truss(p, 300);
        sim::Ledger mild_ledger;
        sim::Pcg32 mild_rng = test::biology_rng(11);
        (void)process::update_crop(mild, mild_cell, p, process::NutrientAvailability{}, test::crop_env(0.0, air),
                                   mild_rng, mild_ledger);
        CHECK(mild.fruits.size() == 300);
        CHECK(mild.trusses[0].flowers_per_m2 == 20.0);
        const double expected_leaf_loss = p.crop.cold_leaf_damage_curve(air);
        CHECK(mild.leaf_g_per_m2 == Approx(process::plant_crop(test::test_crop_init(), p.crop).leaf_g_per_m2 *
                                           (1.0 - expected_leaf_loss)));
    }
}
