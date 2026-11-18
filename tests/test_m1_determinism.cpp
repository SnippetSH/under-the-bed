// 실제 M1 프로세스를 켠 상태의 결정론 테스트 (ADR-0004)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

struct Run {
    sim::World world;
    std::vector<simdata::ScheduledCommand> schedule;
};

Run make_run(const std::string& name) {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m1/" + name + ".toml"));
    REQUIRE(bundle.has_value());
    auto world = sim::World::create(std::move(*params), std::move(bundle->scenario));
    REQUIRE(world.has_value());
    return Run{std::move(*world), std::move(bundle->schedule)};
}

// from 틱부터 to 틱 직전까지 진행해요. 일정표 명령은 해당 틱 직전에 넣어요.
void drive(sim::World& world, const std::vector<simdata::ScheduledCommand>& schedule, std::uint64_t from,
           std::uint64_t to, std::vector<std::uint64_t>* hashes = nullptr) {
    for (std::uint64_t t = from; t < to; ++t) {
        for (const auto& item : schedule) {
            if (item.tick == t) {
                REQUIRE(world.enqueue(item.command).has_value());
            }
        }
        world.step(1);
        if (hashes != nullptr) {
            hashes->push_back(world.state_hash());
        }
    }
}

}  // namespace

// 같은 시나리오를 두 번 돌리면 매 틱 상태 해시가 같아요 (21일).
TEST_CASE("M1 immature compost scenario is deterministic every tick", "[determinism][m1]") {
    constexpr std::uint64_t kTicks = 21 * 24;
    std::vector<std::uint64_t> a;
    std::vector<std::uint64_t> b;
    {
        Run run = make_run("s1_immature");
        a.push_back(run.world.state_hash());
        drive(run.world, run.schedule, 0, kTicks, &a);
    }
    {
        Run run = make_run("s1_immature");
        b.push_back(run.world.state_hash());
        drive(run.world, run.schedule, 0, kTicks, &b);
    }
    REQUIRE(a.size() == kTicks + 1);
    CHECK(a == b);
}

// 틱 100에서 저장 후 복원하고 틱 504까지 진행한 결과가 중단 없는 실행과 같아요.
TEST_CASE("M1 save at tick 100 and continue equals an uninterrupted run", "[determinism][m1][serialization]") {
    constexpr std::uint64_t kSplit = 100;
    constexpr std::uint64_t kTotal = 504;

    Run continuous = make_run("s1_immature");
    Run saved = make_run("s1_immature");
    drive(continuous.world, continuous.schedule, 0, kTotal);
    drive(saved.world, saved.schedule, 0, kSplit);

    auto resumed = sim::World::deserialize(saved.world.serialize());
    REQUIRE(resumed.has_value());
    drive(*resumed, saved.schedule, kSplit, kTotal);

    CHECK(resumed->state_hash() == continuous.world.state_hash());
    CHECK(resumed->serialize() == continuous.world.serialize());
}
