// 결정론 테스트 (ADR-0004)

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/calendar.hpp"
#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

// 같은 명령 목록을 재현하기 위한 간단한 스크립트: 특정 틱 직전에 명령을 넣어요.
bool has_command_before(std::uint64_t tick) { return tick % 97 == 0 || tick % 1000 == 13; }

sim::World load_empty_scenario_world() {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    auto scenario = simdata::load_scenario(test::repo_path("data/scenarios/empty.toml"));
    REQUIRE(scenario.has_value());
    auto world = sim::World::create(*params, std::move(*scenario));
    REQUIRE(world.has_value());
    return std::move(*world);
}

std::vector<std::uint64_t> run_and_record_hashes(sim::World world, std::uint64_t ticks) {
    std::vector<std::uint64_t> hashes;
    hashes.reserve(static_cast<std::size_t>(ticks) + 1);
    hashes.push_back(world.state_hash());
    for (std::uint64_t t = 0; t < ticks; ++t) {
        if (has_command_before(t)) {
            world.enqueue(sim::NoopCommand{});
        }
        world.step(1);
        hashes.push_back(world.state_hash());
    }
    return hashes;
}

}  // namespace

TEST_CASE("M0: empty scenario runs one year with identical hashes every tick",
          "[determinism][m0]") {
    const auto ticks = static_cast<std::uint64_t>(sim::kHoursPerYear);
    const std::vector<std::uint64_t> a = run_and_record_hashes(load_empty_scenario_world(), ticks);
    const std::vector<std::uint64_t> b = run_and_record_hashes(load_empty_scenario_world(), ticks);
    REQUIRE(a.size() == ticks + 1);
    REQUIRE(a == b);
}

TEST_CASE("Save, load, continue equals an uninterrupted run", "[determinism][serialization]") {
    constexpr std::uint64_t kSplit = 1000;
    constexpr std::uint64_t kTotal = sim::kHoursPerYear;

    auto drive = [](sim::World& w, std::uint64_t from, std::uint64_t to) {
        for (std::uint64_t t = from; t < to; ++t) {
            if (has_command_before(t)) {
                w.enqueue(sim::NoopCommand{});
            }
            w.step(1);
        }
    };

    // 두 월드를 같은 지점까지 진행하고, 대기 중인 명령도 하나씩 넣어 둬요 (세이브에 들어가야 해요).
    sim::World continuous = load_empty_scenario_world();
    sim::World saved = load_empty_scenario_world();
    drive(continuous, 0, kSplit);
    drive(saved, 0, kSplit);
    continuous.enqueue(sim::NoopCommand{});
    saved.enqueue(sim::NoopCommand{});

    auto resumed = sim::World::deserialize(saved.serialize());
    REQUIRE(resumed.has_value());
    drive(*resumed, kSplit, kTotal);
    drive(continuous, kSplit, kTotal);

    CHECK(resumed->state_hash() == continuous.state_hash());
    CHECK(resumed->serialize() == continuous.serialize());
}
