// 패키지 1 단위 테스트: 토양 수분 + 용탈 (docs/spec/soil-water.md, nitrogen.md "용탈")
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "sim/processes.hpp"
#include "test_helpers.hpp"

namespace {

constexpr double kDaySolar = 600.0;  // W/m², 낮
constexpr double kNightSolar = 0.0;

double water_balance_error(const sim::SoilCell& cell, double initial, const sim::Ledger& l) {
    const double expected =
        initial + l.water_in_mm - l.water_evaporated_mm - l.water_transpired_mm - l.water_drained_mm;
    return std::abs(cell.water_mm - expected) / std::max(1.0, std::abs(expected));
}

}  // namespace

// 관수는 수분과 장부에 같은 양을 더한다
TEST_CASE("Irrigation adds the same amount to water and the ledger", "[unit][soil_water]") {
    sim::SoilCell cell = test::test_cell();
    sim::Ledger ledger;
    const double before = cell.water_mm;
    sim::process::apply_irrigation(cell, 12.5, test::test_params().aeration, ledger);
    REQUIRE(cell.water_mm == Catch::Approx(before + 12.5));
    REQUIRE(ledger.water_in_mm == Catch::Approx(12.5));
}

// 포장용수량 이하, 관수 없음: 배수 0, 수분 단조 감소
TEST_CASE("At or below field capacity without irrigation: no drainage, water decreases", "[unit][soil_water]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    REQUIRE(cell.water_mm < cell.field_capacity_mm);
    sim::Ledger ledger;
    double prev = cell.water_mm;
    for (int i = 0; i < 200; ++i) {
        sim::process::update_soil_water(cell, params.soil_water, kDaySolar, 0.0, 0.0, ledger);
        REQUIRE(cell.drained_mm_this_tick == 0.0);
        REQUIRE(cell.water_mm < prev);
        prev = cell.water_mm;
    }
    REQUIRE(ledger.water_drained_mm == 0.0);
}

// 포장용수량 초과: 배수 > 0, 초과분이 매 틱 (1 - 배수율) 비율로 줄어든다
TEST_CASE("Above field capacity: excess shrinks by (1 - drainage rate) each tick", "[unit][soil_water]") {
    sim::Params params = test::test_params();
    // 증발을 끄고 배수만 본다
    params.soil_water.evap_base_mm_per_hour = 0.0;
    params.soil_water.evap_mm_per_hour_per_W_per_m2 = 0.0;
    sim::SoilCell cell = test::test_cell();
    cell.water_mm = cell.field_capacity_mm + 20.0;
    const double rate = params.soil_water.drainage_rate_per_hour *
                        params.soil_water.drainage_compaction_curve(cell.compaction_index);
    sim::Ledger ledger;
    double excess = 20.0;
    for (int i = 0; i < 5; ++i) {
        sim::process::update_soil_water(cell, params.soil_water, kNightSolar, 0.0, 0.0, ledger);
        REQUIRE(cell.drained_mm_this_tick > 0.0);
        const double new_excess = cell.water_mm - cell.field_capacity_mm;
        REQUIRE(new_excess == Catch::Approx(excess * (1.0 - rate)));
        REQUIRE(cell.drained_mm_this_tick == Catch::Approx(excess - new_excess));
        excess = new_excess;
    }
}

// 낮 증발 > 밤 증발 (같은 수분)
TEST_CASE("Daytime evaporation exceeds night evaporation at equal water", "[unit][soil_water]") {
    const sim::Params params = test::test_params();
    sim::SoilCell day = test::test_cell();
    sim::SoilCell night = test::test_cell();
    sim::Ledger day_ledger;
    sim::Ledger night_ledger;
    sim::process::update_soil_water(day, params.soil_water, kDaySolar, 0.0, 0.0, day_ledger);
    sim::process::update_soil_water(night, params.soil_water, kNightSolar, 0.0, 0.0, night_ledger);
    REQUIRE(day_ledger.water_evaporated_mm > night_ledger.water_evaporated_mm);
    REQUIRE(night_ledger.water_evaporated_mm >= 0.0);
}

