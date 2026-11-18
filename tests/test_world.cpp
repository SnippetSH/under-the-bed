#include <cmath>
#include <limits>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "sim/world.hpp"
#include "test_helpers.hpp"

using test::make_world;
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
    CHECK(s0.weather.outside_air_temp_degC == 365.0);
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

    const std::uint64_t first = world.enqueue(sim::NoopCommand{});
    const std::uint64_t second = world.enqueue(sim::NoopCommand{});
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
    world.enqueue(sim::NoopCommand{});
    world.step(3);
    CHECK(world.events().size() == 1);

    world.step(1);
    CHECK(world.events().empty());

    world.step(2);
    world.enqueue(sim::NoopCommand{});
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
