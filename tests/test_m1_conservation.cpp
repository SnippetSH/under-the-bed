// M1 보존 법칙 테스트: 물, 질소, 탄소 수지와 음수·NaN 검사 (docs/spec/m1-overview.md "장부")
// 매 틱 셀 합계로 검사해요 (셀 하나 = 1 m2). 상대 오차 1e-9, 스케일은 max(1, |값|).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

constexpr double kRelTol = 1e-9;

struct Totals {
    double water = 0.0;
    double nitrogen = 0.0;
    double carbon = 0.0;
};

Totals totals_of(const sim::Snapshot& snap) {
    Totals t;
    for (const sim::SoilCell& c : snap.soil) {
        t.water += c.water_mm;
        t.nitrogen += c.organic_N_g_per_m2() + c.nh4_N_g_per_m2 + c.no2_N_g_per_m2 + c.no3_N_g_per_m2;
        t.carbon += c.organic_C_g_per_m2();
    }
    return t;
}

bool pools_valid(const sim::SoilCell& c) {
    const double pools[] = {c.water_mm,          c.labile_C_g_per_m2, c.labile_N_g_per_m2, c.stable_C_g_per_m2,
                            c.stable_N_g_per_m2, c.nh4_N_g_per_m2,    c.no2_N_g_per_m2,    c.no3_N_g_per_m2};
    for (double p : pools) {
        if (!std::isfinite(p) || p < 0.0) {
            return false;
        }
    }
    return std::isfinite(c.soil_temp_degC);
}

double rel_err(double actual, double expected) {
    return std::abs(actual - expected) / std::max(1.0, std::abs(expected));
}

struct Loaded {
    sim::Params params;
    simdata::ScenarioBundle bundle;
};

Loaded load(const std::string& name) {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m1/" + name + ".toml"));
    REQUIRE(bundle.has_value());
    return Loaded{std::move(*params), std::move(*bundle)};
}

// 일정표(와 추가 명령)를 넣으며 ticks 틱을 돌고, check_every 틱마다 보존식을 검사해요. 최대 상대 오차를 돌려줘요.
double run_and_check(Loaded loaded, std::vector<simdata::ScheduledCommand> extra, std::uint64_t ticks,
                     std::uint64_t check_every) {
    std::vector<simdata::ScheduledCommand> schedule = loaded.bundle.schedule;
    schedule.insert(schedule.end(), extra.begin(), extra.end());
    std::stable_sort(schedule.begin(), schedule.end(),
                     [](const auto& a, const auto& b) { return a.tick < b.tick; });

    auto world = sim::World::create(std::move(loaded.params), std::move(loaded.bundle.scenario));
    REQUIRE(world.has_value());

    const Totals initial = totals_of(world->snapshot());
    double max_err = 0.0;
    std::size_t next = 0;

    for (std::uint64_t t = 0; t < ticks; ++t) {
        while (next < schedule.size() && schedule[next].tick == t) {
            auto seq = world->enqueue(schedule[next].command);
            REQUIRE(seq.has_value());
            ++next;
        }
        world->step(1);
        const std::uint64_t done = t + 1;
        if (done % check_every != 0 && done != ticks) {
            continue;
        }
        const sim::Snapshot snap = world->snapshot();
        const Totals now = totals_of(snap);
        const sim::Ledger& l = snap.ledger;
        const double ew =
            rel_err(now.water, initial.water + l.water_in_mm - l.water_evaporated_mm - l.water_drained_mm);
        const double en = rel_err(now.nitrogen, initial.nitrogen + l.N_in_g - l.N_leached_g - l.N_gas_g);
        const double ec = rel_err(now.carbon, initial.carbon + l.C_in_g + l.C_root_litter_g - l.C_respired_g);
        max_err = std::max({max_err, ew, en, ec});
        bool all_valid = true;
        for (const sim::SoilCell& c : snap.soil) {
            all_valid = all_valid && pools_valid(c);
        }
        if (ew > kRelTol || en > kRelTol || ec > kRelTol || !all_valid) {
            INFO("tick " << done << " water_err=" << ew << " N_err=" << en << " C_err=" << ec);
            CHECK(ew <= kRelTol);
            CHECK(en <= kRelTol);
            CHECK(ec <= kRelTol);
            CHECK(all_valid);
            return max_err;  // 첫 위반에서 멈춰요
        }
    }
    return max_err;
}

simdata::ScheduledCommand at(std::uint64_t tick, sim::Command command) {
    return simdata::ScheduledCommand{.tick = tick, .command = std::move(command)};
}

sim::TileArea grid_of(const Loaded& l) {
    return sim::whole_grid(l.bundle.scenario.bed_rows, l.bundle.scenario.bed_cols);
}

constexpr std::uint64_t k21Days = 21 * 24;

void report(const std::string& label, double err) {
    WARN(std::format("conservation max relative error [{}] = {:.3e}", label, err));
    CHECK(err <= kRelTol);
}

}  // namespace

// 모든 M1 시나리오에서 21일 동안 매 틱 물, 질소, 탄소가 보존돼요.
TEST_CASE("M1 scenarios conserve water nitrogen and carbon every tick", "[conservation][m1]") {
    const std::string names[] = {"s0_control", "s1_immature", "s1_mature", "s2_alkaline",
                                 "s2_neutral", "s3_over",     "s3_proper"};
    for (const std::string& name : names) {
        report(name, run_and_check(load(name), {}, k21Days, 1));
    }
}

// 극한: 비료 100배 (황산암모늄 5000 g/m2)
TEST_CASE("Extreme 100x fertilizer keeps conservation", "[conservation][m1][extreme]") {
    Loaded l = load("s0_control");
    const sim::TileArea area = grid_of(l);
    report("100x fertilizer",
           run_and_check(std::move(l), {at(0, sim::AddFertilizerCommand{area, "ammonium_sulfate", 5000.0})}, k21Days, 1));
}

// 극한: 수피 퇴비 100 kg/m2
TEST_CASE("Extreme 100 kg compost keeps conservation", "[conservation][m1][extreme]") {
    Loaded l = load("s0_control");
    const sim::TileArea area = grid_of(l);
    report("100 kg compost",
           run_and_check(std::move(l), {at(0, sim::AddCompostCommand{area, "bark_compost", 100.0})}, k21Days, 1));
}

// 극한: 한 번에 1000 mm 관수
TEST_CASE("Extreme 1000 mm irrigation keeps conservation", "[conservation][m1][extreme]") {
    Loaded l = load("s0_control");
    const sim::TileArea area = grid_of(l);
    report("1000 mm irrigation", run_and_check(std::move(l), {at(0, sim::IrrigateCommand{area, 1000.0})}, k21Days, 1));
}

// 극한: 관수 없이 365일 (24틱마다 검사)
TEST_CASE("Extreme 365 days without irrigation keeps conservation", "[conservation][m1][extreme]") {
    report("365 days dry", run_and_check(load("s0_control"), {}, 365 * 24, 24));
}
