// M3 단위 테스트: 망간 (docs/spec/manganese.md "테스트 (단위)")
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>

#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "test_helpers.hpp"

namespace {

constexpr double kNightSolar_W_per_m2 = 0.0;
constexpr double kDaySolar_W_per_m2 = 500.0;
constexpr double kOptimalAir_degC = 20.0;

std::size_t idx(sim::StressFactor factor) { return static_cast<std::size_t>(factor); }

const sim::FertilizerDef& find_fertilizer(const sim::Params& params, const std::string& id) {
    for (const auto& fertilizer : params.fertilizers) {
        if (fertilizer.id == id) {
            return fertilizer;
        }
    }
    throw std::runtime_error("fertilizer not found: " + id);
}

// 산화량 측정용: 산화물 풀을 비우면 한 틱 뒤 산화물 풀 = 산화량 (환원 0)
sim::SoilCell oxidation_probe(double ph, double aeration) {
    sim::SoilCell cell = test::test_cell();
    cell.ph = ph;
    cell.aeration_index = aeration;
    cell.mn_g_per_m2 = 10.0;
    cell.mn_solution_g_per_m2 = 4.0;
    cell.mn_oxide_g_per_m2 = 0.0;
    return cell;
}

// 환원량 측정용: 유효태를 비우면 한 틱 뒤 유효태 = 환원량 (산화 0)
sim::SoilCell reduction_probe(double ph, double aeration) {
    sim::SoilCell cell = test::test_cell();
    cell.ph = ph;
    cell.aeration_index = aeration;
    cell.mn_g_per_m2 = 0.0;
    cell.mn_solution_g_per_m2 = 0.0;
    cell.mn_oxide_g_per_m2 = 100.0;
    return cell;
}

double oxidized_in_one_tick(double ph, double aeration, const sim::Params& params) {
    sim::SoilCell cell = oxidation_probe(ph, aeration);
    sim::process::redox_manganese(cell, params.manganese);
    return cell.mn_oxide_g_per_m2;
}

double reduced_in_one_tick(double ph, double aeration, const sim::Params& params) {
    sim::SoilCell cell = reduction_probe(ph, aeration);
    sim::process::redox_manganese(cell, params.manganese);
    return cell.mn_g_per_m2;
}

sim::Crop make_crop(const sim::Params& params) { return sim::process::plant_crop(test::test_crop_init(), params.crop); }

double biomass_of(const sim::Crop& crop) { return crop.leaf_g_per_m2 + crop.root_g_per_m2; }

// 용액에 Mn이 충분한 유효량 (다른 원소는 0이라 Mn 흡수만 봐요)
sim::process::NutrientAvailability mn_only(double mn_g_per_m2) {
    return {.nh4_N = 0.0, .no3_N = 0.0, .p = 0.0, .k = 0.0, .ca = 0.0, .mg = 0.0, .mn = mn_g_per_m2};
}

// 도중에 쓰는 한 틱 (하우스 기온은 적온)
void step(sim::Crop& crop, sim::SoilCell& cell, const sim::Params& params,
          const sim::process::NutrientAvailability& available, double solar, sim::Ledger& ledger,
          sim::StressCombination mode = sim::StressCombination::Minimum) {
    (void)sim::process::update_crop(crop, cell, params, available, solar, kOptimalAir_degC, mode, 0, ledger);
}

}  // namespace

// 같은 유효태, pH 5.0 대 5.5 대 6.5 → 산화량 6.5 > 5.5 > 5.0, 환원량은 반대
TEST_CASE("Oxidation grows and reduction shrinks as pH rises", "[unit][manganese][m3]") {
    const sim::Params params = test::test_params();
    const double ox_50 = oxidized_in_one_tick(5.0, 1.0, params);
    const double ox_55 = oxidized_in_one_tick(5.5, 1.0, params);
    const double ox_65 = oxidized_in_one_tick(6.5, 1.0, params);
    CHECK(ox_50 > 0.0);
    CHECK(ox_65 > ox_55);
    CHECK(ox_55 > ox_50);

    const double red_50 = reduced_in_one_tick(5.0, 1.0, params);
    const double red_55 = reduced_in_one_tick(5.5, 1.0, params);
    const double red_65 = reduced_in_one_tick(6.5, 1.0, params);
    CHECK(red_65 > 0.0);
    CHECK(red_50 > red_55);
    CHECK(red_55 > red_65);
}