// 젖은 흙일수록 증발이 크다
TEST_CASE("Wetter soil evaporates more", "[unit][soil_water]") {
    const sim::Params params = test::test_params();
    sim::SoilCell wet = test::test_cell();
    sim::SoilCell dry = test::test_cell();
    wet.water_mm = 0.9 * wet.field_capacity_mm;
    dry.water_mm = 0.2 * dry.field_capacity_mm;
    sim::Ledger wet_ledger;
    sim::Ledger dry_ledger;
    sim::process::update_soil_water(wet, params.soil_water, kDaySolar, 0.0, 0.0, wet_ledger);
    sim::process::update_soil_water(dry, params.soil_water, kDaySolar, 0.0, 0.0, dry_ledger);
    REQUIRE(wet_ledger.water_evaporated_mm > dry_ledger.water_evaporated_mm);
}

// 1000 mm 관수: NaN 없음, 수분이 포장용수량 근처로 내려오고 보존된다
TEST_CASE("1000 mm irrigation: finite, drains toward field capacity, water conserved", "[unit][soil_water]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    const double initial = cell.water_mm;
    sim::Ledger ledger;
    sim::process::apply_irrigation(cell, 1000.0, test::test_params().aeration, ledger);
    for (int i = 0; i < 24 * 30; ++i) {
        const double solar = (i % 24 >= 6 && i % 24 < 18) ? kDaySolar : kNightSolar;
        sim::process::update_soil_water(cell, params.soil_water, solar, 0.0, 0.0, ledger);
        REQUIRE(std::isfinite(cell.water_mm));
        REQUIRE(cell.water_mm >= 0.0);
        REQUIRE(cell.drained_mm_this_tick >= 0.0);
    }
    REQUIRE(cell.water_mm <= cell.field_capacity_mm * 1.01);
    REQUIRE(water_balance_error(cell, initial, ledger) < 1e-9);
}

// 1년 무관수: 수분 >= 0, 0 근처로 수렴, 보존
TEST_CASE("One year without irrigation: water stays non-negative and conserved", "[unit][soil_water]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    const double initial = cell.water_mm;
    sim::Ledger ledger;
    for (int i = 0; i < 24 * 365; ++i) {
        const double solar = (i % 24 >= 6 && i % 24 < 18) ? kDaySolar : kNightSolar;
        sim::process::update_soil_water(cell, params.soil_water, solar, 0.0, 0.0, ledger);
        REQUIRE(cell.water_mm >= 0.0);
    }
    REQUIRE(cell.water_mm < 0.05 * cell.field_capacity_mm);
    REQUIRE(water_balance_error(cell, initial, ledger) < 1e-9);
}

// 수분 0, 극단 입력에도 NaN 없음
TEST_CASE("Zero water with extreme inputs stays finite", "[unit][soil_water]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.water_mm = 0.0;
    sim::Ledger ledger;
    sim::process::update_soil_water(cell, params.soil_water, 1.0e6, 0.0, 0.0, ledger);
    REQUIRE(cell.water_mm == 0.0);
    REQUIRE(std::isfinite(ledger.water_evaporated_mm));
    REQUIRE(ledger.water_evaporated_mm == 0.0);
    REQUIRE(cell.drained_mm_this_tick == 0.0);
}

// 배수 0이면 용탈 0
TEST_CASE("No drainage means no leaching", "[unit][leaching]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.drained_mm_this_tick = 0.0;
    const sim::SoilCell before = cell;
    sim::Ledger ledger;
    sim::process::leach(cell, params.leaching, test::test_params().soil_chemistry, ledger);
    REQUIRE(cell == before);
    REQUIRE(ledger.N_leached_g == 0.0);
}

// 배수 > 0이면 NO3- 감소 비율 > NH4+ 감소 비율, 장부 일치
TEST_CASE("Drainage leaches NO3 proportionally more than NH4 and matches the ledger", "[unit][leaching]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.no2_N_g_per_m2 = 1.0;
    cell.drained_mm_this_tick = 10.0;
    const sim::SoilCell before = cell;
    sim::Ledger ledger;
    sim::process::leach(cell, params.leaching, test::test_params().soil_chemistry, ledger);

    const double no3_rel = 1.0 - cell.no3_N_g_per_m2 / before.no3_N_g_per_m2;
    const double nh4_rel = 1.0 - cell.nh4_N_g_per_m2 / before.nh4_N_g_per_m2;
    const double no2_rel = 1.0 - cell.no2_N_g_per_m2 / before.no2_N_g_per_m2;
    REQUIRE(no3_rel > 0.0);
    REQUIRE(no3_rel > nh4_rel);
    REQUIRE(no2_rel == Catch::Approx(no3_rel));
    REQUIRE(nh4_rel >= 0.0);
    REQUIRE(ledger.N_leached_g > 0.0);
    // 보존: 무기 N 감소분 == 장부
    REQUIRE(before.mineral_N_g_per_m2() - cell.mineral_N_g_per_m2() == Catch::Approx(ledger.N_leached_g).epsilon(1e-9));
}

