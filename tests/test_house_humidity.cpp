// M5b 단위 테스트: 하우스 습도 (docs/spec/house-humidity.md 8장 "단위")
// 수증기 수지(음해법)·피복 면 온도·공기 흐름·잎 젖음·연속 젖음 시간·보존·세이브 왕복.
// (증산의 습도 되먹임·밤 기본 증산은 되돌림, ADR-0014: 증산은 M4 식 그대로라 습도와 무관한지만 확인해요.)
// 명령 행(두상 관수 표시, 점적 설치 뒤 표시 없음, InstallDrip 거부), 증산 되돌림 확인, 점적·팬 세이브 왕복은
// 파일 끝 "명령·되돌림 확인" 부분에 있어요 (m5-overview 15.4 4단계 테스트 보강).
// 팬은 fan_on을 직접 세워서 확인해요. 기대값은 방향만 써요 (6장 목표값은 차수 보고에서 확인).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "test_helpers.hpp"

namespace {

using Catch::Approx;
using sim::WeatherHour;
namespace process = sim::process;

constexpr double kG_per_mm = 1000.0;
constexpr double kConservationRelTol = 1e-9;

sim::Params humidity_params() {
    return test::test_params();
}

// 창 닫힌 겹수 n 하우스, 기온을 직접 정해요 (열수지는 이 테스트의 대상이 아니에요)
sim::HouseState closed_house(const sim::Params& p, int layers, double air_temp_degC) {
    sim::HouseState h = process::initial_house(p.house, layers, WeatherHour{}, 0);
    h.air_temp_degC = air_temp_degC;
    h.vent_path_index = process::cover_state(p.house, h).path_index;
    return h;
}

void open_all(const sim::Params& p, sim::HouseState& h, double fraction) {
    for (auto& layer : h.window_open_fraction) {
        layer = {fraction, fraction};
    }
    h.vent_path_index = process::cover_state(p.house, h).path_index;
}

double air_mm(const sim::Params& p, const sim::HouseState& h) {
    return h.vapor_g_per_m3 * p.house.air_volume_m3_per_m2 / kG_per_mm;
}

// 보존식 오차 (상대): 공기 중 수증기 = 시작값 + 원천 − 결로 − 환기 − 안개·이슬
double vapor_balance_error(const sim::Params& p, const sim::HouseState& h, double start_mm) {
    const double expected =
        start_mm + h.vapor_source_mm - h.water_condensed_mm - h.vapor_vented_mm - h.vapor_deposited_mm;
    const double scale = std::max({1e-12, std::abs(start_mm), h.vapor_source_mm, h.water_condensed_mm,
                                   std::abs(h.vapor_vented_mm), h.vapor_deposited_mm, air_mm(p, h)});
    return std::abs(air_mm(p, h) - expected) / scale;
}

// 같은 조건으로 n틱 수증기 수지를 돌려요 (원천 mm/틱 = 셀 평균)
void run_vapor(const sim::Params& p, sim::HouseState& h, const WeatherHour& w, double source_mm, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        process::update_house_vapor(h, p.house, p.humidity, w, source_mm);
    }
}

// 살아 있는 작물, LAI = 잎 × 비엽면적
sim::Crop crop_with_lai(const sim::Params& p, double lai) {
    sim::Crop c;
    c.present = true;
    c.alive = true;
    c.leaf_g_per_m2 = lai / p.crop.specific_leaf_area_m2_per_g;
    return c;
}

}  // namespace

// ---- 포화 표, 피복 면 온도 ----

// 포화 표: 단조 증가, 20 °C ≈ 17.3 g/m³, 0 °C ≈ 4.85
TEST_CASE("Saturation vapor table is increasing and matches spec points", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    CHECK(process::saturation_vapor_g_per_m3(p.humidity, 20.0) == Approx(17.3));
    CHECK(process::saturation_vapor_g_per_m3(p.humidity, 0.0) == Approx(4.85));
    double previous = -1.0;
    for (double t = -15.0; t <= 75.0; t += 0.5) {
        const double sat = process::saturation_vapor_g_per_m3(p.humidity, t);
        CHECK(sat >= previous);
        CHECK(sat > 0.0);
        previous = sat;
    }
}

// 피복 면 온도: 3겹 닫힘 frac ≈ 0.77, 창이 열릴수록 작아지고, 모든 겹 양쪽 100%면 바깥 기온
TEST_CASE("Cover inner temperature fraction is about 0.77 for three closed layers", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    auto h = closed_house(p, 3, 10.0);
    const double closed_degC = process::cover_inner_temp_degC(p.house, p.humidity, h, 0.0);
    CHECK(closed_degC / 10.0 == Approx(0.77).margin(0.005));
    // 2겹 0.65, 4겹 0.83 (명세 3장 끝)
    CHECK(process::cover_inner_temp_degC(p.house, p.humidity, closed_house(p, 2, 10.0), 0.0) / 10.0 ==
          Approx(0.65).margin(0.005));
    CHECK(process::cover_inner_temp_degC(p.house, p.humidity, closed_house(p, 4, 10.0), 0.0) / 10.0 ==
          Approx(0.83).margin(0.005));
    double previous = closed_degC;
    for (const double fraction : {0.3, 0.5, 0.7}) {
        auto opened = closed_house(p, 3, 10.0);
        open_all(p, opened, fraction);
        const double t = process::cover_inner_temp_degC(p.house, p.humidity, opened, 0.0);
        CHECK(t < previous);
        previous = t;
    }
    auto fully = closed_house(p, 3, 10.0);
    open_all(p, fully, 1.0);
    CHECK(process::cover_inner_temp_degC(p.house, p.humidity, fully, 0.0) == Approx(0.0).margin(1e-12));
    CHECK(process::cover_state(p.house, fully).resistance_m2K_per_W == Approx(0.0).margin(1e-12));
}

