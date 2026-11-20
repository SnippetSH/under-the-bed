// M5b 지상부 병해충 보존·불변 조건·결정론 (docs/spec/foliar-disease.md 9장 "보존", "결정론" 행).
// data/scenarios/m5b/*.toml (S26~S32)과 극한 변형을 매 틱 검사해요.
// (전체 길이 S26~S34·습도 변형·m4 season_standard/n_excess의 같은 매 틱 검사는 test_m5b_scenarios.cpp가 방향 테스트와
//  함께 해요. 여기서는 측정 창 전 구간과 극한 입력을 봐요.)
//   원소: 토양 + 체내 = 초기 + in − leached − gas − harvested − removed (RemovePlant만 계 밖 항).
//         썩은 과실은 낙엽 규칙으로 토양(계 안), 흰가루 과실은 수확 반출 장부에 들어가요.
//   유기 C: 초기 + C_in + C_root_litter + C_leaf_litter − C_respired (제거한 포기의 C는 토양 밖이라 항 없음).
//   물·수증기: 토양 물 수지, 하우스 수증기 수지와 원천 일치 (house-humidity 7장).
//   불변: 지상부 지수 0~1, 탄저병 감염은 같은 작물이 이어지는 동안 줄지 않음, 하우스 포자·습도 0~1, 잎 젖음 0~1,
//         토양 무기 N 평균 ≥ 0, NaN 없음. 검사 함수는 m5b_world_checks.hpp (test_m5b_scenarios.cpp와 공유).
// 상대 오차 1e-9, 스케일은 max(1, |값|). 첫 위반에서 멈춰요.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/world.hpp"
#include "m5b_world_checks.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

using test::m5b::kRelTol;
constexpr std::uint64_t kDay = 24;

struct Loaded {
    sim::Params params;
    simdata::ScenarioBundle bundle;
};

Loaded load(const std::string& name) {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m5b/" + name + ".toml"));
    REQUIRE(bundle.has_value());
    return Loaded{std::move(*params), std::move(*bundle)};
}

struct Options {
    std::optional<sim::FoliarDisease> remove_on_onset;  // sim-cli --remove-on-onset과 같은 동작
    std::uint64_t remove_all_at_tick = 0;               // 0이 아니면 그 틱에 하우스 전체 RemovePlant
};

struct Result {
    double max_err = 0.0;
    sim::Ledger ledger;
    std::uint64_t foliar_onsets = 0;
    std::uint64_t final_hash = 0;
};

Result run(Loaded loaded, std::uint64_t ticks, const Options& options = {}, bool check = true) {
    auto world = sim::World::create(std::move(loaded.params), std::move(loaded.bundle.scenario));
    REQUIRE(world.has_value());
    const auto& schedule = loaded.bundle.schedule;
    const int cols = world->scenario().bed_cols;
    const int rows = world->scenario().bed_rows;
    sim::Snapshot prev = world->snapshot();
    const sim::Snapshot init_snap = prev;
    const test::m5b::Totals init = test::m5b::totals_of(prev);
    Result result;
    std::size_t next = 0;
    std::vector<sim::Command> removals;
    for (std::uint64_t t = 0; t < ticks; ++t) {
        for (sim::Command& c : removals) {
            (void)world->enqueue(std::move(c));
        }
        removals.clear();
        while (next < schedule.size() && schedule[next].tick == t) {
            REQUIRE(world->enqueue(schedule[next].command).has_value());
            ++next;
        }
        if (options.remove_all_at_tick != 0 && t == options.remove_all_at_tick) {
            REQUIRE(world->enqueue(sim::RemovePlantCommand{.area = sim::whole_grid(rows, cols)}).has_value());
        }
        world->step(1);
        for (const sim::Event& e : world->events()) {
            if (const auto* onset = std::get_if<sim::FoliarDiseaseOnsetEvent>(&e.payload)) {
                ++result.foliar_onsets;
                if (options.remove_on_onset && onset->disease == *options.remove_on_onset) {
                    const int r = static_cast<int>(onset->tile_index) / cols;
                    const int c = static_cast<int>(onset->tile_index) % cols;
                    removals.emplace_back(sim::RemovePlantCommand{.area = {.row = r, .col = c, .rows = 1, .cols = 1}});
                }
            }
        }
        if (!check) {
            continue;
        }
        sim::Snapshot snap = world->snapshot();
        const auto errs = test::m5b::budget_errors(world->params(), init_snap, init, snap);
        const auto& kNames = test::m5b::kBudgetNames;
        const double worst = *std::max_element(errs.begin(), errs.end());
        result.max_err = std::max(result.max_err, worst);
        const std::string reason = test::m5b::invalid_reason(prev, snap);
        if (worst > kRelTol || !reason.empty()) {
            INFO("tick " << (t + 1) << " " << reason);
            for (std::size_t i = 0; i < errs.size(); ++i) {
                INFO(kNames[i] << "_err=" << errs[i]);
                CHECK(errs[i] <= kRelTol);
            }
            CHECK(reason.empty());
            result.ledger = snap.ledger;
            return result;
        }
        prev = std::move(snap);
    }
    result.ledger = world->snapshot().ledger;
    result.final_hash = world->state_hash();
    return result;
}

Result report(const std::string& label, const Result& r) {
    WARN(std::format("m5b conservation [{}] max rel err {:.3e}; rot {} mildew {} removed {} onsets {}", label,
                     r.max_err, r.ledger.fruit_rot_count, r.ledger.harvested_mildew, r.ledger.plants_removed,
                     r.foliar_onsets));
    CHECK(r.max_err <= kRelTol);
    return r;
}

}  // namespace

