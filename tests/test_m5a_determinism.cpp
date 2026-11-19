// M5a 결정론 테스트 (docs/spec/m5-overview.md 9장 "결정론" 행, ADR-0004, soil-microbes 5번 RngStream::Disease)
// 1. 같은 시드: S20(4종), S22(4종), 시들음병·역병 오염 흙의 season_standard를 두 번 돌리면 하루마다 상태 해시가 같고
//    끝 상태(직렬화 바이트)가 같아요. 오염 흙 작기는 병해 차이 스트림(병원균 시작 지수·포기 감수성)을 실제로 써요.
//    (오염 없는 season_standard 원본은 test_m4_determinism.cpp가 같은 파라미터로 이미 검사해요.)
// 2. 소독 도중 저장·복원: 필름·잔류·열 사멸이 진행 중인 틱에 저장해 이어 돌린 결과가 중단 없는 실행과 같아요.
//    복원 뒤 정식(Plant)이 병해 스트림에서 감수성을 뽑으므로, 병해 스트림 상태도 저장·복원돼야 같아요.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

constexpr std::uint64_t kDay = 24;
constexpr std::uint64_t kHashEvery = 24;  // 해시 비교 간격 (틱)

sim::Params load_repo_params() {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    return std::move(*params);
}

// 월드 + 일정표 + 다음에 넣을 일정 위치
struct Driver {
    sim::World world;
    std::vector<simdata::ScheduledCommand> schedule;
    std::size_t next = 0;

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

using BundleEdit = std::function<void(simdata::ScenarioBundle&)>;

// relative = "m5a/s20_none" 같은 data/scenarios/ 아래 경로 (확장자 없음)
Driver make_driver(const std::string& relative, const BundleEdit& edit = {}) {
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/" + relative + ".toml"));
    REQUIRE(bundle.has_value());
    if (edit) {
        edit(*bundle);
    }
    std::stable_sort(bundle->schedule.begin(), bundle->schedule.end(),
                     [](const auto& a, const auto& b) { return a.tick < b.tick; });
    auto world = sim::World::create(load_repo_params(), std::move(bundle->scenario));
    REQUIRE(world.has_value());
    return Driver{std::move(*world), std::move(bundle->schedule), 0};
}

// 같은 시드로 두 번 돌려 하루마다 해시, 끝에서 직렬화 바이트·스냅샷을 비교해요. 끝 스냅샷을 돌려줘요.
sim::Snapshot check_same_seed(const std::string& relative, std::uint64_t ticks, const BundleEdit& edit = {}) {
    INFO("scenario " << relative);
    Driver a = make_driver(relative, edit);
    Driver b = make_driver(relative, edit);
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
    CHECK(sa.soil == sb.soil);
    CHECK(sa.crops == sb.crops);
    CHECK(sa.ledger == sb.ledger);
    return sa;
}

// split 틱 직전에 저장(save_with_pending이면 그 틱의 명령을 큐에 넣은 뒤) → 복원 → end까지 진행한 결과가
// 중단 없는 실행과 같아요. 저장 시점 스냅샷을 돌려줘요(호출자가 "소독 도중"인지 확인).
sim::Snapshot check_save_resume(const std::string& relative, std::uint64_t split, std::uint64_t end,
                                bool save_with_pending) {
    INFO("scenario " << relative << " split " << split);
    Driver straight = make_driver(relative);
    Driver first = make_driver(relative);
    for (std::uint64_t t = 0; t < split; ++t) {
        straight.run_tick(t);
        first.run_tick(t);
    }
    if (save_with_pending) {
        straight.enqueue_due(split);
        first.enqueue_due(split);
        REQUIRE(first.world.snapshot().pending_command_count > 0);
    }
    const sim::Snapshot at_save = first.world.snapshot();
    const std::vector<std::byte> bytes = first.world.serialize();
    auto restored = sim::World::deserialize(bytes);
    REQUIRE(restored.has_value());
    CHECK(restored->state_hash() == first.world.state_hash());
    CHECK(restored->serialize() == bytes);  // 다시 저장해도 같은 바이트 (생물·병해 난수 스트림 포함)
    Driver resumed{std::move(*restored), first.schedule, first.next};

    for (std::uint64_t t = split; t < end; ++t) {
        if (save_with_pending && t == split) {
            straight.world.step(1);  // 명령은 이미 넣었어요
            resumed.world.step(1);
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
    return at_save;
}

bool any_crop_present(const sim::Snapshot& s) {
    for (const sim::Crop& c : s.crops) {
        if (c.present) {
            return true;
        }
    }
    return false;
}

}  // namespace

// 같은 시드: S20 네 시나리오 (70일, 소독·회복 전체)
TEST_CASE("M5a S20 same seed gives the same hash every day and final state", "[determinism][m5a][s20]") {
    for (const char* name : {"s20_none", "s20_solar", "s20_dazomet", "s20_chloropicrin"}) {
        check_same_seed(std::string("m5a/") + name, 70 * kDay);
    }
}

// 같은 시드: S22 네 시나리오 (정식 뒤 120일, 감염·고사 포함. 소독 변형은 182일)
TEST_CASE("M5a S22 same seed gives the same hash every day and final state", "[determinism][m5a][s22]") {
    for (const char* name : {"s22_fusarium_none", "s22_fusarium_acid", "s22_fusarium_antagonist"}) {
        check_same_seed(std::string("m5a/") + name, 120 * kDay);
    }
    const sim::Snapshot solar = check_same_seed("m5a/s22_fusarium_solar", 182 * kDay);
    CHECK(any_crop_present(solar));
}

// 같은 시드: 시들음병균·역병균 0.3으로 오염된 흙의 기준 작기(263일). 병원균 시작 지수 차이와 포기 감수성이
// 병해 스트림에서 뽑히고, 감염이 실제로 진행돼요
TEST_CASE("M5a contaminated season_standard same seed gives the same hash every day and final state",
          "[determinism][m5a][season]") {
    const auto contaminate = [](simdata::ScenarioBundle& b) {
        b.scenario.soil.microbe_index[sim::microbe(sim::MicrobeGroup::Fusarium)] = 0.3;
        b.scenario.soil.microbe_index[sim::microbe(sim::MicrobeGroup::Phytophthora)] = 0.3;
    };
    const sim::Snapshot end = check_same_seed("m4/season_standard", 263 * kDay, contaminate);
    double infection = 0.0;
    double lo = 1.0;
    double hi = 0.0;
    for (std::size_t i = 0; i < end.crops.size(); ++i) {
        infection += end.crops[i].infection_index[static_cast<std::size_t>(sim::SoilDisease::Fusarium)];
        lo = std::min(lo, end.crops[i].disease_susceptibility_factor);
        hi = std::max(hi, end.crops[i].disease_susceptibility_factor);
    }
    CHECK(infection > 0.0);
    CHECK(hi > lo);  // 감수성 차이가 실제로 뽑혔어요
}

// 저장·복원: 태양열 소독 도중(투명 필름, 12일째)에 저장 → 정식(tick 1488, 병해 스트림에서 감수성) 뒤까지 이어 돌림
TEST_CASE("M5a save during solarization and continue past planting equals an uninterrupted run",
          "[determinism][m5a][serialization][s22]") {
    const sim::Snapshot at_save = check_save_resume("m5a/s22_fusarium_solar", 12 * kDay, 1488 + 10 * kDay, false);
    CHECK(at_save.soil[0].mulch_def_index != sim::kNoMulch);
    CHECK(at_save.soil[0].soil_temp_degC > 30.0);  // 필름 아래 밀폐 하우스 지온 (자정 저장)
    CHECK_FALSE(any_crop_present(at_save));
}

// 저장·복원: 정식 명령이 큐에 들어 있는 채(태양열 소독 뒤 tick 1488) 저장
TEST_CASE("M5a save with a pending Plant command and continue equals an uninterrupted run",
          "[determinism][m5a][serialization][s22]") {
    const sim::Snapshot at_save = check_save_resume("m5a/s22_fusarium_solar", 1488, 1488 + 10 * kDay, true);
    CHECK_FALSE(any_crop_present(at_save));
}

// 저장·복원: 다조멧 필름 아래(잔류가 남은 4일째)에 저장 → 경운·정식(3일 뒤)·약제 뿌리 손상까지 이어 돌림
TEST_CASE("M5a save during fumigation and continue past planting equals an uninterrupted run",
          "[determinism][m5a][serialization][s21]") {
    const sim::Snapshot at_save = check_save_resume("m5a/s21_dazomet_day3", 4 * kDay, 21 * kDay, false);
    double residue = 0.0;
    for (const sim::SoilCell& c : at_save.soil) {
        residue += c.fumigant_residue_index[0];
    }
    CHECK(residue > 0.0);
    CHECK(at_save.soil[0].mulch_def_index != sim::kNoMulch);
}
