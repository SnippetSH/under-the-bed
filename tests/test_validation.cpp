// M2 패키지 단위 테스트 (docs/spec/m2-overview.md "구현 작업 분할")
#include <cmath>
#include <functional>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "sim/world.hpp"
#include "test_helpers.hpp"

namespace {

bool creates_with(const std::function<void(sim::Params&)>& break_params) {
    sim::Params p = test::test_params();
    break_params(p);
    return sim::World::create(std::move(p), test::test_scenario()).has_value();
}

bool creates_with(const std::function<void(sim::Scenario&)>& break_scenario, int /*scenario tag*/) {
    sim::Scenario s = test::test_scenario();
    break_scenario(s);
    return sim::World::create(test::test_params(), std::move(s)).has_value();
}

}  // namespace

// 기본 테스트 파라미터·시나리오는 M2 항목까지 모두 통과해야 해요.
TEST_CASE("Valid M2 params and scenario pass validation", "[unit][validation]") {
    CHECK(sim::World::create(test::test_params(), test::test_scenario()).has_value());

    sim::Scenario with_crop = test::test_scenario();
    with_crop.crop = test::test_crop_init();
    with_crop.stress_combination = sim::StressCombination::Product;
    CHECK(sim::World::create(test::test_params(), std::move(with_crop)).has_value());
}

// 잘못된 M2 파라미터는 거부해요.
TEST_CASE("Invalid M2 params are rejected", "[unit][validation]") {
    CHECK_FALSE(creates_with([](sim::Params& p) { p.organic_matter.o2_curve.points.clear(); }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.soil_water.drainage_compaction_curve.points = {{{1.0, 1.0}}, {{0.5, 0.5}}}; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.nitrification.step2_o2_curve.points.back()[1] = -0.1; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.aeration.tilled_compaction_index = 1.5; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.aeration.compaction_per_mm_irrigation = -1.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.ions.k_g_per_eq = 0.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.soil_chemistry.p_solution_fraction = 1.2; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.soil_chemistry.ph_min = p.soil_chemistry.ph_max; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.soil_chemistry.ph_max = 15.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.soil_chemistry.ec_min_water_mm = 0.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.soil_chemistry.uptake_H_eq_per_eq_NO3 = std::nan(""); }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.availability.p_ph_curve.points.resize(1); }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.root_damage.acid_curve.points.clear(); }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.crop.par_fraction = 1.5; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.crop.luxury_uptake_ratio = 0.9; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.crop.specific_leaf_area_m2_per_g = 0.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.crop.death_root_health = -0.1; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.crop.target_Mg_g_per_g = 0.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.crop.root_dieback_per_damage = -0.1; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.crop.root_dieback_per_damage = 1.1; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.crop.dry_matter_C_fraction = -0.1; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.crop.dry_matter_C_fraction = 1.1; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.composts[0].k_g_per_kg = -1.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.fertilizers[0].direct_acid_eq_per_kg = std::numeric_limits<double>::infinity(); }));
}

TEST_CASE("Negative direct acid and NO3 uptake proton values are allowed", "[unit][validation]") {
    CHECK(creates_with([](sim::Params& p) { p.fertilizers[0].direct_acid_eq_per_kg = -2.0; }));
    CHECK(creates_with([](sim::Params& p) { p.composts[0].direct_acid_eq_per_kg = -2.0; }));
    CHECK(creates_with([](sim::Params& p) { p.soil_chemistry.uptake_H_eq_per_eq_NO3 = -3.0; }));
}

// 석회 정의 검증
TEST_CASE("Lime definitions are validated", "[unit][validation]") {
    CHECK_FALSE(creates_with([](sim::Params& p) { p.limes[1].id = p.limes[0].id; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.limes[0].id.clear(); }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.limes[0].eq_per_kg = -1.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.limes[0].ca_fraction = 1.2; }));
    CHECK_FALSE(creates_with([](sim::Params& p) {
        p.limes[1].ca_fraction = 0.7;
        p.limes[1].mg_fraction = 0.5;
    }));
}

// 잘못된 M2 시나리오는 거부해요.
TEST_CASE("Invalid M2 scenarios are rejected", "[unit][validation]") {
    CHECK_FALSE(creates_with([](sim::Scenario& s) { s.soil.saturation_mm = s.soil.field_capacity_mm; }, 0));
    CHECK_FALSE(creates_with([](sim::Scenario& s) { s.soil.compaction_index = 1.1; }, 0));
    CHECK_FALSE(creates_with([](sim::Scenario& s) { s.soil.base_buffer_eq_per_m2_per_pH = 0.0; }, 0));
    CHECK_FALSE(creates_with([](sim::Scenario& s) { s.soil.ca_g_per_m2 = -1.0; }, 0));
    CHECK_FALSE(creates_with([](sim::Scenario& s) { s.crop = sim::CropInit{0.0, 0.0}; }, 0));
    CHECK_FALSE(creates_with([](sim::Scenario& s) { s.crop = sim::CropInit{-1.0, 5.0}; }, 0));
    CHECK_FALSE(creates_with([](sim::Scenario& s) { s.crop = sim::CropInit{std::nan(""), 5.0}; }, 0));
    CHECK_FALSE(creates_with([](sim::Scenario& s) { s.stress_combination = static_cast<sim::StressCombination>(7); }, 0));
    CHECK(creates_with([](sim::Scenario& s) { s.crop = sim::CropInit{0.0, 5.0}; }, 0));
}