// ---- 수증기 수지 ----

// 원천 0, 창 닫힘, 안이 바깥보다 습함: 수증기가 바깥 값 쪽으로 단조 감소 (틈새)
TEST_CASE("Closed house vapor decays toward outside without a source", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    auto h = closed_house(p, 3, 20.0);
    const WeatherHour w{15.0, 0.0, 50.0};
    const double v_out = 0.5 * process::saturation_vapor_g_per_m3(p.humidity, 15.0);
    h.vapor_g_per_m3 = 10.0;  // 피복 면 포화(약 16 g/m³)보다 낮아 결로 없음
    const double start_mm = air_mm(p, h);
    double previous = h.vapor_g_per_m3;
    for (int i = 0; i < 48; ++i) {
        process::update_house_vapor(h, p.house, p.humidity, w, 0.0);
        CHECK(h.vapor_g_per_m3 < previous);
        CHECK(h.vapor_g_per_m3 > v_out);
        previous = h.vapor_g_per_m3;
    }
    CHECK(h.water_condensed_mm == 0.0);
    CHECK(h.vapor_vented_mm > 0.0);
    CHECK(vapor_balance_error(p, h, start_mm) < kConservationRelTol);
}

// 큰 원천(시간당 300 g/m²), 창 닫힘: house_rh_index ≤ 1, 초과분이 결로와 안개·이슬 장부로, 보존식 성립
TEST_CASE("Large vapor source stays at or below saturation and is conserved", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    auto h = closed_house(p, 3, 8.0);
    const WeatherHour w{0.0, 0.0, 80.0};
    process::init_house_humidity(h, p.house, p.humidity, w);
    const double start_mm = h.vapor_initial_mm;
    for (int i = 0; i < 24; ++i) {
        process::update_house_vapor(h, p.house, p.humidity, w, 0.3);
        CHECK(h.rh_index <= 1.0);
        CHECK(h.vapor_g_per_m3 <= process::saturation_vapor_g_per_m3(p.humidity, h.air_temp_degC) * (1.0 + 1e-12));
        CHECK(vapor_balance_error(p, h, start_mm) < kConservationRelTol);
    }
    CHECK(h.water_condensed_mm > 0.0);
    CHECK(h.vapor_deposited_mm > 0.0);
    CHECK(h.vapor_source_mm == Approx(24 * 0.3));
}

// 겨울 밤, 창 닫힘, 3겹: 습도 ≥ 0.8이고 결로 > 0.
// 조건: 안 5.5 °C, 밖 −3 °C 90%, 틈새 0.1(명세 결정 1 값), 원천 시간당 2 g/m² (토양 증발 + 작물, 테스트 입력)
TEST_CASE("Closed winter night with three layers is humid and condenses", "[unit][m5b][humidity]") {
    sim::Params p = humidity_params();
    p.house.infiltration_air_changes_per_hour = 0.1;
    auto h = closed_house(p, 3, 5.5);
    const WeatherHour w{-3.0, 0.0, 90.0};
    process::init_house_humidity(h, p.house, p.humidity, w);
    run_vapor(p, h, w, 0.002, 48);
    CHECK(h.rh_index >= 0.8);
    CHECK(h.water_condensed_mm > 0.0);
    CHECK(vapor_balance_error(p, h, h.vapor_initial_mm) < kConservationRelTol);
}

// 같은 밤, 난방 10 °C 대 없음(2 °C): 난방 쪽 습도가 낮음
TEST_CASE("Heating lowers relative humidity on the same night", "[unit][m5b][humidity]") {
    sim::Params p = humidity_params();
    p.house.infiltration_air_changes_per_hour = 0.1;
    const WeatherHour w{-5.0, 0.0, 90.0};
    auto cold = closed_house(p, 3, 2.0);
    auto heated = closed_house(p, 3, 10.0);
    process::init_house_humidity(cold, p.house, p.humidity, w);
    process::init_house_humidity(heated, p.house, p.humidity, w);
    run_vapor(p, cold, w, 0.002, 48);
    run_vapor(p, heated, w, 0.002, 48);
    CHECK(heated.rh_index < cold.rh_index);
}

