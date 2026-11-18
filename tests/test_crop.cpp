// M2 패키지 단위 테스트 (docs/spec/m2-overview.md "구현 작업 분할")
// 작물: docs/spec/strawberry-minimal.md "테스트 (단위)"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>

#include "sim/processes.hpp"
#include "test_helpers.hpp"

using Catch::Matchers::WithinRel;

namespace {

constexpr double kRelTol = 1e-12;
constexpr double kDaySolar_W_per_m2 = 500.0;  // 맑은 낮 하우스 안 일사 (테스트 입력)
constexpr double kNightSolar_W_per_m2 = 0.0;
constexpr double kOptimalAir_degC = 20.0;  // temp_curve가 1인 기온

std::size_t idx(sim::StressFactor factor) { return static_cast<std::size_t>(factor); }

sim::Crop make_crop(const sim::Params& params) { return sim::process::plant_crop(test::test_crop_init(), params.crop); }

// 테스트에서 직접 만드는 유효량: 용액 상태 전체를 유효로 봐요 (M3: 양이온은 용액에서만 흡수, pH 곡선은 1로 가정)
sim::process::NutrientAvailability full_availability(const sim::SoilCell& cell) {
    return {.nh4_N = cell.nh4_solution_N_g_per_m2,
            .no3_N = cell.no3_N_g_per_m2,
            .p = cell.p_g_per_m2,
            .k = cell.k_solution_g_per_m2,
            .ca = cell.ca_solution_g_per_m2,
            .mg = cell.mg_solution_g_per_m2,
            .mn = cell.mn_solution_g_per_m2};
}

struct ElementTotals {
    double N, P, K, Ca, Mg;
};

ElementTotals soil_plus_plant(const sim::SoilCell& cell, const sim::Crop& crop) {
    return {.N = cell.nh4_N_g_per_m2 + cell.no2_N_g_per_m2 + cell.no3_N_g_per_m2 + cell.labile_N_g_per_m2 + crop.plant_N_g_per_m2,
            .P = cell.p_g_per_m2 + crop.plant_P_g_per_m2,
            .K = cell.k_g_per_m2 + crop.plant_K_g_per_m2,
            .Ca = cell.ca_g_per_m2 + crop.plant_Ca_g_per_m2,
            .Mg = cell.mg_g_per_m2 + crop.plant_Mg_g_per_m2};
}

std::optional<sim::PlantDiedEvent> step(sim::Crop& crop, sim::SoilCell& cell, const sim::Params& params,
                                        const sim::process::NutrientAvailability& available, double solar_W_per_m2,
                                        sim::Ledger& ledger,
                                        sim::StressCombination mode = sim::StressCombination::Minimum,
                                        double air_degC = kOptimalAir_degC) {
    return sim::process::update_crop(crop, cell, params, available, solar_W_per_m2, air_degC, mode, 7, ledger);
}

// 사망할 때까지(또는 상한까지) 밤 틱을 반복
std::optional<sim::PlantDiedEvent> run_until_death(sim::Crop& crop, sim::SoilCell& cell, const sim::Params& params,
                                                   int max_ticks) {
    sim::Ledger ledger;
    const auto available = full_availability(cell);
    for (int t = 0; t < max_ticks; ++t) {
        if (auto died = step(crop, cell, params, available, kNightSolar_W_per_m2, ledger)) {
            return died;
        }
    }
    return std::nullopt;
}

// 체내 양분을 사치 흡수 한도(적정 농도 x 배율)에 맞춰 흡수가 0이 되게 해요
void fill_to_luxury(sim::Crop& crop, const sim::CropParams& cp) {
    const double biomass = crop.leaf_g_per_m2 + crop.root_g_per_m2;
    crop.plant_N_g_per_m2 = cp.target_N_g_per_g * cp.luxury_uptake_ratio * biomass;
    crop.plant_P_g_per_m2 = cp.target_P_g_per_g * cp.luxury_uptake_ratio * biomass;
    crop.plant_K_g_per_m2 = cp.target_K_g_per_g * cp.luxury_uptake_ratio * biomass;
    crop.plant_Ca_g_per_m2 = cp.target_Ca_g_per_g * cp.luxury_uptake_ratio * biomass;
    crop.plant_Mg_g_per_m2 = cp.target_Mg_g_per_g * cp.luxury_uptake_ratio * biomass;
}

bool finite_non_negative(double v) { return std::isfinite(v) && v >= 0.0; }

}  // namespace

// 수관 차광: LAI가 클수록 커지고, 작물이 없거나 죽으면 0
TEST_CASE("Crop canopy interception grows with leaf and is zero without a living crop", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::Crop crop = make_crop(params);
    const double base = sim::process::canopy_interception(crop, params.crop);
    CHECK(base > 0.0);
    CHECK(base < 1.0);

    sim::Crop leafy = crop;
    leafy.leaf_g_per_m2 *= 2.0;
    CHECK(sim::process::canopy_interception(leafy, params.crop) > base);

    sim::Crop dead = crop;
    dead.alive = false;
    CHECK(sim::process::canopy_interception(dead, params.crop) == 0.0);

    const sim::Crop absent;
    CHECK(sim::process::canopy_interception(absent, params.crop) == 0.0);
}

// 물·염류 계수: 마르면 water 감소, 짜면 salt 감소
TEST_CASE("Crop water and salt factors fall with drought and salinity", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    const auto wet = sim::process::water_salt_factors(cell, params.crop);
    CHECK(wet.water == 1.0);
    CHECK(wet.salt == 1.0);

    cell.water_mm = cell.field_capacity_mm * 0.3;
    cell.ec_dS_per_m = 4.0;
    const auto stressed = sim::process::water_salt_factors(cell, params.crop);
    CHECK(stressed.water < wet.water);
    CHECK(stressed.salt < wet.salt);
}

// 밤 (일사 0): 생장 0, 증산 0
TEST_CASE("Crop at night has zero growth and zero transpiration", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = make_crop(params);
    const double leaf_before = crop.leaf_g_per_m2;
    const double root_before = crop.root_g_per_m2;
    sim::Ledger ledger;

    const auto factors = sim::process::water_salt_factors(cell, params.crop);
    CHECK(sim::process::transpiration_demand_mm(crop, params.crop, kNightSolar_W_per_m2, factors) == 0.0);
    CHECK(sim::process::transpiration_demand_mm(crop, params.crop, kDaySolar_W_per_m2, factors) > 0.0);

    CHECK_FALSE(step(crop, cell, params, full_availability(cell), kNightSolar_W_per_m2, ledger));
    CHECK(crop.last_growth_g_per_m2 == 0.0);
    CHECK(crop.leaf_g_per_m2 == leaf_before);
    CHECK(crop.root_g_per_m2 == root_before);
}

// 같은 조건에서 잎이 많으면 받는 빛이 늘어 생장량 증가
TEST_CASE("Crop with more leaf grows more under the same conditions", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell_a = test::test_cell();
    sim::SoilCell cell_b = test::test_cell();
    sim::Crop small = make_crop(params);
    sim::Crop large = make_crop(params);
    large.leaf_g_per_m2 *= 2.0;
    large.plant_N_g_per_m2 = params.crop.target_N_g_per_g * (large.leaf_g_per_m2 + large.root_g_per_m2);
    large.plant_P_g_per_m2 = params.crop.target_P_g_per_g * (large.leaf_g_per_m2 + large.root_g_per_m2);
    large.plant_K_g_per_m2 = params.crop.target_K_g_per_g * (large.leaf_g_per_m2 + large.root_g_per_m2);
    large.plant_Ca_g_per_m2 = params.crop.target_Ca_g_per_g * (large.leaf_g_per_m2 + large.root_g_per_m2);
    large.plant_Mg_g_per_m2 = params.crop.target_Mg_g_per_g * (large.leaf_g_per_m2 + large.root_g_per_m2);
    large.plant_Mn_g_per_m2 = params.crop.target_Mn_g_per_g * (large.leaf_g_per_m2 + large.root_g_per_m2);
    sim::Ledger ledger;

    (void)step(small, cell_a, params, full_availability(cell_a), kDaySolar_W_per_m2, ledger);
    (void)step(large, cell_b, params, full_availability(cell_b), kDaySolar_W_per_m2, ledger);
    CHECK(small.last_growth_g_per_m2 > 0.0);
    CHECK(large.last_growth_g_per_m2 > small.last_growth_g_per_m2);
}

