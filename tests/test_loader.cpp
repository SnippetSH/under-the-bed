#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "sim/calendar.hpp"
#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {

std::string weather_csv(std::size_t rows, std::string_view newline = "\n") {
    std::string text = "# comment line\n";
    text += simdata::kWeatherCsvHeader;
    text += newline;
    for (std::size_t i = 0; i < rows; ++i) {
        text += std::format("{},{},{},{},{}{}", i / sim::kHoursPerDay + 1, i % sim::kHoursPerDay,
                            -2.5, 100.0, 60.0, newline);
    }
    return text;
}

constexpr std::string_view kValidScenario = R"(
schema_version = 4
[scenario]
name = "t"
seed = 7
start_day_of_year = 100
weather_csv = "w.csv"
[house]
bed_rows = 2
bed_cols = 5
[soil]
field_capacity_mm = 60.0
water_mm = 45
soil_temp_degC = 20.0
ph = 6.5
nh4_N_g_per_m2 = 2.0
no2_N_g_per_m2 = 0.0
no3_N_g_per_m2 = 10.0
labile_C_g_per_m2 = 50.0
labile_N_g_per_m2 = 5.0
stable_C_g_per_m2 = 3600.0
stable_N_g_per_m2 = 360.0
saturation_mm = 110.0
compaction_index = 0.3
base_buffer_eq_per_m2_per_pH = 5.0
k_g_per_m2 = 60.0
ca_g_per_m2 = 400.0
mg_g_per_m2 = 60.0
p_g_per_m2 = 25.0
s_g_per_m2 = 3.0
cec_mineral_eq_per_m2 = 25.0
mn_g_per_m2 = 5.0
mn_oxide_g_per_m2 = 100.0
p_fixed_g_per_m2 = 150.0
[difficulty]
stress_combination = "minimum"
)";