// 겨울 한낮, 창 닫힘 대 환기: 환기 쪽 수증기 농도가 낮음
TEST_CASE("Venting lowers house vapor at a winter noon", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    const WeatherHour w{2.0, 400.0, 50.0};
    auto closed = closed_house(p, 3, 22.0);
    auto vented = closed_house(p, 3, 18.0);
    open_all(p, vented, 0.5);
    process::init_house_humidity(closed, p.house, p.humidity, w);
    process::init_house_humidity(vented, p.house, p.humidity, w);
    run_vapor(p, closed, w, 0.2, 6);
    run_vapor(p, vented, w, 0.2, 6);
    CHECK(vented.vapor_g_per_m3 < closed.vapor_g_per_m3);
    CHECK(vented.vapor_vented_mm > closed.vapor_vented_mm);
    CHECK(vapor_balance_error(p, vented, vented.vapor_initial_mm) < kConservationRelTol);
}

// 4겹 대 2겹, 같은 밤: 4겹 쪽 결로가 작고 습도가 높음
TEST_CASE("Four layers condense less and are more humid than two", "[unit][m5b][humidity]") {
    sim::Params p = humidity_params();
    p.house.infiltration_air_changes_per_hour = 0.1;
    const WeatherHour w{-3.0, 0.0, 90.0};
    auto two = closed_house(p, 2, 5.5);
    auto four = closed_house(p, 4, 5.5);
    process::init_house_humidity(two, p.house, p.humidity, w);
    process::init_house_humidity(four, p.house, p.humidity, w);
    run_vapor(p, two, w, 0.002, 48);
    run_vapor(p, four, w, 0.002, 48);
    CHECK(four.water_condensed_mm < two.water_condensed_mm);
    CHECK(four.rh_index > two.rh_index);
}

// 바깥이 안보다 습함(여름 새벽): 환기 순량이 음수, 보존식 성립
TEST_CASE("Wetter outside air gives a negative net vent flow", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    const WeatherHour w{20.0, 0.0, 100.0};
    auto h = closed_house(p, 3, 22.0);
    open_all(p, h, 0.5);
    h.vapor_g_per_m3 = 8.0;
    const double start_mm = air_mm(p, h);
    process::update_house_vapor(h, p.house, p.humidity, w, 0.0);
    CHECK(h.vapor_vented_mm < 0.0);
    CHECK(h.vapor_g_per_m3 > 8.0);
    CHECK(vapor_balance_error(p, h, start_mm) < kConservationRelTol);
}

// 틈새 0.1 대 0.3, 같은 겨울 밤, 창 닫힘: 0.1 쪽 수증기·습도가 높음
TEST_CASE("Lower infiltration keeps more vapor on a closed winter night", "[unit][m5b][humidity]") {
    sim::Params tight = humidity_params();
    tight.house.infiltration_air_changes_per_hour = 0.1;
    sim::Params leaky = humidity_params();
    leaky.house.infiltration_air_changes_per_hour = 0.3;
    const WeatherHour w{-3.0, 0.0, 90.0};
    auto a = closed_house(tight, 3, 5.5);
    auto b = closed_house(leaky, 3, 5.5);
    process::init_house_humidity(a, tight.house, tight.humidity, w);
    process::init_house_humidity(b, leaky.house, leaky.humidity, w);
    run_vapor(tight, a, w, 0.001, 48);
    run_vapor(leaky, b, w, 0.001, 48);
    CHECK(a.vapor_g_per_m3 > b.vapor_g_per_m3);
    CHECK(a.rh_index > b.rh_index);
}

// 습도 이동 평균: 상대습도 쪽으로 min(1, 완화 비율)만큼 다가가요
TEST_CASE("Relative humidity mean relaxes toward the current value", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    auto h = closed_house(p, 3, 10.0);
    h.vapor_g_per_m3 = 5.0;
    h.rh_mean_index = 0.0;
    process::update_house_vapor(h, p.house, p.humidity, WeatherHour{5.0, 0.0, 70.0}, 0.0);
    CHECK(h.rh_mean_index == Approx(h.rh_index * p.humidity.rh_mean_relax_rate_per_hour));
}

// ---- 공기 흐름, 팬 (명령 없이 fan_on을 직접 세움) ----

// 팬 켬 대 끔: 공기 흐름 지수가 fan_airflow_index만큼 크고, 하우스 수증기는 같음
TEST_CASE("Fan adds its airflow index but does not change house vapor", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    auto off = closed_house(p, 3, 6.0);
    auto on = off;
    on.fan_on = true;
    process::update_airflow(off, p.humidity);
    process::update_airflow(on, p.humidity);
    CHECK(on.airflow_index - off.airflow_index == Approx(p.humidity.fan_airflow_index));
    // 창을 조금 열면 경로 × vent_airflow_factor가 더해지고 1에서 멈춰요
    auto vented = off;
    open_all(p, vented, 0.3);
    process::update_airflow(vented, p.humidity);
    CHECK(vented.airflow_index == Approx(std::min(1.0, p.humidity.vent_airflow_factor * vented.vent_path_index)));
    vented.fan_on = true;
    process::update_airflow(vented, p.humidity);
    CHECK(vented.airflow_index <= 1.0);

    const WeatherHour w{-3.0, 0.0, 90.0};
    run_vapor(p, off, w, 0.002, 24);
    run_vapor(p, on, w, 0.002, 24);
    CHECK(on.vapor_g_per_m3 == off.vapor_g_per_m3);
}