// M2 용액 비율 파라미터(k·ca·mg 용액 비율, 용탈 NH4 비율)는 M3에서 사라져 M3 파라미터 검증으로 대체됐어요.
// 잘못된 M3 파라미터는 거부해요.
TEST_CASE("Invalid M3 params are rejected", "[unit][validation][m3]") {
    CHECK_FALSE(creates_with([](sim::Params& p) { p.cation_exchange.cec_organic_eq_per_g_C = -0.1; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.cation_exchange.exchange_rate_per_hour = -0.1; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.cation_exchange.nh4_retention_factor = 0.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.cation_exchange.k_retention_factor = -1.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.cation_exchange.ca_retention_factor = std::nan(""); }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.cation_exchange.mg_retention_factor = 0.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.cation_exchange.solver_iterations = 0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.cation_exchange.solver_iterations = 201; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.cation_exchange.buffer_eq_per_pH_per_eq_cec = -0.1; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.antagonism.ca_curve.points.resize(1); }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.antagonism.ca_curve.points.back()[1] = 1.5; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.antagonism.mg_curve.points.back()[1] = -0.1; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.antagonism.min_demand_factor = 1.2; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.antagonism.min_demand_factor = -0.1; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.phosphorus.fixation_rate_per_hour = -1.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.phosphorus.fixation_ph_curve.points.clear(); }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.phosphorus.release_rate_per_hour = std::nan(""); }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.manganese.retention_ph_curve.points.back()[1] = 0.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.manganese.oxidation_rate_per_hour = -1.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.manganese.oxidation_o2_curve.points.clear(); }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.manganese.reduction_rate_per_hour = -1.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.manganese.reduction_ph_curve.points.resize(1); }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.ions.mn_g_per_eq = 0.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.crop.target_Mn_g_per_g = 0.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.crop.mn_uptake_rate_per_g_root_per_hour = -1.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.crop.mn_max_uptake_ratio = 0.5; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.crop.mn_toxicity_curve.points.back()[1] = 1.5; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.composts[0].mn_g_per_kg = -1.0; }));
    CHECK_FALSE(creates_with([](sim::Params& p) { p.fertilizers[0].mn_g_per_kg = -1.0; }));
}

// 잘못된 M3 시나리오(토양)는 거부해요.
TEST_CASE("Invalid M3 scenarios are rejected", "[unit][validation][m3]") {
    CHECK_FALSE(creates_with([](sim::Scenario& s) { s.soil.cec_mineral_eq_per_m2 = 0.0; }, 0));
    CHECK_FALSE(creates_with([](sim::Scenario& s) { s.soil.cec_mineral_eq_per_m2 = -1.0; }, 0));
    CHECK_FALSE(creates_with([](sim::Scenario& s) { s.soil.mn_g_per_m2 = -1.0; }, 0));
    CHECK_FALSE(creates_with([](sim::Scenario& s) { s.soil.mn_oxide_g_per_m2 = -1.0; }, 0));
    CHECK_FALSE(creates_with([](sim::Scenario& s) { s.soil.p_fixed_g_per_m2 = -1.0; }, 0));
    CHECK(creates_with([](sim::Scenario& s) { s.soil.mn_oxide_g_per_m2 = 0.0; s.soil.p_fixed_g_per_m2 = 0.0; }, 0));
}

// M3 파라미터가 허용하는 경계값은 통과해요 (길항 곡선 y는 1까지, 하한 0~1).
TEST_CASE("Valid M3 boundary params pass validation", "[unit][validation][m3]") {
    CHECK(creates_with([](sim::Params& p) { p.antagonism.min_demand_factor = 0.0; }));
    CHECK(creates_with([](sim::Params& p) { p.antagonism.min_demand_factor = 1.0; }));
    CHECK(creates_with([](sim::Params& p) { p.cation_exchange.solver_iterations = 1; }));
    CHECK(creates_with([](sim::Params& p) { p.cation_exchange.exchange_rate_per_hour = 0.0; }));
    CHECK(creates_with([](sim::Params& p) { p.phosphorus.release_rate_per_hour = 0.0; }));
}
