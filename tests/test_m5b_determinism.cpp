// M5b 결정론·저장 복원 테스트 (foliar-disease 9장 "결정론" 행, house-humidity 8장 "결정론", ADR-0004, RngStream::Foliar)
// 1. 같은 시드: 측정 창까지 전체 길이로 두 번 돌려 하루마다 상태 해시, 끝 상태(직렬화 바이트)가 같아요.
//    - s26_closed 전체 3456틱 (측정 창 12/1~1/31의 잿빛곰팡이 썩음이 Foliar 난수를 씀)
//    - s31_predator 방사 뒤 6주까지 2808틱 (천적 탐색·포식)
//    - s28_overhead + 발병 즉시 제거(sim-cli --remove-on-onset anthracnose) 2160틱 (RemovePlant가 이벤트로 들어감)
//    (test_m5b_foliar_world.cpp의 "M5b scenarios are deterministic"이 S26 62일, S28 90일, S31 45일, s29_standard 263일의
//     끝 해시를 이미 비교해요. 여기서는 측정 창과 방사·제거 구간을 더해요.)
// 2. 도중 저장·복원: 저장 → 복원 → 이어 돌린 결과가 중단 없는 실행과 같아요 (M5a 저장 테스트와 같은 방식).
//    습도 상태·잎 젖음·지상부 병해·응애·천적·점적 미설치·대기 중인 M5b 명령 4종을 덮어요.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

constexpr std::uint64_t kDay = 24;
constexpr std::uint64_t kHashEvery = 24;

sim::Params load_repo_params() {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    return std::move(*params);
}

// 월드 + 일정표 + 다음 일정 위치 + (선택) 발병 즉시 제거
struct Driver {
    sim::World world;
    std::vector<simdata::ScheduledCommand> schedule;
    std::size_t next = 0;
    std::optional<sim::FoliarDisease> remove_on_onset;
    std::vector<sim::Command> removals;

    void enqueue_due(std::uint64_t tick) {
        for (sim::Command& c : removals) {
            (void)world.enqueue(std::move(c));  // sim-cli와 같이 일정표보다 먼저, 빈 칸 거부는 무시
        }
        removals.clear();
        while (next < schedule.size() && schedule[next].tick == tick) {
            REQUIRE(world.enqueue(schedule[next].command).has_value());
            ++next;
        }
    }
    void step_and_collect() {
        world.step(1);
        if (!remove_on_onset) {
            return;
        }
        const int cols = world.scenario().bed_cols;
        for (const sim::Event& e : world.events()) {
            if (const auto* onset = std::get_if<sim::FoliarDiseaseOnsetEvent>(&e.payload)) {
                if (onset->disease == *remove_on_onset) {
                    const int r = static_cast<int>(onset->tile_index) / cols;
                    const int c = static_cast<int>(onset->tile_index) % cols;
                    removals.emplace_back(sim::RemovePlantCommand{.area = {.row = r, .col = c, .rows = 1, .cols = 1}});
                }
            }
        }
    }
    void run_tick(std::uint64_t tick) {
        enqueue_due(tick);
        step_and_collect();
    }
};

// relative = "m5b/s26_closed" 같은 data/scenarios/ 아래 경로
Driver make_driver(const std::string& relative, std::optional<sim::FoliarDisease> remove_on_onset = std::nullopt) {
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/" + relative + ".toml"));
    REQUIRE(bundle.has_value());
    std::stable_sort(bundle->schedule.begin(), bundle->schedule.end(),
                     [](const auto& a, const auto& b) { return a.tick < b.tick; });
    auto world = sim::World::create(load_repo_params(), std::move(bundle->scenario));
    REQUIRE(world.has_value());
    return Driver{std::move(*world), std::move(bundle->schedule), 0, remove_on_onset, {}};
}