// 형식 검증용 params 문서 (값은 명세 값이 아니어도 돼요)
constexpr std::string_view kValidParams = R"(
schema_version = 4
[house]
base_offset_degC = 2.0
solar_gain_degC_per_W_per_m2 = 0.015
solar_transmittance_factor = 0.7
[soil_water]
evap_mm_per_hour_per_W_per_m2 = 0.0004
evap_base_mm_per_hour = 0.005
evap_moisture_curve = [[0.0, 0.0], [1.0, 1.0]]
drainage_rate_per_hour = 0.3
drainage_compaction_curve = [[0, 1.0], [1.0, 0.1]]
[soil_temperature]
relax_rate_per_hour = 0.04
heat_degC_per_g_C = 0.4
[organic_matter]
labile_decay_rate_per_hour = 0.004
stable_decay_rate_per_hour = 0.00002
humification_fraction = 0.3
humus_C_to_N_ratio = 10
temp_curve = [[0, 0.05], [10, 0.25]]
moisture_curve = [[0, 0], [2.0, 1.0]]
o2_curve = [[0, 0.15], [1, 1.0]]
[nitrification]
step1_rate_per_hour = 0.01
step2_rate_per_hour = 0.08
temp_curve = [[0, 0], [50, 0.1]]
moisture_curve = [[0, 0], [2.0, 1.0]]
step1_ph_curve = [[4.5, 0.1], [9.0, 0.6]]
step2_ph_curve = [[4.5, 0.1], [9.0, 0.1]]
ammonia_fraction_ph_curve = [[6, 0.0006], [10, 0.85]]
free_ammonia_inhibition_curve = [[0, 1], [3.0, 0.05]]
nitrite_toxicity_threshold_g_N_per_m2 = 2.0
nitrite_alarm_reset_fraction = 0.8
step1_o2_curve = [[0, 0.05], [1, 1.0]]
step2_o2_curve = [[0, 0.0], [1, 1.0]]
nitrite_toxicity_ph_curve = [[4.5, 8.0], [9.0, 1.0]]
[cation_exchange]
cec_organic_eq_per_g_C = 0.0015
exchange_rate_per_hour = 0.3
nh4_retention_factor = 70.0
k_retention_factor = 250.0
ca_retention_factor = 760.0
mg_retention_factor = 380.0
solver_iterations = 60
buffer_eq_per_pH_per_eq_cec = 0.15
[antagonism]
ca_curve = [[0.0, 1.0], [1.2, 0.15]]
mg_curve = [[0.0, 1.0], [1.2, 0.1]]
min_demand_factor = 0.6
[phosphorus]
fixation_rate_per_hour = 0.0015
fixation_ph_curve = [[4.5, 1.0], [8.5, 1.0]]
release_rate_per_hour = 0.00005
[manganese]
retention_ph_curve = [[4.5, 450.0], [8.5, 2000.0]]
oxidation_rate_per_hour = 0.002
oxidation_ph_curve = [[4.5, 0.005], [8.5, 10.0]]
oxidation_o2_curve = [[0, 0.0], [1, 1.0]]
reduction_rate_per_hour = 0.00006
reduction_ph_curve = [[4.5, 6.0], [8.5, 0.3]]
reduction_o2_curve = [[0, 20.0], [1, 1.0]]
[aeration]
compaction_cap_curve = [[0, 1.0], [1.0, 0.35]]
organic_C_cap_bonus_per_g_C = 0.00004
compaction_per_mm_irrigation = 0.0015
tilled_compaction_index = 0.1
till_root_damage_fraction = 0.5
[ions]
nh4_N_g_per_eq = 14.007
no3_N_g_per_eq = 14.007
no2_N_g_per_eq = 14.007
k_g_per_eq = 39.098
ca_g_per_eq = 20.039
mg_g_per_eq = 12.153
p_g_per_eq = 30.974
s_g_per_eq = 16.03
mn_g_per_eq = 27.469
[soil_chemistry]
p_solution_fraction = 0.002
s_solution_fraction = 0.8
nitrification_H_eq_per_eq_N = 2.0
uptake_H_eq_per_eq_NH4 = 1.0
uptake_H_eq_per_eq_NO3 = -1.0
organic_C_buffer_eq_per_g_C_per_pH = 0.002
ph_min = 3.0
ph_max = 10.0
lime_dissolution_rate_per_hour = 0.004
lime_dissolution_ph_curve = [[4.5, 1.0], [8.0, 0.0]]
lime_dissolution_moisture_curve = [[0, 0], [2.0, 1.0]]
ec_meq_per_L_per_dS_per_m = 10.0
ec_min_water_mm = 1.0
[availability]
n_ph_curve = [[4.0, 0.5], [9.0, 0.8]]
p_ph_curve = [[4.5, 0.2], [8.5, 0.05]]
k_ph_curve = [[4.0, 0.5], [9.0, 1.0]]
ca_ph_curve = [[4.0, 0.3], [9.0, 1.0]]
mg_ph_curve = [[4.0, 0.3], [9.0, 1.0]]
[root_damage]
waterlog_curve = [[0, 0.01], [0.5, 0]]
heat_curve = [[32, 0], [45, 0.03]]
nitrite_curve = [[0, 0], [8, 0.015]]
acid_curve = [[4.0, 0.006], [5.6, 0.0]]
[crop]
specific_leaf_area_m2_per_g = 0.02
light_interception_curve = [[0, 0], [4, 0.94]]
par_fraction = 0.48
light_use_efficiency_g_per_MJ = 2.5
temp_curve = [[3, 0], [40, 0]]
leaf_allocation_fraction = 0.7
water_stress_curve = [[0.2, 0], [2.0, 1.0]]
salt_stress_curve = [[0, 1], [8, 0.05]]
nutrient_stress_curve = [[0, 0], [1.0, 1.0]]
target_N_g_per_g = 0.028
target_P_g_per_g = 0.0035
target_K_g_per_g = 0.020
target_Ca_g_per_g = 0.012
target_Mg_g_per_g = 0.004
luxury_uptake_ratio = 1.3
uptake_rate_per_g_root_per_hour = 0.002
ca_uptake_rate_per_g_root_per_hour = 0.00019
mg_uptake_rate_per_g_root_per_hour = 0.00021
transpiration_mm_per_hour_per_W_per_m2 = 0.0015
root_recovery_rate_per_hour = 0.002
death_root_health = 0.05
root_dieback_per_damage = 1.0
dry_matter_C_fraction = 0.45
target_Mn_g_per_g = 0.0001
mn_uptake_rate_per_g_root_per_hour = 0.00004
mn_max_uptake_ratio = 30.0
mn_toxicity_curve = [[0, 1], [30, 0.1]]
[[compost]]
id = "c1"
name = "compost one"
C_g_per_kg = 180.0
N_g_per_kg = 12.0
labile_C_fraction = 0.6
labile_N_fraction = 0.7
k_g_per_kg = 12.0
ca_g_per_kg = 10.0
mg_g_per_kg = 4.0
p_g_per_kg = 4.0
s_g_per_kg = 2.0
mn_g_per_kg = 0.3
direct_acid_eq_per_kg = 0.0
[[compost]]
id = "c2"
name = "b"
C_g_per_kg = 150
N_g_per_kg = 12
labile_C_fraction = 0.1
labile_N_fraction = 0.1
k_g_per_kg = 12.0
ca_g_per_kg = 10.0
mg_g_per_kg = 4.0
p_g_per_kg = 4.0
s_g_per_kg = 2.0
mn_g_per_kg = 0.3
direct_acid_eq_per_kg = 0.0
[[fertilizer]]
id = "f1"
name = "ammonium sulfate"
nh4_N_g_per_kg = 210.0
no3_N_g_per_kg = 0.0
k_g_per_kg = 12.0
ca_g_per_kg = 10.0
mg_g_per_kg = 4.0
p_g_per_kg = 4.0
s_g_per_kg = 2.0
mn_g_per_kg = 0.3
direct_acid_eq_per_kg = 0.0
[[lime]]
id = "l1"
name = "lime one"
eq_per_kg = 20.0
ca_fraction = 1.0
mg_fraction = 0.0
)";