// minimum 대 product: product 생장량 ≤ minimum 생장량 (계수가 여러 개 1 미만이면 엄격히 작음)
TEST_CASE("Crop product stress combination grows no more than minimum", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell_min = test::test_cell();
    cell_min.water_mm = cell_min.field_capacity_mm * 0.6;  // water 계수 < 1
    sim::SoilCell cell_prod = cell_min;
    sim::Crop crop_min = make_crop(params);
    crop_min.root_health = 0.8;  // root 계수 < 1
    sim::Crop crop_prod = crop_min;
    sim::Ledger ledger;

    (void)step(crop_min, cell_min, params, full_availability(cell_min), kDaySolar_W_per_m2, ledger,
               sim::StressCombination::Minimum);
    (void)step(crop_prod, cell_prod, params, full_availability(cell_prod), kDaySolar_W_per_m2, ledger,
               sim::StressCombination::Product);
    CHECK(crop_min.last_growth_g_per_m2 > 0.0);
    CHECK(crop_prod.last_growth_g_per_m2 <= crop_min.last_growth_g_per_m2);
    CHECK(crop_prod.last_growth_g_per_m2 < crop_min.last_growth_g_per_m2);

    // 계수가 하나만 1 미만이면 두 모드가 같아요
    sim::SoilCell cell_a = test::test_cell();
    sim::SoilCell cell_b = test::test_cell();
    sim::Crop one_a = make_crop(params);
    one_a.root_health = 0.8;
    sim::Crop one_b = one_a;
    (void)step(one_a, cell_a, params, full_availability(cell_a), kDaySolar_W_per_m2, ledger,
               sim::StressCombination::Minimum);
    (void)step(one_b, cell_b, params, full_availability(cell_b), kDaySolar_W_per_m2, ledger,
               sim::StressCombination::Product);
    CHECK_THAT(one_b.last_growth_g_per_m2, WithinRel(one_a.last_growth_g_per_m2, kRelTol));
}

// 유효 P만 부족: 자라면서 P 충족도가 떨어지고 stress_hours의 P만 증가
TEST_CASE("Crop with only P unavailable loses P sufficiency and records P stress", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = make_crop(params);
    sim::Ledger ledger;
    auto available = full_availability(cell);
    available.p = 0.0;

    for (int t = 0; t < 48; ++t) {
        (void)step(crop, cell, params, available, kDaySolar_W_per_m2, ledger);
    }
    const auto status = sim::process::nutrient_status(crop, params.crop);
    CHECK(status.sufficiency_P < 1.0);
    CHECK(status.sufficiency_N == 1.0);
    CHECK(status.sufficiency_K == 1.0);
    CHECK(status.sufficiency_Ca == 1.0);
    CHECK(status.sufficiency_Mg == 1.0);
    CHECK(crop.stress_hours[idx(sim::StressFactor::NutrientP)] > 0.0);
    for (std::size_t i = 0; i < sim::kStressFactorCount; ++i) {
        if (i != idx(sim::StressFactor::NutrientP)) {
            CHECK(crop.stress_hours[i] == 0.0);
        }
    }
    CHECK(ledger.P_uptake_g == 0.0);
}

// 모든 계수가 1이면 stress_hours를 기록하지 않음
TEST_CASE("Crop stress hours are not recorded when all factors are one", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = make_crop(params);
    sim::Ledger ledger;
    (void)step(crop, cell, params, full_availability(cell), kDaySolar_W_per_m2, ledger,
               sim::StressCombination::Product);
    CHECK(crop.last_growth_g_per_m2 > 0.0);
    for (const double hours : crop.stress_hours) {
        CHECK(hours == 0.0);
    }
}

// 동률이면 StressFactor 순서상 앞선 요인 (Water가 Root보다 앞)
TEST_CASE("Crop stress hours tie goes to the earlier factor", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.water_mm = cell.field_capacity_mm * 0.4;  // water_stress_curve(0.4) = 0.3
    sim::Crop crop = make_crop(params);
    crop.root_health = params.crop.water_stress_curve(cell.relative_water());
    sim::Ledger ledger;
    (void)step(crop, cell, params, full_availability(cell), kNightSolar_W_per_m2, ledger);
    CHECK(crop.stress_hours[idx(sim::StressFactor::Water)] == 1.0);
    CHECK(crop.stress_hours[idx(sim::StressFactor::Root)] == 0.0);
}

// 체내 양분이 목표의 1.3배면 흡수 0
TEST_CASE("Crop at luxury nutrient content takes up nothing", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = make_crop(params);
    const double biomass = crop.leaf_g_per_m2 + crop.root_g_per_m2;
    const double luxury = params.crop.luxury_uptake_ratio;
    crop.plant_N_g_per_m2 = params.crop.target_N_g_per_g * luxury * biomass;
    crop.plant_P_g_per_m2 = params.crop.target_P_g_per_g * luxury * biomass;
    crop.plant_K_g_per_m2 = params.crop.target_K_g_per_g * luxury * biomass;
    crop.plant_Ca_g_per_m2 = params.crop.target_Ca_g_per_g * luxury * biomass;
    crop.plant_Mg_g_per_m2 = params.crop.target_Mg_g_per_g * luxury * biomass;
    const sim::SoilCell cell_before = cell;
    sim::Ledger ledger;

    (void)step(crop, cell, params, full_availability(cell), kNightSolar_W_per_m2, ledger);
    CHECK(ledger.N_uptake_g == 0.0);
    CHECK(ledger.P_uptake_g == 0.0);
    CHECK(ledger.K_uptake_g == 0.0);
    CHECK(ledger.Ca_uptake_g == 0.0);
    CHECK(ledger.Mg_uptake_g == 0.0);
    CHECK(cell.nh4_N_g_per_m2 == cell_before.nh4_N_g_per_m2);
    CHECK(cell.no3_N_g_per_m2 == cell_before.no3_N_g_per_m2);
    CHECK(cell.pending_H_eq_per_m2 == 0.0);

    const auto status = sim::process::nutrient_status(crop, params.crop);
    CHECK(status.sufficiency_N == 1.0);
    CHECK_THAT(status.excess_N, WithinRel(luxury - 1.0, 1e-9));
}

// 뿌리 건강도 0.5: 흡수 능력 절반
TEST_CASE("Crop with half root health has half the uptake capacity", "[unit][crop]") {
    const sim::Params params = test::test_params();
    // 요구량이 능력보다 크도록 체내 양분을 적정치보다 낮게
    sim::Crop healthy = make_crop(params);
    healthy.plant_N_g_per_m2 *= 0.5;
    healthy.plant_P_g_per_m2 *= 0.5;
    healthy.plant_K_g_per_m2 *= 0.5;
    healthy.plant_Ca_g_per_m2 *= 0.5;
    healthy.plant_Mg_g_per_m2 *= 0.5;
    sim::Crop half = healthy;
    half.root_health = 0.5;
    const sim::process::NutrientAvailability available{
        .nh4_N = 0.2, .no3_N = 0.8, .p = 0.1, .k = 1.0, .ca = 1.0, .mg = 0.5};

    sim::SoilCell cell_healthy = test::test_cell();
    sim::SoilCell cell_half = test::test_cell();
    sim::Ledger ledger_healthy;
    sim::Ledger ledger_half;
    (void)step(healthy, cell_healthy, params, available, kNightSolar_W_per_m2, ledger_healthy);
    (void)step(half, cell_half, params, available, kNightSolar_W_per_m2, ledger_half);

    REQUIRE(ledger_healthy.N_uptake_g > 0.0);
    CHECK_THAT(ledger_half.N_uptake_g, WithinRel(0.5 * ledger_healthy.N_uptake_g, kRelTol));
    CHECK_THAT(ledger_half.P_uptake_g, WithinRel(0.5 * ledger_healthy.P_uptake_g, kRelTol));
    CHECK_THAT(ledger_half.K_uptake_g, WithinRel(0.5 * ledger_healthy.K_uptake_g, kRelTol));
    CHECK_THAT(ledger_half.Ca_uptake_g, WithinRel(0.5 * ledger_healthy.Ca_uptake_g, kRelTol));
    CHECK_THAT(ledger_half.Mg_uptake_g, WithinRel(0.5 * ledger_healthy.Mg_uptake_g, kRelTol));
}