// 한 틱 식: 산화 = 유효태 × min(1, 속도 × pH곡선 × 통기곡선), 환원 = 산화물 × min(1, 속도 × pH곡선 × 통기곡선)
TEST_CASE("One tick of redox follows the spec formula", "[unit][manganese][m3]") {
    const sim::Params params = test::test_params();
    const sim::ManganeseParams& mp = params.manganese;
    sim::SoilCell cell = test::test_cell();
    cell.ph = 5.8;
    cell.aeration_index = 0.5;
    cell.mn_g_per_m2 = 12.0;
    cell.mn_solution_g_per_m2 = 5.0;
    cell.mn_oxide_g_per_m2 = 80.0;
    const double oxidized =
        12.0 * std::min(1.0, mp.oxidation_rate_per_hour * mp.oxidation_ph_curve(5.8) * mp.oxidation_o2_curve(0.5));
    const double reduced =
        80.0 * std::min(1.0, mp.reduction_rate_per_hour * mp.reduction_ph_curve(5.8) * mp.reduction_o2_curve(0.5));
    sim::process::redox_manganese(cell, mp);
    CHECK(cell.mn_g_per_m2 == Catch::Approx(12.0 - oxidized + reduced));
    CHECK(cell.mn_oxide_g_per_m2 == Catch::Approx(80.0 + oxidized - reduced));
    // 산화는 용액을 비례 차감하고 환원은 용액으로 들어가요
    CHECK(cell.mn_solution_g_per_m2 == Catch::Approx(5.0 * (12.0 - oxidized) / 12.0 + reduced));
}

// 같은 유효태, pH 5.0 대 6.5 대 8.0 → 평형 용액 Mn은 5.0 > 6.5 > 8.0 (흡착 세기)
TEST_CASE("Equilibrium solution Mn falls as pH rises through stronger retention", "[unit][manganese][m3]") {
    const sim::Params params = test::test_params();
    const auto solution_at = [&](double ph) {
        sim::SoilCell cell = test::test_cell();
        cell.ph = ph;
        return sim::process::exchange_equilibrium(cell, params).mn_solution_g_per_m2;
    };
    const double at_50 = solution_at(5.0);
    const double at_65 = solution_at(6.5);
    const double at_80 = solution_at(8.0);
    CHECK(at_50 > at_65);
    CHECK(at_65 > at_80);
    // 세기 w_Mn은 pH 의존 곡선 그대로
    sim::SoilCell cell = test::test_cell();
    cell.ph = 5.0;
    CHECK(sim::process::manganese_retention_factor(cell, params.manganese) == params.manganese.retention_ph_curve(5.0));
}

// 같은 pH 6.5, 통기 0.1 대 0.8 → 0.1 쪽 환원량 큼, 산화량 작음
TEST_CASE("Poor aeration raises reduction and lowers oxidation at the same pH", "[unit][manganese][m3]") {
    const sim::Params params = test::test_params();
    CHECK(reduced_in_one_tick(6.5, 0.1, params) > reduced_in_one_tick(6.5, 0.8, params));
    CHECK(oxidized_in_one_tick(6.5, 0.1, params) < oxidized_in_one_tick(6.5, 0.8, params));
    // 산소가 없으면 산화는 완전히 멈춰요
    CHECK(oxidized_in_one_tick(6.5, 0.0, params) == 0.0);
}