// 문서 안의 문자열을 한 번 바꿔요 (없으면 테스트 실수라서 실패).
std::string replaced(std::string_view text, std::string_view from, std::string_view to) {
    std::string out(text);
    const auto pos = out.find(from);
    REQUIRE(pos != std::string::npos);
    out.replace(pos, from.size(), to);
    return out;
}

// [[schedule]] 항목을 유효한 시나리오 뒤에 붙여요.
std::string scenario_with(std::string_view extra) {
    return std::string(kValidScenario) + std::string(extra);
}

const sim::AddCompostCommand& as_compost(const simdata::ScheduledCommand& c) {
    return std::get<sim::AddCompostCommand>(c.command);
}

}  // namespace

TEST_CASE("Params TOML loads strictly", "[unit][loader]") {
    SECTION("valid document fills every field") {
        auto p = simdata::parse_params_toml(kValidParams, "p.toml");
        REQUIRE(p.has_value());
        CHECK(p->schema_version == 4);
        CHECK(p->house.base_offset_degC == 2.0);
        CHECK(p->soil_water.evap_moisture_curve.points.size() == 2);
        CHECK(p->soil_water.evap_moisture_curve(0.5) == 0.5);
        CHECK(p->organic_matter.humus_C_to_N_ratio == 10.0);  // 정수로 쓴 실수 키
        CHECK(p->nitrification.nitrite_toxicity_threshold_g_N_per_m2 == 2.0);
        CHECK(p->cation_exchange.nh4_retention_factor == 70.0);
        CHECK(p->cation_exchange.solver_iterations == 60);
        REQUIRE(p->composts.size() == 2);
        CHECK(p->composts[0].id == "c1");
        CHECK(p->composts[1].C_g_per_kg == 150.0);
        REQUIRE(p->fertilizers.size() == 1);
        CHECK(p->fertilizers[0].nh4_N_g_per_kg == 210.0);
    }
    SECTION("M2 tables, curves and entries are read") {
        auto p = simdata::parse_params_toml(kValidParams, "p.toml");
        REQUIRE(p.has_value());
        CHECK(p->soil_water.drainage_compaction_curve.points.size() == 2);
        CHECK(p->organic_matter.o2_curve(1.0) == 1.0);
        CHECK(p->nitrification.step2_o2_curve.points.size() == 2);
        CHECK(p->nitrification.nitrite_toxicity_ph_curve(4.5) == 8.0);
        CHECK(p->aeration.tilled_compaction_index == 0.1);
        CHECK(p->ions.ca_g_per_eq == 20.039);
        CHECK(p->soil_chemistry.uptake_H_eq_per_eq_NO3 == -1.0);
        CHECK(p->soil_chemistry.ph_max == 10.0);
        CHECK(p->availability.p_ph_curve.points.size() == 2);
        CHECK(p->root_damage.acid_curve.points.size() == 2);
        CHECK(p->crop.target_P_g_per_g == 0.0035);
        CHECK(p->crop.death_root_health == 0.05);
        CHECK(p->composts[0].k_g_per_kg == 12.0);
        CHECK(p->composts[1].direct_acid_eq_per_kg == 0.0);
        CHECK(p->fertilizers[0].s_g_per_kg == 2.0);
        REQUIRE(p->limes.size() == 1);
        CHECK(p->limes[0].id == "l1");
        CHECK(p->limes[0].eq_per_kg == 20.0);
    }
    SECTION("missing M2 table is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "[aeration]\n", "[aerat]\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'aeration'"));
    }
    SECTION("missing M2 key in an existing table is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "o2_curve = [[0, 0.15], [1, 1.0]]\n", ""),
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'organic_matter.o2_curve'"));
    }
    SECTION("missing inorganic key in a [[compost]] entry is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "direct_acid_eq_per_kg = 0.0\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'compost[0].direct_acid_eq_per_kg'"));
    }
    SECTION("unknown key in [[lime]] is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "eq_per_kg = 20.0\n", "eq_per_kg = 20.0\nph = 9\n"),
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'lime[0].ph'"));
    }
    SECTION("missing [[lime]] is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "[[lime]]", "[[lyme]]"), "p.toml");
        REQUIRE_FALSE(p.has_value());
    }
    SECTION("float key given as integer is accepted") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "base_offset_degC = 2.0", "base_offset_degC = 2"), "p.toml");
        REQUIRE(p.has_value());
        CHECK(p->house.base_offset_degC == 2.0);
    }
    SECTION("unknown key in a nested table is rejected") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "[cation_exchange]\n", "[cation_exchange]\nnh4_fraction = 1\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("p.toml"));
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'cation_exchange.nh4_fraction'"));
    }
    SECTION("unknown key inside a [[compost]] entry is rejected") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "id = \"c2\"\n", "id = \"c2\"\nK_g_per_kg = 1\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'compost[1].K_g_per_kg'"));
    }
    SECTION("unknown top-level table is rejected") {
        auto p = simdata::parse_params_toml(std::string(kValidParams) + "[weather]\nx = 1\n", "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'weather'"));
    }
    SECTION("missing key is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "drainage_rate_per_hour = 0.3\n", ""),
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message,
                   ContainsSubstring("missing required key 'soil_water.drainage_rate_per_hour'"));
    }
    SECTION("missing table is rejected") {
        auto p = simdata::parse_params_toml("schema_version = 4\n", "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'house'"));
    }
    SECTION("missing key in a [[fertilizer]] entry is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "no3_N_g_per_kg = 0.0\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message,
                   ContainsSubstring("missing required key 'fertilizer[0].no3_N_g_per_kg'"));
    }
    SECTION("curve with a non-pair element is rejected") {
        constexpr std::string_view good = "[[0.0, 0.0], [1.0, 1.0]]";
        auto triple = simdata::parse_params_toml(
            replaced(kValidParams, good, "[[0.0, 0.0], [1.0, 1.0, 2.0]]"), "p.toml");
        REQUIRE_FALSE(triple.has_value());
        CHECK_THAT(triple.error().message, ContainsSubstring("soil_water.evap_moisture_curve[1]"));
        CHECK_FALSE(simdata::parse_params_toml(replaced(kValidParams, good, "[[0.0, 0.0], 1.0]"), "p.toml")
                        .has_value());
        CHECK_FALSE(simdata::parse_params_toml(replaced(kValidParams, good, "[[0.0, 0.0], [1.0, \"a\"]]"),
                                               "p.toml")
                        .has_value());
    }
    SECTION("wrong type is rejected") {
        auto p = simdata::parse_params_toml("schema_version = \"2\"\n", "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("must be a integer"));
        auto q = simdata::parse_params_toml(
            replaced(kValidParams, "humification_fraction = 0.3", "humification_fraction = \"x\""), "p.toml");
        REQUIRE_FALSE(q.has_value());
        CHECK_THAT(q.error().message, ContainsSubstring("organic_matter.humification_fraction"));
    }
    SECTION("old schema version is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "schema_version = 4", "schema_version = 3"),
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK(p.error().code == sim::ErrorCode::UnsupportedVersion);
    }
    SECTION("missing schema_version") {
        auto p = simdata::parse_params_toml("", "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'schema_version'"));
    }
    SECTION("syntax error reports the location") {
        auto p = simdata::parse_params_toml("schema_version = \n", "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("p.toml:1:"));
    }
}