// ---- 보존: S26~S32 전부 (README 실행 일수, 매 틱) ----

TEST_CASE("M5b S26 and S27 scenarios conserve every budget every tick", "[conservation][m5b][foliar][s26][s27]") {
    for (const char* name : {"s26_closed", "s26_vented", "s27_closed_fan"}) {
        report(name, run(load(name), 62 * kDay));
    }
}

TEST_CASE("M5b S28 scenarios conserve every budget every tick", "[conservation][m5b][foliar][s28]") {
    for (const char* name : {"s28_drip", "s28_overhead", "s28_overhead_bare"}) {
        report(name, run(load(name), 90 * kDay));
    }
    // s28_overhead_remove: 탄저병 발병 칸을 다음 틱에 뽑아요 (sim-cli --remove-on-onset anthracnose)
    const Result r = report("s28_overhead_remove",
                            run(load("s28_overhead_remove"), 90 * kDay, {.remove_on_onset = sim::FoliarDisease::Anthracnose}));
    CHECK(r.ledger.plants_removed > 0);
    CHECK(r.ledger.N_removed_g > 0.0);
}

TEST_CASE("M5b S30 to S32 scenarios conserve every budget every tick", "[conservation][m5b][foliar][s30][s31][s32]") {
    for (const char* name : {"s30_hot", "s30_cool", "s31_predator", "s31_predator_late", "s31_predator_dry"}) {
        report(name, run(load(name), 45 * kDay));
    }
    for (const char* name : {"s32_bright", "s32_dark", "s32_overhead", "s32_dry"}) {
        report(name, run(load(name), 76 * kDay));
    }
}

// ---- 극한: 두상 관수 매 시간, 포자 1 시작(배경 1), 천적 1000마리/m², 전 포기 제거 ----
TEST_CASE("M5b extreme foliar inputs keep conservation", "[conservation][m5b][foliar][extreme]") {
    Loaded l = load("s28_overhead");
    // 배경은 1 미만만 허용 (배경 원천 bg × L ÷ (1 − bg))
    l.bundle.scenario.foliar = {.botrytis_background_index = 0.999, .powdery_mildew_background_index = 0.999};
    l.bundle.scenario.soil.microbe_index[sim::microbe(sim::MicrobeGroup::Anthracnose)] = 1.0;
    const sim::TileArea all = sim::whole_grid(l.bundle.scenario.bed_rows, l.bundle.scenario.bed_cols);
    for (std::uint64_t t = 0; t < 60 * kDay; ++t) {
        l.bundle.schedule.push_back({.tick = t, .command = sim::IrrigateCommand{.area = all, .amount_mm = 1.0}});
    }
    l.bundle.schedule.push_back(
        {.tick = 5, .command = sim::ReleasePredatorsCommand{.area = all, .predator_id = "persimilis", .per_m2 = 1000.0}});
    std::stable_sort(l.bundle.schedule.begin(), l.bundle.schedule.end(),
                     [](const auto& a, const auto& b) { return a.tick < b.tick; });
    const Result r = report("extreme overhead every hour + spores 1 + 1000 predators + remove all",
                            run(std::move(l), 70 * kDay, {.remove_all_at_tick = 60 * kDay}));
    CHECK(r.ledger.plants_removed > 0);
    // 1000 + s28 시나리오의 표준 방사 15 (결정 44, m5-overview 15.3 항목 37)
    CHECK(r.ledger.predators_released_per_m2 == 1015.0);
}

// ---- 극한 기상: 바깥 −15 °C, 바깥 습도 0%·100% 하루씩 번갈아, 관수 0 (house-humidity 8장 극한 행의 월드 판) ----
// 수증기 보존·원천 일치, 습도·잎 젖음 0~1, NaN 없음을 매 틱 검사해요 (run()이 m5b_world_checks.hpp로 검사)
TEST_CASE("M5b extreme cold weather with dry and saturated outside air keeps conservation",
          "[conservation][m5b][humidity][extreme]") {
    for (const bool irrigate : {true, false}) {
        Loaded l = load("s28_overhead");
        for (std::size_t i = 0; i < l.bundle.scenario.weather_hourly.size(); ++i) {
            sim::WeatherHour& w = l.bundle.scenario.weather_hourly[i];
            w.outside_air_temp_degC = -15.0;
            w.outside_rh_percent = (i / kDay) % 2 == 0 ? 0.0 : 100.0;
        }
        if (!irrigate) {
            std::erase_if(l.bundle.schedule, [](const simdata::ScheduledCommand& c) {
                return std::holds_alternative<sim::IrrigateCommand>(c.command);
            });
        }
        report(irrigate ? "extreme -15 C outside rh 0/100" : "extreme -15 C outside rh 0/100 without irrigation",
               run(std::move(l), 30 * kDay));
    }
}

// ---- 결정론: S26, S28, S31, s29_standard 같은 시드 → 같은 해시 ----
TEST_CASE("M5b scenarios are deterministic", "[determinism][m5b][foliar]") {
    struct Case {
        const char* name;
        std::uint64_t days;
    };
    for (const Case c : {Case{"s26_closed", 62}, Case{"s28_overhead", 90}, Case{"s31_predator", 45},
                         Case{"s29_standard", 263}}) {
        const Result a = run(load(c.name), c.days * kDay, {}, false);
        const Result b = run(load(c.name), c.days * kDay, {}, false);
        INFO(c.name);
        CHECK(a.final_hash == b.final_hash);
        CHECK(a.ledger == b.ledger);
    }
}
