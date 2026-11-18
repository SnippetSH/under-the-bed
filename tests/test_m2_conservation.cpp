// M2 보존 법칙 테스트: 물, N, P, K, Ca, Mg, S, 유기 C 수지(토양 + 작물 체내)와 음수·NaN·범위 검사
// (docs/spec/m2-overview.md "장부 추가", "시나리오 테스트 (M2)" 보존 행)
// 매 틱 셀 합계로 검사해요 (셀 하나 = 1 m2). 상대 오차 1e-9, 스케일은 max(1, |값|).
// 석회의 Ca/Mg는 녹을 때만 Ca_in_g/Mg_in_g에 들어가므로 미용해 석회는 수지 바깥이에요.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

constexpr double kRelTol = 1e-9;

struct Totals {
    double water = 0.0;
    double nitrogen = 0.0;  // 유기 + NH4 + NO2 + NO3 + 식물 체내
    double p = 0.0;
    double k = 0.0;
    double ca = 0.0;
    double mg = 0.0;
    double s = 0.0;
    double carbon = 0.0;
    double mn = 0.0;  // M3
};

Totals totals_of(const sim::Snapshot& snap) {
    Totals t;
    for (std::size_t i = 0; i < snap.soil.size(); ++i) {
        const sim::SoilCell& c = snap.soil[i];
        const sim::Crop& crop = snap.crops[i];
        t.water += c.water_mm;
        t.nitrogen += c.total_N_g_per_m2() + crop.plant_N_g_per_m2;
        t.p += c.p_g_per_m2 + c.p_fixed_g_per_m2 + crop.plant_P_g_per_m2;  // M3: 유효 + 고정
        t.mn += c.mn_g_per_m2 + c.mn_oxide_g_per_m2 + crop.plant_Mn_g_per_m2;
        t.k += c.k_g_per_m2 + crop.plant_K_g_per_m2;
        t.ca += c.ca_g_per_m2 + crop.plant_Ca_g_per_m2;
        t.mg += c.mg_g_per_m2 + crop.plant_Mg_g_per_m2;
        t.s += c.s_g_per_m2;
        t.carbon += c.organic_C_g_per_m2();
    }
    return t;
}

bool finite_nonneg(double v) { return std::isfinite(v) && v >= 0.0; }

bool state_valid(const sim::Snapshot& snap, const sim::SoilChemistryParams& chem) {
    for (const sim::SoilCell& c : snap.soil) {
        const double pools[] = {c.water_mm,        c.labile_C_g_per_m2, c.labile_N_g_per_m2, c.stable_C_g_per_m2,
                                c.stable_N_g_per_m2, c.nh4_N_g_per_m2,  c.no2_N_g_per_m2,    c.no3_N_g_per_m2,
                                c.k_g_per_m2,      c.ca_g_per_m2,       c.mg_g_per_m2,       c.p_g_per_m2,
                                c.s_g_per_m2,      c.lime_undissolved_eq_per_m2, c.ec_dS_per_m,
                                c.mn_g_per_m2, c.mn_oxide_g_per_m2, c.p_fixed_g_per_m2, c.nh4_solution_N_g_per_m2, c.k_solution_g_per_m2,
                                c.ca_solution_g_per_m2, c.mg_solution_g_per_m2, c.mn_solution_g_per_m2,
                                c.cec_eq_per_m2};
        for (double p : pools) {
            if (!finite_nonneg(p)) {
                return false;
            }
        }
        // M3: 용액 ≤ 총량, 0 ≤ φ ≤ 1
        if (c.nh4_solution_N_g_per_m2 > c.nh4_N_g_per_m2 || c.k_solution_g_per_m2 > c.k_g_per_m2 ||
            c.ca_solution_g_per_m2 > c.ca_g_per_m2 || c.mg_solution_g_per_m2 > c.mg_g_per_m2 ||
            c.mn_solution_g_per_m2 > c.mn_g_per_m2 || !(c.exchange_free_site_index >= 0.0) ||
            !(c.exchange_free_site_index <= 1.0)) {
            return false;
        }
        if (!std::isfinite(c.soil_temp_degC) || !std::isfinite(c.ph) || c.ph < chem.ph_min || c.ph > chem.ph_max) {
            return false;
        }
    }
    for (const sim::Crop& crop : snap.crops) {
        const double v[] = {crop.leaf_g_per_m2,    crop.root_g_per_m2,    crop.plant_N_g_per_m2,
                            crop.plant_P_g_per_m2, crop.plant_K_g_per_m2, crop.plant_Ca_g_per_m2,
                            crop.plant_Mg_g_per_m2, crop.plant_Mn_g_per_m2};
        for (double x : v) {
            if (!finite_nonneg(x)) {
                return false;
            }
        }
        if (!std::isfinite(crop.root_health) || crop.root_health < 0.0 || crop.root_health > 1.0) {
            return false;
        }
    }
    return true;
}