TEST_CASE("Repository params.toml matches the spec values used by tests", "[unit][loader][data]") {
    auto loaded = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(loaded.has_value());
    CHECK(*loaded == test::test_params());
}

TEST_CASE("Scenario TOML loads strictly", "[unit][loader]") {
    SECTION("valid") {
        auto doc = simdata::parse_scenario_toml(kValidScenario, "s.toml");
        REQUIRE(doc.has_value());
        CHECK(doc->scenario.name == "t");
        CHECK(doc->scenario.seed == 7);
        CHECK(doc->scenario.start_day_of_year == 100);
        CHECK(doc->scenario.bed_rows == 2);
        CHECK(doc->scenario.bed_cols == 5);
        CHECK(doc->weather_csv == "w.csv");
        CHECK(doc->scenario.soil.water_mm == 45.0);  // 정수로 쓴 실수 키
        CHECK(doc->scenario.soil.stable_N_g_per_m2 == 360.0);
        CHECK(doc->schedule.empty());
        CHECK(doc->scenario.soil.saturation_mm == 110.0);
        CHECK(doc->scenario.soil.compaction_index == 0.3);
        CHECK(doc->scenario.soil.base_buffer_eq_per_m2_per_pH == 5.0);
        CHECK(doc->scenario.soil.k_g_per_m2 == 60.0);
        CHECK(doc->scenario.soil.ca_g_per_m2 == 400.0);
        CHECK(doc->scenario.soil.mg_g_per_m2 == 60.0);
        CHECK(doc->scenario.soil.p_g_per_m2 == 25.0);
        CHECK(doc->scenario.soil.s_g_per_m2 == 3.0);
        CHECK_FALSE(doc->scenario.crop.has_value());
        CHECK(doc->scenario.stress_combination == sim::StressCombination::Minimum);
    }
    SECTION("[crop] present") {
        auto doc = simdata::parse_scenario_toml(
            replaced(kValidScenario, "[difficulty]", "[crop]\nleaf_g_per_m2 = 30.0\nroot_g_per_m2 = 10\n[difficulty]"),
            "s.toml");
        REQUIRE(doc.has_value());
        REQUIRE(doc->scenario.crop.has_value());
        CHECK(doc->scenario.crop->leaf_g_per_m2 == 30.0);
        CHECK(doc->scenario.crop->root_g_per_m2 == 10.0);
    }
    SECTION("[crop] with a missing or unknown key is rejected") {
        auto missing = simdata::parse_scenario_toml(
            replaced(kValidScenario, "[difficulty]", "[crop]\nleaf_g_per_m2 = 30.0\n[difficulty]"), "s.toml");
        REQUIRE_FALSE(missing.has_value());
        CHECK_THAT(missing.error().message, ContainsSubstring("missing required key 'crop.root_g_per_m2'"));
        auto unknown = simdata::parse_scenario_toml(
            replaced(kValidScenario, "[difficulty]",
                     "[crop]\nleaf_g_per_m2 = 30.0\nroot_g_per_m2 = 10\nfruit_g_per_m2 = 1\n[difficulty]"),
            "s.toml");
        REQUIRE_FALSE(unknown.has_value());
        CHECK_THAT(unknown.error().message, ContainsSubstring("unknown key 'crop.fruit_g_per_m2'"));
    }
    SECTION("[difficulty] product") {
        auto doc = simdata::parse_scenario_toml(replaced(kValidScenario, "\"minimum\"", "\"product\""), "s.toml");
        REQUIRE(doc.has_value());
        CHECK(doc->scenario.stress_combination == sim::StressCombination::Product);
    }
    SECTION("[difficulty] invalid value") {
        auto doc = simdata::parse_scenario_toml(replaced(kValidScenario, "\"minimum\"", "\"average\""), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("difficulty.stress_combination"));
    }
    SECTION("missing [difficulty]") {
        auto doc = simdata::parse_scenario_toml(
            replaced(kValidScenario, "[difficulty]\nstress_combination = \"minimum\"\n", ""), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("missing required key 'difficulty'"));
    }
    SECTION("missing M2 key in [soil]") {
        auto doc = simdata::parse_scenario_toml(replaced(kValidScenario, "saturation_mm = 110.0\n", ""), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("missing required key 'soil.saturation_mm'"));
    }
    SECTION("missing [soil] table") {
        auto doc = simdata::parse_scenario_toml(replaced(kValidScenario, "[soil]", "[soyl]"), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("missing required key 'soil'"));
    }
    SECTION("unknown key in [soil]") {
        auto doc = simdata::parse_scenario_toml(
            replaced(kValidScenario, "[difficulty]", "clay_percent = 3\n[difficulty]"), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("unknown key 'soil.clay_percent'"));
    }
    SECTION("missing key in [soil]") {
        auto doc = simdata::parse_scenario_toml(replaced(kValidScenario, "ph = 6.5\n", ""), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("missing required key 'soil.ph'"));
    }
    SECTION("unknown nested key names its table") {
        auto doc = simdata::parse_scenario_toml(
            replaced(kValidScenario, "bed_cols = 5\n", "bed_cols = 5\nbed_depth_cm = 30\n"), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("unknown key 'house.bed_depth_cm'"));
    }
    SECTION("missing table") {
        auto doc = simdata::parse_scenario_toml("schema_version = 4\n[scenario]\n", "s.toml");
        CHECK_FALSE(doc.has_value());
    }
    SECTION("negative seed") {
        CHECK_FALSE(
            simdata::parse_scenario_toml(replaced(kValidScenario, "seed = 7", "seed = -1"), "s.toml").has_value());
    }
    SECTION("unsupported schema version") {
        auto doc = simdata::parse_scenario_toml(replaced(kValidScenario, "schema_version = 4", "schema_version = 3"),
                                                "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK(doc.error().code == sim::ErrorCode::UnsupportedVersion);
    }
}

TEST_CASE("Scenario schedule expands into sorted commands", "[unit][loader][schedule]") {
    SECTION("compost with the default area covers the whole grid") {
        auto doc = simdata::parse_scenario_toml(scenario_with(R"(
[[schedule]]
tick = 5
action = "add_compost"
compost = "immature_manure"
kg_per_m2 = 3
)"),
                                                "s.toml");
        REQUIRE(doc.has_value());
        REQUIRE(doc->schedule.size() == 1);
        CHECK(doc->schedule[0].tick == 5);
        const auto& c = as_compost(doc->schedule[0]);
        CHECK(c.compost_id == "immature_manure");
        CHECK(c.kg_per_m2 == 3.0);
        CHECK(c.area == sim::TileArea{0, 0, 2, 5});
    }
    SECTION("explicit area, irrigate and fertilizer") {
        auto doc = simdata::parse_scenario_toml(scenario_with(R"(
[[schedule]]
tick = 0
action = "irrigate"
amount_mm = 12.5
area = [1, 2, 1, 3]
[[schedule]]
tick = 1
action = "add_fertilizer"
fertilizer = "urea"
g_per_m2 = 20
)"),
                                                "s.toml");
        REQUIRE(doc.has_value());
        REQUIRE(doc->schedule.size() == 2);
        const auto& irrigate = std::get<sim::IrrigateCommand>(doc->schedule[0].command);
        CHECK(irrigate.amount_mm == 12.5);
        CHECK(irrigate.area == sim::TileArea{1, 2, 1, 3});
        const auto& fert = std::get<sim::AddFertilizerCommand>(doc->schedule[1].command);
        CHECK(fert.fertilizer_id == "urea");
        CHECK(fert.g_per_m2 == 20.0);
        CHECK(fert.area == sim::TileArea{0, 0, 2, 5});
    }
    SECTION("repeats expand and the result is sorted stably by tick") {
        auto doc = simdata::parse_scenario_toml(scenario_with(R"(
[[schedule]]
tick = 10
action = "irrigate"
amount_mm = 1
repeat_every_ticks = 24
repeat_count = 3
[[schedule]]
tick = 0
action = "add_compost"
compost = "a"
kg_per_m2 = 1
[[schedule]]
tick = 10
action = "add_compost"
compost = "b"
kg_per_m2 = 2
)"),
                                                "s.toml");
        REQUIRE(doc.has_value());
        REQUIRE(doc->schedule.size() == 5);
        const std::uint64_t ticks[] = {0, 10, 10, 34, 58};
        for (std::size_t i = 0; i < 5; ++i) {
            CHECK(doc->schedule[i].tick == ticks[i]);
        }
        CHECK(as_compost(doc->schedule[0]).compost_id == "a");
        // 같은 틱 10은 파일 순서대로: 관수(첫 항목) 다음에 퇴비 b
        CHECK(std::holds_alternative<sim::IrrigateCommand>(doc->schedule[1].command));
        CHECK(as_compost(doc->schedule[2]).compost_id == "b");
        CHECK(std::holds_alternative<sim::IrrigateCommand>(doc->schedule[3].command));
        CHECK(std::holds_alternative<sim::IrrigateCommand>(doc->schedule[4].command));
    }
    SECTION("add_lime and till") {
        auto doc = simdata::parse_scenario_toml(scenario_with(R"(
[[schedule]]
tick = 3
action = "add_lime"
lime = "dolomite"
g_per_m2 = 150
area = [0, 0, 1, 2]
[[schedule]]
tick = 4
action = "till"
)"),
                                                "s.toml");
        REQUIRE(doc.has_value());
        REQUIRE(doc->schedule.size() == 2);
        const auto& lime = std::get<sim::AddLimeCommand>(doc->schedule[0].command);
        CHECK(lime.lime_id == "dolomite");
        CHECK(lime.g_per_m2 == 150.0);
        CHECK(lime.area == sim::TileArea{0, 0, 1, 2});
        const auto& till = std::get<sim::TillCommand>(doc->schedule[1].command);
        CHECK(till.area == sim::TileArea{0, 0, 2, 5});
    }
    SECTION("till with an area") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"till\"\narea = [1, 1, 1, 1]\n"), "s.toml");
        REQUIRE(doc.has_value());
        CHECK(std::get<sim::TillCommand>(doc->schedule[0].command).area == sim::TileArea{1, 1, 1, 1});
    }
    SECTION("till with an amount key is rejected") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"till\"\ng_per_m2 = 5\n"), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("unknown key 'schedule[0].g_per_m2'"));
    }
    SECTION("add_lime without a lime id is rejected") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"add_lime\"\ng_per_m2 = 5\n"), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("missing required key 'schedule[0].lime'"));
    }
    SECTION("unknown action") {
        auto doc = simdata::parse_scenario_toml(scenario_with("[[schedule]]\ntick = 0\naction = \"harvest\"\n"),
                                                "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("unknown action 'harvest'"));
    }
    SECTION("key not valid for the action") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"irrigate\"\namount_mm = 3\nkg_per_m2 = 1\n"),
            "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("unknown key 'schedule[0].kg_per_m2'"));
    }
    SECTION("missing action-specific key") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"add_compost\"\nkg_per_m2 = 1\n"), "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("missing required key 'schedule[0].compost'"));
    }
    SECTION("repeat keys must come as a pair") {
        auto only_every = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"irrigate\"\namount_mm = 3\nrepeat_every_ticks = 24\n"),
            "s.toml");
        CHECK_FALSE(only_every.has_value());
        auto only_count = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = 0\naction = \"irrigate\"\namount_mm = 3\nrepeat_count = 2\n"),
            "s.toml");
        CHECK_FALSE(only_count.has_value());
    }
    SECTION("repeat values below 1 and malformed area are rejected") {
        constexpr std::string_view head = "[[schedule]]\ntick = 0\naction = \"irrigate\"\namount_mm = 3\n";
        CHECK_FALSE(simdata::parse_scenario_toml(
                        scenario_with(std::string(head) + "repeat_every_ticks = 24\nrepeat_count = 0\n"), "s.toml")
                        .has_value());
        CHECK_FALSE(simdata::parse_scenario_toml(
                        scenario_with(std::string(head) + "repeat_every_ticks = 0\nrepeat_count = 2\n"), "s.toml")
                        .has_value());
        CHECK_FALSE(simdata::parse_scenario_toml(scenario_with(std::string(head) + "area = [0, 0, 1]\n"), "s.toml")
                        .has_value());
    }
    SECTION("negative tick") {
        auto doc = simdata::parse_scenario_toml(
            scenario_with("[[schedule]]\ntick = -1\naction = \"irrigate\"\namount_mm = 3\n"), "s.toml");
        CHECK_FALSE(doc.has_value());
    }
}