// NH4 흡수는 pending_H 증가, NO3 흡수는 감소
TEST_CASE("Crop NH4 uptake raises pending H and NO3 uptake lowers it", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::Crop crop_nh4 = make_crop(params);
    crop_nh4.plant_N_g_per_m2 *= 0.5;
    sim::Crop crop_no3 = crop_nh4;
    sim::SoilCell cell_nh4 = test::test_cell();
    sim::SoilCell cell_no3 = test::test_cell();
    sim::Ledger ledger_nh4;
    sim::Ledger ledger_no3;

    auto nh4_only = full_availability(cell_nh4);
    nh4_only.no3_N = 0.0;
    auto no3_only = full_availability(cell_no3);
    no3_only.nh4_N = 0.0;

    (void)step(crop_nh4, cell_nh4, params, nh4_only, kNightSolar_W_per_m2, ledger_nh4);
    (void)step(crop_no3, cell_no3, params, no3_only, kNightSolar_W_per_m2, ledger_no3);

    REQUIRE(ledger_nh4.N_uptake_g > 0.0);
    REQUIRE(ledger_no3.N_uptake_g > 0.0);
    CHECK(cell_nh4.pending_H_eq_per_m2 > 0.0);
    CHECK(cell_no3.pending_H_eq_per_m2 < 0.0);
    // NH4만 유효하면 NO3 풀은 그대로, 반대도 마찬가지
    CHECK(cell_nh4.no3_N_g_per_m2 == test::test_cell().no3_N_g_per_m2);
    CHECK(cell_no3.nh4_N_g_per_m2 == test::test_cell().nh4_N_g_per_m2);
    CHECK_THAT(cell_nh4.pending_H_eq_per_m2,
               WithinRel(ledger_nh4.N_uptake_g / params.ions.nh4_N_g_per_eq *
                             params.soil_chemistry.uptake_H_eq_per_eq_NH4,
                         kRelTol));
}

// N 유효량이 0이면 흡수 0, pending_H 그대로
TEST_CASE("Crop with zero N availability takes up no N", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::Crop crop = make_crop(params);
    crop.plant_N_g_per_m2 *= 0.5;
    sim::SoilCell cell = test::test_cell();
    sim::Ledger ledger;
    auto available = full_availability(cell);
    available.nh4_N = 0.0;
    available.no3_N = 0.0;
    (void)step(crop, cell, params, available, kNightSolar_W_per_m2, ledger);
    CHECK(ledger.N_uptake_g == 0.0);
    CHECK(cell.pending_H_eq_per_m2 == 0.0);
    CHECK(std::isfinite(crop.plant_N_g_per_m2));
}

// 원소별 보존: 토양 풀 + 체내 풀이 update_crop 전후로 같음, 장부 흡수량 = 체내 증가량
TEST_CASE("Crop uptake conserves soil plus plant pools for every element", "[unit][crop][conservation]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = make_crop(params);
    sim::Ledger ledger;
    const ElementTotals initial = soil_plus_plant(cell, crop);
    const sim::Crop crop_initial = crop;

    for (int t = 0; t < 240; ++t) {
        const double solar = (t % 24 >= 6 && t % 24 < 18) ? kDaySolar_W_per_m2 : kNightSolar_W_per_m2;
        (void)step(crop, cell, params, full_availability(cell), solar, ledger);
        const ElementTotals now = soil_plus_plant(cell, crop);
        REQUIRE_THAT(now.N, WithinRel(initial.N, kRelTol));
        REQUIRE_THAT(now.P, WithinRel(initial.P, kRelTol));
        REQUIRE_THAT(now.K, WithinRel(initial.K, kRelTol));
        REQUIRE_THAT(now.Ca, WithinRel(initial.Ca, kRelTol));
        REQUIRE_THAT(now.Mg, WithinRel(initial.Mg, kRelTol));
    }
    REQUIRE(ledger.N_uptake_g > 0.0);
    CHECK_THAT(crop.plant_N_g_per_m2 - crop_initial.plant_N_g_per_m2, WithinRel(ledger.N_uptake_g, 1e-9));
    CHECK_THAT(crop.plant_P_g_per_m2 - crop_initial.plant_P_g_per_m2, WithinRel(ledger.P_uptake_g, 1e-9));
    CHECK_THAT(crop.plant_K_g_per_m2 - crop_initial.plant_K_g_per_m2, WithinRel(ledger.K_uptake_g, 1e-9));
    CHECK_THAT(crop.plant_Ca_g_per_m2 - crop_initial.plant_Ca_g_per_m2, WithinRel(ledger.Ca_uptake_g, 1e-9));
    CHECK_THAT(crop.plant_Mg_g_per_m2 - crop_initial.plant_Mg_g_per_m2, WithinRel(ledger.Mg_uptake_g, 1e-9));
}

// 흡수량은 유효량·요구량·토양 풀을 넘지 않음
TEST_CASE("Crop uptake never exceeds availability, demand or the soil pool", "[unit][crop]") {
    const sim::Params params = test::test_params();
    const auto check_bounds = [&](sim::Crop crop, sim::SoilCell cell,
                                  const sim::process::NutrientAvailability& available) {
        const double biomass = crop.leaf_g_per_m2 + crop.root_g_per_m2;
        const double luxury = params.crop.luxury_uptake_ratio;
        const double demand_N = std::max(0.0, params.crop.target_N_g_per_g * luxury * biomass - crop.plant_N_g_per_m2);
        const double demand_P = std::max(0.0, params.crop.target_P_g_per_g * luxury * biomass - crop.plant_P_g_per_m2);
        const double demand_K = std::max(0.0, params.crop.target_K_g_per_g * luxury * biomass - crop.plant_K_g_per_m2);
        const sim::SoilCell before = cell;
        sim::Ledger ledger;
        (void)step(crop, cell, params, available, kNightSolar_W_per_m2, ledger);
        // N은 NH4·NO3로 나눠 더하므로 반올림 1 ulp 정도를 허용
        constexpr double kSplitTol = 1.0 + 1e-12;
        CHECK(ledger.N_uptake_g > 0.0);
        CHECK(ledger.N_uptake_g <= (available.nh4_N + available.no3_N) * kSplitTol);
        CHECK(ledger.N_uptake_g <= demand_N * kSplitTol);
        CHECK(ledger.N_uptake_g <= (before.nh4_N_g_per_m2 + before.no3_N_g_per_m2) * kSplitTol);
        CHECK(ledger.P_uptake_g <= available.p);
        CHECK(ledger.P_uptake_g <= demand_P);
        CHECK(ledger.K_uptake_g <= available.k);
        CHECK(ledger.K_uptake_g <= demand_K);
        CHECK(cell.nh4_N_g_per_m2 >= 0.0);
        CHECK(cell.no3_N_g_per_m2 >= 0.0);
        CHECK(cell.p_g_per_m2 >= 0.0);
        CHECK(cell.k_g_per_m2 >= 0.0);
        CHECK(cell.ca_g_per_m2 >= 0.0);
        CHECK(cell.mg_g_per_m2 >= 0.0);
    };

    SECTION("availability limits a hungry crop with a huge root") {
        sim::Crop crop = make_crop(params);
        crop.root_g_per_m2 = 5000.0;  // rate × root ≥ 1 → 능력 = 유효량 전체
        crop.plant_N_g_per_m2 = 0.0;
        crop.plant_P_g_per_m2 = 0.0;
        crop.plant_K_g_per_m2 = 0.0;
        check_bounds(crop, test::test_cell(),
                     {.nh4_N = 0.01, .no3_N = 0.02, .p = 0.001, .k = 0.01, .ca = 0.01, .mg = 0.01});
    }
    SECTION("demand limits a well-fed crop in rich soil") {
        sim::Crop crop = make_crop(params);
        crop.root_g_per_m2 = 5000.0;
        // 사치 흡수 상한 바로 아래: 요구량이 유효량보다 훨씬 작음
        const double biomass = crop.leaf_g_per_m2 + crop.root_g_per_m2;
        const double near_luxury = 0.999 * params.crop.luxury_uptake_ratio * biomass;
        crop.plant_N_g_per_m2 = params.crop.target_N_g_per_g * near_luxury;
        crop.plant_P_g_per_m2 = params.crop.target_P_g_per_g * near_luxury;
        crop.plant_K_g_per_m2 = params.crop.target_K_g_per_g * near_luxury;
        const sim::SoilCell cell = test::test_cell();
        check_bounds(crop, cell, full_availability(cell));
    }
    SECTION("soil pool limits when availability claims more than the soil holds") {
        sim::Crop crop = make_crop(params);
        crop.root_g_per_m2 = 5000.0;
        crop.plant_N_g_per_m2 = 0.0;
        crop.plant_P_g_per_m2 = 0.0;
        crop.plant_K_g_per_m2 = 0.0;
        sim::SoilCell cell = test::test_cell();
        cell.nh4_N_g_per_m2 = 0.001;
        cell.no3_N_g_per_m2 = 0.002;
        cell.p_g_per_m2 = 0.0001;
        cell.k_g_per_m2 = 0.0;
        check_bounds(crop, cell, {.nh4_N = 1000, .no3_N = 1000, .p = 1000, .k = 1000, .ca = 1000, .mg = 1000});
    }
}

