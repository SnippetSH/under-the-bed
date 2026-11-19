// docs/spec/m5-overview.md 13장 "M5a 사전 작업" (2026-11-19 승인) 단위 테스트:
// 토양 증발 냉각(soil-temperature), 근권 온도 흡수 배율·부드러운 N 사치 상한(strawberry-minimal 3번),
// Mn 흡수 조절(manganese), 유지 호흡 활성 보정(strawberry-fruit 4번), 포기 간 화아분화 차이(strawberry-phenology 2번).
// 저장 탄수화물(strawberry-fruit 4b)은 test_fruit.cpp에 있어요.
#include <algorithm>
#include <cstddef>
#include <set>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "test_helpers.hpp"

namespace {

using Catch::Approx;
namespace process = sim::process;

constexpr double kNightSolar_W_per_m2 = 0.0;
constexpr double kAir_degC = 20.0;

// 흡수가 능력(뿌리·속도)에 묶이도록 작은 유효량
process::NutrientAvailability small_availability() {
    return {.nh4_N = 0.0, .no3_N = 0.5, .p = 0.5, .k = 0.5, .ca = 0.5, .mg = 0.5, .mn = 0.01};
}

sim::Ledger night_uptake(sim::Crop crop, const sim::Params& p, double soil_temp_degC) {
    sim::SoilCell cell = test::test_cell();
    cell.soil_temp_degC = soil_temp_degC;
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(1);
    (void)process::update_crop(crop, cell, p, small_availability(), test::crop_env(kNightSolar_W_per_m2, kAir_degC),
                               rng, ledger);
    return ledger;
}

sim::Crop make_crop(const sim::Params& p) { return process::plant_crop(test::test_crop_init(), p.crop); }

}  // namespace

// 증발 냉각: 완화 뒤 증발 mm x 계수만큼 내려가요. 계수 0이면 옛 식, 음수 증발은 무시
TEST_CASE("Soil evaporation cools the soil after relaxation", "[unit][m5a][temperature]") {
    sim::Params p = test::test_params();
    sim::SoilCell base = test::test_cell();
    base.soil_temp_degC = 30.0;
    sim::SoilCell off = base;
    process::relax_soil_temperature(off, p.soil_temperature, 35.0, 1.2);
    sim::SoilCell plain = base;
    process::relax_soil_temperature(plain, p.soil_temperature, 35.0);
    CHECK(off.soil_temp_degC == plain.soil_temp_degC);

    p.soil_temperature.evap_cooling_degC_per_mm = 2.5;
    sim::SoilCell cooled = base;
    process::relax_soil_temperature(cooled, p.soil_temperature, 35.0, 1.2);
    CHECK(cooled.soil_temp_degC == Approx(plain.soil_temp_degC - 1.2 * 2.5));
    sim::SoilCell negative = base;
    process::relax_soil_temperature(negative, p.soil_temperature, 35.0, -3.0);
    CHECK(negative.soil_temp_degC == plain.soil_temp_degC);
}

// update_soil_water가 돌려주는 값이 장부의 이번 틱 증발량과 같아요
TEST_CASE("Soil water update returns the evaporation of the tick", "[unit][m5a][soil_water]") {
    const sim::Params p = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::Ledger ledger;
    const double evaporated = process::update_soil_water(cell, p.soil_water, 600.0, 0.0, 0.0, ledger);
    CHECK(evaporated > 0.0);
    CHECK(evaporated == Approx(ledger.water_evaporated_mm));
    sim::Ledger dark;
    sim::SoilCell cell2 = test::test_cell();
    const double dark_evaporated = process::update_soil_water(cell2, p.soil_water, 0.0, 0.0, 0.0, dark);
    CHECK(dark_evaporated == Approx(dark.water_evaporated_mm));
}