TEST_CASE("Weather CSV loads strictly", "[unit][loader]") {
    SECTION("valid, with comments and CRLF line endings") {
        auto w = simdata::parse_weather_csv(weather_csv(sim::kHoursPerYear, "\r\n"), "w.csv");
        REQUIRE(w.has_value());
        CHECK(w->size() == sim::kHoursPerYear);
        CHECK(w->front().outside_air_temp_degC == -2.5);
    }
    SECTION("missing a row") {
        auto w = simdata::parse_weather_csv(weather_csv(sim::kHoursPerYear - 1), "w.csv");
        REQUIRE_FALSE(w.has_value());
        CHECK_THAT(w.error().message, ContainsSubstring("expected 8760 data rows"));
    }
    SECTION("extra row") {
        std::string text = weather_csv(sim::kHoursPerYear);
        text += "1,0,0,0,0\n";
        CHECK_FALSE(simdata::parse_weather_csv(text, "w.csv").has_value());
    }
    SECTION("wrong header") {
        std::string text = weather_csv(sim::kHoursPerYear);
        text.replace(text.find("hour,"), 5, "hr,");
        CHECK_FALSE(simdata::parse_weather_csv(text, "w.csv").has_value());
    }
    SECTION("rows out of order") {
        std::string text = weather_csv(sim::kHoursPerYear);
        const auto pos = text.find("\n1,1,");
        text.replace(pos, 5, "\n1,2,");
        auto w = simdata::parse_weather_csv(text, "w.csv");
        REQUIRE_FALSE(w.has_value());
        CHECK_THAT(w.error().message, ContainsSubstring("expected day_of_year=1 hour=1"));
    }
    SECTION("malformed number") {
        std::string text = weather_csv(sim::kHoursPerYear);
        text.replace(text.find("-2.5"), 4, "abc");
        CHECK_FALSE(simdata::parse_weather_csv(text, "w.csv").has_value());
    }
}

