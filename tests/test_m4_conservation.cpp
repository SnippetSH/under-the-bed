// M4 보존 법칙 테스트 (docs/spec/m4-overview.md "시나리오 테스트 (M4)" 보존 행)
// 물, N, P, K, Ca, Mg, Mn, S, C 수지를 매 틱 하우스 합계로 검사해요 (타일 1칸 = 1 m2).
//   토양 + 체내 = 초기 + in - leached - gas - harvested   (유기 C: 초기 + C_in + C_root_litter + C_leaf_litter - C_respired)
// 함께 모든 풀 >= 0, 용액 <= 총량, 0 <= phi <= 1, pH 범위, NaN 없음, 과실·화방 값 >= 0을 매 틱 확인해요.
// 상대 오차 1e-9, 스케일은 max(1, |값|). 극한: 작기 중 관수 0, 질소 100배, 겨울 난방 없음, 4중 + 창 전부 열림 + 난방.
// 작기(263일) 케이스는 [season] 태그를 달아요.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

constexpr double kRelTol = 1e-9;
constexpr std::uint64_t kSeasonTicks = 6312;

struct Totals {
    double water = 0.0, N = 0.0, P = 0.0, K = 0.0, Ca = 0.0, Mg = 0.0, Mn = 0.0, S = 0.0, C = 0.0;
};

Totals totals_of(const sim::Snapshot& snap) {
    Totals t;
    for (std::size_t i = 0; i < snap.soil.size(); ++i) {
        const sim::SoilCell& c = snap.soil[i];
        const sim::Crop& crop = snap.crops[i];
        t.water += c.water_mm;
        t.N += c.total_N_g_per_m2() + crop.plant_N_g_per_m2;
        t.P += c.p_g_per_m2 + c.p_fixed_g_per_m2 + crop.plant_P_g_per_m2;
        t.K += c.k_g_per_m2 + crop.plant_K_g_per_m2;
        t.Ca += c.ca_g_per_m2 + crop.plant_Ca_g_per_m2;
        t.Mg += c.mg_g_per_m2 + crop.plant_Mg_g_per_m2;
        t.Mn += c.mn_g_per_m2 + c.mn_oxide_g_per_m2 + crop.plant_Mn_g_per_m2;
        t.S += c.s_g_per_m2;
        t.C += c.organic_C_g_per_m2();
    }
    return t;
}

double rel_err(double actual, double expected) {
    return std::abs(actual - expected) / std::max(1.0, std::abs(expected));
}

bool finite_nonneg(double v) { return std::isfinite(v) && v >= 0.0; }

// 모든 풀 >= 0 (유한), 용액 <= 총량, 0 <= phi <= 1, pH 범위, 하우스·작물·과실·화방 값이 유한하고 음수가 아님
bool state_valid(const sim::Snapshot& snap, const sim::SoilChemistryParams& chem) {
    const sim::HouseState& h = snap.house;
    if (!std::isfinite(h.air_temp_degC) || !std::isfinite(h.air_temp_mean_degC) || !finite_nonneg(h.co2_ppm) ||
        !finite_nonneg(h.heating_energy_MJ_per_m2) || !finite_nonneg(h.heating_power_W_per_m2) ||
        !std::isfinite(snap.house_air_temp_degC)) {
        return false;
    }
    for (const sim::SoilCell& c : snap.soil) {
        const double pools[] = {c.water_mm,           c.labile_C_g_per_m2,   c.labile_N_g_per_m2,
                                c.stable_C_g_per_m2,  c.stable_N_g_per_m2,   c.nh4_N_g_per_m2,
                                c.no2_N_g_per_m2,     c.no3_N_g_per_m2,      c.k_g_per_m2,
                                c.ca_g_per_m2,        c.mg_g_per_m2,         c.p_g_per_m2,
                                c.s_g_per_m2,         c.lime_undissolved_eq_per_m2, c.ec_dS_per_m,
                                c.mn_g_per_m2,        c.mn_oxide_g_per_m2,   c.p_fixed_g_per_m2,
                                c.nh4_solution_N_g_per_m2, c.k_solution_g_per_m2, c.ca_solution_g_per_m2,
                                c.mg_solution_g_per_m2, c.mn_solution_g_per_m2, c.cec_eq_per_m2};
        for (double p : pools) {
            if (!finite_nonneg(p)) {
                return false;
            }
        }
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
        const double v[] = {crop.leaf_g_per_m2,    crop.crown_g_per_m2,   crop.root_g_per_m2,    crop.fruit_g_per_m2(),
                            crop.plant_N_g_per_m2, crop.plant_P_g_per_m2, crop.plant_K_g_per_m2, crop.plant_Ca_g_per_m2,
                            crop.plant_Mg_g_per_m2, crop.plant_Mn_g_per_m2, crop.induction_index,
                            crop.establishment_index, crop.size_satisfaction_mean,
                            crop.harvest.harvested_fresh_g, crop.harvest.harvested_deformed_fresh_g,
                            crop.harvest.harvested_brix_weighted, crop.harvest.harvested_firmness_weighted};
        for (double x : v) {
            if (!finite_nonneg(x)) {
                return false;
            }
        }
        if (!std::isfinite(crop.root_health) || crop.root_health < 0.0 || crop.root_health > 1.0) {
            return false;
        }
        // 과실·꽃 개수는 크기 타입이라 음수일 수 없어요: 각 항목의 값이 유한하고 >= 0인지 확인해요
        for (const sim::Fruit& f : crop.fruits) {
            if (!finite_nonneg(f.dry_g) || !finite_nonneg(f.development_index) || !finite_nonneg(f.potential_fresh_g) ||
                !(f.pollination_index <= 1.0) || !(f.pollination_index >= 0.0)) {
                return false;
            }
        }
        for (const sim::Truss& t : crop.trusses) {
            if (!finite_nonneg(t.flowers_per_m2) || !finite_nonneg(t.development_index)) {
                return false;
            }
        }
    }
    const sim::Ledger& l = snap.ledger;
    for (double x : {l.N_harvested_g, l.P_harvested_g, l.K_harvested_g, l.Ca_harvested_g, l.Mg_harvested_g,
                     l.Mn_harvested_g, l.C_leaf_litter_g, l.N_gas_g}) {
        if (!finite_nonneg(x)) {
            return false;
        }
    }
    return true;
}

