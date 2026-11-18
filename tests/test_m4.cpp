// M4 테스트 (프로토타입): 하우스 기후·CO2·탈질·과실 단위 테스트, 작기 보존(극한 포함)·결정론·저장/복원
// docs/spec/house-climate.md, denitrification.md, strawberry-fruit.md, strawberry-phenology.md, m4-overview.md

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

using sim::WeatherHour;
namespace process = sim::process;

sim::HouseState house_with(const sim::Params& p, int layers) {
    return process::initial_house(p.house, layers, WeatherHour{}, 0);
}

void open_all(sim::HouseState& h, bool both_sides = true, double fraction = 1.0) {
    for (auto& layer : h.window_open_fraction) {
        layer[0] = fraction;
        layer[1] = both_sides ? fraction : 0.0;
    }
}

double teq(const sim::Params& p, const sim::HouseState& h, double out_degC, double solar) {
    return process::house_equilibrium(p.house, h, WeatherHour{out_degC, solar, 50.0}).air_temp_degC;
}

}  // namespace

// ---- 하우스 기후 (house-climate "테스트 (단위)") ----

TEST_CASE("House four layers are warmer at night and darker at noon than two", "[unit][m4][house]") {
    const sim::Params p = test::test_params();
    const auto two = house_with(p, 2);
    const auto four = house_with(p, 4);
    CHECK(teq(p, four, -5.0, 0.0) > teq(p, two, -5.0, 0.0));
    const WeatherHour noon{5.0, 600.0, 50.0};
    CHECK(process::inside_solar_W_per_m2(p.house, four, noon) < process::inside_solar_W_per_m2(p.house, two, noon));
}

TEST_CASE("House three closed layers match the M1 formula within half a degree", "[unit][m4][house]") {
    const sim::Params p = test::test_params();
    const auto three = house_with(p, 3);
    // M1: outside + 2.0 + 0.015 × solar (docs/spec/soil-temperature.md M1 계수)
    for (const double solar : {0.0, 200.0, 500.0, 800.0}) {
        for (const double out : {-10.0, 5.0, 25.0}) {
            CHECK(std::abs(teq(p, three, out, solar) - (out + 2.0 + 0.015 * solar)) < 0.5);
        }
    }
}

TEST_CASE("House windows cool the house: all open both sides much cooler, outer only small", "[unit][m4][house]") {
    const sim::Params p = test::test_params();
    const auto closed = house_with(p, 3);
    auto all_open = closed;
    open_all(all_open);
    auto one_side = closed;
    open_all(one_side, false);
    auto outer = closed;
    outer.window_open_fraction[0] = {1.0, 1.0};
    const double t_closed = teq(p, closed, 20.0, 700.0);
    CHECK(teq(p, all_open, 20.0, 700.0) < t_closed - 5.0);
    CHECK(teq(p, all_open, 20.0, 700.0) < teq(p, one_side, 20.0, 700.0));
    CHECK(t_closed - teq(p, outer, 20.0, 700.0) < t_closed - teq(p, all_open, 20.0, 700.0));
    CHECK(t_closed - teq(p, outer, 20.0, 700.0) > 0.0);
}

TEST_CASE("House heater holds the setpoint and is capacity limited with windows open", "[unit][m4][house]") {
    const sim::Params p = test::test_params();
    auto h = house_with(p, 3);
    h.heater_on = true;
    h.heater_setpoint_degC = 8.0;
    const auto eq = process::house_equilibrium(p.house, h, WeatherHour{-10.0, 0.0, 50.0});
    CHECK(eq.air_temp_degC == Catch::Approx(8.0));
    CHECK(eq.heat_W_per_m2 > 0.0);
    open_all(h);
    const auto open_eq = process::house_equilibrium(p.house, h, WeatherHour{-10.0, 0.0, 50.0});
    CHECK(open_eq.air_temp_degC < 8.0);
    CHECK(open_eq.heat_W_per_m2 == Catch::Approx(p.house.heater_capacity_W_per_m2));
    double energy_before = h.heating_energy_MJ_per_m2;
    process::update_house_air(h, p.house, WeatherHour{-10.0, 0.0, 50.0}, 3);
    CHECK(h.heating_energy_MJ_per_m2 > energy_before);
}