TEST_CASE("Repository data files load and build a world", "[unit][loader][data]") {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    auto scenario = simdata::load_scenario(test::repo_path("data/scenarios/empty.toml"));
    REQUIRE(scenario.has_value());
    CHECK(scenario->weather_hourly.size() == sim::kHoursPerYear);
    CHECK(sim::World::create(*params, std::move(*scenario)).has_value());
}

TEST_CASE("Scenario bundle carries the schedule", "[unit][loader][data]") {
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/empty.toml"));
    REQUIRE(bundle.has_value());
    CHECK(bundle->scenario.name == "empty");
    CHECK(bundle->schedule.empty());
}

TEST_CASE("Repository scenarios load with the M2 fields", "[unit][loader][data]") {
    auto empty = simdata::load_scenario(test::repo_path("data/scenarios/empty.toml"));
    REQUIRE(empty.has_value());
    CHECK(empty->soil.saturation_mm == 110.0);
    CHECK_FALSE(empty->crop.has_value());
    CHECK(empty->stress_combination == sim::StressCombination::Minimum);

    auto m1 = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m1/s3_over.toml"));
    REQUIRE(m1.has_value());
    CHECK_FALSE(m1->scenario.crop.has_value());
    CHECK(m1->scenario.soil.water_mm == 45.0);
    CHECK_FALSE(m1->schedule.empty());
}