// ---- 잎 젖음 ----

// 팬 켬 대 끔, LAI 3, 바람 없음: 끈 쪽만 젖기 시작.
// 명세 8장 표는 습도 0.9로 적었지만 4.3 식으로는 팬 0.6이면 0.888부터 젖어서(4.3 아래 설명 "0.84 → 0.89")
// 습도 0.9에서는 양쪽 다 젖어요. 그래서 두 문턱(0.84, 0.888) 사이인 0.86으로 확인해요 (보고 사항).
TEST_CASE("Fan prevents wetting at a humidity where still air wets", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    auto off = crop_with_lai(p, 3.0);
    auto on = off;
    process::update_leaf_wetness(off, p.crop, p.humidity, 0.86, 0.0, 0.0, false);
    process::update_leaf_wetness(on, p.crop, p.humidity, 0.86, p.humidity.fan_airflow_index, 0.0, false);
    CHECK(off.leaf_wetness_index > 0.0);
    CHECK(on.leaf_wetness_index == 0.0);
    CHECK(off.canopy_rh_index > on.canopy_rh_index);
}

// 젖은 잎, 팬 켬 대 끔, 습도 0.7: 켠 쪽이 빨리 마름
TEST_CASE("Fan dries wet leaves faster", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    auto off = crop_with_lai(p, 2.0);
    off.leaf_wetness_index = 1.0;
    auto on = off;
    process::update_leaf_wetness(off, p.crop, p.humidity, 0.7, 0.0, 0.0, false);
    process::update_leaf_wetness(on, p.crop, p.humidity, 0.7, p.humidity.fan_airflow_index, 0.0, false);
    CHECK(on.leaf_wetness_index < off.leaf_wetness_index);
    CHECK(off.leaf_wetness_index < 1.0);
}

// LAI 4 대 1, 바람 없음: 4 쪽 canopy_rh가 높음
TEST_CASE("Denser canopy has higher canopy humidity in still air", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    auto dense = crop_with_lai(p, 4.0);
    auto sparse = crop_with_lai(p, 1.0);
    process::update_leaf_wetness(dense, p.crop, p.humidity, 0.8, 0.0, 0.0, false);
    process::update_leaf_wetness(sparse, p.crop, p.humidity, 0.8, 0.0, 0.0, false);
    CHECK(dense.canopy_rh_index > sparse.canopy_rh_index);
    CHECK(dense.canopy_rh_index == Approx(0.8 + p.humidity.canopy_rh_boost_index));
}

// 습도 1, 하우스 안 일사 200 W/m²: 새로 젖지 않음 (낮 이슬 없음)
TEST_CASE("No new dew forms in daylight even at saturation", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    auto crop = crop_with_lai(p, 3.0);
    process::update_leaf_wetness(crop, p.crop, p.humidity, 1.0, 0.0, 200.0, false);
    CHECK(crop.leaf_wetness_index == 0.0);
    // 같은 습도의 밤에는 젖어요
    process::update_leaf_wetness(crop, p.crop, p.humidity, 1.0, 0.0, 0.0, false);
    CHECK(crop.leaf_wetness_index > 0.0);
}

// 두상 관수 표시(함수 인자로 직접): 그 틱 w = overhead_wet_index
TEST_CASE("Overhead flag sets leaf wetness to the overhead index", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    auto crop = crop_with_lai(p, 2.0);
    process::update_leaf_wetness(crop, p.crop, p.humidity, 0.5, 0.0, 300.0, true);
    CHECK(crop.leaf_wetness_index == Approx(p.humidity.overhead_wet_index));
}

namespace {
// 젖는 틱: 습도 1, 밤 / 마르는 틱: 습도 0.5, 맑은 낮 (한 틱에 거의 다 마름)
void wet_tick(const sim::Params& p, sim::Crop& c) { process::update_leaf_wetness(c, p.crop, p.humidity, 1.0, 0.0, 0.0, false); }
void dry_tick(const sim::Params& p, sim::Crop& c) {
    process::update_leaf_wetness(c, p.crop, p.humidity, 0.5, 0.0, 500.0, false);
}
}  // namespace

// 젖음 3 h → 마름 2 h → 젖음 3 h: wet_run_hours가 끊기지 않고 6
TEST_CASE("Short dry break keeps the wet run going", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    auto crop = crop_with_lai(p, 2.0);
    for (int i = 0; i < 3; ++i) {
        wet_tick(p, crop);
    }
    CHECK(crop.wet_run_hours == 3.0);
    for (int i = 0; i < 2; ++i) {
        dry_tick(p, crop);
        CHECK(crop.leaf_wetness_index < p.humidity.wet_threshold_index);
    }
    CHECK(crop.wet_run_hours == 3.0);
    CHECK(crop.dry_run_hours == 2.0);
    for (int i = 0; i < 3; ++i) {
        wet_tick(p, crop);
    }
    CHECK(crop.wet_run_hours == 6.0);
    CHECK(crop.dry_run_hours == 0.0);
}