// 손상 요인 4가지 각각: 뿌리 건강도 감소, 해당 요인이 사망 원인으로 기록
TEST_CASE("Crop root damage from each cause lowers health and is reported at death", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::RootDamageCause expected = sim::RootDamageCause::Waterlog;

    SECTION("waterlog") {
        cell.aeration_index = 0.0;
        expected = sim::RootDamageCause::Waterlog;
    }
    SECTION("heat") {
        cell.soil_temp_degC = 45.0;
        expected = sim::RootDamageCause::Heat;
    }
    SECTION("nitrite") {
        cell.ph = 7.0;
        cell.no2_N_g_per_m2 = 8.0;
        expected = sim::RootDamageCause::Nitrite;
    }
    SECTION("acid") {
        cell.ph = 4.0;
        expected = sim::RootDamageCause::Acid;
    }

    sim::Crop crop = make_crop(params);
    sim::Ledger ledger;
    (void)step(crop, cell, params, full_availability(cell), kNightSolar_W_per_m2, ledger);
    CHECK(crop.root_health < 1.0);
    CHECK(crop.alive);

    const auto died = run_until_death(crop, cell, params, 5000);
    REQUIRE(died.has_value());
    CHECK(died->cause == expected);
    CHECK(died->tile_index == 7u);
    CHECK_FALSE(crop.alive);
    CHECK(crop.present);
    CHECK(crop.root_health < params.crop.death_root_health);
    CHECK(crop.root_health >= 0.0);
}

// 손상이 섞여 있으면 이번 틱 가장 큰 항이 사망 원인
TEST_CASE("Crop death cause is the dominant damage term of the tick", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::RootDamageCause expected = sim::RootDamageCause::Waterlog;

    // 각 경우: 지배 항 1개 + 더 작은 다른 항 1개
    SECTION("waterlog dominates heat") {
        cell.aeration_index = 0.0;   // 0.01
        cell.soil_temp_degC = 35.0;  // 0.002
        expected = sim::RootDamageCause::Waterlog;
    }
    SECTION("heat dominates waterlog") {
        cell.aeration_index = 0.2;   // 0.004
        cell.soil_temp_degC = 40.0;  // 0.01
        expected = sim::RootDamageCause::Heat;
    }
    SECTION("nitrite dominates acid") {
        cell.ph = 5.0;               // acid 0.0006, 독성 배율 5.5
        cell.no2_N_g_per_m2 = 2.0;   // 유효 11 → 0.015
        expected = sim::RootDamageCause::Nitrite;
    }
    SECTION("acid dominates waterlog") {
        cell.ph = 4.0;               // 0.006
        cell.aeration_index = 0.4;   // 0.0005
        expected = sim::RootDamageCause::Acid;
    }

    sim::Crop crop = make_crop(params);
    crop.root_health = params.crop.death_root_health * 1.0001;  // 이번 틱 손상으로 고사
    sim::Ledger ledger;
    const auto died = step(crop, cell, params, full_availability(cell), kNightSolar_W_per_m2, ledger);
    REQUIRE(died.has_value());
    CHECK(died->cause == expected);
    CHECK_FALSE(crop.alive);
}

// 손상 없음: 뿌리 건강도가 1 쪽으로 회복
TEST_CASE("Crop root health recovers toward one without damage", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = make_crop(params);
    crop.root_health = 0.5;
    sim::Ledger ledger;
    (void)step(crop, cell, params, full_availability(cell), kNightSolar_W_per_m2, ledger);
    CHECK(crop.root_health > 0.5);
    CHECK(crop.root_health < 1.0);
    CHECK_THAT(crop.root_health, WithinRel(0.5 + params.crop.root_recovery_rate_per_hour * 0.5, kRelTol));

    for (int t = 0; t < 100000; ++t) {
        (void)step(crop, cell, params, full_availability(cell), kNightSolar_W_per_m2, ledger);
    }
    CHECK(crop.root_health <= 1.0);
    CHECK(crop.root_health > 0.99);
}

// 손상 없음: 뿌리 건물은 줄지 않음 (밤이라 생장도 없음)
TEST_CASE("Crop root mass does not shrink without damage", "[unit][crop][dieback]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = make_crop(params);
    crop.root_health = 0.5;
    const double root_before = crop.root_g_per_m2;
    sim::Ledger ledger;
    (void)step(crop, cell, params, full_availability(cell), kNightSolar_W_per_m2, ledger);
    CHECK(crop.root_g_per_m2 == root_before);
    CHECK(ledger.C_root_litter_g == 0.0);
}

// 손상 있음: 뿌리 건물이 root x min(1, damage) x dieback만큼 줄고, 그 몫의 양분·C가 토양으로 (원소·C 보존)
TEST_CASE("Crop root dieback moves root mass nutrients and carbon to the soil", "[unit][crop][dieback][conservation]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.aeration_index = 0.0;  // 과습 손상
    sim::Crop crop = make_crop(params);
    fill_to_luxury(crop, params.crop);  // 흡수 0: 변화는 뿌리 고사만
    const sim::Crop before = crop;
    const sim::SoilCell cell_before = cell;
    const ElementTotals initial = soil_plus_plant(cell, crop);
    sim::Ledger ledger;
    (void)step(crop, cell, params, full_availability(cell), kNightSolar_W_per_m2, ledger);

    const double damage = params.root_damage.waterlog_curve(cell_before.aeration_index);
    REQUIRE(damage > 0.0);
    const double dead_root = before.root_g_per_m2 * std::min(1.0, damage) * params.crop.root_dieback_per_damage;
    const double share = dead_root / (before.leaf_g_per_m2 + before.root_g_per_m2);
    CHECK_THAT(crop.root_g_per_m2, WithinRel(before.root_g_per_m2 - dead_root, kRelTol));
    CHECK(crop.leaf_g_per_m2 == before.leaf_g_per_m2);
    CHECK_THAT(crop.plant_N_g_per_m2, WithinRel(before.plant_N_g_per_m2 * (1.0 - share), 1e-9));
    CHECK_THAT(crop.plant_Mg_g_per_m2, WithinRel(before.plant_Mg_g_per_m2 * (1.0 - share), 1e-9));
    // 밤이라 흡수 없음: 토양 풀이 정확히 그 몫만큼 늘어요
    CHECK_THAT(cell.labile_N_g_per_m2 - cell_before.labile_N_g_per_m2,
               WithinRel(before.plant_N_g_per_m2 * share, 1e-9));
    CHECK_THAT(cell.p_g_per_m2 - cell_before.p_g_per_m2, WithinRel(before.plant_P_g_per_m2 * share, 1e-9));
    CHECK_THAT(cell.k_g_per_m2 - cell_before.k_g_per_m2, WithinRel(before.plant_K_g_per_m2 * share, 1e-9));
    CHECK_THAT(cell.ca_g_per_m2 - cell_before.ca_g_per_m2, WithinRel(before.plant_Ca_g_per_m2 * share, 1e-9));
    CHECK_THAT(cell.mg_g_per_m2 - cell_before.mg_g_per_m2, WithinRel(before.plant_Mg_g_per_m2 * share, 1e-9));
    const double litter_C = dead_root * params.crop.dry_matter_C_fraction;
    CHECK_THAT(cell.labile_C_g_per_m2 - cell_before.labile_C_g_per_m2, WithinRel(litter_C, 1e-9));
    CHECK_THAT(ledger.C_root_litter_g, WithinRel(litter_C, 1e-9));

    const ElementTotals now = soil_plus_plant(cell, crop);
    CHECK_THAT(now.N, WithinRel(initial.N, kRelTol));
    CHECK_THAT(now.P, WithinRel(initial.P, kRelTol));
    CHECK_THAT(now.K, WithinRel(initial.K, kRelTol));
    CHECK_THAT(now.Ca, WithinRel(initial.Ca, kRelTol));
    CHECK_THAT(now.Mg, WithinRel(initial.Mg, kRelTol));
    // 장부 흡수/유입은 없어요 (계 안의 이동)
    CHECK(ledger.N_uptake_g == 0.0);
    CHECK(ledger.N_in_g == 0.0);
}