TEST_CASE("Missing files report an I/O error", "[unit][loader]") {
    auto p = simdata::load_params(test::repo_path("data/does-not-exist.toml"));
    REQUIRE_FALSE(p.has_value());
    CHECK(p.error().code == sim::ErrorCode::Io);
}

// ---- M3 (schema 4) ----

TEST_CASE("M3 params tables, keys and entries are read", "[unit][loader][m3]") {
    auto p = simdata::parse_params_toml(kValidParams, "p.toml");
    REQUIRE(p.has_value());
    CHECK(p->cation_exchange.cec_organic_eq_per_g_C == 0.0015);
    CHECK(p->cation_exchange.exchange_rate_per_hour == 0.3);
    CHECK(p->cation_exchange.ca_retention_factor == 760.0);
    CHECK(p->cation_exchange.buffer_eq_per_pH_per_eq_cec == 0.15);
    CHECK(p->antagonism.ca_curve(1.2) == 0.15);
    CHECK(p->antagonism.mg_curve(1.2) == 0.1);
    CHECK(p->antagonism.min_demand_factor == 0.6);
    CHECK(p->phosphorus.fixation_rate_per_hour == 0.0015);
    CHECK(p->phosphorus.release_rate_per_hour == 0.00005);
    CHECK(p->manganese.retention_ph_curve(4.5) == 450.0);
    CHECK(p->manganese.oxidation_o2_curve.points.size() == 2);
    CHECK(p->manganese.reduction_rate_per_hour == 0.00006);
    CHECK(p->ions.mn_g_per_eq == 27.469);
    CHECK(p->crop.ca_uptake_rate_per_g_root_per_hour == 0.00019);
    CHECK(p->crop.mg_uptake_rate_per_g_root_per_hour == 0.00021);
    CHECK(p->crop.target_Mn_g_per_g == 0.0001);
    CHECK(p->crop.mn_uptake_rate_per_g_root_per_hour == 0.00004);
    CHECK(p->crop.mn_max_uptake_ratio == 30.0);
    CHECK(p->crop.mn_toxicity_curve(30.0) == 0.1);
    CHECK(p->composts[0].mn_g_per_kg == 0.3);
    CHECK(p->fertilizers[0].mn_g_per_kg == 0.3);
}