// 용탈 비율 공식: f = drained / (water + drained)
TEST_CASE("Leaching fraction equals drained over water plus drained", "[unit][leaching]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.water_mm = 30.0;
    cell.drained_mm_this_tick = 10.0;
    const double no3_before = cell.no3_N_g_per_m2;
    sim::Ledger ledger;
    sim::process::leach(cell, params.leaching, test::test_params().soil_chemistry, ledger);
    REQUIRE(cell.no3_N_g_per_m2 == Catch::Approx(no3_before * (1.0 - 10.0 / 40.0)));
}

// 배수가 많을수록 용탈이 크다, 풀은 음수가 되지 않는다
TEST_CASE("More drainage leaches more and pools stay non-negative", "[unit][leaching]") {
    const sim::Params params = test::test_params();
    sim::SoilCell small = test::test_cell();
    sim::SoilCell large = test::test_cell();
    small.drained_mm_this_tick = 2.0;
    large.drained_mm_this_tick = 20.0;
    sim::Ledger small_ledger;
    sim::Ledger large_ledger;
    sim::process::leach(small, params.leaching, test::test_params().soil_chemistry, small_ledger);
    sim::process::leach(large, params.leaching, test::test_params().soil_chemistry, large_ledger);
    REQUIRE(large_ledger.N_leached_g > small_ledger.N_leached_g);

    // 극단: 남은 수분 0, 큰 배수
    sim::SoilCell extreme = test::test_cell();
    extreme.water_mm = 0.0;
    extreme.drained_mm_this_tick = 500.0;
    sim::Ledger ledger;
    sim::process::leach(extreme, params.leaching, test::test_params().soil_chemistry, ledger);
    REQUIRE(extreme.no3_N_g_per_m2 >= 0.0);
    REQUIRE(extreme.nh4_N_g_per_m2 >= 0.0);
    REQUIRE(extreme.no2_N_g_per_m2 >= 0.0);
    REQUIRE(std::isfinite(ledger.N_leached_g));
}

// M2: 증산 포함 물 보존
TEST_CASE("Water is conserved including transpiration", "[unit][soil_water][m2]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    const double initial = cell.water_mm;
    sim::Ledger ledger;
    for (int i = 0; i < 24 * 60; ++i) {
        const double solar = (i % 24 >= 6 && i % 24 < 18) ? kDaySolar : kNightSolar;
        if (i % 72 == 0) {
            sim::process::apply_irrigation(cell, 80.0, params.aeration, ledger);
        }
        sim::process::update_soil_water(cell, params.soil_water, solar, 0.5, 0.3, ledger);
        REQUIRE(cell.water_mm >= 0.0);
    }
    REQUIRE(ledger.water_transpired_mm > 0.0);
    REQUIRE(water_balance_error(cell, initial, ledger) < 1e-9);
}

// M2: 수관이 덮으면 증발이 줄어든다
TEST_CASE("Canopy interception reduces evaporation", "[unit][soil_water][m2]") {
    const sim::Params params = test::test_params();
    sim::SoilCell open_cell = test::test_cell();
    sim::SoilCell shaded = test::test_cell();
    sim::Ledger open_ledger;
    sim::Ledger shaded_ledger;
    sim::process::update_soil_water(open_cell, params.soil_water, kDaySolar, 0.0, 0.0, open_ledger);
    sim::process::update_soil_water(shaded, params.soil_water, kDaySolar, 0.6, 0.0, shaded_ledger);
    REQUIRE(shaded_ledger.water_evaporated_mm < open_ledger.water_evaporated_mm);
    REQUIRE(shaded_ledger.water_evaporated_mm == Catch::Approx(open_ledger.water_evaporated_mm * 0.4));
}