// 고사 순서: 뿌리 건강도 갱신 → 뿌리 고사 → 고사 판정 (고사하는 틱에도 뿌리 고사가 반영됨)
TEST_CASE("Crop root dieback is applied on the tick the plant dies", "[unit][crop][dieback]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.aeration_index = 0.0;
    sim::Crop crop = make_crop(params);
    crop.root_health = params.crop.death_root_health * 1.0001;
    const double root_before = crop.root_g_per_m2;
    sim::Ledger ledger;
    const auto died = step(crop, cell, params, full_availability(cell), kNightSolar_W_per_m2, ledger);
    REQUIRE(died.has_value());
    CHECK(crop.root_g_per_m2 < root_before);
    CHECK(ledger.C_root_litter_g > 0.0);
}

// dieback = 0: 뿌리 건물 변화 없음 (M2 초안 동작)
TEST_CASE("Crop root mass is unchanged when dieback is zero", "[unit][crop][dieback]") {
    sim::Params params = test::test_params();
    params.crop.root_dieback_per_damage = 0.0;
    sim::SoilCell cell = test::test_cell();
    cell.aeration_index = 0.0;
    sim::Crop crop = make_crop(params);
    fill_to_luxury(crop, params.crop);
    const sim::Crop before = crop;
    const sim::SoilCell cell_before = cell;
    sim::Ledger ledger;
    (void)step(crop, cell, params, full_availability(cell), kNightSolar_W_per_m2, ledger);
    CHECK(crop.root_health < 1.0);
    CHECK(crop.root_g_per_m2 == before.root_g_per_m2);
    CHECK(crop.plant_N_g_per_m2 == before.plant_N_g_per_m2);
    CHECK(cell.labile_C_g_per_m2 == cell_before.labile_C_g_per_m2);
    CHECK(ledger.C_root_litter_g == 0.0);
}

// 같은 아질산, pH 5.0 대 7.0: 5.0에서 (아질산에 의한) 손상이 큼
TEST_CASE("Crop nitrite damage is larger at pH 5 than at pH 7", "[unit][crop]") {
    const sim::Params params = test::test_params();
    const auto health_after = [&](double ph, double no2_g_N_per_m2) {
        sim::SoilCell cell = test::test_cell();
        cell.ph = ph;
        cell.no2_N_g_per_m2 = no2_g_N_per_m2;
        sim::Crop crop = make_crop(params);
        sim::Ledger ledger;
        (void)step(crop, cell, params, full_availability(cell), kNightSolar_W_per_m2, ledger);
        return crop.root_health;
    };
    constexpr double kNitrite_g_N_per_m2 = 1.5;
    // pH 자체(산성) 손상을 빼고 아질산 몫만 비교
    const double nitrite_loss_ph5 = health_after(5.0, 0.0) - health_after(5.0, kNitrite_g_N_per_m2);
    const double nitrite_loss_ph7 = health_after(7.0, 0.0) - health_after(7.0, kNitrite_g_N_per_m2);
    CHECK(nitrite_loss_ph5 > nitrite_loss_ph7);
    CHECK(nitrite_loss_ph5 > 0.0);
    CHECK(health_after(5.0, kNitrite_g_N_per_m2) < health_after(7.0, kNitrite_g_N_per_m2));
}

// 죽은 작물: 생장·흡수·증산 0, 아무것도 바뀌지 않음
TEST_CASE("Dead crop does not grow, take up or transpire", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = make_crop(params);
    crop.plant_N_g_per_m2 *= 0.5;
    crop.alive = false;
    const sim::Crop crop_before = crop;
    const sim::SoilCell cell_before = cell;
    sim::Ledger ledger;

    const auto factors = sim::process::water_salt_factors(cell, params.crop);
    CHECK(sim::process::transpiration_demand_mm(crop, params.crop, kDaySolar_W_per_m2, factors) == 0.0);
    CHECK(sim::process::canopy_interception(crop, params.crop) == 0.0);
    CHECK_FALSE(step(crop, cell, params, full_availability(cell), kDaySolar_W_per_m2, ledger));
    CHECK(crop == crop_before);
    CHECK(cell == cell_before);
    CHECK(ledger == sim::Ledger{});

    // 정식하지 않은 타일도 마찬가지
    sim::Crop absent;
    CHECK_FALSE(step(absent, cell, params, full_availability(cell), kDaySolar_W_per_m2, ledger));
    CHECK(absent == sim::Crop{});
}

// 건물량 0이어도 충족도·과잉 지수가 유한
TEST_CASE("Crop nutrient status is finite with zero biomass", "[unit][crop]") {
    const sim::Params params = test::test_params();
    sim::Crop crop = make_crop(params);
    crop.leaf_g_per_m2 = 0.0;
    crop.root_g_per_m2 = 0.0;
    const auto status = sim::process::nutrient_status(crop, params.crop);
    CHECK(status.sufficiency_N == 1.0);
    CHECK(status.excess_N == 0.0);
    CHECK(std::isfinite(status.sufficiency_Mg));
    CHECK(std::isfinite(status.excess_Mg));

    sim::SoilCell cell = test::test_cell();
    sim::Ledger ledger;
    (void)step(crop, cell, params, full_availability(cell), kDaySolar_W_per_m2, ledger);
    CHECK(crop.last_growth_g_per_m2 == 0.0);
    CHECK(ledger.N_uptake_g == 0.0);
}