// 통기 회복 → 유효태가 바로 돌아가지 않고 산화 속도로 서서히 줄어든다
TEST_CASE("Available Mn declines slowly after aeration recovers", "[unit][manganese][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.ph = 6.5;
    // 과습: 산화물이 녹아 나와 유효태가 쌓여요
    cell.aeration_index = 0.0;
    for (int hour = 0; hour < 24 * 5; ++hour) {
        sim::process::redox_manganese(cell, params.manganese);
    }
    const double peak = cell.mn_g_per_m2;
    REQUIRE(peak > 5.0);  // 처음 5 g보다 늘었어요

    // 물이 빠짐: 통기 회복
    cell.aeration_index = 1.0;
    double previous = peak;
    for (int hour = 0; hour < 24; ++hour) {
        sim::process::redox_manganese(cell, params.manganese);
        CHECK(cell.mn_g_per_m2 < previous);  // 서서히 줄어요
        previous = cell.mn_g_per_m2;
        if (hour == 0) {
            CHECK(cell.mn_g_per_m2 > 0.99 * peak);  // 바로 돌아가지 않아요
        }
    }
    CHECK(cell.mn_g_per_m2 > 0.85 * peak);  // 하루가 지나도 대부분 남아요
}

// 산화·환원 속도 극단(곡선 × 속도 ≥ 1) → 원천 풀이 0이 되고 음수가 아니다
TEST_CASE("Extreme redox rates empty the source pool without going negative", "[unit][manganese][m3][extreme]") {
    sim::Params params = test::test_params();
    SECTION("oxidation extreme") {
        params.manganese.oxidation_rate_per_hour = 1.0e6;
        params.manganese.reduction_rate_per_hour = 0.0;
        sim::SoilCell cell = test::test_cell();
        const double total = cell.mn_g_per_m2 + cell.mn_oxide_g_per_m2;
        sim::process::redox_manganese(cell, params.manganese);
        CHECK(cell.mn_g_per_m2 == 0.0);
        CHECK(cell.mn_solution_g_per_m2 == 0.0);
        CHECK(cell.mn_oxide_g_per_m2 >= 0.0);
        CHECK(cell.mn_g_per_m2 + cell.mn_oxide_g_per_m2 == Catch::Approx(total).epsilon(1e-12));
    }
    SECTION("reduction extreme") {
        params.manganese.oxidation_rate_per_hour = 0.0;
        params.manganese.reduction_rate_per_hour = 1.0e6;
        sim::SoilCell cell = test::test_cell();
        const double total = cell.mn_g_per_m2 + cell.mn_oxide_g_per_m2;
        sim::process::redox_manganese(cell, params.manganese);
        CHECK(cell.mn_oxide_g_per_m2 == 0.0);
        CHECK(cell.mn_g_per_m2 >= 0.0);
        CHECK(cell.mn_solution_g_per_m2 <= cell.mn_g_per_m2);
        CHECK(cell.mn_g_per_m2 + cell.mn_oxide_g_per_m2 == Catch::Approx(total).epsilon(1e-12));
    }
    SECTION("both extreme swaps the pools") {
        params.manganese.oxidation_rate_per_hour = 1.0e6;
        params.manganese.reduction_rate_per_hour = 1.0e6;
        sim::SoilCell cell = test::test_cell();
        const double mn_before = cell.mn_g_per_m2;
        const double oxide_before = cell.mn_oxide_g_per_m2;
        sim::process::redox_manganese(cell, params.manganese);
        CHECK(cell.mn_g_per_m2 >= 0.0);
        CHECK(cell.mn_oxide_g_per_m2 >= 0.0);
        CHECK(cell.mn_g_per_m2 == Catch::Approx(oxide_before * std::min(1.0, params.manganese.reduction_rate_per_hour *
                                                                                params.manganese.reduction_ph_curve(cell.ph) *
                                                                                params.manganese.reduction_o2_curve(cell.aeration_index))));
        CHECK(cell.mn_oxide_g_per_m2 == Catch::Approx(mn_before));
    }
}

