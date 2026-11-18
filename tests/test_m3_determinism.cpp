// M3 프로세스(양이온 교환, 길항, 인산 고정, 망간)를 켠 상태의 결정론 테스트 (ADR-0004)
// 저장/복원은 평형 재계산 없이 이어져야 해요 (용액 상태가 세이브에 들어 있음).

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
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m3/" + name + ".toml"));
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

constexpr std::uint64_t kTicks = 14 * 24;

void check_twice_identical(const std::string& name) {
    std::vector<std::uint64_t> a;
    std::vector<std::uint64_t> b;
    sim::Snapshot snap_a;
    sim::Snapshot snap_b;
    {
        Run run = make_run(name);
        a.push_back(run.world.state_hash());
        drive(run.world, run.schedule, 0, kTicks, &a);
        snap_a = run.world.snapshot();
    }
    {
        Run run = make_run(name);
        b.push_back(run.world.state_hash());
        drive(run.world, run.schedule, 0, kTicks, &b);
        snap_b = run.world.snapshot();
    }
    REQUIRE(a.size() == kTicks + 1);
    CHECK(a == b);
    CHECK(snap_a.soil == snap_b.soil);
    CHECK(snap_a.crops == snap_b.crops);
    CHECK(snap_a.ledger == snap_b.ledger);
}

void check_save_resume(const std::string& name) {
    constexpr std::uint64_t kSplit = 100;
    Run continuous = make_run(name);
    Run saved = make_run(name);
    drive(continuous.world, continuous.schedule, 0, kTicks);
    drive(saved.world, saved.schedule, 0, kSplit);

    auto resumed = sim::World::deserialize(saved.world.serialize());
    REQUIRE(resumed.has_value());
    drive(*resumed, saved.schedule, kSplit, kTicks);

    CHECK(resumed->state_hash() == continuous.world.state_hash());
    CHECK(resumed->serialize() == continuous.world.serialize());
}

}  // namespace

// 같은 시나리오를 두 번 돌리면 매 틱 상태 해시가 같아요 (14일). 스냅샷도 같아요.
TEST_CASE("M3 K excess scenario is deterministic every tick", "[determinism][m3]") {
    check_twice_identical("s5_k_excess");
}

TEST_CASE("M3 sand scenario is deterministic every tick", "[determinism][m3]") {
    check_twice_identical("s6_sand");
}

TEST_CASE("M3 manganese acid scenario is deterministic every tick", "[determinism][m3]") {
    check_twice_identical("s11_acid");
}

TEST_CASE("M3 phosphorus scenario is deterministic every tick", "[determinism][m3]") {
    check_twice_identical("s12_neutral");
}

// 틱 100에서 저장 후 복원하고 끝까지 진행한 결과가 중단 없는 실행과 같아요.
TEST_CASE("M3 K excess save at tick 100 and continue equals an uninterrupted run",
          "[determinism][m3][serialization]") {
    check_save_resume("s5_k_excess");
}

TEST_CASE("M3 sand save at tick 100 and continue equals an uninterrupted run", "[determinism][m3][serialization]") {
    check_save_resume("s6_sand");
}

TEST_CASE("M3 manganese acid save at tick 100 and continue equals an uninterrupted run",
          "[determinism][m3][serialization]") {
    check_save_resume("s11_acid");
}

TEST_CASE("M3 phosphorus save at tick 100 and continue equals an uninterrupted run",
          "[determinism][m3][serialization]") {
    check_save_resume("s12_neutral");
}

// 저장 직후 복원한 월드는 같은 시점의 스냅샷이 원본과 같고, 복원이 용액 상태를 다시 평형시키지 않아요.
TEST_CASE("M3 load does not re-equilibrate and snapshots match", "[determinism][m3][serialization]") {
    Run run = make_run("s11_acid");
    drive(run.world, run.schedule, 0, 100);
    auto resumed = sim::World::deserialize(run.world.serialize());
    REQUIRE(resumed.has_value());
    CHECK(resumed->state_hash() == run.world.state_hash());
    const sim::Snapshot a = run.world.snapshot();
    const sim::Snapshot b = resumed->snapshot();
    REQUIRE(a.soil.size() == b.soil.size());
    for (std::size_t i = 0; i < a.soil.size(); ++i) {
        CHECK(a.soil[i] == b.soil[i]);
    }
    CHECK(a.ledger == b.ledger);
}
