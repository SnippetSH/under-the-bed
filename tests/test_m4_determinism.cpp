// M4 결정론 테스트 (ADR-0004): 작기(263일)를 같은 시드로 두 번 돌리면 하루마다(24틱) 상태 해시가 같고,
// 작기 도중(과실·꽃이 있는 시점) 저장 후 복원해 이어 돌린 결과가 중단 없는 실행과 같아요 (생물 난수 스트림 포함).
// 시드가 다르면 결과가 달라질 수 있어요 (생물 변이 난수).

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

constexpr std::uint64_t kSeasonTicks = 6312;
constexpr std::uint64_t kHashEvery = 24;  // 해시 비교 간격 (틱)

sim::Params repo_params() {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    return std::move(*params);
}

simdata::ScenarioBundle load_bundle(const std::string& name) {
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m4/" + name + ".toml"));
    REQUIRE(bundle.has_value());
    return std::move(*bundle);
}

// 월드 + 일정표 + 다음에 넣을 일정 위치
struct Driver {
    sim::World world;
    std::vector<simdata::ScheduledCommand> schedule;
    std::size_t next = 0;

    // 이 틱을 처리하기 직전에 넣을 명령을 큐에 넣어요
    void enqueue_due(std::uint64_t tick) {
        while (next < schedule.size() && schedule[next].tick == tick) {
            REQUIRE(world.enqueue(schedule[next].command).has_value());
            ++next;
        }
    }
    void run_tick(std::uint64_t tick) {
        enqueue_due(tick);
        world.step(1);
    }
};

Driver make_driver(const std::string& name, std::uint64_t seed_override = 0) {
    simdata::ScenarioBundle bundle = load_bundle(name);
    if (seed_override != 0) {
        bundle.scenario.seed = seed_override;
    }
    auto world = sim::World::create(repo_params(), std::move(bundle.scenario));
    REQUIRE(world.has_value());
    return Driver{std::move(*world), std::move(bundle.schedule), 0};
}

// 저장 시점 스냅샷에 달린 과실(착과)과 아직 안 핀 꽃이 모두 있는지
bool has_fruits_and_flowers(const sim::Snapshot& snap) {
    bool fruit = false, flower = false;
    for (const sim::Crop& crop : snap.crops) {
        for (const sim::Fruit& f : crop.fruits) {
            fruit = fruit || f.set;
            flower = flower || !f.set;
        }
    }
    return fruit && flower;
}

// 같은 시드로 두 번 돌려 매 틱 해시를 비교해요 (두 월드를 나란히 진행)
void check_same_seed_every_tick(const std::string& name) {
    Driver a = make_driver(name);
    Driver b = make_driver(name);
    REQUIRE(a.world.state_hash() == b.world.state_hash());
    std::uint64_t mismatches = 0;
    for (std::uint64_t t = 0; t < kSeasonTicks; ++t) {
        a.run_tick(t);
        b.run_tick(t);
        // 해시 계산이 비싸서 하루(24틱)마다 비교해요. 사이에서 어긋나면 이후 해시와 끝의 직렬화 바이트가 달라져요.
        if ((t + 1) % kHashEvery != 0 && t + 1 != kSeasonTicks) {
            continue;
        }
        if (a.world.state_hash() != b.world.state_hash()) {
            INFO("first mismatch at tick " << (t + 1));
            ++mismatches;
            break;
        }
    }
    CHECK(mismatches == 0);
    CHECK(a.world.serialize() == b.world.serialize());
    const sim::Snapshot sa = a.world.snapshot();
    const sim::Snapshot sb = b.world.snapshot();
    CHECK(sa.soil == sb.soil);
    CHECK(sa.crops == sb.crops);
    CHECK(sa.house == sb.house);
    CHECK(sa.ledger == sb.ledger);
}