sim::Snapshot check_same_seed(const std::string& relative, std::uint64_t ticks,
                              std::optional<sim::FoliarDisease> remove_on_onset = std::nullopt) {
    INFO("scenario " << relative);
    Driver a = make_driver(relative, remove_on_onset);
    Driver b = make_driver(relative, remove_on_onset);
    REQUIRE(a.world.state_hash() == b.world.state_hash());
    for (std::uint64_t t = 0; t < ticks; ++t) {
        a.run_tick(t);
        b.run_tick(t);
        if ((t + 1) % kHashEvery != 0 && t + 1 != ticks) {
            continue;
        }
        if (a.world.state_hash() != b.world.state_hash()) {
            INFO("first mismatch at tick " << (t + 1));
            FAIL("same seed runs diverged");
        }
    }
    CHECK(a.world.serialize() == b.world.serialize());
    const sim::Snapshot sa = a.world.snapshot();
    const sim::Snapshot sb = b.world.snapshot();
    CHECK(sa.house == sb.house);
    CHECK(sa.crops == sb.crops);
    CHECK(sa.ledger == sb.ledger);
    return sa;
}

using Extra = std::function<void(sim::World&)>;

// split 틱 직전에 저장(그 틱의 일정표 명령과 extra 명령을 큐에 넣은 뒤) → 복원 → end까지 진행한 결과가
// 중단 없는 실행과 같아요. 저장 시점 스냅샷을 돌려줘요.
sim::Snapshot check_save_resume(const std::string& relative, std::uint64_t split, std::uint64_t end,
                                const Extra& extra = {}) {
    INFO("scenario " << relative << " split " << split);
    Driver straight = make_driver(relative);
    Driver first = make_driver(relative);
    for (std::uint64_t t = 0; t < split; ++t) {
        straight.run_tick(t);
        first.run_tick(t);
    }
    straight.enqueue_due(split);
    first.enqueue_due(split);
    if (extra) {
        extra(straight.world);
        extra(first.world);
    }
    const sim::Snapshot at_save = first.world.snapshot();
    const std::vector<std::byte> bytes = first.world.serialize();
    auto restored = sim::World::deserialize(bytes);
    REQUIRE(restored.has_value());
    CHECK(restored->state_hash() == first.world.state_hash());
    CHECK(restored->serialize() == bytes);  // 다시 저장해도 같은 바이트 (Foliar 난수 스트림 포함)
    const sim::Snapshot restored_snap = restored->snapshot();
    CHECK(restored_snap.house == at_save.house);
    CHECK(restored_snap.crops == at_save.crops);
    CHECK(restored_snap.soil == at_save.soil);
    CHECK(restored_snap.ledger == at_save.ledger);
    CHECK(restored_snap.pending_command_count == at_save.pending_command_count);
    Driver resumed{std::move(*restored), first.schedule, first.next, std::nullopt, {}};

    for (std::uint64_t t = split; t < end; ++t) {
        if (t == split) {
            straight.step_and_collect();  // 명령은 이미 넣었어요
            resumed.step_and_collect();
        } else {
            straight.run_tick(t);
            resumed.run_tick(t);
        }
        if ((t + 1) % 12 != 0) {
            continue;
        }
        if (straight.world.state_hash() != resumed.world.state_hash()) {
            INFO("first mismatch at tick " << (t + 1));
            FAIL("resumed run diverged from the uninterrupted run");
        }
    }
    CHECK(resumed.world.state_hash() == straight.world.state_hash());
    CHECK(resumed.world.serialize() == straight.world.serialize());
    CHECK(resumed.world.snapshot().crops == straight.world.snapshot().crops);
    CHECK(resumed.world.snapshot().house == straight.world.snapshot().house);
    return at_save;
}

double crop_sum(const sim::Snapshot& s, double sim::Crop::*field) {
    double v = 0.0;
    for (const sim::Crop& c : s.crops) {
        v += c.*field;
    }
    return v;
}

}  // namespace

// 같은 시드: s26_closed 전체 (측정 창의 잿빛곰팡이 썩음 = Foliar 난수)
TEST_CASE("M5b S26 closed full season same seed gives the same hash every day", "[determinism][m5b][s26]") {
    const sim::Snapshot end = check_same_seed("m5b/s26_closed", 3456);
    CHECK(end.ledger.fruit_rot_count > 0);
}