double rel_err(double actual, double expected) {
    return std::abs(actual - expected) / std::max(1.0, std::abs(expected));
}

struct Loaded {
    sim::Params params;
    simdata::ScenarioBundle bundle;
};

Loaded load(const std::string& name, const std::string& dir = "m2") {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/" + dir + "/" + name + ".toml"));
    REQUIRE(bundle.has_value());
    return Loaded{std::move(*params), std::move(*bundle)};
}

// 일정표(와 추가 명령)를 넣으며 ticks 틱을 돌고, check_every 틱마다 보존식을 검사해요. 최대 상대 오차를 돌려줘요.
double run_and_check(Loaded loaded, std::vector<simdata::ScheduledCommand> extra, std::uint64_t ticks,
                     std::uint64_t check_every, bool drop_irrigation = false) {
    std::vector<simdata::ScheduledCommand> schedule;
    for (const auto& item : loaded.bundle.schedule) {
        if (drop_irrigation && std::holds_alternative<sim::IrrigateCommand>(item.command)) {
            continue;
        }
        schedule.push_back(item);
    }
    schedule.insert(schedule.end(), extra.begin(), extra.end());
    std::stable_sort(schedule.begin(), schedule.end(),
                     [](const auto& a, const auto& b) { return a.tick < b.tick; });

    const sim::SoilChemistryParams chem = loaded.params.soil_chemistry;
    auto world = sim::World::create(std::move(loaded.params), std::move(loaded.bundle.scenario));
    REQUIRE(world.has_value());

    const Totals init = totals_of(world->snapshot());
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
        const std::array<double, 9> errs = {
            rel_err(now.water, init.water + l.water_in_mm - l.water_evaporated_mm - l.water_transpired_mm -
                                   l.water_drained_mm),
            rel_err(now.nitrogen, init.nitrogen + l.N_in_g - l.N_leached_g - l.N_gas_g),
            rel_err(now.p, init.p + l.P_in_g - l.P_leached_g),
            rel_err(now.k, init.k + l.K_in_g - l.K_leached_g),
            rel_err(now.ca, init.ca + l.Ca_in_g - l.Ca_leached_g),
            rel_err(now.mg, init.mg + l.Mg_in_g - l.Mg_leached_g),
            rel_err(now.s, init.s + l.S_in_g - l.S_leached_g),
            rel_err(now.carbon, init.carbon + l.C_in_g + l.C_root_litter_g - l.C_respired_g),
            rel_err(now.mn, init.mn + l.Mn_in_g - l.Mn_leached_g),
        };
        static constexpr const char* kNames[9] = {"water", "N", "P", "K", "Ca", "Mg", "S", "C", "Mn"};
        const double worst = *std::max_element(errs.begin(), errs.end());
        max_err = std::max(max_err, worst);
        const bool valid = state_valid(snap, chem);
        if (worst > kRelTol || !valid) {
            INFO("tick " << done);
            for (std::size_t i = 0; i < errs.size(); ++i) {
                INFO(kNames[i] << "_err=" << errs[i]);
                CHECK(errs[i] <= kRelTol);
            }
            CHECK(valid);
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

void report(const std::string& label, double err) {
    WARN(std::format("conservation max relative error [{}] = {:.3e}", label, err));
    CHECK(err <= kRelTol);
}

}  // namespace

// 모든 M2 시나리오에서 전체 기간 매 틱 보존식과 상태 범위가 지켜져요.
TEST_CASE("M2 scenarios conserve every budget every tick", "[conservation][m2]") {
    struct Item {
        const char* name;
        std::uint64_t days;
    };
    const Item items[] = {{"s4_optimal", 21},   {"s4_alkaline", 21}, {"s4_acid", 21},       {"s8_proper", 14},
                          {"s8_waterlogged", 14}, {"s9_lime", 14},    {"s9_none", 14},       {"s10_standard", 14},
                          {"s10_excess", 14},   {"s10_dry", 14}};
    for (const Item& item : items) {
        report(item.name, run_and_check(load(item.name), {}, item.days * 24, 1));
    }
}

// 극한: 황산암모늄 5 kg/m2
TEST_CASE("M2 extreme 5 kg ammonium sulfate keeps conservation", "[conservation][m2][extreme]") {
    Loaded l = load("s4_optimal");
    const sim::TileArea area = grid_of(l);
    report("5 kg ammonium sulfate",
           run_and_check(std::move(l), {at(0, sim::AddFertilizerCommand{area, "ammonium_sulfate", 5000.0})}, 21 * 24, 1));
}

// 극한: 고토석회 3 kg/m2
TEST_CASE("M2 extreme 3 kg dolomite keeps conservation", "[conservation][m2][extreme]") {
    Loaded l = load("s4_optimal");
    const sim::TileArea area = grid_of(l);
    report("3 kg dolomite",
           run_and_check(std::move(l), {at(0, sim::AddLimeCommand{area, "dolomite", 3000.0})}, 21 * 24, 1));
}

// 극한: 한 번에 300 mm 관수
TEST_CASE("M2 extreme 300 mm irrigation keeps conservation", "[conservation][m2][extreme]") {
    Loaded l = load("s4_optimal");
    const sim::TileArea area = grid_of(l);
    report("300 mm irrigation", run_and_check(std::move(l), {at(0, sim::IrrigateCommand{area, 300.0})}, 21 * 24, 1));
}

// 극한: 매일 경운
TEST_CASE("M2 extreme daily tillage keeps conservation", "[conservation][m2][extreme]") {
    Loaded l = load("s4_optimal");
    const sim::TileArea area = grid_of(l);
    std::vector<simdata::ScheduledCommand> extra;
    for (std::uint64_t day = 0; day < 21; ++day) {
        extra.push_back(at(day * 24, sim::TillCommand{area}));
    }
    report("daily tillage", run_and_check(std::move(l), std::move(extra), 21 * 24, 1));
}

// 극한: 관수 없이 60일 (24틱마다 검사)
TEST_CASE("M2 extreme 60 days without irrigation keeps conservation", "[conservation][m2][extreme]") {
    report("60 days dry", run_and_check(load("s4_optimal"), {}, 60 * 24, 24, true));
}

// ---- M3 프로토타입: M3 시나리오와 극한 (m3-overview "보존" 행) ----
TEST_CASE("M3 scenarios conserve every budget every tick", "[conservation][m3]") {
    const char* names[] = {"s5_standard", "s5_k_excess", "s6_loam",     "s6_sand",     "s11_neutral", "s11_acid",
                           "s11_wet",     "s11_alkaline", "s12_neutral", "s12_acid",    "s12_alkaline"};
    for (const char* name : names) {
        report(name, run_and_check(load(name, "m3"), {}, 28 * 24, 1));
    }
}

TEST_CASE("M3 extreme 100x potassium sulfate keeps conservation", "[conservation][m3][extreme]") {
    Loaded l = load("s5_standard", "m3");
    const sim::TileArea area = grid_of(l);
    report("100x K2SO4", run_and_check(std::move(l), {at(0, sim::AddFertilizerCommand{area, "potassium_sulfate", 2000.0})},
                                       21 * 24, 1));
}

TEST_CASE("M3 extreme 100x lime on sand keeps conservation", "[conservation][m3][extreme]") {
    Loaded l = load("s6_sand", "m3");
    const sim::TileArea area = grid_of(l);
    report("100x lime sand", run_and_check(std::move(l), {at(0, sim::AddLimeCommand{area, "calcium_carbonate", 30000.0})},
                                           21 * 24, 1));
}

TEST_CASE("M3 extreme 100x manganese sulfate keeps conservation", "[conservation][m3][extreme]") {
    Loaded l = load("s11_neutral", "m3");
    const sim::TileArea area = grid_of(l);
    report("100x MnSO4", run_and_check(std::move(l), {at(0, sim::AddFertilizerCommand{area, "manganese_sulfate", 1000.0})},
                                       21 * 24, 1));
}

TEST_CASE("M3 extreme mineral CEC 0.01 keeps conservation", "[conservation][m3][extreme]") {
    Loaded l = load("s6_sand", "m3");
    l.bundle.scenario.soil.cec_mineral_eq_per_m2 = 0.01;
    l.bundle.scenario.soil.stable_C_g_per_m2 = 0.0;
    l.bundle.scenario.soil.stable_N_g_per_m2 = 0.0;
    report("CEC 0.01", run_and_check(std::move(l), {}, 21 * 24, 1));
}

TEST_CASE("M3 extreme no irrigation keeps conservation", "[conservation][m3][extreme]") {
    report("S5 dry 60 days", run_and_check(load("s5_k_excess", "m3"), {}, 60 * 24, 24, true));
}