// 극한 입력(물 0, EC 100, pH 3/10, NO2 1000, 고온·강일사)에서도 유한하고 음수 없음
TEST_CASE("Crop stays finite and non-negative under extreme inputs", "[unit][crop][extreme]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    double air_degC = kOptimalAir_degC;
    double solar_W_per_m2 = kDaySolar_W_per_m2;

    SECTION("zero water") { cell.water_mm = 0.0; }
    SECTION("EC 100") { cell.ec_dS_per_m = 100.0; }
    SECTION("pH 3") { cell.ph = 3.0; }
    SECTION("pH 10") { cell.ph = 10.0; }
    SECTION("NO2 1000") { cell.no2_N_g_per_m2 = 1000.0; }
    SECTION("hot and bright") {
        cell.soil_temp_degC = 80.0;
        air_degC = 60.0;
        solar_W_per_m2 = 5000.0;
    }
    SECTION("everything at once") {
        cell.water_mm = 0.0;
        cell.ec_dS_per_m = 100.0;
        cell.ph = 3.0;
        cell.no2_N_g_per_m2 = 1000.0;
        cell.aeration_index = 0.0;
        cell.soil_temp_degC = 80.0;
    }

    sim::Crop crop = make_crop(params);
    sim::Ledger ledger;
    const ElementTotals initial = soil_plus_plant(cell, crop);
    const sim::process::NutrientAvailability huge{
        .nh4_N = 1e6, .no3_N = 1e6, .p = 1e6, .k = 1e6, .ca = 1e6, .mg = 1e6};
    for (int t = 0; t < 2000; ++t) {
        (void)sim::process::update_crop(crop, cell, params, huge, solar_W_per_m2, air_degC,
                                        sim::StressCombination::Product, 0, ledger);
        const auto factors = sim::process::water_salt_factors(cell, params.crop);
        REQUIRE(finite_non_negative(sim::process::transpiration_demand_mm(crop, params.crop, solar_W_per_m2, factors)));
    }

    CHECK(finite_non_negative(crop.leaf_g_per_m2));
    CHECK(finite_non_negative(crop.root_g_per_m2));
    CHECK(finite_non_negative(crop.root_health));
    CHECK(crop.root_health <= 1.0);
    CHECK(finite_non_negative(crop.plant_N_g_per_m2));
    CHECK(finite_non_negative(crop.plant_P_g_per_m2));
    CHECK(finite_non_negative(crop.plant_K_g_per_m2));
    CHECK(finite_non_negative(crop.plant_Ca_g_per_m2));
    CHECK(finite_non_negative(crop.plant_Mg_g_per_m2));
    CHECK(finite_non_negative(crop.last_growth_g_per_m2));
    for (const double hours : crop.stress_hours) {
        CHECK(finite_non_negative(hours));
    }
    CHECK(finite_non_negative(cell.nh4_N_g_per_m2));
    CHECK(finite_non_negative(cell.no3_N_g_per_m2));
    CHECK(finite_non_negative(cell.p_g_per_m2));
    CHECK(finite_non_negative(cell.k_g_per_m2));
    CHECK(finite_non_negative(cell.ca_g_per_m2));
    CHECK(finite_non_negative(cell.mg_g_per_m2));
    CHECK(std::isfinite(cell.pending_H_eq_per_m2));

    const auto status = sim::process::nutrient_status(crop, params.crop);
    CHECK(finite_non_negative(status.sufficiency_N));
    CHECK(finite_non_negative(status.excess_N));

    const ElementTotals now = soil_plus_plant(cell, crop);
    CHECK_THAT(now.N, WithinRel(initial.N, kRelTol));
    CHECK_THAT(now.P, WithinRel(initial.P, kRelTol));
    CHECK_THAT(now.K, WithinRel(initial.K, kRelTol));
    CHECK_THAT(now.Ca, WithinRel(initial.Ca, kRelTol));
    CHECK_THAT(now.Mg, WithinRel(initial.Mg, kRelTol));
}

// ---- M3: Ca·Mg 전용 흡수 속도, 길항(수요 상한과 하한), 용액 상태 흡수, 곱셈 모드, 뿌리 고사 반환 ----
// (docs/spec/strawberry-minimal.md M3 개정, cation-exchange.md "흡수 길항")

namespace {

// 길항 지수를 원하는 값으로 만든 셀: Ca·Mg 용액을 정하고 K(또는 NH4) 용액으로 (K + NH4) / (Ca + Mg) 당량비를 맞춰요.
sim::SoilCell cell_with_antagonism_index(const sim::Params& params, double antagonism_index, double ca_solution,
                                         double mg_solution, double nh4_share_of_monovalent = 0.0) {
    sim::SoilCell cell = test::test_cell();
    cell.ca_solution_g_per_m2 = ca_solution;
    cell.mg_solution_g_per_m2 = mg_solution;
    cell.ca_g_per_m2 = std::max(cell.ca_g_per_m2, 2.0 * ca_solution);
    cell.mg_g_per_m2 = std::max(cell.mg_g_per_m2, 2.0 * mg_solution);
    const double divalent_eq = ca_solution / params.ions.ca_g_per_eq + mg_solution / params.ions.mg_g_per_eq;
    const double monovalent_eq = antagonism_index * divalent_eq;
    cell.k_solution_g_per_m2 = (1.0 - nh4_share_of_monovalent) * monovalent_eq * params.ions.k_g_per_eq;
    cell.nh4_solution_N_g_per_m2 = nh4_share_of_monovalent * monovalent_eq * params.ions.nh4_N_g_per_eq;
    cell.k_g_per_m2 = std::max(cell.k_g_per_m2, 2.0 * cell.k_solution_g_per_m2);
    cell.nh4_N_g_per_m2 = std::max(cell.nh4_N_g_per_m2, 2.0 * cell.nh4_solution_N_g_per_m2);
    return cell;
}

// 흡수 능력이 병목이 되지 않도록 크게 준 유효량 (직접 지정)
sim::process::NutrientAvailability abundant_availability() {
    return {.nh4_N = 0.0, .no3_N = 0.0, .p = 0.0, .k = 0.0, .ca = 2000.0, .mg = 2000.0, .mn = 0.0};
}

// 목표 농도 × 사치 배율 × 길항 배율 × 건물량
double luxury_cap_g_per_m2(double target_g_per_g, const sim::Params& params, const sim::Crop& crop, double factor) {
    return target_g_per_g * params.crop.luxury_uptake_ratio * factor * (crop.leaf_g_per_m2 + crop.root_g_per_m2);
}

}  // namespace

// Ca·Mg는 일반 흡수 속도가 아니라 전용 속도(ca_·mg_uptake_rate)로 능력이 정해진다
TEST_CASE("Ca and Mg uptake capacity use their own uptake rates", "[unit][crop][m3]") {
    const sim::Params params = test::test_params();
    const auto run = [&](const sim::Params& p) {
        sim::SoilCell cell = cell_with_antagonism_index(p, 0.0, 30.0, 10.0);  // 길항 없음 (K·NH4 용액 0)
        sim::Crop crop = make_crop(p);
        crop.plant_Ca_g_per_m2 = 0.0;
        crop.plant_Mg_g_per_m2 = 0.0;
        const sim::process::NutrientAvailability available{
            .nh4_N = 0.0, .no3_N = 0.0, .p = 0.0, .k = 0.0, .ca = 10.0, .mg = 10.0, .mn = 0.0};
        sim::Ledger ledger;
        (void)step(crop, cell, p, available, kNightSolar_W_per_m2, ledger);
        return ledger;
    };
    const sim::Ledger base = run(params);
    const double root = make_crop(params).root_g_per_m2;  // M4: 모종 B 8포기 = 12 g/m²
    // 식: 유효량 × min(1, 전용 속도 × 뿌리 × 건강도) × water × salt (여기서 water = salt = 1, 건강도 1, 길항 1)
    CHECK(base.Ca_uptake_g ==
          Catch::Approx(10.0 * std::min(1.0, params.crop.ca_uptake_rate_per_g_root_per_hour * root)));
    CHECK(base.Mg_uptake_g ==
          Catch::Approx(10.0 * std::min(1.0, params.crop.mg_uptake_rate_per_g_root_per_hour * root)));
    // Ca·Mg 전용 속도는 일반 속도보다 작아요
    CHECK(params.crop.ca_uptake_rate_per_g_root_per_hour < params.crop.uptake_rate_per_g_root_per_hour);
    CHECK(params.crop.mg_uptake_rate_per_g_root_per_hour < params.crop.uptake_rate_per_g_root_per_hour);

    // Ca 속도만 2배 → Ca 흡수만 2배, Mg는 그대로
    sim::Params fast_ca = params;
    fast_ca.crop.ca_uptake_rate_per_g_root_per_hour *= 2.0;
    const sim::Ledger doubled = run(fast_ca);
    CHECK(doubled.Ca_uptake_g == Catch::Approx(2.0 * base.Ca_uptake_g));
    CHECK(doubled.Mg_uptake_g == base.Mg_uptake_g);

    // Mg 속도만 2배
    sim::Params fast_mg = params;
    fast_mg.crop.mg_uptake_rate_per_g_root_per_hour *= 2.0;
    const sim::Ledger mg_doubled = run(fast_mg);
    CHECK(mg_doubled.Mg_uptake_g == Catch::Approx(2.0 * base.Mg_uptake_g));
    CHECK(mg_doubled.Ca_uptake_g == base.Ca_uptake_g);

    // 일반 속도를 바꿔도 Ca·Mg 흡수는 변하지 않아요
    sim::Params fast_general = params;
    fast_general.crop.uptake_rate_per_g_root_per_hour *= 3.0;
    const sim::Ledger general = run(fast_general);
    CHECK(general.Ca_uptake_g == base.Ca_uptake_g);
    CHECK(general.Mg_uptake_g == base.Mg_uptake_g);
}