// 같은 Ca 당량, 석회(pH 상승) 대 질산칼슘(pH 변화 없음), 산성 토양 14일 → 석회 쪽 용액 Mn과 유효태가 작다
TEST_CASE("Lime lowers solution and available Mn more than calcium nitrate at the same Ca equivalents",
          "[unit][manganese][m3]") {
    const sim::Params params = test::test_params();
    const sim::LimeDef& lime = params.limes.front();  // 탄산석회: Ca 100%
    const sim::FertilizerDef& nitrate = find_fertilizer(params, "calcium_nitrate");
    REQUIRE(lime.id == "calcium_carbonate");
    const double nitrate_g_per_m2 = 100.0;
    const double ca_eq = nitrate_g_per_m2 / 1000.0 * nitrate.ca_g_per_kg / params.ions.ca_g_per_eq;
    const double lime_g_per_m2 = ca_eq / lime.ca_fraction / lime.eq_per_kg * 1000.0;  // 같은 Ca 당량

    const auto run = [&](bool with_lime) {
        sim::Scenario scenario = test::test_scenario(1, 1, 1);
        scenario.soil.ph = 5.0;
        sim::World world = test::make_world(std::move(scenario));
        const sim::TileArea area = sim::whole_grid(1, 1);
        if (with_lime) {
            test::must_enqueue(world, sim::AddLimeCommand{area, lime.id, lime_g_per_m2});
        } else {
            test::must_enqueue(world, sim::AddFertilizerCommand{area, nitrate.id, nitrate_g_per_m2});
        }
        double solution_sum = 0.0;
        const int ticks = 24 * 14;
        for (int t = 0; t < ticks; ++t) {
            world.step(1);
            solution_sum += world.snapshot().soil[0].mn_solution_g_per_m2;
        }
        const sim::SoilCell final_cell = world.snapshot().soil[0];
        struct Result {
            double mean_solution;
            double available;
            double ph;
        };
        return Result{solution_sum / ticks, final_cell.mn_g_per_m2, final_cell.ph};
    };

    const auto limed = run(true);
    const auto nitrated = run(false);
    CHECK(limed.ph > nitrated.ph);  // 석회만 pH를 올려요
    CHECK(limed.mean_solution < nitrated.mean_solution);
    CHECK(limed.available < nitrated.available);
}

// 체내 Mn이 적정의 30배 → 흡수 0 (수요 상한 mn_max_uptake_ratio)
TEST_CASE("Mn uptake stops when the plant holds the maximum multiple of the target", "[unit][manganese][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = make_crop(params);
    crop.plant_Mn_g_per_m2 = params.crop.target_Mn_g_per_g * params.crop.mn_max_uptake_ratio * biomass_of(crop);
    sim::Ledger ledger;
    step(crop, cell, params, mn_only(cell.mn_solution_g_per_m2), kNightSolar_W_per_m2, ledger);
    CHECK(ledger.Mn_uptake_g == 0.0);
    CHECK(crop.plant_Mn_g_per_m2 == params.crop.target_Mn_g_per_g * params.crop.mn_max_uptake_ratio * biomass_of(crop));

    // 상한 바로 아래면 모자란 만큼만 흡수 (능력이 충분하면 수요가 병목)
    sim::SoilCell cell_below = test::test_cell();
    cell_below.mn_g_per_m2 = 50.0;
    cell_below.mn_solution_g_per_m2 = 50.0;
    sim::Crop below = make_crop(params);
    const double cap = params.crop.target_Mn_g_per_g * params.crop.mn_max_uptake_ratio * biomass_of(below);
    const double gap = 0.01 * cap;
    below.plant_Mn_g_per_m2 = cap - gap;
    sim::Ledger below_ledger;
    step(below, cell_below, params, mn_only(50.0), kNightSolar_W_per_m2, below_ledger);
    CHECK(below_ledger.Mn_uptake_g == Catch::Approx(gap));
    CHECK(below.plant_Mn_g_per_m2 == Catch::Approx(cap));
}

