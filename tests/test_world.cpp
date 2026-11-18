#include <cmath>
#include <cstddef>
#include <limits>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "test_helpers.hpp"

using test::make_world;
using test::must_enqueue;
using test::test_params;
using test::test_scenario;

TEST_CASE("World::create validates params and scenario", "[unit][world]") {
    SECTION("valid input") {
        CHECK(sim::World::create(test_params(), test_scenario()).has_value());
    }
    SECTION("unsupported params schema") {
        sim::Params p = test_params();
        p.schema_version = 999;
        auto w = sim::World::create(p, test_scenario());
        REQUIRE_FALSE(w.has_value());
        CHECK(w.error().code == sim::ErrorCode::UnsupportedVersion);
    }
    SECTION("start day out of range") {
        CHECK_FALSE(sim::World::create(test_params(), test_scenario(0)).has_value());
        CHECK_FALSE(sim::World::create(test_params(), test_scenario(366)).has_value());
    }
    SECTION("empty grid") {
        CHECK_FALSE(sim::World::create(test_params(), test_scenario(1, 0, 3)).has_value());
        CHECK_FALSE(sim::World::create(test_params(), test_scenario(1, 2, -1)).has_value());
    }
    SECTION("weather table with the wrong size") {
        sim::Scenario s = test_scenario();
        s.weather_hourly.pop_back();
        CHECK_FALSE(sim::World::create(test_params(), s).has_value());
    }
    SECTION("non-finite or out-of-range weather") {
        sim::Scenario nan = test_scenario();
        nan.weather_hourly[100].outside_air_temp_degC = std::numeric_limits<double>::quiet_NaN();
        CHECK_FALSE(sim::World::create(test_params(), nan).has_value());

        sim::Scenario negative_solar = test_scenario();
        negative_solar.weather_hourly[5].solar_radiation_W_per_m2 = -1.0;
        CHECK_FALSE(sim::World::create(test_params(), negative_solar).has_value());

        sim::Scenario wet = test_scenario();
        wet.weather_hourly[7].outside_rh_percent = 100.5;
        CHECK_FALSE(sim::World::create(test_params(), wet).has_value());
    }
}

TEST_CASE("Snapshot reports time and the matching weather row", "[unit][world]") {
    sim::World world = make_world(test_scenario(365, 2, 3));

    sim::Snapshot s0 = world.snapshot();
    CHECK(s0.tick == 0);
    CHECK(s0.time == sim::CalendarTime{0, 365, 0});
    CHECK(s0.weather == world.scenario().weather_hourly[sim::weather_index(365, 0)]);
    CHECK(s0.soil.size() == 6);
    CHECK(s0.bed_rows == 2);
    CHECK(s0.bed_cols == 3);

    world.step(24 + 5);  // 다음 해 1일 5시
    sim::Snapshot s1 = world.snapshot();
    CHECK(s1.tick == 29);
    CHECK(s1.time == sim::CalendarTime{1, 1, 5});
    CHECK(s1.weather == world.scenario().weather_hourly[sim::weather_index(1, 5)]);
}

TEST_CASE("Commands apply at the start of the next tick, in order", "[unit][world][command]") {
    sim::World world = make_world();

    const std::uint64_t first = test::must_enqueue(world, sim::NoopCommand{});
    const std::uint64_t second = test::must_enqueue(world, sim::NoopCommand{});
    CHECK(first == 0);
    CHECK(second == 1);
    CHECK(world.snapshot().pending_command_count == 2);
    CHECK(world.events().empty());  // 투입만으로는 적용되지 않아요

    world.step(1);
    CHECK(world.snapshot().pending_command_count == 0);
    REQUIRE(world.events().size() == 2);
    const auto& e0 = world.events()[0];
    const auto& e1 = world.events()[1];
    CHECK(e0.tick == 0);
    CHECK(std::get<sim::CommandAppliedEvent>(e0.payload).command_seq == first);
    CHECK(e1.tick == 0);
    CHECK(std::get<sim::CommandAppliedEvent>(e1.payload).command_seq == second);
}