// split 직전(명령을 넣은 뒤 step 전이면 pending_at_save)에 저장 -> 복원 -> 끝까지 진행한 결과가 중단 없는 실행과 같아요
void check_save_resume(const std::string& name, std::uint64_t split, bool save_with_pending) {
    Driver straight = make_driver(name);
    std::vector<std::uint64_t> straight_hashes;  // 틱 split 이후 매 틱 해시
    Driver first = make_driver(name);

    for (std::uint64_t t = 0; t < split; ++t) {
        straight.run_tick(t);
        first.run_tick(t);
    }
    if (save_with_pending) {
        // 저장 틱의 명령을 큐에 넣고 처리하기 전에 저장해요 (대기 명령도 저장·복원돼야 해요)
        straight.enqueue_due(split);
        first.enqueue_due(split);
        REQUIRE(first.world.snapshot().pending_command_count > 0);
    }
    REQUIRE(has_fruits_and_flowers(first.world.snapshot()));

    const std::vector<std::byte> bytes = first.world.serialize();
    auto restored = sim::World::deserialize(bytes);
    REQUIRE(restored.has_value());
    CHECK(restored->state_hash() == first.world.state_hash());
    CHECK(restored->serialize() == bytes);  // 한 번 더 저장해도 같은 바이트 (난수 스트림 포함)
    Driver resumed{std::move(*restored), first.schedule, first.next};

    for (std::uint64_t t = split; t < kSeasonTicks; ++t) {
        if (save_with_pending && t == split) {
            straight.world.step(1);  // 명령은 이미 넣었어요
            resumed.world.step(1);
        } else {
            straight.run_tick(t);
            resumed.run_tick(t);
        }
        if ((t + 1) % 12 != 0) {
            continue;  // 해시는 12틱마다 비교해요 (끝에서는 직렬화 바이트까지 비교)
        }
        if (straight.world.state_hash() != resumed.world.state_hash()) {
            INFO("first mismatch at tick " << (t + 1));
            FAIL("resumed run diverged from the uninterrupted run");
        }
    }
    CHECK(resumed.world.state_hash() == straight.world.state_hash());
    CHECK(resumed.world.serialize() == straight.world.serialize());
}

}  // namespace

// 같은 시드: 기준 작기를 두 번 돌려 하루마다 상태 해시가 같고 끝 상태(직렬화 바이트)가 같아요
TEST_CASE("M4 season_standard same seed gives the same hash every day and final state", "[determinism][m4][season]") {
    check_same_seed_every_tick("season_standard");
}

// 같은 시드: 적과 작기 (적화·적과의 난수 선택 포함)
TEST_CASE("M4 season_thinned same seed gives the same hash every day and final state", "[determinism][m4][season]") {
    check_same_seed_every_tick("season_thinned");
}

// 다른 시드는 생물 변이(수정률, 개화 시점, 크기)가 달라 결과가 달라질 수 있어요 (같을 필요는 없음)
TEST_CASE("M4 different seeds may differ", "[determinism][m4]") {
    constexpr std::uint64_t kTicks = 3000;  // 과실이 달린 뒤까지
    Driver a = make_driver("season_standard");
    Driver b = make_driver("season_standard", 20261119);
    for (std::uint64_t t = 0; t < kTicks; ++t) {
        a.run_tick(t);
        b.run_tick(t);
    }
    CHECK(a.world.state_hash() != b.world.state_hash());
}

// 저장·복원: 작기 5,800틱(과실·꽃이 달린 시점)에서 저장해 이어 돌린 결과가 중단 없는 실행과 같아요
TEST_CASE("M4 season_standard save mid-season and continue equals an uninterrupted run",
          "[determinism][m4][serialization][season]") {
    check_save_resume("season_standard", 5800, false);
}

// 저장·복원: 적과 작기, 적화·적과 명령이 큐에 들어 있는 채(틱 4334) 저장
TEST_CASE("M4 season_thinned save with a pending command and continue equals an uninterrupted run",
          "[determinism][m4][serialization][season]") {
    check_save_resume("season_thinned", 4334, true);
}