// 길항은 수요 상한에도 걸린다: 상한 = 목표 × 1.3 × max(min_demand_factor, 배율) × 건물량. 하한 덕에 흡수가 완전히 멈추지 않는다
TEST_CASE("Antagonism lowers the Ca and Mg demand cap down to the minimum demand floor", "[unit][crop][m3]") {
    const sim::Params params = test::test_params();
    REQUIRE(params.antagonism.min_demand_factor > params.antagonism.ca_curve(3.0));  // 하한이 쓰이는 영역이 있어야 의미가 있어요
    for (const double index : {0.0, 0.3, 0.7, 3.0}) {
        INFO("antagonism index " << index);
        sim::SoilCell cell = cell_with_antagonism_index(params, index, 100.0, 20.0);
        sim::Crop crop = make_crop(params);
        const double ca_factor = std::max(params.antagonism.min_demand_factor, params.antagonism.ca_curve(index));
        const double mg_factor = std::max(params.antagonism.min_demand_factor, params.antagonism.mg_curve(index));
        // 시작 체내량은 어떤 상한보다도 낮게 (수요가 양수)
        crop.plant_Ca_g_per_m2 = 0.3 * luxury_cap_g_per_m2(params.crop.target_Ca_g_per_g, params, crop, 1.0);
        crop.plant_Mg_g_per_m2 = 0.3 * luxury_cap_g_per_m2(params.crop.target_Mg_g_per_g, params, crop, 1.0);
        sim::Ledger ledger;
        (void)step(crop, cell, params, abundant_availability(), kNightSolar_W_per_m2, ledger);
        // 능력이 충분하므로 수요 상한(하한 적용 후 배율)까지 정확히 채워져요
        CHECK(crop.plant_Ca_g_per_m2 ==
              Catch::Approx(luxury_cap_g_per_m2(params.crop.target_Ca_g_per_g, params, crop, ca_factor)));
        CHECK(crop.plant_Mg_g_per_m2 ==
              Catch::Approx(luxury_cap_g_per_m2(params.crop.target_Mg_g_per_g, params, crop, mg_factor)));
    }

    // 강한 길항에서도 하한 때문에 흡수가 0이 되지 않는다 (체내 양이 하한 상한보다 낮으면 계속 흡수)
    sim::SoilCell strong = cell_with_antagonism_index(params, 3.0, 100.0, 20.0);
    sim::Crop crop = make_crop(params);
    crop.plant_Ca_g_per_m2 = 0.5 * luxury_cap_g_per_m2(params.crop.target_Ca_g_per_g, params, crop, 1.0);
    sim::Ledger ledger;
    (void)step(crop, strong, params, abundant_availability(), kNightSolar_W_per_m2, ledger);
    CHECK(ledger.Ca_uptake_g > 0.0);
    CHECK(crop.antagonism_Ca_factor < params.antagonism.min_demand_factor);  // 능력 배율은 하한 없이 더 낮아요
}

// 길항은 흡수 능력에도 걸리지만 하한은 능력에 적용되지 않는다 (능력 배율 = 곡선값 그대로)
TEST_CASE("Antagonism factor scales capacity without the floor", "[unit][crop][m3]") {
    const sim::Params params = test::test_params();
    const double index = 3.0;
    const double curve_ca = params.antagonism.ca_curve(index);
    REQUIRE(curve_ca < params.antagonism.min_demand_factor);
    sim::SoilCell cell = cell_with_antagonism_index(params, index, 30.0, 10.0);
    sim::Crop crop = make_crop(params);
    crop.plant_Ca_g_per_m2 = 0.0;
    const sim::process::NutrientAvailability available{
        .nh4_N = 0.0, .no3_N = 0.0, .p = 0.0, .k = 0.0, .ca = 10.0, .mg = 0.0, .mn = 0.0};
    sim::Ledger ledger;
    (void)step(crop, cell, params, available, kNightSolar_W_per_m2, ledger);
    const double root = make_crop(params).root_g_per_m2;  // M4: 모종 B 8포기 = 12 g/m²
    CHECK(crop.antagonism_Ca_factor == Catch::Approx(curve_ca));
    CHECK(ledger.Ca_uptake_g ==
          Catch::Approx(10.0 * std::min(1.0, params.crop.ca_uptake_rate_per_g_root_per_hour * root) * curve_ca));
}

// 길항 지수의 분자에는 K뿐 아니라 NH4도 들어간다 (같은 당량이면 같은 길항)
TEST_CASE("Antagonism counts NH4 like K by equivalents", "[unit][crop][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell with_k = cell_with_antagonism_index(params, 0.8, 30.0, 10.0, 0.0);
    sim::SoilCell with_nh4 = cell_with_antagonism_index(params, 0.8, 30.0, 10.0, 1.0);
    sim::Crop crop_k = make_crop(params);
    sim::Crop crop_nh4 = make_crop(params);
    sim::Ledger ledger;
    (void)step(crop_k, with_k, params, abundant_availability(), kNightSolar_W_per_m2, ledger);
    (void)step(crop_nh4, with_nh4, params, abundant_availability(), kNightSolar_W_per_m2, ledger);
    CHECK(crop_k.antagonism_Ca_factor < 1.0);
    CHECK(crop_nh4.antagonism_Ca_factor == Catch::Approx(crop_k.antagonism_Ca_factor));
    CHECK(crop_nh4.antagonism_Mg_factor == Catch::Approx(crop_k.antagonism_Mg_factor));
}

// 분모(용액 Ca + Mg)가 0이면 길항 없음 (배율 1), NaN 없음
TEST_CASE("Antagonism is absent when there is no divalent cation in solution", "[unit][crop][m3][extreme]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.ca_solution_g_per_m2 = 0.0;
    cell.mg_solution_g_per_m2 = 0.0;
    cell.k_solution_g_per_m2 = 10.0;
    sim::Crop crop = make_crop(params);
    crop.antagonism_Ca_factor = 0.123;  // 이전 값이 남아 있어도 이번 틱 값으로 덮어써요
    sim::Ledger ledger;
    (void)step(crop, cell, params, full_availability(cell), kNightSolar_W_per_m2, ledger);
    CHECK(crop.antagonism_Ca_factor == 1.0);
    CHECK(crop.antagonism_Mg_factor == 1.0);
}

// 흡수는 용액 상태에서: 총량과 용액이 같은 양 줄고 흡수량 = 장부. NO3는 총량만
TEST_CASE("Cation uptake draws the same amount from total and solution", "[unit][crop][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    const sim::SoilCell before = cell;
    sim::Crop crop = make_crop(params);
    crop.plant_N_g_per_m2 = 0.0;
    crop.plant_K_g_per_m2 = 0.0;
    crop.plant_Ca_g_per_m2 = 0.0;
    crop.plant_Mg_g_per_m2 = 0.0;
    crop.plant_Mn_g_per_m2 = 0.0;
    sim::Ledger ledger;
    (void)step(crop, cell, params, full_availability(cell), kNightSolar_W_per_m2, ledger);

    const double nh4_drop = before.nh4_N_g_per_m2 - cell.nh4_N_g_per_m2;
    const double no3_drop = before.no3_N_g_per_m2 - cell.no3_N_g_per_m2;
    CHECK(nh4_drop > 0.0);
    CHECK(before.nh4_solution_N_g_per_m2 - cell.nh4_solution_N_g_per_m2 == Catch::Approx(nh4_drop));
    CHECK(no3_drop > 0.0);
    CHECK(ledger.N_uptake_g == Catch::Approx(nh4_drop + no3_drop));

    CHECK(ledger.K_uptake_g > 0.0);
    CHECK(before.k_g_per_m2 - cell.k_g_per_m2 == Catch::Approx(ledger.K_uptake_g));
    CHECK(before.k_solution_g_per_m2 - cell.k_solution_g_per_m2 == Catch::Approx(ledger.K_uptake_g));
    CHECK(ledger.Ca_uptake_g > 0.0);
    CHECK(before.ca_g_per_m2 - cell.ca_g_per_m2 == Catch::Approx(ledger.Ca_uptake_g));
    CHECK(before.ca_solution_g_per_m2 - cell.ca_solution_g_per_m2 == Catch::Approx(ledger.Ca_uptake_g));
    CHECK(ledger.Mg_uptake_g > 0.0);
    CHECK(before.mg_g_per_m2 - cell.mg_g_per_m2 == Catch::Approx(ledger.Mg_uptake_g));
    CHECK(before.mg_solution_g_per_m2 - cell.mg_solution_g_per_m2 == Catch::Approx(ledger.Mg_uptake_g));
    CHECK(ledger.Mn_uptake_g > 0.0);
    CHECK(before.mn_g_per_m2 - cell.mn_g_per_m2 == Catch::Approx(ledger.Mn_uptake_g));
    CHECK(before.mn_solution_g_per_m2 - cell.mn_solution_g_per_m2 == Catch::Approx(ledger.Mn_uptake_g));
}