// Mn 흡수 능력 = 용액 유효량 × min(1, 속도 × 뿌리 × 건강도) × water × salt, 흡수는 용액에서 총량과 함께 빠진다
TEST_CASE("Mn uptake capacity follows the spec and draws from solution and total together", "[unit][manganese][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.mn_g_per_m2 = 30.0;
    cell.mn_solution_g_per_m2 = 20.0;
    sim::Crop crop = make_crop(params);
    crop.plant_Mn_g_per_m2 = 0.0;  // 수요가 충분히 큼
    crop.root_health = 0.5;
    const double expected = 20.0 * std::min(1.0, params.crop.mn_uptake_rate_per_g_root_per_hour * crop.root_g_per_m2 * 0.5);
    sim::Ledger ledger;
    step(crop, cell, params, mn_only(20.0), kNightSolar_W_per_m2, ledger);
    CHECK(ledger.Mn_uptake_g == Catch::Approx(expected));
    CHECK(crop.plant_Mn_g_per_m2 == Catch::Approx(expected));
    CHECK(cell.mn_g_per_m2 == Catch::Approx(30.0 - expected));
    CHECK(cell.mn_solution_g_per_m2 == Catch::Approx(20.0 - expected));
    CHECK(cell.mn_oxide_g_per_m2 == 100.0);  // 산화물은 흡수되지 않아요
}

// 체내 Mn 비율 10 → 생장 계수 0.7, ToxicityMn이 가장 작으면 stress_hours에 기록
TEST_CASE("Mn toxicity ratio ten gives a growth factor of 0.7 and records ToxicityMn stress hours",
          "[unit][manganese][m3]") {
    const sim::Params params = test::test_params();
    REQUIRE(params.crop.mn_toxicity_curve(10.0) == Catch::Approx(0.7));
    sim::SoilCell healthy_cell = test::test_cell();
    sim::SoilCell toxic_cell = test::test_cell();
    sim::Crop healthy = make_crop(params);
    sim::Crop toxic = make_crop(params);
    toxic.plant_Mn_g_per_m2 = 10.0 * params.crop.target_Mn_g_per_g * biomass_of(toxic);
    REQUIRE(sim::process::nutrient_status(toxic, params.crop).plant_Mn_ratio == Catch::Approx(10.0));
    sim::Ledger ledger;
    const sim::process::NutrientAvailability none = mn_only(0.0);
    step(healthy, healthy_cell, params, none, kDaySolar_W_per_m2, ledger);
    step(toxic, toxic_cell, params, none, kDaySolar_W_per_m2, ledger);
    REQUIRE(healthy.last_growth_g_per_m2 > 0.0);
    CHECK(toxic.last_growth_g_per_m2 == Catch::Approx(0.7 * healthy.last_growth_g_per_m2));
    CHECK(toxic.stress_hours[idx(sim::StressFactor::ToxicityMn)] == 1.0);
    for (std::size_t i = 0; i < sim::kStressFactorCount; ++i) {
        if (i != idx(sim::StressFactor::ToxicityMn)) {
            CHECK(toxic.stress_hours[i] == 0.0);
        }
    }
    CHECK(healthy.stress_hours[idx(sim::StressFactor::ToxicityMn)] == 0.0);
}

// Mn 결핍: 체내 Mn이 모자라면 NutrientMn 스트레스 시간이 기록된다
TEST_CASE("Mn deficiency records NutrientMn stress hours", "[unit][manganese][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = make_crop(params);
    crop.plant_Mn_g_per_m2 = 0.5 * params.crop.target_Mn_g_per_g * biomass_of(crop);
    sim::Ledger ledger;
    step(crop, cell, params, mn_only(0.0), kDaySolar_W_per_m2, ledger);
    CHECK(crop.stress_hours[idx(sim::StressFactor::NutrientMn)] == 1.0);
    CHECK(crop.stress_hours[idx(sim::StressFactor::ToxicityMn)] == 0.0);
}