struct Loaded {
    sim::Params params;
    simdata::ScenarioBundle bundle;
};

Loaded load(const std::string& name) {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m4/" + name + ".toml"));
    REQUIRE(bundle.has_value());
    return Loaded{std::move(*params), std::move(*bundle)};
}

// ticks 틱을 매 틱 돌며 보존식과 상태 유효성을 검사해요. 첫 위반에서 멈추고 최대 상대 오차를 돌려줘요.
double run_and_check(Loaded loaded, std::uint64_t ticks) {
    std::stable_sort(loaded.bundle.schedule.begin(), loaded.bundle.schedule.end(),
                     [](const auto& a, const auto& b) { return a.tick < b.tick; });
    const sim::SoilChemistryParams chem = loaded.params.soil_chemistry;
    auto world = sim::World::create(std::move(loaded.params), std::move(loaded.bundle.scenario));
    REQUIRE(world.has_value());
    const auto& schedule = loaded.bundle.schedule;

    const Totals init = totals_of(world->snapshot());
    double max_err = 0.0;
    std::size_t next = 0;
    for (std::uint64_t t = 0; t < ticks; ++t) {
        while (next < schedule.size() && schedule[next].tick == t) {
            REQUIRE(world->enqueue(schedule[next].command).has_value());
            ++next;
        }
        world->step(1);
        const sim::Snapshot snap = world->snapshot();
        const Totals now = totals_of(snap);
        const sim::Ledger& l = snap.ledger;
        const std::array<double, 9> errs = {
            rel_err(now.water, init.water + l.water_in_mm - l.water_evaporated_mm - l.water_transpired_mm -
                                   l.water_drained_mm),
            rel_err(now.N, init.N + l.N_in_g - l.N_leached_g - l.N_gas_g - l.N_harvested_g),
            rel_err(now.P, init.P + l.P_in_g - l.P_leached_g - l.P_harvested_g),
            rel_err(now.K, init.K + l.K_in_g - l.K_leached_g - l.K_harvested_g),
            rel_err(now.Ca, init.Ca + l.Ca_in_g - l.Ca_leached_g - l.Ca_harvested_g),
            rel_err(now.Mg, init.Mg + l.Mg_in_g - l.Mg_leached_g - l.Mg_harvested_g),
            rel_err(now.Mn, init.Mn + l.Mn_in_g - l.Mn_leached_g - l.Mn_harvested_g),
            rel_err(now.S, init.S + l.S_in_g - l.S_leached_g),
            rel_err(now.C, init.C + l.C_in_g + l.C_root_litter_g + l.C_leaf_litter_g - l.C_respired_g),
        };
        static constexpr const char* kNames[9] = {"water", "N", "P", "K", "Ca", "Mg", "Mn", "S", "C"};
        const double worst = *std::max_element(errs.begin(), errs.end());
        max_err = std::max(max_err, worst);
        const bool valid = state_valid(snap, chem);
        if (worst > kRelTol || !valid) {
            INFO("tick " << (t + 1));
            for (std::size_t i = 0; i < errs.size(); ++i) {
                INFO(kNames[i] << "_err=" << errs[i]);
                CHECK(errs[i] <= kRelTol);
            }
            CHECK(valid);
            return max_err;
        }
    }
    return max_err;
}

void report(const std::string& label, double err) {
    WARN(std::format("conservation max relative error [{}] = {:.3e}", label, err));
    CHECK(err <= kRelTol);
}

void season_case(const std::string& name) { report(name, run_and_check(load(name), kSeasonTicks)); }

// 작기 시나리오(season_standard) 변형: 일정표를 고쳐 극한을 만들어요
Loaded standard_season() { return load("season_standard"); }

}  // namespace