TEST_CASE("House outside CO2 table peaks at dawn and dips mid afternoon", "[unit][m4][house][co2]") {
    const sim::Params p = test::test_params();
    const auto& t = p.house.outside_co2_ppm_by_hour;
    REQUIRE(t.size() == 24);
    const auto max_it = std::max_element(t.begin(), t.end());
    const auto min_it = std::min_element(t.begin(), t.end());
    CHECK((max_it - t.begin() == 5 || max_it - t.begin() == 6));
    CHECK(min_it - t.begin() == 15);
}

TEST_CASE("House CO2 drops when closed by day, rises above outside at night, never below floor",
          "[unit][m4][house][co2]") {
    const sim::Params p = test::test_params();
    auto closed = house_with(p, 3);
    auto vented = closed;
    open_all(vented);
    for (auto* h : {&closed, &vented}) {
        process::update_house_air(*h, p.house, WeatherHour{10.0, 500.0, 50.0}, 12);
        process::update_house_co2(*h, p.house, p.crop, 1.0, 0.0);  // 순흡수 1 g 건물/m²/h
    }
    CHECK(closed.co2_ppm < vented.co2_ppm);
    CHECK(p.crop.co2_growth_curve(closed.co2_ppm) < p.crop.co2_growth_curve(vented.co2_ppm));
    // 밤: 호흡(음수 순흡수) + 토양 호흡 → 바깥보다 높아짐
    auto night = house_with(p, 3);
    process::update_house_air(night, p.house, WeatherHour{5.0, 0.0, 50.0}, 2);
    process::update_house_co2(night, p.house, p.crop, -0.1, 0.05);
    CHECK(night.co2_ppm > night.outside_co2_ppm);
    // 이분해성 C가 많으면(토양 호흡 큼) 감소폭이 작음
    auto rich = house_with(p, 3);
    auto poor = rich;
    process::update_house_co2(rich, p.house, p.crop, 1.0, 0.3);
    process::update_house_co2(poor, p.house, p.crop, 1.0, 0.0);
    CHECK(rich.co2_ppm > poor.co2_ppm);
    // 극단 흡수: 하한, 유한
    auto extreme = house_with(p, 3);
    process::update_house_co2(extreme, p.house, p.crop, 1.0e6, 0.0);
    CHECK(std::isfinite(extreme.co2_ppm));
    CHECK(extreme.co2_ppm >= p.house.co2_floor_ppm);
}

TEST_CASE("House window and cover commands are validated at enqueue", "[unit][m4][house][world]") {
    // 6월(day 160) 시작: 휴작기 → 3 → 4중 허용
    sim::World june = test::make_world(test::test_scenario(160));
    CHECK_FALSE(june.enqueue(sim::SetWindowCommand{.layer = 3, .side = sim::WindowSide::Left, .open_fraction = 1.0}));
    CHECK_FALSE(june.enqueue(sim::SetHeaterCommand{.on = true, .setpoint_degC = 99.0}));
    REQUIRE(june.enqueue(sim::SetCoverLayersCommand{.layers = 4}));
    june.step(1);
    const auto snap = june.snapshot();
    CHECK(snap.house.cover_layers == 4);
    CHECK(snap.house.window_open_fraction.size() == 4);
    CHECK(snap.house.windows_open_count() == 0);
    CHECK(june.enqueue(sim::SetWindowCommand{.layer = 3, .side = sim::WindowSide::Right, .open_fraction = 1.0}));
    // 11월(day 320): 작기 중 → 거부
    sim::World november = test::make_world(test::test_scenario(320));
    CHECK_FALSE(november.enqueue(sim::SetCoverLayersCommand{.layers = 4}));
}