// 젖음 3 h → 마름 4 h: wet_run_hours = 0
TEST_CASE("Dry break of break hours resets the wet run", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    auto crop = crop_with_lai(p, 2.0);
    for (int i = 0; i < 3; ++i) {
        wet_tick(p, crop);
    }
    for (int i = 0; i < 3; ++i) {
        dry_tick(p, crop);
    }
    CHECK(crop.wet_run_hours == 3.0);
    dry_tick(p, crop);
    CHECK(crop.wet_run_hours == 0.0);
}

// 작물이 없거나 죽은 셀은 w = 0, 시간 0
TEST_CASE("Dead or absent crop has no leaf wetness", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    auto crop = crop_with_lai(p, 2.0);
    crop.leaf_wetness_index = 0.8;
    crop.wet_run_hours = 5.0;
    crop.dry_run_hours = 1.0;
    crop.alive = false;
    process::update_leaf_wetness(crop, p.crop, p.humidity, 1.0, 0.0, 0.0, false);
    CHECK(crop.leaf_wetness_index == 0.0);
    CHECK(crop.wet_run_hours == 0.0);
    CHECK(crop.dry_run_hours == 0.0);
    sim::Crop absent;
    process::update_leaf_wetness(absent, p.crop, p.humidity, 1.0, 0.0, 0.0, false);
    CHECK(absent.leaf_wetness_index == 0.0);
}

// ---- 극한 ----

// 극한: 증산 100배, 바깥 −15 °C, 바깥 습도 0·100%: 0 ≤ rh ≤ 1, v ≥ 0, 보존식 성립, NaN 없음
TEST_CASE("Humidity stays bounded and conserved under extreme inputs", "[unit][m5b][humidity][extreme]") {
    const sim::Params p = humidity_params();
    for (const double outside_rh : {0.0, 100.0}) {
        for (const double source_mm : {0.0, 30.0}) {
            for (const bool vented : {false, true}) {
                auto h = closed_house(p, 3, 5.0);
                if (vented) {
                    open_all(p, h, 1.0);
                }
                const WeatherHour w{-15.0, 0.0, outside_rh};
                process::init_house_humidity(h, p.house, p.humidity, w);
                for (int i = 0; i < 48; ++i) {
                    process::update_house_vapor(h, p.house, p.humidity, w, source_mm);
                    REQUIRE(std::isfinite(h.vapor_g_per_m3));
                    REQUIRE(std::isfinite(h.rh_index));
                    CHECK(h.vapor_g_per_m3 >= 0.0);
                    CHECK(h.rh_index >= 0.0);
                    CHECK(h.rh_index <= 1.0);
                    CHECK(h.rh_mean_index >= 0.0);
                    CHECK(h.rh_mean_index <= 1.0);
                }
                CHECK(vapor_balance_error(p, h, h.vapor_initial_mm) < kConservationRelTol);
            }
        }
    }
    // 잎 젖음도 0~1, 시간 ≥ 0
    auto crop = crop_with_lai(p, 50.0);
    for (int i = 0; i < 48; ++i) {
        process::update_leaf_wetness(crop, p.crop, p.humidity, i % 2 == 0 ? 2.0 : -1.0, 5.0, 1e6, false);
        CHECK(crop.leaf_wetness_index >= 0.0);
        CHECK(crop.leaf_wetness_index <= 1.0);
        CHECK(crop.wet_run_hours >= 0.0);
        CHECK(crop.dry_run_hours >= 0.0);
    }
}

// ---- 월드 통합: 보존·원천 일치·세이브 왕복 ----

namespace {

// 겨울 같은 날씨(밤 −3 °C 90%, 낮 8 °C 50%, 맑은 낮)와 작물, 매일 11~15시 한낮 환기
sim::Scenario humid_scenario() {
    sim::Scenario s = test::test_scenario(10, 2, 3);
    s.crop = test::test_crop_init();
    for (std::size_t i = 0; i < s.weather_hourly.size(); ++i) {
        const int hour = static_cast<int>(i % sim::kHoursPerDay);
        const bool day = hour >= 7 && hour < 17;
        s.weather_hourly[i] = WeatherHour{
            .outside_air_temp_degC = day ? 8.0 : -3.0,
            .solar_radiation_W_per_m2 = day ? 60.0 * static_cast<double>(std::min(hour - 6, 17 - hour)) : 0.0,
            .outside_rh_percent = day ? 50.0 : 90.0,
        };
    }
    return s;
}

sim::World humid_world() {
    sim::Params p = humidity_params();
    p.house.infiltration_air_changes_per_hour = 0.1;
    auto world = sim::World::create(std::move(p), humid_scenario());
    if (!world) {
        throw std::runtime_error(world.error().message);
    }
    return std::move(*world);
}

// 다음 틱 시각이 hour이면 한낮 환기 열고/닫기
void schedule_vent(sim::World& w) {
    const auto hour = w.snapshot().time.hour;
    if (hour == 11 || hour == 15) {
        const double open = hour == 11 ? 0.5 : 0.0;
        for (int layer = 0; layer < 3; ++layer) {
            for (const auto side : {sim::WindowSide::Left, sim::WindowSide::Right}) {
                test::must_enqueue(w, sim::SetWindowCommand{.layer = layer, .side = side, .open_fraction = open});
            }
        }
    }
}

}  // namespace