TEST_CASE("Events cover only the last step call", "[unit][world][event]") {
    sim::World world = make_world();
    test::must_enqueue(world, sim::NoopCommand{});
    world.step(3);
    CHECK(world.events().size() == 1);

    world.step(1);
    CHECK(world.events().empty());

    world.step(2);
    test::must_enqueue(world, sim::NoopCommand{});
    world.step(2);
    REQUIRE(world.events().size() == 1);
    CHECK(world.events()[0].tick == 6);  // 틱 0..5를 지난 뒤 처리된 첫 틱

    world.step(0);
    CHECK(world.events().empty());
}

TEST_CASE("State hash changes as the world advances", "[unit][world]") {
    sim::World world = make_world();
    const std::uint64_t h0 = world.state_hash();
    world.step(1);
    CHECK(world.state_hash() != h0);
}

TEST_CASE("enqueue validates commands before queueing", "[unit][world][command]") {
    sim::World world = make_world(test_scenario(1, 2, 3));
    const sim::TileArea all = sim::whole_grid(2, 3);

    CHECK(world.enqueue(sim::IrrigateCommand{all, 10.0}).has_value());
    CHECK(world.enqueue(sim::AddCompostCommand{all, "immature_manure", 3.0}).has_value());
    CHECK(world.enqueue(sim::AddFertilizerCommand{all, "urea", 20.0}).has_value());
    CHECK(world.enqueue(sim::IrrigateCommand{{1, 2, 1, 1}, 0.0}).has_value());  // 마지막 칸, 0 mm 허용

    const std::size_t queued = world.snapshot().pending_command_count;
    CHECK_FALSE(world.enqueue(sim::IrrigateCommand{all, -1.0}).has_value());
    CHECK_FALSE(world.enqueue(sim::IrrigateCommand{{0, 0, 3, 3}, 1.0}).has_value());   // 행 초과
    CHECK_FALSE(world.enqueue(sim::IrrigateCommand{{0, 2, 1, 2}, 1.0}).has_value());   // 열 초과
    CHECK_FALSE(world.enqueue(sim::IrrigateCommand{{0, 0, 0, 1}, 1.0}).has_value());   // 빈 영역
    CHECK_FALSE(world.enqueue(sim::IrrigateCommand{{-1, 0, 1, 1}, 1.0}).has_value());  // 음수 위치
    CHECK_FALSE(world.enqueue(sim::AddCompostCommand{all, "no_such_compost", 1.0}).has_value());
    CHECK_FALSE(world.enqueue(sim::AddFertilizerCommand{all, "no_such_fertilizer", 1.0}).has_value());
    CHECK_FALSE(world.enqueue(sim::AddFertilizerCommand{all, "urea",
                                                        std::numeric_limits<double>::infinity()}).has_value());
    CHECK(world.snapshot().pending_command_count == queued);  // 거부된 명령은 큐에 없어요
}

TEST_CASE("World::create validates M1 parameters and soil", "[unit][world]") {
    SECTION("bad curve") {
        sim::Params p = test_params();
        p.nitrification.step2_ph_curve.points = {{{7.0, 1.0}}};
        CHECK_FALSE(sim::World::create(p, test_scenario()).has_value());
    }
    SECTION("fraction out of range") {
        sim::Params p = test_params();
        p.organic_matter.humification_fraction = 1.5;
        CHECK_FALSE(sim::World::create(p, test_scenario()).has_value());
    }
    SECTION("duplicate compost id") {
        sim::Params p = test_params();
        p.composts.push_back(p.composts.front());
        CHECK_FALSE(sim::World::create(p, test_scenario()).has_value());
    }
    SECTION("non-positive field capacity") {
        sim::Scenario s = test_scenario();
        s.soil.field_capacity_mm = 0.0;
        CHECK_FALSE(sim::World::create(test_params(), s).has_value());
    }
    SECTION("negative initial pool") {
        sim::Scenario s = test_scenario();
        s.soil.no3_N_g_per_m2 = -0.1;
        CHECK_FALSE(sim::World::create(test_params(), s).has_value());
    }
}