// 용액이 모자라면 능력·수요가 커도 용액까지만 흡수 (용액은 0 아래로 안 감)
TEST_CASE("Cation uptake never exceeds the solution amount", "[unit][crop][m3][extreme]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.k_solution_g_per_m2 = 0.01;
    sim::Crop crop = make_crop(params);
    crop.plant_K_g_per_m2 = 0.0;
    auto available = full_availability(cell);
    available.k = 1000.0;  // 유효량 입력이 용액보다 커도
    sim::Ledger ledger;
    (void)step(crop, cell, params, available, kNightSolar_W_per_m2, ledger);
    CHECK(ledger.K_uptake_g <= 0.01);
    CHECK(cell.k_solution_g_per_m2 >= 0.0);
    CHECK(cell.k_g_per_m2 >= 0.0);
    CHECK(cell.k_solution_g_per_m2 <= cell.k_g_per_m2);
}

// product 모드: 각 양분 계수(Mn 결핍·독성 포함)를 모두 곱한다. minimum은 가장 작은 것 하나
TEST_CASE("Product mode multiplies every nutrient and toxicity factor", "[unit][crop][m3]") {
    const sim::Params params = test::test_params();
    const sim::CropParams& cp = params.crop;
    const sim::process::NutrientAvailability none{};
    const auto growth = [&](const std::function<void(sim::Crop&)>& tweak, sim::StressCombination mode) {
        sim::SoilCell cell = test::test_cell();
        sim::Crop crop = make_crop(params);
        tweak(crop);
        sim::Ledger ledger;
        (void)step(crop, cell, params, none, kDaySolar_W_per_m2, ledger, mode);
        return crop.last_growth_g_per_m2;
    };
    const auto required = [&](double target_g_per_g) {
        const sim::Crop crop = make_crop(params);
        return target_g_per_g * (crop.leaf_g_per_m2 + crop.root_g_per_m2);
    };
    const double reference = growth([](sim::Crop&) {}, sim::StressCombination::Product);
    REQUIRE(reference > 0.0);

    // K 0.8, Ca 0.8, Mn 0.9 충족: 곱 = f(0.8) × f(0.8) × f(0.9), 최소 = 가장 작은 f
    const auto deficient = [&](sim::Crop& crop) {
        crop.plant_K_g_per_m2 = 0.8 * required(cp.target_K_g_per_g);
        crop.plant_Ca_g_per_m2 = 0.8 * required(cp.target_Ca_g_per_g);
        crop.plant_Mn_g_per_m2 = 0.9 * required(cp.target_Mn_g_per_g);
    };
    const double f_k = cp.nutrient_stress_curve(0.8);
    const double f_mn = cp.nutrient_stress_curve(0.9);
    REQUIRE(f_k < 1.0);
    REQUIRE(f_mn < 1.0);
    CHECK(growth(deficient, sim::StressCombination::Product) == Catch::Approx(reference * f_k * f_k * f_mn));
    CHECK(growth(deficient, sim::StressCombination::Minimum) == Catch::Approx(reference * std::min(f_k, f_mn)));

    // Mn 독성(체내 비율 10 → 0.7)과 K 결핍(0.8)이 함께: 곱 = 0.7 × f(0.8)
    const auto toxic_and_deficient = [&](sim::Crop& crop) {
        crop.plant_K_g_per_m2 = 0.8 * required(cp.target_K_g_per_g);
        crop.plant_Mn_g_per_m2 = 10.0 * required(cp.target_Mn_g_per_g);
    };
    const double tox = cp.mn_toxicity_curve(10.0);
    CHECK(growth(toxic_and_deficient, sim::StressCombination::Product) == Catch::Approx(reference * tox * f_k));
    CHECK(growth(toxic_and_deficient, sim::StressCombination::Minimum) ==
          Catch::Approx(reference * std::min(tox, f_k)));
}

// 뿌리 고사: 죽은 뿌리 몫의 K·Ca·Mg·Mn이 토양 총량과 용액에 함께 돌아간다. N은 이분해성 유기 N, P는 유효 풀로
TEST_CASE("Root dieback returns K Ca Mg and Mn to both the total and the solution",
          "[unit][crop][conservation][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.aeration_index = 0.0;  // 과습 손상 (waterlog_curve(0) > 0) → 뿌리 고사
    const sim::SoilCell before = cell;
    sim::Crop crop = make_crop(params);
    const sim::Crop crop_before = crop;
    const double damage = params.root_damage.waterlog_curve(0.0);
    REQUIRE(damage > 0.0);
    const double biomass = crop.leaf_g_per_m2 + crop.root_g_per_m2;
    const double dead_root = crop.root_g_per_m2 * std::min(1.0, damage) * params.crop.root_dieback_per_damage;
    const double share = dead_root / biomass;
    REQUIRE(dead_root > 0.0);

    sim::Ledger ledger;
    // 흡수는 0이도록 유효량 0 (밤이라 생장도 0)
    (void)step(crop, cell, params, sim::process::NutrientAvailability{}, kNightSolar_W_per_m2, ledger);
    REQUIRE(crop.root_g_per_m2 == Catch::Approx(crop_before.root_g_per_m2 - dead_root));

    struct Row {
        double plant_before, plant_after, total_before, total_after, solution_before, solution_after;
    };
    const Row rows[] = {
        {crop_before.plant_K_g_per_m2, crop.plant_K_g_per_m2, before.k_g_per_m2, cell.k_g_per_m2,
         before.k_solution_g_per_m2, cell.k_solution_g_per_m2},
        {crop_before.plant_Ca_g_per_m2, crop.plant_Ca_g_per_m2, before.ca_g_per_m2, cell.ca_g_per_m2,
         before.ca_solution_g_per_m2, cell.ca_solution_g_per_m2},
        {crop_before.plant_Mg_g_per_m2, crop.plant_Mg_g_per_m2, before.mg_g_per_m2, cell.mg_g_per_m2,
         before.mg_solution_g_per_m2, cell.mg_solution_g_per_m2},
        {crop_before.plant_Mn_g_per_m2, crop.plant_Mn_g_per_m2, before.mn_g_per_m2, cell.mn_g_per_m2,
         before.mn_solution_g_per_m2, cell.mn_solution_g_per_m2},
    };
    for (const Row& row : rows) {
        const double returned = row.plant_before * share;
        CHECK(returned > 0.0);
        CHECK(row.plant_before - row.plant_after == Catch::Approx(returned));
        CHECK(row.total_after - row.total_before == Catch::Approx(returned));
        CHECK(row.solution_after - row.solution_before == Catch::Approx(returned));
        CHECK(row.solution_after <= row.total_after);
    }
    // N은 이분해성 유기 N으로, P는 유효 풀로 (M2 규칙 그대로)
    CHECK(cell.labile_N_g_per_m2 - before.labile_N_g_per_m2 == Catch::Approx(crop_before.plant_N_g_per_m2 * share));
    CHECK(cell.nh4_solution_N_g_per_m2 == before.nh4_solution_N_g_per_m2);
    CHECK(cell.p_g_per_m2 - before.p_g_per_m2 == Catch::Approx(crop_before.plant_P_g_per_m2 * share));
}