// 여러 날 월드: 매 틱 수증기 보존(상대 1e-9), 원천 = 셀 평균 (증산 + 증발) 증가분, 0 ≤ rh ≤ 1, 잎 젖음 0~1
TEST_CASE("World conserves house vapor every tick over several days", "[m5b][humidity][conservation]") {
    sim::World world = humid_world();
    const sim::Params& p = world.params();
    const auto start = world.snapshot();
    const double start_mm = start.house.vapor_initial_mm;
    CHECK(air_mm(p, start.house) == Approx(start_mm));
    const double cells = static_cast<double>(start.soil.size());
    double prev_water_vapor_mm = start.ledger.water_transpired_mm + start.ledger.water_evaporated_mm;
    double prev_source_mm = start.house.vapor_source_mm;
    bool any_wet = false;
    bool any_condensed = false;
    for (int t = 0; t < 24 * 5; ++t) {
        schedule_vent(world);
        world.step(1);
        const auto s = world.snapshot();
        REQUIRE(vapor_balance_error(p, s.house, start_mm) < kConservationRelTol);
        const double water_vapor_mm = s.ledger.water_transpired_mm + s.ledger.water_evaporated_mm;
        CHECK(s.house.vapor_source_mm - prev_source_mm ==
              Approx((water_vapor_mm - prev_water_vapor_mm) / cells).margin(1e-12));
        prev_water_vapor_mm = water_vapor_mm;
        prev_source_mm = s.house.vapor_source_mm;
        CHECK(s.house.rh_index >= 0.0);
        CHECK(s.house.rh_index <= 1.0);
        for (const sim::Crop& c : s.crops) {
            CHECK(c.leaf_wetness_index >= 0.0);
            CHECK(c.leaf_wetness_index <= 1.0);
            any_wet = any_wet || c.wet_run_hours > 0.0;
        }
        any_condensed = any_condensed || s.house.water_condensed_mm > 0.0;
    }
    CHECK(world.snapshot().house.vapor_source_mm > 0.0);
    CHECK(world.snapshot().house.vapor_vented_mm != 0.0);
    // 방향 확인용(값은 6장 목표값 대상이 아님): 겨울 밤 밀폐 하우스는 결로가 생기고 잎이 젖어요
    CHECK(any_condensed);
    CHECK(any_wet);
}

// 세이브 왕복: 수증기, 습도 평균, 팬, 젖음, 시간 상태, 장부가 같고 이어서 돌려도 해시가 같음
TEST_CASE("Humidity state survives a save round trip", "[unit][m5b][humidity][serialization]") {
    sim::World world = humid_world();
    for (int t = 0; t < 30; ++t) {
        schedule_vent(world);
        world.step(1);
    }
    const std::vector<std::byte> bytes = world.serialize();
    auto loaded = sim::World::deserialize(bytes);
    REQUIRE(loaded.has_value());
    const auto a = world.snapshot();
    const auto b = loaded->snapshot();
    CHECK(b.house.vapor_g_per_m3 == a.house.vapor_g_per_m3);
    CHECK(b.house.rh_mean_index == a.house.rh_mean_index);
    CHECK(b.house.fan_on == a.house.fan_on);
    CHECK(b.house.vapor_source_mm == a.house.vapor_source_mm);
    CHECK(b.house.water_condensed_mm == a.house.water_condensed_mm);
    CHECK(b.house.vapor_vented_mm == a.house.vapor_vented_mm);
    CHECK(b.house.vapor_deposited_mm == a.house.vapor_deposited_mm);
    CHECK(b.house.vapor_initial_mm == a.house.vapor_initial_mm);
    CHECK(b.house.rh_index == a.house.rh_index);
    CHECK(b.house.airflow_index == a.house.airflow_index);
    CHECK(b.house.cover_inner_temp_degC == a.house.cover_inner_temp_degC);
    REQUIRE(b.crops.size() == a.crops.size());
    for (std::size_t i = 0; i < a.crops.size(); ++i) {
        CHECK(b.crops[i].leaf_wetness_index == a.crops[i].leaf_wetness_index);
        CHECK(b.crops[i].wet_run_hours == a.crops[i].wet_run_hours);
        CHECK(b.crops[i].dry_run_hours == a.crops[i].dry_run_hours);
        CHECK(b.crops[i].canopy_rh_index == a.crops[i].canopy_rh_index);
    }
    CHECK(loaded->params().humidity == world.params().humidity);
    CHECK(loaded->state_hash() == world.state_hash());
    for (int t = 0; t < 30; ++t) {
        schedule_vent(world);
        schedule_vent(*loaded);
        world.step(1);
        loaded->step(1);
    }
    CHECK(loaded->state_hash() == world.state_hash());
}

// ---- 명령·되돌림 확인 (8장 단위 표, 15.4 4단계 보강) ----