// 근권 온도: 차가운 흙에서는 N과 다른 원소의 흡수 능력이 곡선 배율만큼 줄어요 (모든 원소, 디렉터 결정)
TEST_CASE("Cold root zone scales uptake of every element by the temperature curve", "[unit][m5a][crop]") {
    sim::Params p = test::test_params();
    p.crop.uptake_temp_curve = {{{{5, 0.2}}, {{10, 0.45}}, {{15, 0.75}}, {{20, 1}}, {{30, 1}}, {{35, 0.7}}}};
    const sim::Crop crop = make_crop(p);
    const sim::Ledger warm = night_uptake(crop, p, 20.0);
    const sim::Ledger cold = night_uptake(crop, p, 5.0);
    REQUIRE(warm.N_uptake_g > 0.0);
    REQUIRE(warm.K_uptake_g > 0.0);
    REQUIRE(warm.Ca_uptake_g > 0.0);
    REQUIRE(warm.Mn_uptake_g > 0.0);
    CHECK(cold.N_uptake_g == Approx(0.2 * warm.N_uptake_g));
    CHECK(cold.K_uptake_g == Approx(0.2 * warm.K_uptake_g));
    CHECK(cold.Ca_uptake_g == Approx(0.2 * warm.Ca_uptake_g));
    CHECK(cold.Mg_uptake_g == Approx(0.2 * warm.Mg_uptake_g));
    CHECK(cold.Mn_uptake_g == Approx(0.2 * warm.Mn_uptake_g));
    // 항등 곡선(test_params)이면 온도와 무관
    const sim::Params identity = test::test_params();
    CHECK(night_uptake(crop, identity, 5.0).N_uptake_g == night_uptake(crop, identity, 20.0).N_uptake_g);
}

// 부드러운 N 상한: 체내 N 비율에 따라 흡수 능력이 곡선대로 줄고, 상한(max ratio)에서 수요 0
TEST_CASE("Soft N luxury curve tapers N uptake as plant N rises", "[unit][m5a][crop]") {
    sim::Params p = test::test_params();
    p.crop.n_luxury_max_ratio = 1.6;
    sim::Params identity = p;
    p.crop.n_luxury_uptake_curve = {{{{0, 1}}, {{0.9, 1}}, {{1.1, 0.4}}, {{1.3, 0.1}}, {{1.5, 0}}}};
    sim::Crop crop = make_crop(p);
    const double required_N = process::nutrient_requirement(crop, p.crop).N;
    crop.plant_N_g_per_m2 = 1.2 * required_N;
    const double tapered = night_uptake(crop, p, 20.0).N_uptake_g;
    const double full = night_uptake(crop, identity, 20.0).N_uptake_g;
    REQUIRE(full > 0.0);
    // 1.1 -> 0.4, 1.3 -> 0.1 사이. 같은 틱의 밤 호흡이 구조를 줄여 요구량(비율)이 조금 바뀌므로 1% 허용
    CHECK(tapered == Approx(0.25 * full).epsilon(0.01));
    // 비율이 낮으면 곡선 1: 흡수 그대로
    crop.plant_N_g_per_m2 = 0.8 * required_N;
    CHECK(night_uptake(crop, p, 20.0).N_uptake_g == Approx(night_uptake(crop, identity, 20.0).N_uptake_g));
    // 상한 1.6에 닿으면 곡선과 무관하게 0
    crop.plant_N_g_per_m2 = 1.6 * required_N;
    CHECK(night_uptake(crop, identity, 20.0).N_uptake_g == 0.0);
}

// Mn 흡수 조절: 체내 Mn이 요구량의 1.5배면 흡수 능력 x 0.2, 다른 원소는 그대로
TEST_CASE("Mn uptake regulation curve limits Mn accumulation", "[unit][m5a][manganese]") {
    sim::Params p = test::test_params();
    const sim::Params identity = p;
    p.crop.mn_uptake_regulation_curve = {{{{0, 1}}, {{1, 1}}, {{1.5, 0.2}}, {{2, 0.03}}, {{100, 0.03}}}};
    sim::Crop crop = make_crop(p);
    crop.plant_Mn_g_per_m2 = 1.5 * process::nutrient_requirement(crop, p.crop).Mn;
    const sim::Ledger regulated = night_uptake(crop, p, 20.0);
    const sim::Ledger free = night_uptake(crop, identity, 20.0);
    REQUIRE(free.Mn_uptake_g > 0.0);
    CHECK(regulated.Mn_uptake_g == Approx(0.2 * free.Mn_uptake_g).epsilon(0.01));  // 밤 호흡으로 비율이 조금 바뀜
    CHECK(regulated.K_uptake_g == free.K_uptake_g);
}