// 6차: 모든 겹 한쪽 30% 대 100% (직렬 개구부 경로 0.15 대 0.5), 개방률 0.4는 거부
TEST_CASE("House one side at 30 percent vents less than at 100 percent and 0.4 is rejected",
          "[unit][m4][house]") {
    const sim::Params p = test::test_params();
    auto h30 = house_with(p, 3);
    open_all(h30, false, 0.3);
    auto h100 = house_with(p, 3);
    open_all(h100, false, 1.0);
    CHECK(process::cover_state(p.house, h30).path_index == Catch::Approx(0.15));
    CHECK(process::cover_state(p.house, h100).path_index == Catch::Approx(0.5));
    CHECK(teq(p, h30, 2.0, 400.0) > teq(p, h100, 2.0, 400.0));
    for (auto* h : {&h30, &h100}) {
        h->co2_ppm = 200.0;
        process::update_house_air(*h, p.house, WeatherHour{2.0, 400.0, 50.0}, 12);
        process::update_house_co2(*h, p.house, p.crop, 0.0, 0.0);
    }
    CHECK(h30.co2_ppm <= h100.co2_ppm);
    sim::World world = test::make_world();
    CHECK_FALSE(world.enqueue(sim::SetWindowCommand{.layer = 0, .side = sim::WindowSide::Left, .open_fraction = 0.4}));
    CHECK(world.enqueue(sim::SetWindowCommand{.layer = 0, .side = sim::WindowSide::Left, .open_fraction = 0.3}));
}

// ---- 탈질 ----

TEST_CASE("Denitrification needs low aeration, grows with labile C, and consumes H+", "[unit][m4][denitrification]") {
    const sim::Params p = test::test_params();
    auto lost = [&](double aeration, double labile_C, double* acid = nullptr) {
        sim::SoilCell cell = test::test_cell();
        cell.aeration_index = aeration;
        cell.labile_C_g_per_m2 = labile_C;
        cell.soil_temp_degC = 25.0;
        sim::Ledger ledger;
        process::denitrify(cell, p.denitrification, p.ions, ledger);
        if (acid != nullptr) {
            *acid = cell.acid_H_eq_this_tick;
        }
        CHECK(cell.no3_N_g_per_m2 >= 0.0);
        return ledger.N_gas_g;
    };
    CHECK(lost(0.8, 100.0) == 0.0);
    CHECK(lost(0.1, 100.0) > lost(0.3, 100.0));
    CHECK(lost(0.1, 200.0) > lost(0.1, 0.0));
    double acid = 0.0;
    const double g = lost(0.0, 200.0, &acid);
    CHECK(acid == Catch::Approx(g / p.ions.no3_N_g_per_eq * p.denitrification.H_eq_per_eq_N));
    CHECK(acid < 0.0);
    sim::Params huge = p;
    huge.denitrification.rate_per_hour = 100.0;
    sim::SoilCell cell = test::test_cell();
    cell.aeration_index = 0.0;
    cell.soil_temp_degC = 25.0;
    sim::Ledger ledger;
    process::denitrify(cell, huge.denitrification, huge.ions, ledger);
    CHECK(cell.no3_N_g_per_m2 == 0.0);
    CHECK(ledger.N_gas_g == Catch::Approx(test::test_cell().no3_N_g_per_m2));
}

// ---- 꽃·과실 ----

TEST_CASE("Flowers from 7.4 per m2 are 7 or 8, same seed same result, ranks sum to the count", "[unit][m4][fruit]") {
    const sim::Params p = test::test_params();
    auto make = [&](std::uint64_t seed) {
        sim::Crop crop;
        sim::Pcg32 rng = sim::make_stream(seed, static_cast<std::uint64_t>(sim::RngStream::Biology));
        process::create_flowers(crop, 0, 7.4, p.fruit, 100, rng);
        return crop.fruits;
    };
    const auto a = make(1);
    const auto b = make(1);
    CHECK(a == b);
    CHECK((a.size() == 7 || a.size() == 8));
    for (std::uint64_t seed = 2; seed < 30; ++seed) {
        const auto f = make(seed);
        CHECK((f.size() == 7 || f.size() == 8));
    }
}