// 팬 켬 대 끔, 습도 0.91, LAI 3 (이슬 문턱 0.95): 끈 쪽만 젖기 시작 (8장 표의 현재 값.
// 끔 0.91 + 0.08 = 0.99 ≥ 0.95, 팬 0.6이면 0.91 + 0.08 × 0.4 = 0.942 < 0.95)
// (test_params()의 이슬 문턱은 2차 전 값 0.92라서 최종 값 0.95를 가진 repo_params()로 봐요)
TEST_CASE("Fan prevents wetting at the spec humidity 0.91 with LAI 3", "[unit][m5b][humidity]") {
    const sim::Params p = test::repo_params();
    REQUIRE(p.humidity.dew_rh_index == Approx(0.95));
    auto off = crop_with_lai(p, 3.0);
    auto on = off;
    process::update_leaf_wetness(off, p.crop, p.humidity, 0.91, 0.0, 0.0, false);
    process::update_leaf_wetness(on, p.crop, p.humidity, 0.91, p.humidity.fan_airflow_index, 0.0, false);
    CHECK(off.leaf_wetness_index > 0.0);
    CHECK(on.leaf_wetness_index == 0.0);
}

// 습도 1, 하우스 안 일사 200 W/m²: 젖은 잎이 마름 (일사 항은 습도 곡선과 무관, 1차 구조 변경)
TEST_CASE("Sunlit wet leaves dry even in saturated air", "[unit][m5b][humidity]") {
    const sim::Params p = humidity_params();
    auto crop = crop_with_lai(p, 3.0);
    crop.leaf_wetness_index = 1.0;
    process::update_leaf_wetness(crop, p.crop, p.humidity, 1.0, 0.0, 200.0, false);
    CHECK(crop.leaf_wetness_index < 1.0);
    // 같은 포화 공기의 밤에는 마르지 않음 (이슬 조건이라 젖음 쪽)
    auto night = crop_with_lai(p, 3.0);
    night.leaf_wetness_index = 1.0;
    process::update_leaf_wetness(night, p.crop, p.humidity, 1.0, 0.0, 0.0, false);
    CHECK(night.leaf_wetness_index == 1.0);
}

namespace {

// 등급 A 2×3 하우스 (병해 없음), 점적 설치 여부만 정해요
sim::World grade_a_world(bool drip_installed) {
    sim::Scenario s = test::test_scenario(330, 2, 3);
    s.crop = sim::CropInit{.seedling_grade = "A", .plants_per_m2 = 8.0, .initial_establishment_index = 1.0};
    s.drip_installed = drip_installed;
    return test::make_world(std::move(s));
}

}  // namespace

// 두상 관수(점적 미설치): 그 틱 w = overhead_wet_index, 셀에 overhead 표시.
// 점적 설치 뒤 같은 관수는 표시가 없고 잎 젖음은 관수하지 않은 쌍둥이 월드와 같음
TEST_CASE("Overhead irrigation wets leaves until drip is installed", "[unit][m5b][humidity][command]") {
    sim::World world = grade_a_world(false);
    const double overhead_wet = world.params().humidity.overhead_wet_index;
    world.step(3);  // 밤 (일사 없음)
    test::must_enqueue(world, sim::IrrigateCommand{.area = sim::whole_grid(2, 3), .amount_mm = 2.0});
    world.step(1);
    {
        const sim::Snapshot s = world.snapshot();
        CHECK_FALSE(s.house.drip_installed);
        for (std::size_t i = 0; i < s.soil.size(); ++i) {
            CHECK(s.soil[i].overhead_irrigated_this_tick);
            CHECK(s.crops[i].leaf_wetness_index == overhead_wet);
        }
    }
    world.step(1);  // 표시는 틱마다 다시 계산 (관수 없는 틱은 표시 없음)
    for (const sim::SoilCell& c : world.snapshot().soil) {
        CHECK_FALSE(c.overhead_irrigated_this_tick);
    }
    test::must_enqueue(world, sim::InstallDripCommand{});
    world.step(12);  // 잎이 마를 시간 (낮)
    sim::World twin = world;
    test::must_enqueue(world, sim::IrrigateCommand{.area = sim::whole_grid(2, 3), .amount_mm = 2.0});
    world.step(1);
    twin.step(1);
    const sim::Snapshot a = world.snapshot();
    const sim::Snapshot b = twin.snapshot();
    CHECK(a.house.drip_installed);
    for (std::size_t i = 0; i < a.soil.size(); ++i) {
        CHECK_FALSE(a.soil[i].overhead_irrigated_this_tick);
        CHECK(a.crops[i].leaf_wetness_index == b.crops[i].leaf_wetness_index);
        CHECK(a.crops[i].leaf_wetness_index < overhead_wet);
        CHECK(a.soil[i].water_mm > b.soil[i].water_mm);  // 물은 토양으로 들어갔어요
    }
}