// 황산망간 투입 → 유효태·용액·Mn_in_g 증가, S도 증가. 산화물은 그대로
TEST_CASE("Manganese sulfate raises available Mn solution Mn the ledger and sulfur but not the oxide pool",
          "[unit][manganese][m3]") {
    const sim::Params params = test::test_params();
    const sim::FertilizerDef& mnso4 = find_fertilizer(params, "manganese_sulfate");
    sim::SoilCell cell = test::test_cell();
    const sim::SoilCell before = cell;
    const double g_per_m2 = 20.0;
    const double expected_mn = g_per_m2 / 1000.0 * mnso4.mn_g_per_kg;
    sim::Ledger ledger;
    sim::process::apply_fertilizer(cell, mnso4, g_per_m2, ledger);
    CHECK(cell.mn_g_per_m2 - before.mn_g_per_m2 == Catch::Approx(expected_mn));
    CHECK(cell.mn_solution_g_per_m2 - before.mn_solution_g_per_m2 == Catch::Approx(expected_mn));
    CHECK(ledger.Mn_in_g == Catch::Approx(expected_mn));
    CHECK(cell.s_g_per_m2 > before.s_g_per_m2);
    CHECK(ledger.S_in_g > 0.0);
    CHECK(cell.mn_oxide_g_per_m2 == before.mn_oxide_g_per_m2);
}

// 퇴비의 Mn도 유효태 총량과 용액에 들어가고 장부에 기록된다
TEST_CASE("Compost manganese goes to available Mn and solution", "[unit][manganese][m3]") {
    const sim::Params params = test::test_params();
    const sim::CompostDef& compost = params.composts.front();
    REQUIRE(compost.mn_g_per_kg > 0.0);
    sim::SoilCell cell = test::test_cell();
    const sim::SoilCell before = cell;
    sim::Ledger ledger;
    sim::process::apply_compost_minerals(cell, compost, 4.0, ledger);
    CHECK(cell.mn_g_per_m2 - before.mn_g_per_m2 == Catch::Approx(4.0 * compost.mn_g_per_kg));
    CHECK(cell.mn_solution_g_per_m2 - before.mn_solution_g_per_m2 == Catch::Approx(4.0 * compost.mn_g_per_kg));
    CHECK(ledger.Mn_in_g == Catch::Approx(4.0 * compost.mn_g_per_kg));
    CHECK(cell.mn_oxide_g_per_m2 == before.mn_oxide_g_per_m2);
}

// 용탈은 용액 Mn만 씻고 산화물은 씻지 않는다 (장부 Mn_leached_g)
TEST_CASE("Leaching takes only solution Mn and never the oxide pool", "[unit][manganese][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.drained_mm_this_tick = 10.0;
    const sim::SoilCell before = cell;
    sim::Ledger ledger;
    sim::process::leach(cell, params, ledger);
    const double f = 10.0 / (before.water_mm + 10.0);
    CHECK(ledger.Mn_leached_g == Catch::Approx(before.mn_solution_g_per_m2 * f));
    CHECK(before.mn_g_per_m2 - cell.mn_g_per_m2 == Catch::Approx(ledger.Mn_leached_g));
    CHECK(before.mn_solution_g_per_m2 - cell.mn_solution_g_per_m2 == Catch::Approx(ledger.Mn_leached_g));
    CHECK(cell.mn_oxide_g_per_m2 == before.mn_oxide_g_per_m2);
}

// 불변: 반복 산화·환원·교환 뒤에도 0 ≤ 용액 ≤ 유효태, 산화물 ≥ 0, 유효태 + 산화물 보존
TEST_CASE("Redox and exchange keep Mn pools valid and conserved over many ticks",
          "[unit][manganese][conservation][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    const double total = cell.mn_g_per_m2 + cell.mn_oxide_g_per_m2;
    for (int hour = 0; hour < 24 * 30; ++hour) {
        cell.ph = 4.5 + 4.0 * ((hour / 12) % 9) / 8.0;
        cell.aeration_index = (hour / 36) % 2 == 0 ? 0.05 : 1.0;
        sim::process::update_cation_exchange(cell, params);
        REQUIRE(cell.mn_g_per_m2 >= 0.0);
        REQUIRE(cell.mn_oxide_g_per_m2 >= 0.0);
        REQUIRE(cell.mn_solution_g_per_m2 >= 0.0);
        REQUIRE(cell.mn_solution_g_per_m2 <= cell.mn_g_per_m2);
        REQUIRE(cell.mn_g_per_m2 + cell.mn_oxide_g_per_m2 == Catch::Approx(total).epsilon(1e-12));
    }
}