// M2: 증산은 요구량만큼, 물이 모자라면 남은 물까지만
TEST_CASE("Transpiration takes the demand but is limited by available water", "[unit][soil_water][m2]") {
    const sim::Params params = test::test_params();
    SECTION("demand below water") {
        sim::SoilCell cell = test::test_cell();
        sim::Ledger ledger;
        sim::process::update_soil_water(cell, params.soil_water, kNightSolar, 0.0, 0.5, ledger);
        REQUIRE(ledger.water_transpired_mm == Catch::Approx(0.5));
    }
    SECTION("demand above water") {
        sim::SoilCell cell = test::test_cell();
        cell.water_mm = 0.2;
        sim::Ledger ledger;
        sim::process::update_soil_water(cell, params.soil_water, kDaySolar, 0.0, 5.0, ledger);
        REQUIRE(cell.water_mm == 0.0);
        REQUIRE(ledger.water_transpired_mm > 0.0);
        REQUIRE(ledger.water_transpired_mm <= 0.2);
        REQUIRE(ledger.water_evaporated_mm + ledger.water_transpired_mm == Catch::Approx(0.2));
    }
}

// M2: 다짐이 심하면 배수가 느리다
TEST_CASE("Compaction slows drainage", "[unit][soil_water][m2]") {
    sim::Params params = test::test_params();
    params.soil_water.evap_base_mm_per_hour = 0.0;
    params.soil_water.evap_mm_per_hour_per_W_per_m2 = 0.0;
    sim::SoilCell loose = test::test_cell();
    sim::SoilCell packed = test::test_cell();
    loose.compaction_index = 0.0;
    packed.compaction_index = 0.9;
    loose.water_mm = loose.field_capacity_mm + 20.0;
    packed.water_mm = packed.field_capacity_mm + 20.0;
    sim::Ledger loose_ledger;
    sim::Ledger packed_ledger;
    sim::process::update_soil_water(loose, params.soil_water, kNightSolar, 0.0, 0.0, loose_ledger);
    sim::process::update_soil_water(packed, params.soil_water, kNightSolar, 0.0, 0.0, packed_ledger);
    REQUIRE(packed.drained_mm_this_tick > 0.0);
    REQUIRE(packed.drained_mm_this_tick < loose.drained_mm_this_tick);
}

// M2: 포화 초과분은 같은 틱에 배수되고 용탈에 반영된다
TEST_CASE("Water above saturation drains in the same tick and is leached", "[unit][soil_water][m2]") {
    sim::Params params = test::test_params();
    params.soil_water.evap_base_mm_per_hour = 0.0;
    params.soil_water.evap_mm_per_hour_per_W_per_m2 = 0.0;
    sim::SoilCell cell = test::test_cell();
    sim::Ledger ledger;
    const double initial = cell.water_mm;
    sim::process::apply_irrigation(cell, 100.0, params.aeration, ledger);
    REQUIRE(cell.water_mm > cell.saturation_mm);
    const double no3_before = cell.no3_N_g_per_m2;
    sim::process::update_soil_water(cell, params.soil_water, kNightSolar, 0.0, 0.0, ledger);
    REQUIRE(cell.water_mm <= cell.saturation_mm);
    REQUIRE(cell.drained_mm_this_tick >= initial + 100.0 - cell.saturation_mm);
    REQUIRE(water_balance_error(cell, initial, ledger) < 1e-12);
    sim::process::leach(cell, params.leaching, params.soil_chemistry, ledger);
    REQUIRE(cell.no3_N_g_per_m2 < no3_before);
}

// M2: 관수는 다짐을 늘린다 (1에서 멈춤)
TEST_CASE("Irrigation increases compaction up to one", "[unit][soil_water][m2]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::Ledger ledger;
    const double before = cell.compaction_index;
    sim::process::apply_irrigation(cell, 10.0, params.aeration, ledger);
    REQUIRE(cell.compaction_index == Catch::Approx(before + 10.0 * params.aeration.compaction_per_mm_irrigation));
    sim::process::apply_irrigation(cell, 1.0e6, params.aeration, ledger);
    REQUIRE(cell.compaction_index == 1.0);
}