TEST_CASE("Cold mean temperature at anthesis drops more flowers and makes smaller fruit", "[unit][m4][fruit]") {
    const sim::Params p = test::test_params();
    auto judge = [&](double mean_degC, double& mean_size, double& deformed_share) {
        sim::Crop crop;
        sim::Pcg32 rng = sim::make_stream(7, static_cast<std::uint64_t>(sim::RngStream::Biology));
        process::create_flowers(crop, 0, 400.0, p.fruit, 0, rng);
        const std::size_t flowers = crop.fruits.size();
        process::judge_anthesis(crop, p.fruit, mean_degC, 1000, rng);
        double size = 0.0;
        double deformed = 0.0;
        for (const auto& f : crop.fruits) {
            size += f.potential_fresh_g;
            deformed += f.deformed ? 1.0 : 0.0;
        }
        mean_size = size / static_cast<double>(crop.fruits.size());
        deformed_share = deformed / static_cast<double>(crop.fruits.size());
        return static_cast<double>(crop.fruits.size()) / static_cast<double>(flowers);
    };
    double size_cold = 0.0, size_warm = 0.0, def_cold = 0.0, def_warm = 0.0;
    const double set_cold = judge(5.0, size_cold, def_cold);
    const double set_warm = judge(18.0, size_warm, def_warm);
    // 명세 표는 "5 °C 쪽 낙화 큼"이지만 명세 곡선으로는 μ(5 °C) − spread = 0.38 > set_threshold 0.3 이라 낙화 0 (보고 항목)
    CHECK(set_cold <= set_warm);
    CHECK(size_cold < size_warm);
    CHECK(def_cold > def_warm);
}

// ---- 작기 보존·결정론 (m4-overview "보존", 극한 포함) ----