// ---- 작기 시나리오 7종 (263일, 매 틱) ----

TEST_CASE("M4 season_standard conserves every budget every tick", "[conservation][m4][season]") {
    season_case("season_standard");
}
TEST_CASE("M4 season_no_topdress conserves every budget every tick", "[conservation][m4][season]") {
    season_case("season_no_topdress");
}
TEST_CASE("M4 season_n_excess conserves every budget every tick", "[conservation][m4][season]") {
    season_case("season_n_excess");
}
TEST_CASE("M4 season_k_low conserves every budget every tick", "[conservation][m4][season]") {
    season_case("season_k_low");
}
TEST_CASE("M4 season_k_excess conserves every budget every tick", "[conservation][m4][season]") {
    season_case("season_k_excess");
}
TEST_CASE("M4 season_acid conserves every budget every tick", "[conservation][m4][season]") {
    season_case("season_acid");
}
TEST_CASE("M4 season_thinned conserves every budget every tick", "[conservation][m4][season]") {
    season_case("season_thinned");
}

// ---- 짧은 시나리오: S13(14일), S15(113일), S18(겨울·CO2 7일, 가을 28일) ----

TEST_CASE("M4 S13 scenarios conserve every budget every tick", "[conservation][m4][s13]") {
    for (const char* name : {"s13_proper", "s13_waterlogged"}) {
        report(name, run_and_check(load(name), 14 * 24));
    }
}

TEST_CASE("M4 S15 scenarios conserve every budget every tick", "[conservation][m4][s15]") {
    for (const char* name : {"s15_grade_a", "s15_grade_c"}) {
        report(name, run_and_check(load(name), 113 * 24));
    }
}

TEST_CASE("M4 S18 scenarios conserve every budget every tick", "[conservation][m4][s18]") {
    report("s18_winter_none", run_and_check(load("s18_winter_none"), 7 * 24));
    report("s18_winter_heated", run_and_check(load("s18_winter_heated"), 7 * 24));
    report("s18_co2_closed", run_and_check(load("s18_co2_closed"), 7 * 24));
    report("s18_co2_vented", run_and_check(load("s18_co2_vented"), 7 * 24));
    report("s18_autumn_closed", run_and_check(load("s18_autumn_closed"), 28 * 24));
    report("s18_autumn_vented", run_and_check(load("s18_autumn_vented"), 28 * 24));
}

// ---- 극한 (작기 시나리오 변형, 263일) ----

// 극한: 작기 중 관수 0
TEST_CASE("M4 extreme season without irrigation keeps conservation", "[conservation][m4][extreme][season]") {
    Loaded l = standard_season();
    std::erase_if(l.bundle.schedule,
                  [](const auto& item) { return std::holds_alternative<sim::IrrigateCommand>(item.command); });
    report("no irrigation", run_and_check(std::move(l), kSeasonTicks));
}

// 극한: 질산칼슘 웃거름 100배
TEST_CASE("M4 extreme season with 100x nitrogen keeps conservation", "[conservation][m4][extreme][season]") {
    Loaded l = standard_season();
    std::size_t scaled = 0;
    for (auto& item : l.bundle.schedule) {
        if (auto* f = std::get_if<sim::AddFertilizerCommand>(&item.command)) {
            if (f->fertilizer_id == "calcium_nitrate") {
                f->g_per_m2 *= 100.0;
                ++scaled;
            }
        }
    }
    REQUIRE(scaled > 0);
    report("100x calcium nitrate", run_and_check(std::move(l), kSeasonTicks));
}

// 극한: 겨울 난방 없음
TEST_CASE("M4 extreme season without heating keeps conservation", "[conservation][m4][extreme][season]") {
    Loaded l = standard_season();
    std::erase_if(l.bundle.schedule,
                  [](const auto& item) { return std::holds_alternative<sim::SetHeaterCommand>(item.command); });
    report("no heating", run_and_check(std::move(l), kSeasonTicks));
}

// 극한: 4중 피복 + 창 전부 열림 + 난방(설정 30 도)
TEST_CASE("M4 extreme four layers all windows open with heating keeps conservation",
          "[conservation][m4][extreme][season]") {
    Loaded l = standard_season();
    l.bundle.scenario.cover_layers = 4;
    std::erase_if(l.bundle.schedule, [](const auto& item) {
        return std::holds_alternative<sim::SetWindowCommand>(item.command) ||
               std::holds_alternative<sim::SetHeaterCommand>(item.command);
    });
    for (int layer = 0; layer < 4; ++layer) {
        for (auto side : {sim::WindowSide::Left, sim::WindowSide::Right}) {
            l.bundle.schedule.push_back(
                {0, sim::SetWindowCommand{.layer = layer, .side = side, .open_fraction = 1.0}});
        }
    }
    l.bundle.schedule.push_back({0, sim::SetHeaterCommand{.on = true, .setpoint_degC = 30.0}});
    report("4 layers open + heater", run_and_check(std::move(l), kSeasonTicks));
}
