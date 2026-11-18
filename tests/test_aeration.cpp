// 패키지 1 단위 테스트: 통기 지수와 경운 (docs/spec/aeration.md "테스트 (단위)")
#include <algorithm>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sim/processes.hpp"
#include "test_helpers.hpp"

namespace {

// 유기 C가 없고 다짐 0인 셀 (상한 = 1)
sim::SoilCell clean_cell() {
    sim::SoilCell cell = test::test_cell();
    cell.labile_C_g_per_m2 = 0.0;
    cell.stable_C_g_per_m2 = 0.0;
    cell.compaction_index = 0.0;
    return cell;
}

}  // namespace

// 수분 <= 포장용수량, 다짐 0: 통기 지수 = 상한(1)
TEST_CASE("At or below field capacity with no compaction aeration equals the cap", "[unit][aeration]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = clean_cell();
    for (const double water : {0.0, 20.0, cell.field_capacity_mm}) {
        cell.water_mm = water;
        REQUIRE(sim::process::aeration_index(cell, params.aeration) == Catch::Approx(1.0));
    }
}

// 수분이 포화로 갈수록 통기 지수 단조 감소, 포화에서 0
TEST_CASE("Aeration decreases monotonically to zero at saturation", "[unit][aeration]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = clean_cell();
    double prev = 1.0;
    for (double w = cell.field_capacity_mm; w <= cell.saturation_mm; w += 2.5) {
        cell.water_mm = w;
        const double a = sim::process::aeration_index(cell, params.aeration);
        REQUIRE(a <= prev);
        REQUIRE(a >= 0.0);
        prev = a;
    }
    cell.water_mm = cell.saturation_mm;
    REQUIRE(sim::process::aeration_index(cell, params.aeration) == 0.0);
    cell.water_mm = cell.saturation_mm + 50.0;  // 포화 위도 0 아래로 안 감
    REQUIRE(sim::process::aeration_index(cell, params.aeration) == 0.0);
}

// 같은 수분, 다짐만 증가: 통기 지수 감소 또는 같음 (물이 적어도 다지면 낮아짐)
TEST_CASE("More compaction never raises aeration", "[unit][aeration]") {
    const sim::Params params = test::test_params();
    for (const double water : {30.0, 80.0}) {
        sim::SoilCell cell = clean_cell();
        cell.water_mm = water;
        double prev = 2.0;
        for (double c = 0.0; c <= 1.0; c += 0.1) {
            cell.compaction_index = c;
            const double a = sim::process::aeration_index(cell, params.aeration);
            REQUIRE(a <= prev);
            prev = a;
        }
    }
    sim::SoilCell packed = clean_cell();
    packed.water_mm = 30.0;
    packed.compaction_index = 1.0;
    REQUIRE(sim::process::aeration_index(packed, params.aeration) < 1.0);
}

// 같은 다짐, 유기 C 증가: 통기 지수 증가 또는 같음
TEST_CASE("More organic C never lowers aeration", "[unit][aeration]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = clean_cell();
    cell.water_mm = 30.0;
    cell.compaction_index = 0.8;
    double prev = -1.0;
    for (const double carbon : {0.0, 500.0, 1500.0, 3600.0, 10000.0}) {
        cell.stable_C_g_per_m2 = carbon;
        const double a = sim::process::aeration_index(cell, params.aeration);
        REQUIRE(a >= prev);
        REQUIRE(a <= 1.0);
        prev = a;
    }
    cell.stable_C_g_per_m2 = 0.0;
    const double without = sim::process::aeration_index(cell, params.aeration);
    cell.stable_C_g_per_m2 = 3600.0;
    REQUIRE(sim::process::aeration_index(cell, params.aeration) > without);
}

// 관수 반복: 다짐 단조 증가, 1 이하
TEST_CASE("Repeated irrigation raises compaction monotonically up to one", "[unit][aeration]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = clean_cell();
    sim::Ledger ledger;
    double prev = cell.compaction_index;
    for (int i = 0; i < 500; ++i) {
        sim::process::apply_irrigation(cell, 10.0, params.aeration, ledger);
        REQUIRE(cell.compaction_index >= prev);
        REQUIRE(cell.compaction_index <= 1.0);
        prev = cell.compaction_index;
    }
    REQUIRE(cell.compaction_index > clean_cell().compaction_index);
}

// 경운: 다짐 = tilled 값, 작물이 있으면 뿌리 건강도 감소
TEST_CASE("Tilling resets compaction and damages roots of a living crop", "[unit][aeration]") {
    const sim::Params params = test::test_params();

    SECTION("with a living crop") {
        sim::SoilCell cell = test::test_cell();
        cell.compaction_index = 0.9;
        sim::Crop crop;
        crop.present = true;
        crop.alive = true;
        crop.root_health = 0.8;
        sim::process::apply_till(cell, crop, params.aeration);
        REQUIRE(cell.compaction_index == params.aeration.tilled_compaction_index);
        REQUIRE(crop.root_health == Catch::Approx(0.8 * (1.0 - params.aeration.till_root_damage_fraction)));
        REQUIRE(crop.root_health < 0.8);
    }
    SECTION("without a crop") {
        sim::SoilCell cell = test::test_cell();
        cell.compaction_index = 0.9;
        sim::Crop crop;
        crop.root_health = 0.8;  // present = false: 건드리지 않아요
        sim::process::apply_till(cell, crop, params.aeration);
        REQUIRE(cell.compaction_index == params.aeration.tilled_compaction_index);
        REQUIRE(crop.root_health == 0.8);
    }
    SECTION("dead crop") {
        sim::SoilCell cell = test::test_cell();
        sim::Crop crop;
        crop.present = true;
        crop.alive = false;
        crop.root_health = 0.8;
        sim::process::apply_till(cell, crop, params.aeration);
        REQUIRE(crop.root_health == 0.8);
    }
}

// 다짐 증가: 같은 초과 수분에서 배수량 감소
TEST_CASE("More compaction drains less from the same excess water", "[unit][aeration]") {
    const sim::Params params = test::test_params();
    double prev = 1.0e9;
    for (const double c : {0.0, 0.3, 0.6, 1.0}) {
        sim::SoilCell cell = test::test_cell();
        cell.compaction_index = c;
        cell.water_mm = cell.field_capacity_mm + 20.0;
        sim::Ledger ledger;
        sim::process::update_soil_water(cell, params.soil_water, 0.0, 0.0, 0.0, ledger);
        REQUIRE(cell.drained_mm_this_tick <= prev);
        prev = cell.drained_mm_this_tick;
    }
}

// 포화 초과 관수: 초과분이 같은 틱에 배수
TEST_CASE("Irrigation beyond saturation drains the excess in the same tick", "[unit][aeration]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::Ledger ledger;
    sim::process::apply_irrigation(cell, 90.0, params.aeration, ledger);
    const double over_mm = cell.water_mm - cell.saturation_mm;
    REQUIRE(over_mm > 0.0);
    sim::process::update_soil_water(cell, params.soil_water, 0.0, 0.0, 0.0, ledger);
    REQUIRE(cell.drained_mm_this_tick >= over_mm);
    REQUIRE(cell.water_mm <= cell.saturation_mm);
}