// InstallDrip 두 번: 두 번째는 enqueue에서 거부. 처음부터 점적이 깔린 하우스도 거부
TEST_CASE("InstallDrip is rejected once drip is installed", "[unit][m5b][humidity][command]") {
    sim::World installed = grade_a_world(true);
    CHECK_FALSE(installed.enqueue(sim::InstallDripCommand{}).has_value());

    sim::World world = grade_a_world(false);
    REQUIRE(world.enqueue(sim::InstallDripCommand{}).has_value());
    world.step(1);
    CHECK(world.snapshot().house.drip_installed);
    CHECK(world.snapshot().house.drip_installations == 1.0);
    CHECK_FALSE(world.enqueue(sim::InstallDripCommand{}).has_value());
    world.step(1);
    CHECK(world.snapshot().house.drip_installations == 1.0);
}

// 증산 (되돌림 확인): 같은 일사·LAI에서 틱 시작 하우스 습도만 다르게(바깥 습도 30% 대 95%, 창 전부 열림) 해도
// 증산·증발·토양 물이 같음 (습도와 무관, M4 식). 등급 A·배경 포자 0이라 잎 젖음 차이가 병해로 번지지 않아요
TEST_CASE("Transpiration does not depend on house humidity", "[unit][m5b][humidity]") {
    const auto make = [](double outside_rh_percent) {
        sim::Scenario s = test::test_scenario(120, 2, 3);
        s.crop = sim::CropInit{.seedling_grade = "A", .plants_per_m2 = 8.0, .initial_establishment_index = 1.0};
        for (std::size_t i = 0; i < s.weather_hourly.size(); ++i) {
            const int hour = static_cast<int>(i % sim::kHoursPerDay);
            s.weather_hourly[i].outside_air_temp_degC = 18.0;
            s.weather_hourly[i].solar_radiation_W_per_m2 = hour >= 7 && hour < 17 ? 400.0 : 0.0;
            s.weather_hourly[i].outside_rh_percent = outside_rh_percent;
        }
        sim::World w = test::make_world(std::move(s));
        for (int layer = 0; layer < 3; ++layer) {
            for (const auto side : {sim::WindowSide::Left, sim::WindowSide::Right}) {
                test::must_enqueue(w, sim::SetWindowCommand{.layer = layer, .side = side, .open_fraction = 1.0});
            }
        }
        return w;
    };
    sim::World dry = make(30.0);
    sim::World humid = make(95.0);
    double max_rh_gap = 0.0;
    double transpired = 0.0;
    for (int t = 0; t < 72; ++t) {
        max_rh_gap = std::max(max_rh_gap, humid.snapshot().house.rh_index - dry.snapshot().house.rh_index);
        dry.step(1);
        humid.step(1);
        const sim::Snapshot a = dry.snapshot();
        const sim::Snapshot b = humid.snapshot();
        REQUIRE(a.ledger.water_transpired_mm == b.ledger.water_transpired_mm);
        REQUIRE(a.ledger.water_evaporated_mm == b.ledger.water_evaporated_mm);
        for (std::size_t i = 0; i < a.soil.size(); ++i) {
            REQUIRE(a.soil[i].water_mm == b.soil[i].water_mm);
            REQUIRE(a.crops[i].leaf_g_per_m2 == b.crops[i].leaf_g_per_m2);
        }
        transpired = a.ledger.water_transpired_mm;
    }
    CHECK(max_rh_gap > 0.3);  // 틱 시작 습도가 실제로 크게 달랐어요 (0.6 대 0.95 수준)
    CHECK(transpired > 0.0);
}

// 세이브 왕복: 점적 미설치·팬 가동·두상 관수 하우스의 점적 여부, 팬 시간, 점적 설치 장부, 잎 젖음이 같고,
// 대기 중인 InstallDrip·SetFan까지 이어서 돌려도 해시가 같음
TEST_CASE("Drip and fan state survive a save round trip", "[unit][m5b][humidity][serialization]") {
    sim::World world = grade_a_world(false);
    test::must_enqueue(world, sim::SetFanCommand{.on = true});
    for (int t = 0; t < 30; ++t) {
        if (t % 24 == 8) {
            test::must_enqueue(world, sim::IrrigateCommand{.area = sim::whole_grid(2, 3), .amount_mm = 2.0});
        }
        world.step(1);
    }
    test::must_enqueue(world, sim::InstallDripCommand{});
    test::must_enqueue(world, sim::SetFanCommand{.on = false});
    auto loaded = sim::World::deserialize(world.serialize());
    REQUIRE(loaded.has_value());
    const sim::Snapshot a = world.snapshot();
    const sim::Snapshot b = loaded->snapshot();
    CHECK_FALSE(b.house.drip_installed);
    CHECK(b.house.fan_on);
    CHECK(b.house.fan_hours == a.house.fan_hours);
    CHECK(b.house.fan_hours == 30.0);
    CHECK(b.house.drip_installations == a.house.drip_installations);
    CHECK(b.house == a.house);
    CHECK(b.crops == a.crops);
    CHECK(b.pending_command_count == 2);
    CHECK(loaded->state_hash() == world.state_hash());
    world.step(24);
    loaded->step(24);
    CHECK(loaded->snapshot().house.drip_installed);
    CHECK_FALSE(loaded->snapshot().house.fan_on);
    CHECK(loaded->snapshot().house == world.snapshot().house);
    CHECK(loaded->state_hash() == world.state_hash());
}