// 유지 호흡 활성 보정: 호흡 x maintenance_activity_curve(RGR 이동 평균)
TEST_CASE("Maintenance respiration scales with the growth activity curve", "[unit][m5a][crop]") {
    sim::Params p = test::test_params();
    p.crop.maintenance_activity_curve = {{{{-1, 0.4}}, {{0.05, 1}}}};
    auto respiration = [&](double rgr_mean_per_day) {
        sim::Crop crop = make_crop(p);
        crop.rgr_mean_per_day = rgr_mean_per_day;
        sim::SoilCell cell = test::test_cell();
        sim::Ledger ledger;
        sim::Pcg32 rng = test::biology_rng(1);
        (void)process::update_crop(crop, cell, p, process::NutrientAvailability{},
                                   test::crop_env(kNightSolar_W_per_m2, kAir_degC), rng, ledger);
        return crop.last_respiration_g_per_m2;
    };
    const double active = respiration(0.1);
    REQUIRE(active > 0.0);
    CHECK(respiration(-1.0) == Approx(0.4 * active));
    CHECK(respiration(0.05) == Approx(active));
}

// RGR 이동 평균: 이번 틱 순생장 / (영양 기관 + 과실 + 저장) x 24를 완화 비율로 따라가요. 비율 0이면 그대로
TEST_CASE("Plant RGR mean follows net growth over total biomass", "[unit][m5a][crop]") {
    sim::Params p = test::quiet_params();
    p.crop.rgr_mean_relax_rate_per_hour = 0.5;
    sim::Crop crop = make_crop(p);
    crop.reserve_g_per_m2 = 4.0;
    const double total = process::vegetative_biomass_g_per_m2(crop) + crop.reserve_g_per_m2;
    sim::SoilCell cell = test::test_cell();
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(1);
    (void)process::update_crop(crop, cell, p, process::NutrientAvailability{}, test::crop_env(500.0, kAir_degC), rng,
                               ledger);
    const double net = crop.last_growth_g_per_m2;
    REQUIRE(net > 0.0);
    CHECK(crop.rgr_mean_per_day == Approx(0.5 * net / total * sim::kHoursPerDay));

    sim::Params frozen = test::quiet_params();
    sim::Crop still = make_crop(frozen);
    still.rgr_mean_per_day = 0.03;
    sim::SoilCell cell2 = test::test_cell();
    sim::Ledger ledger2;
    sim::Pcg32 rng2 = test::biology_rng(1);
    (void)process::update_crop(still, cell2, frozen, process::NutrientAvailability{},
                               test::crop_env(500.0, kAir_degC), rng2, ledger2);
    CHECK(still.rgr_mean_per_day == 0.03);
}

// 포기 간 차이: 정식 때 셀마다 화아분화 진행도가 [초기값 - spread, 초기값] 안에서 달라지고, 0이면 모두 같아요.
// 같은 시드면 같은 배치(결정론)
TEST_CASE("Induction spread staggers the initial induction across cells", "[unit][m5a][phenology]") {
    sim::Scenario scenario = test::test_scenario(1, 4, 5);
    scenario.crop = test::test_crop_init();
    auto induction = [&](double spread) {
        sim::Params p = test::test_params();
        p.phenology.induction_spread_index = spread;
        auto world = sim::World::create(p, scenario);
        REQUIRE(world.has_value());
        std::vector<double> values;
        for (const sim::Crop& crop : world->snapshot().crops) {
            values.push_back(crop.induction_index);
        }
        return values;
    };
    const double initial = process::find_seedling_grade(test::test_params(), "B")->initial_induction_index;
    const std::vector<double> none = induction(0.0);
    for (const double v : none) {
        CHECK(v == initial);
    }
    const std::vector<double> spread = induction(0.4);
    const std::set<double> distinct(spread.begin(), spread.end());
    CHECK(distinct.size() > spread.size() / 2);
    for (const double v : spread) {
        CHECK(v <= initial);
        CHECK(v >= std::max(0.0, initial - 0.4));
    }
    CHECK(induction(0.4) == spread);
}