TEST_CASE("Snapshot starts from the scenario soil", "[unit][world]") {
    sim::World world = make_world();
    const sim::Snapshot s = world.snapshot();
    // 파생값(통기 지수, EC)은 생성 시 프로세스 함수로 계산돼요
    sim::SoilCell expected = test::test_cell();
    expected.aeration_index = sim::process::aeration_index(expected, world.params().aeration);
    expected.ec_dS_per_m = sim::process::soil_ec_dS_per_m(expected, world.params());
    for (const sim::SoilCell& cell : s.soil) {
        CHECK(cell == expected);
    }
    CHECK(s.ledger == sim::Ledger{});
}

// M3 (cation-exchange.md "초기화 순서"): 월드 생성 시 통기 지수를 먼저 계산한 뒤 용액을 평형값으로 채워요 (완화 없이 한 번에)
TEST_CASE("World creation fills the solution state at equilibrium", "[unit][world][m3]") {
    sim::World world = make_world();
    const sim::Snapshot s = world.snapshot();
    sim::SoilCell reference = test::test_cell();
    const sim::process::ExchangeEquilibrium eq = sim::process::exchange_equilibrium(reference, world.params());
    for (const sim::SoilCell& cell : s.soil) {
        CHECK(cell.nh4_solution_N_g_per_m2 == eq.nh4_solution_N_g_per_m2);
        CHECK(cell.k_solution_g_per_m2 == eq.k_solution_g_per_m2);
        CHECK(cell.ca_solution_g_per_m2 == eq.ca_solution_g_per_m2);
        CHECK(cell.mg_solution_g_per_m2 == eq.mg_solution_g_per_m2);
        CHECK(cell.mn_solution_g_per_m2 == eq.mn_solution_g_per_m2);
        CHECK(cell.cec_eq_per_m2 == eq.cec_eq_per_m2);
        CHECK(cell.exchange_free_site_index == eq.exchange_free_site_index);
        // 불변 조건: 0 ≤ 용액 ≤ 총량
        CHECK(cell.k_solution_g_per_m2 > 0.0);
        CHECK(cell.k_solution_g_per_m2 <= cell.k_g_per_m2);
        CHECK(cell.mn_solution_g_per_m2 <= cell.mn_g_per_m2);
    }
}

// 세이브 로드는 저장된 용액 상태를 그대로 쓰고 평형을 다시 구하지 않아요
TEST_CASE("Deserializing keeps a non-equilibrium solution state", "[unit][world][m3]") {
    sim::World world = make_world();
    const sim::TileArea area = sim::whole_grid(2, 3);
    test::must_enqueue(world, sim::AddFertilizerCommand{area, "potassium_sulfate", 300.0});
    world.step(1);  // 투입 직후라 평형과 다른 용액 상태
    const sim::Snapshot before = world.snapshot();
    const sim::process::ExchangeEquilibrium eq =
        sim::process::exchange_equilibrium(before.soil.front(), world.params());
    REQUIRE(before.soil.front().k_solution_g_per_m2 > eq.k_solution_g_per_m2 * 1.01);  // 평형이 아님

    const auto bytes = world.serialize();
    auto loaded = sim::World::deserialize(bytes);
    REQUIRE(loaded.has_value());
    const sim::Snapshot after = loaded->snapshot();
    REQUIRE(after.soil.size() == before.soil.size());
    for (std::size_t i = 0; i < before.soil.size(); ++i) {
        CHECK(after.soil[i] == before.soil[i]);
    }
    CHECK(loaded->state_hash() == world.state_hash());
}