TEST_CASE("M3 params reject missing, removed and unknown keys", "[unit][loader][m3]") {
    SECTION("missing M3 table is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "[manganese]\n", "[mangan]\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'manganese'"));
    }
    SECTION("missing M3 key in an existing table is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "solver_iterations = 60\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'cation_exchange.solver_iterations'"));
    }
    SECTION("missing antagonism floor is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "min_demand_factor = 0.6\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'antagonism.min_demand_factor'"));
    }
    SECTION("missing crop Mn key is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "mn_max_uptake_ratio = 30.0\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'crop.mn_max_uptake_ratio'"));
    }
    SECTION("missing mn_g_per_kg in a compost entry is rejected") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "mn_g_per_kg = 0.3\n", ""), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'compost[0].mn_g_per_kg'"));
    }
    SECTION("removed M2 solution fraction keys are unknown now") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "[soil_chemistry]\n", "[soil_chemistry]\nk_solution_fraction = 0.03\n"), "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'soil_chemistry.k_solution_fraction'"));
    }
    SECTION("removed [leaching] table is unknown now") {
        auto p = simdata::parse_params_toml(std::string(kValidParams) + "[leaching]\nnh4_solution_fraction = 0.1\n",
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'leaching'"));
    }
    SECTION("removed mn_retention_factor is unknown now") {
        auto p = simdata::parse_params_toml(
            replaced(kValidParams, "[cation_exchange]\n", "[cation_exchange]\nmn_retention_factor = 1500.0\n"),
            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'cation_exchange.mn_retention_factor'"));
    }
    SECTION("solver_iterations must be an integer") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "solver_iterations = 60", "solver_iterations = 60.5"),
                                            "p.toml");
        CHECK_FALSE(p.has_value());
    }
    SECTION("params schema 3 (M2) is rejected as an old version") {
        auto p = simdata::parse_params_toml(replaced(kValidParams, "schema_version = 4", "schema_version = 3"),
                                            "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK(p.error().code == sim::ErrorCode::UnsupportedVersion);
    }
}

TEST_CASE("M3 scenario soil keys are read and required", "[unit][loader][m3]") {
    SECTION("valid") {
        auto doc = simdata::parse_scenario_toml(kValidScenario, "s.toml");
        REQUIRE(doc.has_value());
        CHECK(doc->scenario.soil.cec_mineral_eq_per_m2 == 25.0);
        CHECK(doc->scenario.soil.mn_g_per_m2 == 5.0);
        CHECK(doc->scenario.soil.mn_oxide_g_per_m2 == 100.0);
        CHECK(doc->scenario.soil.p_fixed_g_per_m2 == 150.0);
    }
    for (const char* key : {"cec_mineral_eq_per_m2", "mn_g_per_m2", "mn_oxide_g_per_m2", "p_fixed_g_per_m2"}) {
        INFO(key);
        std::string text(kValidScenario);
        const auto begin = text.find(std::string(key) + " = ");
        REQUIRE(begin != std::string::npos);
        const auto end = text.find('\n', begin);
        text.erase(begin, end - begin + 1);
        auto doc = simdata::parse_scenario_toml(text, "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring(std::string("missing required key 'soil.") + key + "'"));
    }
}

TEST_CASE("Repository params and scenarios carry the M3 fields", "[unit][loader][data][m3]") {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    CHECK(params->schema_version == 4);
    CHECK(params->cation_exchange.solver_iterations == 60);
    CHECK(params->crop.mn_max_uptake_ratio == 30.0);
    bool has_mnso4 = false;
    for (const auto& fertilizer : params->fertilizers) {
        has_mnso4 = has_mnso4 || (fertilizer.id == "manganese_sulfate" && fertilizer.mn_g_per_kg > 0.0);
    }
    CHECK(has_mnso4);

    auto empty = simdata::load_scenario(test::repo_path("data/scenarios/empty.toml"));
    REQUIRE(empty.has_value());
    CHECK(empty->soil.cec_mineral_eq_per_m2 > 0.0);
    CHECK(empty->soil.mn_oxide_g_per_m2 >= 0.0);
}