// 같은 시드: s31_predator 방사 뒤 6주까지 (천적 탐색·포식)
TEST_CASE("M5b S31 predator release same seed gives the same hash every day", "[determinism][m5b][s31]") {
    const sim::Snapshot end = check_same_seed("m5b/s31_predator", 1800 + 6 * 7 * kDay);
    CHECK(end.ledger.predators_released_per_m2 > 0.0);
    CHECK(crop_sum(end, &sim::Crop::predator_mite_index) > 0.0);
}

// 같은 시드: s28_overhead + 발병 즉시 제거 (RemovePlant가 이벤트에 따라 들어가요)
TEST_CASE("M5b S28 removal on onset same seed gives the same hash every day", "[determinism][m5b][s28]") {
    const sim::Snapshot end = check_same_seed("m5b/s28_overhead_remove", 90 * kDay, sim::FoliarDisease::Anthracnose);
    CHECK(end.ledger.plants_removed > 0);
}

// 저장·복원: 두상 관수 하우스(점적 미설치, 등급 C, 탄저병 진행 중) 30일째 08시 관수 틱에, 대기 중인 M5b 명령 4종
// (InstallDrip, SetFan, ReleasePredators, RemovePlant)을 넣은 채 저장 → 20일 이어 돌림
TEST_CASE("M5b save with pending M5b commands on an overhead irrigated house equals an uninterrupted run",
          "[determinism][m5b][serialization][s28]") {
    const auto commands = [](sim::World& w) {
        const int rows = w.scenario().bed_rows;
        const int cols = w.scenario().bed_cols;
        REQUIRE(w.enqueue(sim::InstallDripCommand{}).has_value());
        REQUIRE(w.enqueue(sim::SetFanCommand{.on = true}).has_value());
        REQUIRE(w.enqueue(sim::ReleasePredatorsCommand{.area = sim::whole_grid(rows, cols), .predator_id = "persimilis",
                                                       .per_m2 = 15.0})
                    .has_value());
        REQUIRE(w.enqueue(sim::RemovePlantCommand{.area = {.row = 0, .col = 0, .rows = 1, .cols = 1}}).has_value());
    };
    const sim::Snapshot at_save = check_save_resume("m5b/s28_overhead", 30 * kDay + 8, 50 * kDay, commands);
    CHECK_FALSE(at_save.house.drip_installed);
    CHECK(at_save.pending_command_count >= 5);  // 일정표 관수 + 4종
    CHECK(crop_sum(at_save, &sim::Crop::infection_anthracnose_index) > 0.0);
    CHECK(at_save.house.anthracnose_sporulation_index >= 0.0);
}

// 저장·복원: s27_closed_fan 측정 창 5일째 밤 23시(팬 가동, 잎 젖음 진행 중) → 10일 이어 돌림
TEST_CASE("M5b save during a wet fan night and continue equals an uninterrupted run",
          "[determinism][m5b][serialization][s27]") {
    const sim::Snapshot at_save = check_save_resume("m5b/s27_closed_fan", 1968 + 5 * kDay + 23, 1968 + 15 * kDay);
    CHECK(at_save.house.fan_on);
    CHECK(at_save.house.fan_hours > 0.0);
    CHECK(at_save.house.botrytis_spore_index > 0.0);
    CHECK(crop_sum(at_save, &sim::Crop::wet_run_hours) > 0.0);
    CHECK(crop_sum(at_save, &sim::Crop::infection_botrytis_index) > 0.0);
}

// 저장·복원: s31_predator 방사 1주 뒤(천적·응애 진행 중) → 3주 이어 돌림
TEST_CASE("M5b save after a predator release and continue equals an uninterrupted run",
          "[determinism][m5b][serialization][s31]") {
    const sim::Snapshot at_save = check_save_resume("m5b/s31_predator", 1800 + 7 * kDay, 1800 + 28 * kDay);
    CHECK(crop_sum(at_save, &sim::Crop::predator_mite_index) > 0.0);
    CHECK(crop_sum(at_save, &sim::Crop::spider_mite_index) > 0.0);
    CHECK(at_save.house.spider_mite_mean_index > 0.0);
}