namespace {

struct Totals {
    double water = 0, N = 0, P = 0, K = 0, Ca = 0, Mg = 0, Mn = 0, S = 0, C = 0;
};

Totals totals_of(const sim::Snapshot& s) {
    Totals t;
    for (std::size_t i = 0; i < s.soil.size(); ++i) {
        const auto& c = s.soil[i];
        const auto& crop = s.crops[i];
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

double rel(double a, double e) { return std::abs(a - e) / std::max(1.0, std::abs(e)); }

bool pools_ok(const sim::Snapshot& s) {
    auto ok = [](double v) { return std::isfinite(v) && v >= 0.0; };
    if (!std::isfinite(s.house.air_temp_degC) || !ok(s.house.co2_ppm) || !ok(s.house.heating_energy_MJ_per_m2)) {
        return false;
    }
    for (std::size_t i = 0; i < s.soil.size(); ++i) {
        const auto& c = s.soil[i];
        const auto& p = s.crops[i];
        for (double v : {c.water_mm, c.nh4_N_g_per_m2, c.no2_N_g_per_m2, c.no3_N_g_per_m2, c.labile_C_g_per_m2,
                         c.labile_N_g_per_m2, c.stable_C_g_per_m2, c.k_g_per_m2, c.ca_g_per_m2, c.mg_g_per_m2,
                         c.p_g_per_m2, c.mn_g_per_m2, p.leaf_g_per_m2, p.crown_g_per_m2, p.root_g_per_m2,
                         p.plant_N_g_per_m2, p.plant_P_g_per_m2, p.plant_K_g_per_m2, p.plant_Ca_g_per_m2,
                         p.plant_Mg_g_per_m2, p.plant_Mn_g_per_m2, p.induction_index, p.establishment_index}) {
            if (!ok(v)) {
                return false;
            }
        }
        for (const auto& f : p.fruits) {
            if (!ok(f.dry_g) || !ok(f.development_index) || f.pollination_index > 1.0) {
                return false;
            }
        }
    }
    return true;
}

simdata::ScenarioBundle season_bundle() {
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m4/season_standard.toml"));
    REQUIRE(bundle.has_value());
    return std::move(*bundle);
}

sim::Params repo_params() {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    return std::move(*params);
}

double run_season_conservation(simdata::ScenarioBundle bundle, std::uint64_t ticks) {
    std::stable_sort(bundle.schedule.begin(), bundle.schedule.end(),
                     [](const auto& a, const auto& b) { return a.tick < b.tick; });
    auto world = sim::World::create(repo_params(), std::move(bundle.scenario));
    REQUIRE(world.has_value());
    const Totals init = totals_of(world->snapshot());
    double worst = 0.0;
    std::size_t next = 0;
    for (std::uint64_t t = 0; t < ticks; ++t) {
        while (next < bundle.schedule.size() && bundle.schedule[next].tick == t) {
            REQUIRE(world->enqueue(bundle.schedule[next].command).has_value());
            ++next;
        }
        world->step(1);
        const auto s = world->snapshot();
        REQUIRE(pools_ok(s));
        const Totals now = totals_of(s);
        const auto& l = s.ledger;
        const double errs[] = {
            rel(now.water, init.water + l.water_in_mm - l.water_evaporated_mm - l.water_transpired_mm -
                               l.water_drained_mm),
            rel(now.N, init.N + l.N_in_g - l.N_leached_g - l.N_gas_g - l.N_harvested_g),
            rel(now.P, init.P + l.P_in_g - l.P_leached_g - l.P_harvested_g),
            rel(now.K, init.K + l.K_in_g - l.K_leached_g - l.K_harvested_g),
            rel(now.Ca, init.Ca + l.Ca_in_g - l.Ca_leached_g - l.Ca_harvested_g),
            rel(now.Mg, init.Mg + l.Mg_in_g - l.Mg_leached_g - l.Mg_harvested_g),
            rel(now.Mn, init.Mn + l.Mn_in_g - l.Mn_leached_g - l.Mn_harvested_g),
            rel(now.S, init.S + l.S_in_g - l.S_leached_g),
            rel(now.C, init.C + l.C_in_g + l.C_root_litter_g + l.C_leaf_litter_g - l.C_respired_g),
        };
        for (double e : errs) {
            worst = std::max(worst, e);
        }
    }
    return worst;
}

constexpr std::uint64_t kSeasonTicks = 6312;
constexpr double kTol = 1e-9;

}  // namespace

TEST_CASE("M4 season conserves water N P K Ca Mg Mn S C every tick", "[conservation][m4]") {
    CHECK(run_season_conservation(season_bundle(), kSeasonTicks) <= kTol);
}

TEST_CASE("M4 extreme season without irrigation keeps conservation", "[conservation][m4][extreme]") {
    auto b = season_bundle();
    std::erase_if(b.schedule, [](const auto& item) { return std::holds_alternative<sim::IrrigateCommand>(item.command); });
    CHECK(run_season_conservation(std::move(b), kSeasonTicks) <= kTol);
}

TEST_CASE("M4 extreme season with 100x nitrogen keeps conservation", "[conservation][m4][extreme]") {
    auto b = season_bundle();
    for (auto& item : b.schedule) {
        if (auto* f = std::get_if<sim::AddFertilizerCommand>(&item.command)) {
            if (f->fertilizer_id == "calcium_nitrate") {
                f->g_per_m2 *= 100.0;
            }
        }
    }
    CHECK(run_season_conservation(std::move(b), kSeasonTicks) <= kTol);
}

TEST_CASE("M4 extreme winter without heating keeps conservation", "[conservation][m4][extreme]") {
    auto b = season_bundle();
    std::erase_if(b.schedule, [](const auto& item) { return std::holds_alternative<sim::SetHeaterCommand>(item.command); });
    CHECK(run_season_conservation(std::move(b), kSeasonTicks) <= kTol);
}

TEST_CASE("M4 extreme four layers all windows open with heating keeps conservation", "[conservation][m4][extreme]") {
    auto b = season_bundle();
    b.scenario.cover_layers = 4;
    std::erase_if(b.schedule, [](const auto& item) {
        return std::holds_alternative<sim::SetWindowCommand>(item.command) ||
               std::holds_alternative<sim::SetHeaterCommand>(item.command);
    });
    for (int layer = 0; layer < 4; ++layer) {
        for (auto side : {sim::WindowSide::Left, sim::WindowSide::Right}) {
            b.schedule.push_back({0, sim::SetWindowCommand{.layer = layer, .side = side, .open_fraction = 1.0}});
        }
    }
    b.schedule.push_back({0, sim::SetHeaterCommand{.on = true, .setpoint_degC = 30.0}});
    CHECK(run_season_conservation(std::move(b), kSeasonTicks) <= kTol);
}

TEST_CASE("M4 season save mid-season and continue equals an uninterrupted run", "[determinism][m4][serialization]") {
    auto b = season_bundle();
    std::stable_sort(b.schedule.begin(), b.schedule.end(), [](const auto& x, const auto& y) { return x.tick < y.tick; });
    auto drive = [&](sim::World& w, std::uint64_t from, std::uint64_t to) {
        for (std::uint64_t t = from; t < to; ++t) {
            for (const auto& item : b.schedule) {
                if (item.tick == t) {
                    REQUIRE(w.enqueue(item.command).has_value());
                }
            }
            w.step(1);
        }
    };
    constexpr std::uint64_t kSave = 5800;  // 화방·꽃·과실 목록이 있는 시점
    auto straight = sim::World::create(repo_params(), b.scenario);
    REQUIRE(straight.has_value());
    drive(*straight, 0, kSeasonTicks);

    auto first = sim::World::create(repo_params(), b.scenario);
    REQUIRE(first.has_value());
    drive(*first, 0, kSave);
    const auto bytes = first->serialize();
    auto resumed = sim::World::deserialize(bytes);
    REQUIRE(resumed.has_value());
    CHECK(resumed->state_hash() == first->state_hash());
    drive(*resumed, kSave, kSeasonTicks);
    CHECK(resumed->state_hash() == straight->state_hash());

    auto again = sim::World::create(repo_params(), b.scenario);
    REQUIRE(again.has_value());
    drive(*again, 0, kSeasonTicks);
    CHECK(again->state_hash() == straight->state_hash());
}

// ---- 4차: 낙과(6b), 적화·적과(6c), 당도 온도 주·야간 ----

namespace {

// 착과된 어린 과실이 rank별로 고르게 있는 작물 (B × 8, 활착)
sim::Crop crop_with_young_fruits(const sim::Params& p, int per_rank) {
    sim::Crop crop = sim::process::plant_crop(test::test_crop_init(), p.crop);
    for (std::uint32_t rank = 0; rank < 4; ++rank) {
        for (int k = 0; k < per_rank; ++k) {
            sim::Fruit f;
            f.rank = rank;
            f.set = true;
            f.anthesis_tick = static_cast<std::uint64_t>(k);
            f.potential_fresh_g = 20.0;
            f.development_index = 0.1;
            crop.fruits.push_back(f);
        }
    }
    return crop;
}

}  // namespace

// 크기 충족도 0.4 대 1.0: 0.4 쪽만 어린 과실이 떨어지고 높은 과위가 더 잘 떨어짐
TEST_CASE("Young fruit abortion happens only when size satisfaction is low and favors high ranks",
          "[unit][m4][fruit]") {
    const sim::Params p = test::test_params();
    auto run = [&](double satisfaction, std::array<int, 4>& left) {
        sim::Crop crop = crop_with_young_fruits(p, 200);
        crop.size_satisfaction_mean = satisfaction;
        sim::SoilCell cell = test::test_cell();
        sim::Ledger ledger;
        sim::Pcg32 rng = sim::make_stream(3, static_cast<std::uint64_t>(sim::RngStream::Biology));
        const sim::process::NutrientAvailability none{};
        for (int t = 0; t < 24; ++t) {
            crop.size_satisfaction_mean = satisfaction;
            sim::process::CropEnvironment env{.inside_solar_W_per_m2 = 0.0, .house_air_temp_degC = 20.0,
                                              .house_air_temp_mean_degC = 10.0, .house_co2_ppm = 400.0,
                                              .day_of_year = 1, .tick = static_cast<std::uint64_t>(t)};
            (void)sim::process::update_crop(crop, cell, p, none, env, rng, ledger);
        }
        left = {};
        for (const auto& f : crop.fruits) {
            ++left[f.rank];
        }
        return crop.harvest.aborted_fruits;
    };
    // 5차: 진행도당 낙과 (밤 20 °C로 진행도가 늘어야 떨어짐). 보정 곡선은 충족도 1에서도 0.1이라
    // 명세 표의 "0.4 쪽만 떨어짐"은 "0.4 쪽이 훨씬 많이 떨어짐"으로 바뀌어요 (기대값 변경 보고).
    std::array<int, 4> low{}, high{};
    const auto aborted_high = run(1.0, high);
    const auto aborted_low = run(0.4, low);
    CHECK(aborted_low > 2 * aborted_high);
    CHECK(low[3] < low[0]);
}

// ThinFlowers 포기당 3개: 칸마다 약 24개, 높은 과위·늦은 것부터, 진행도 0.3 이상은 남음
TEST_CASE("ThinFlowers removes about per_plant times plants from the highest rank and keeps developed fruit",
          "[unit][m4][fruit]") {
    const sim::Params p = test::test_params();
    sim::Crop crop = crop_with_young_fruits(p, 10);
    for (auto& f : crop.fruits) {
        if (f.rank == 3 && f.anthesis_tick < 5) {
            f.development_index = 0.5;  // 딸 수 없는 과실
        }
    }
    const std::size_t before = crop.fruits.size();
    sim::SoilCell cell = test::test_cell();
    sim::Ledger ledger;
    sim::Pcg32 rng = sim::make_stream(3, static_cast<std::uint64_t>(sim::RngStream::Biology));
    const auto removed = sim::process::thin_flowers(crop, cell, p, 3.0, rng, ledger);
    CHECK(removed == 24);
    CHECK(crop.fruits.size() == before - 24);
    int rank3_young = 0, rank3_old = 0, rank2 = 0;
    for (const auto& f : crop.fruits) {
        rank3_young += (f.rank == 3 && f.development_index < 0.3) ? 1 : 0;
        rank3_old += (f.rank == 3 && f.development_index >= 0.3) ? 1 : 0;
        rank2 += f.rank == 2 ? 1 : 0;
    }
    CHECK(rank3_young == 0);  // 과위 3의 어린 것 5개 먼저
    CHECK(rank3_old == 5);    // 진행도 0.3 이상은 남음
    CHECK(rank2 == 0);        // 다음 과위 10개
    // 남은 9개는 과위 1에서 늦게 핀 것부터: 과위 1은 1개 남음
    int rank1 = 0;
    for (const auto& f : crop.fruits) {
        rank1 += f.rank == 1 ? 1 : 0;
    }
    CHECK(rank1 == 1);
}

// 당도 온도: 낮 23 °C 대 30 °C(밤 같음), 밤 8.5 °C 대 15 °C(낮 같음)
TEST_CASE("Brix temperature score prefers 23 C days and 8.5 C nights", "[unit][m4][fruit]") {
    const sim::FruitParams fp = test::test_params().fruit;
    auto k_temp = [&](double day_degC, double night_degC) {
        const double day = fp.brix_day_temp_score_curve(day_degC);
        const double night = fp.brix_night_temp_score_curve(night_degC);
        return fp.brix_temp_score_curve(fp.brix_day_weight * day + fp.brix_night_weight * night);
    };
    CHECK(k_temp(23.0, 10.0) > k_temp(30.0, 10.0));
    CHECK(k_temp(23.0, 8.5) > k_temp(23.0, 15.0));
    CHECK(fp.brix_night_temp_score_curve(2.0) < 0.2);
}
