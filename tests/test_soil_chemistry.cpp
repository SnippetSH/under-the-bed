// 패키지 2 단위 테스트: 토양 화학 + 용탈 확장 (docs/spec/soil-chemistry.md)
// 원칙: 방향과 "한 식"의 정확한 산술만 검증하고, 현재 파라미터 값에 기대는 매직 넘버는 쓰지 않아요.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "sim/processes.hpp"
#include "test_helpers.hpp"

namespace {

constexpr double kGramsPerKilogram = 1000.0;  // 단위 환산
constexpr double kMilliPerUnit = 1000.0;      // 단위 환산 (eq → meq)

const sim::LimeDef& find_lime(const sim::Params& params, const std::string& id) {
    for (const auto& lime : params.limes) {
        if (lime.id == id) {
            return lime;
        }
    }
    throw std::runtime_error("lime not found: " + id);
}

const sim::FertilizerDef& find_fertilizer(const sim::Params& params, const std::string& id) {
    for (const auto& fertilizer : params.fertilizers) {
        if (fertilizer.id == id) {
            return fertilizer;
        }
    }
    throw std::runtime_error("fertilizer not found: " + id);
}

// M3: buffer = base_buffer + buffer_eq_per_pH_per_eq_cec × 광물 CEC + organic_C_buffer × 유기 C
double buffer_of(const sim::SoilCell& cell, const sim::Params& params) {
    return cell.base_buffer_eq_per_m2_per_pH +
           params.cation_exchange.buffer_eq_per_pH_per_eq_cec * cell.cec_mineral_eq_per_m2 +
           params.soil_chemistry.organic_C_buffer_eq_per_g_C_per_pH * cell.organic_C_g_per_m2();
}

// 셀의 총량과 용액을 함께 0으로 (EC·용탈 시험용)
void zero_cations(sim::SoilCell& cell) {
    cell.nh4_N_g_per_m2 = cell.nh4_solution_N_g_per_m2 = 0.0;
    cell.k_g_per_m2 = cell.k_solution_g_per_m2 = 0.0;
    cell.ca_g_per_m2 = cell.ca_solution_g_per_m2 = 0.0;
    cell.mg_g_per_m2 = cell.mg_solution_g_per_m2 = 0.0;
    cell.mn_g_per_m2 = cell.mn_solution_g_per_m2 = 0.0;
}

// 석회가 없는 셀 (pH 단계에서 산만 보이도록)
sim::SoilCell cell_without_lime() {
    sim::SoilCell cell = test::test_cell();
    cell.lime_undissolved_eq_per_m2 = 0.0;
    cell.acid_H_eq_this_tick = 0.0;
    cell.pending_H_eq_per_m2 = 0.0;
    return cell;
}

// 곡선 y가 처음 0이 되는 x (석회 용해가 멈추는 pH)
double first_zero_x(const sim::PiecewiseLinear& curve) {
    for (const auto& point : curve.points) {
        if (point[1] == 0.0) {
            return point[0];
        }
    }
    throw std::runtime_error("curve never reaches zero");
}

// 모든 성분이 0이 아닌 시험용 비료 (값은 테스트 입력이고 파라미터가 아니에요)
sim::FertilizerDef full_fertilizer() {
    return sim::FertilizerDef{.id = "test_full", .name = "test", .nh4_N_g_per_kg = 50, .no3_N_g_per_kg = 70,
                              .k_g_per_kg = 110, .ca_g_per_kg = 130, .mg_g_per_kg = 170, .p_g_per_kg = 190,
                              .s_g_per_kg = 230, .mn_g_per_kg = 90, .direct_acid_eq_per_kg = 3.0};
}

struct ElementTotals {
    double k = 0.0, ca = 0.0, mg = 0.0, p = 0.0, s = 0.0, n = 0.0, mn = 0.0;
};

ElementTotals totals_of(const sim::SoilCell& cell) {
    return ElementTotals{.k = cell.k_g_per_m2, .ca = cell.ca_g_per_m2, .mg = cell.mg_g_per_m2, .p = cell.p_g_per_m2,
                         .s = cell.s_g_per_m2, .n = cell.total_N_g_per_m2(),
                         .mn = cell.mn_g_per_m2};
}

void require_pools_non_negative(const sim::SoilCell& cell) {
    REQUIRE(cell.k_g_per_m2 >= 0.0);
    REQUIRE(cell.ca_g_per_m2 >= 0.0);
    REQUIRE(cell.mg_g_per_m2 >= 0.0);
    REQUIRE(cell.p_g_per_m2 >= 0.0);
    REQUIRE(cell.s_g_per_m2 >= 0.0);
    REQUIRE(cell.nh4_N_g_per_m2 >= 0.0);
    REQUIRE(cell.no2_N_g_per_m2 >= 0.0);
    REQUIRE(cell.no3_N_g_per_m2 >= 0.0);
    REQUIRE(cell.lime_undissolved_eq_per_m2 >= 0.0);
    // M3: 0 <= 용액 <= 총량
    REQUIRE(cell.mn_g_per_m2 >= 0.0);
    REQUIRE(cell.nh4_solution_N_g_per_m2 >= 0.0);
    REQUIRE(cell.nh4_solution_N_g_per_m2 <= cell.nh4_N_g_per_m2);
    REQUIRE(cell.k_solution_g_per_m2 >= 0.0);
    REQUIRE(cell.k_solution_g_per_m2 <= cell.k_g_per_m2);
    REQUIRE(cell.ca_solution_g_per_m2 >= 0.0);
    REQUIRE(cell.ca_solution_g_per_m2 <= cell.ca_g_per_m2);
    REQUIRE(cell.mg_solution_g_per_m2 >= 0.0);
    REQUIRE(cell.mg_solution_g_per_m2 <= cell.mg_g_per_m2);
    REQUIRE(cell.mn_solution_g_per_m2 >= 0.0);
    REQUIRE(cell.mn_solution_g_per_m2 <= cell.mn_g_per_m2);
}

}  // namespace

// ---- 투입: 비료·퇴비·석회 ----

// 비료: 원소별 풀 증가 = 양 × 함량, 장부 *_in_g도 정확히 같은 양, 직접 산은 이번 틱 H+에
TEST_CASE("Fertilizer adds every element to pools and ledger exactly", "[unit][soil_chemistry]") {
    const sim::FertilizerDef fert = full_fertilizer();
    const double g_per_m2 = 250.0;
    const double kg = g_per_m2 / kGramsPerKilogram;
    sim::SoilCell cell = cell_without_lime();
    const sim::SoilCell before = cell;
    sim::Ledger ledger;
    sim::process::apply_fertilizer(cell, fert, g_per_m2, ledger);

    REQUIRE(cell.nh4_N_g_per_m2 - before.nh4_N_g_per_m2 == Catch::Approx(kg * fert.nh4_N_g_per_kg));
    REQUIRE(cell.no3_N_g_per_m2 - before.no3_N_g_per_m2 == Catch::Approx(kg * fert.no3_N_g_per_kg));
    REQUIRE(ledger.N_in_g == Catch::Approx(kg * (fert.nh4_N_g_per_kg + fert.no3_N_g_per_kg)));
    REQUIRE(cell.k_g_per_m2 - before.k_g_per_m2 == Catch::Approx(kg * fert.k_g_per_kg));
    REQUIRE(cell.ca_g_per_m2 - before.ca_g_per_m2 == Catch::Approx(kg * fert.ca_g_per_kg));
    REQUIRE(cell.mg_g_per_m2 - before.mg_g_per_m2 == Catch::Approx(kg * fert.mg_g_per_kg));
    REQUIRE(cell.p_g_per_m2 - before.p_g_per_m2 == Catch::Approx(kg * fert.p_g_per_kg));
    REQUIRE(cell.s_g_per_m2 - before.s_g_per_m2 == Catch::Approx(kg * fert.s_g_per_kg));
    REQUIRE(cell.mn_g_per_m2 - before.mn_g_per_m2 == Catch::Approx(kg * fert.mn_g_per_kg));
    REQUIRE(ledger.Mn_in_g == kg * fert.mn_g_per_kg);
    REQUIRE(ledger.K_in_g == kg * fert.k_g_per_kg);
    REQUIRE(ledger.Ca_in_g == kg * fert.ca_g_per_kg);
    REQUIRE(ledger.Mg_in_g == kg * fert.mg_g_per_kg);
    REQUIRE(ledger.P_in_g == kg * fert.p_g_per_kg);
    REQUIRE(ledger.S_in_g == kg * fert.s_g_per_kg);
    REQUIRE(cell.acid_H_eq_this_tick == Catch::Approx(kg * fert.direct_acid_eq_per_kg));
    // 명령 단계에서는 pH가 바뀌지 않아요
    REQUIRE(cell.ph == before.ph);
    REQUIRE(ledger.H_eq_net == 0.0);
}

// 데이터 정의 비료 전부: 장부 증가 = 풀 증가 (원소별)
TEST_CASE("Every defined fertilizer keeps pool increase equal to ledger input", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    for (const auto& fert : params.fertilizers) {
        INFO(fert.id);
        sim::SoilCell cell = cell_without_lime();
        const sim::SoilCell before = cell;
        sim::Ledger ledger;
        sim::process::apply_fertilizer(cell, fert, 100.0, ledger);
        REQUIRE(cell.mineral_N_g_per_m2() - before.mineral_N_g_per_m2() == Catch::Approx(ledger.N_in_g));
        REQUIRE(cell.k_g_per_m2 - before.k_g_per_m2 == Catch::Approx(ledger.K_in_g));
        REQUIRE(cell.ca_g_per_m2 - before.ca_g_per_m2 == Catch::Approx(ledger.Ca_in_g));
        REQUIRE(cell.mg_g_per_m2 - before.mg_g_per_m2 == Catch::Approx(ledger.Mg_in_g));
        REQUIRE(cell.p_g_per_m2 - before.p_g_per_m2 == Catch::Approx(ledger.P_in_g));
        REQUIRE(cell.s_g_per_m2 - before.s_g_per_m2 == Catch::Approx(ledger.S_in_g));
        REQUIRE(cell.mn_g_per_m2 - before.mn_g_per_m2 == Catch::Approx(ledger.Mn_in_g));
    }
    // 황산칼륨은 K와 S를 함께 넣어요 (명세 "비료·퇴비 정의 확장")
    const sim::FertilizerDef& potassium_sulfate = find_fertilizer(params, "potassium_sulfate");
    sim::SoilCell cell = cell_without_lime();
    const sim::SoilCell before = cell;
    sim::Ledger ledger;
    sim::process::apply_fertilizer(cell, potassium_sulfate, 100.0, ledger);
    REQUIRE(cell.k_g_per_m2 > before.k_g_per_m2);
    REQUIRE(cell.s_g_per_m2 > before.s_g_per_m2);
}

// 0·음수·NaN 투입량은 아무것도 바꾸지 않는다
TEST_CASE("Non-positive fertilizer, compost and lime amounts change nothing", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    const sim::FertilizerDef fert = full_fertilizer();
    for (const double amount : {0.0, -5.0, std::nan("")}) {
        sim::SoilCell cell = cell_without_lime();
        const sim::SoilCell before = cell;
        sim::Ledger ledger;
        sim::process::apply_fertilizer(cell, fert, amount, ledger);
        sim::process::apply_compost_minerals(cell, params.composts.front(), amount, ledger);
        sim::process::apply_lime(cell, params.limes.front(), amount);
        REQUIRE(cell == before);
        REQUIRE(ledger == sim::Ledger{});
    }
}

// 퇴비 무기 성분: 풀과 장부 정확, 유기 C·N은 건드리지 않음, 직접 산(음수 = 염기)
TEST_CASE("Compost minerals add K Ca Mg P S exactly and leave organic pools alone", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    sim::CompostDef compost = params.composts.front();
    compost.direct_acid_eq_per_kg = -0.5;  // 시험용 입력: 염기성 퇴비
    const double kg = 4.0;
    sim::SoilCell cell = cell_without_lime();
    const sim::SoilCell before = cell;
    sim::Ledger ledger;
    sim::process::apply_compost_minerals(cell, compost, kg, ledger);

    REQUIRE(cell.k_g_per_m2 - before.k_g_per_m2 == Catch::Approx(kg * compost.k_g_per_kg));
    REQUIRE(cell.ca_g_per_m2 - before.ca_g_per_m2 == Catch::Approx(kg * compost.ca_g_per_kg));
    REQUIRE(cell.mg_g_per_m2 - before.mg_g_per_m2 == Catch::Approx(kg * compost.mg_g_per_kg));
    REQUIRE(cell.p_g_per_m2 - before.p_g_per_m2 == Catch::Approx(kg * compost.p_g_per_kg));
    REQUIRE(cell.s_g_per_m2 - before.s_g_per_m2 == Catch::Approx(kg * compost.s_g_per_kg));
    REQUIRE(cell.mn_g_per_m2 - before.mn_g_per_m2 == Catch::Approx(kg * compost.mn_g_per_kg));
    REQUIRE(ledger.Mn_in_g == kg * compost.mn_g_per_kg);
    REQUIRE(ledger.K_in_g == kg * compost.k_g_per_kg);
    REQUIRE(ledger.Ca_in_g == kg * compost.ca_g_per_kg);
    REQUIRE(ledger.Mg_in_g == kg * compost.mg_g_per_kg);
    REQUIRE(ledger.P_in_g == kg * compost.p_g_per_kg);
    REQUIRE(ledger.S_in_g == kg * compost.s_g_per_kg);
    REQUIRE(cell.acid_H_eq_this_tick == Catch::Approx(kg * compost.direct_acid_eq_per_kg));
    REQUIRE(cell.acid_H_eq_this_tick < 0.0);
    // 유기 C·N과 무기 N은 organic_matter.cpp 담당이라 그대로
    REQUIRE(cell.labile_C_g_per_m2 == before.labile_C_g_per_m2);
    REQUIRE(cell.stable_C_g_per_m2 == before.stable_C_g_per_m2);
    REQUIRE(cell.organic_N_g_per_m2() == before.organic_N_g_per_m2());
    REQUIRE(cell.mineral_N_g_per_m2() == before.mineral_N_g_per_m2());
    REQUIRE(ledger.N_in_g == 0.0);
    REQUIRE(ledger.C_in_g == 0.0);
}

// 석회 투입 직후: pH 변화 없음, 미용해 풀만 g/1000 × eq_per_kg 늘어남, 장부는 그대로
TEST_CASE("Applying lime only grows the undissolved pool and leaves pH unchanged", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    const sim::LimeDef& lime = find_lime(params, "calcium_carbonate");
    const double g_per_m2 = 300.0;
    sim::SoilCell cell = cell_without_lime();
    const sim::SoilCell before = cell;
    sim::process::apply_lime(cell, lime, g_per_m2);

    REQUIRE(cell.ph == before.ph);
    REQUIRE(cell.lime_undissolved_eq_per_m2 == Catch::Approx(g_per_m2 / kGramsPerKilogram * lime.eq_per_kg));
    REQUIRE(cell.lime_ca_fraction == Catch::Approx(lime.ca_fraction));
    REQUIRE(cell.lime_mg_fraction == Catch::Approx(lime.mg_fraction));
    REQUIRE(cell.ca_g_per_m2 == before.ca_g_per_m2);
    REQUIRE(cell.mg_g_per_m2 == before.mg_g_per_m2);
    REQUIRE(cell.acid_H_eq_this_tick == before.acid_H_eq_this_tick);
}

// 두 석회를 섞으면 Ca·Mg 비율은 당량 가중 평균
TEST_CASE("Mixed limes average Ca and Mg fractions weighted by equivalents", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    const sim::LimeDef& calcite = find_lime(params, "calcium_carbonate");
    const sim::LimeDef& dolomite = find_lime(params, "dolomite");
    const double calcite_g = 200.0;
    const double dolomite_g = 500.0;
    sim::SoilCell cell = cell_without_lime();
    sim::process::apply_lime(cell, calcite, calcite_g);
    sim::process::apply_lime(cell, dolomite, dolomite_g);

    const double calcite_eq = calcite_g / kGramsPerKilogram * calcite.eq_per_kg;
    const double dolomite_eq = dolomite_g / kGramsPerKilogram * dolomite.eq_per_kg;
    const double total_eq = calcite_eq + dolomite_eq;
    REQUIRE(cell.lime_undissolved_eq_per_m2 == Catch::Approx(total_eq));
    REQUIRE(cell.lime_ca_fraction ==
            Catch::Approx((calcite_eq * calcite.ca_fraction + dolomite_eq * dolomite.ca_fraction) / total_eq));
    REQUIRE(cell.lime_mg_fraction ==
            Catch::Approx((calcite_eq * calcite.mg_fraction + dolomite_eq * dolomite.mg_fraction) / total_eq));
    // 평균은 두 석회 비율 사이에 있어요
    REQUIRE(cell.lime_mg_fraction > calcite.mg_fraction);
    REQUIRE(cell.lime_mg_fraction < dolomite.mg_fraction);
}

// ---- 석회 용해 → pH ----

// 질산화로 N 1 eq 생성 → pH가 (H+ eq per N eq)/buffer만큼 정확히 내려감
TEST_CASE("Nitrification of 1 eq N lowers pH by exactly H per N over buffer", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    const sim::SoilChemistryParams& chem = params.soil_chemistry;
    sim::SoilCell cell = cell_without_lime();
    // nitrify()가 1 eq N을 질산화했을 때 남기는 H+ (nitrification.cpp가 같은 식으로 기록)
    cell.acid_H_eq_this_tick = 1.0 * chem.nitrification_H_eq_per_eq_N;
    const double ph_before = cell.ph;
    const double buffer = buffer_of(cell, params);
    sim::Ledger ledger;
    sim::process::update_lime_and_ph(cell, params, ledger);

    REQUIRE(ph_before - cell.ph == Catch::Approx(chem.nitrification_H_eq_per_eq_N / buffer));
    REQUIRE(ledger.H_eq_net == Catch::Approx(chem.nitrification_H_eq_per_eq_N));
    REQUIRE(cell.acid_H_eq_this_tick == 0.0);
    REQUIRE(cell.pending_H_eq_per_m2 == 0.0);
}

// 같은 산이라도 유기 C가 많으면 pH 변화폭이 작다
TEST_CASE("Same acid moves pH less in soil with more organic carbon", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    sim::SoilCell low_oc = cell_without_lime();
    sim::SoilCell high_oc = cell_without_lime();
    high_oc.stable_C_g_per_m2 *= 3.0;
    low_oc.acid_H_eq_this_tick = 2.0;
    high_oc.acid_H_eq_this_tick = 2.0;
    const double ph_before = low_oc.ph;
    sim::Ledger ledger;
    sim::process::update_lime_and_ph(low_oc, params, ledger);
    sim::process::update_lime_and_ph(high_oc, params, ledger);

    REQUIRE(ph_before - low_oc.ph > 0.0);
    REQUIRE(ph_before - high_oc.ph > 0.0);
    REQUIRE(ph_before - high_oc.ph < ph_before - low_oc.ph);
}

// 지난 틱 흡수의 순 염기(pending_H 음수)는 pH를 올리고, 소비 후 0이 된다
TEST_CASE("Negative pending H from uptake raises pH and is consumed", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = cell_without_lime();
    cell.pending_H_eq_per_m2 = -1.5;
    const double ph_before = cell.ph;
    const double buffer = buffer_of(cell, params);
    sim::Ledger ledger;
    sim::process::update_lime_and_ph(cell, params, ledger);

    REQUIRE(cell.ph > ph_before);
    REQUIRE(cell.ph - ph_before == Catch::Approx(1.5 / buffer));
    REQUIRE(cell.pending_H_eq_per_m2 == 0.0);
    REQUIRE(ledger.H_eq_net == Catch::Approx(-1.5));
}

// 이번 틱 산과 pending_H는 함께 더해져 한 번에 반영된다
TEST_CASE("Direct acid and pending H combine into one net H", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = cell_without_lime();
    cell.acid_H_eq_this_tick = 0.8;
    cell.pending_H_eq_per_m2 = -0.3;
    const double ph_before = cell.ph;
    const double buffer = buffer_of(cell, params);
    sim::Ledger ledger;
    sim::process::update_lime_and_ph(cell, params, ledger);
    REQUIRE(ph_before - cell.ph == Catch::Approx((0.8 - 0.3) / buffer));
    REQUIRE(ledger.H_eq_net == Catch::Approx(0.8 - 0.3));
}

// 석회 용해 한 틱: 식 그대로 (미용해 × min(1, 속도 × pH곡선 × 수분곡선)), Ca·Mg는 풀과 장부에, pH 상승 = 용해/buffer
TEST_CASE("One tick of lime dissolution follows the spec formula exactly", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    const sim::SoilChemistryParams& chem = params.soil_chemistry;
    const sim::LimeDef& dolomite = find_lime(params, "dolomite");
    sim::SoilCell cell = cell_without_lime();
    cell.ph = 5.2;
    sim::process::apply_lime(cell, dolomite, 400.0);
    const sim::SoilCell before = cell;
    const double buffer = buffer_of(cell, params);
    sim::Ledger ledger;
    sim::process::update_lime_and_ph(cell, params, ledger);

    const double expected_dissolved =
        before.lime_undissolved_eq_per_m2 *
        std::min(1.0, chem.lime_dissolution_rate_per_hour * chem.lime_dissolution_ph_curve(before.ph) *
                          chem.lime_dissolution_moisture_curve(before.relative_water()));
    REQUIRE(expected_dissolved > 0.0);
    REQUIRE(before.lime_undissolved_eq_per_m2 - cell.lime_undissolved_eq_per_m2 ==
            Catch::Approx(expected_dissolved));
    const double expected_ca_g = expected_dissolved * before.lime_ca_fraction * params.ions.ca_g_per_eq;
    const double expected_mg_g = expected_dissolved * before.lime_mg_fraction * params.ions.mg_g_per_eq;
    REQUIRE(cell.ca_g_per_m2 - before.ca_g_per_m2 == Catch::Approx(expected_ca_g));
    REQUIRE(cell.mg_g_per_m2 - before.mg_g_per_m2 == Catch::Approx(expected_mg_g));
    // M3: 석회 Ca·Mg는 총량과 용액에 같은 양이 들어가요
    REQUIRE(cell.ca_solution_g_per_m2 - before.ca_solution_g_per_m2 == Catch::Approx(expected_ca_g));
    REQUIRE(cell.mg_solution_g_per_m2 - before.mg_solution_g_per_m2 == Catch::Approx(expected_mg_g));
    REQUIRE(ledger.Ca_in_g == Catch::Approx(expected_ca_g));
    REQUIRE(ledger.Mg_in_g == Catch::Approx(expected_mg_g));
    REQUIRE(cell.ph - before.ph == Catch::Approx(expected_dissolved / buffer));
    REQUIRE(ledger.H_eq_net == Catch::Approx(-expected_dissolved));
}

// 석회는 pH 5.0에서 7.0보다 빨리 녹는다
TEST_CASE("Lime dissolves faster at pH 5.0 than at pH 7.0", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    const sim::LimeDef& lime = find_lime(params, "calcium_carbonate");
    sim::SoilCell acid = cell_without_lime();
    sim::SoilCell neutral = cell_without_lime();
    acid.ph = 5.0;
    neutral.ph = 7.0;
    sim::process::apply_lime(acid, lime, 500.0);
    sim::process::apply_lime(neutral, lime, 500.0);
    const double start_eq = acid.lime_undissolved_eq_per_m2;
    sim::Ledger acid_ledger;
    sim::Ledger neutral_ledger;
    sim::process::update_lime_and_ph(acid, params, acid_ledger);
    sim::process::update_lime_and_ph(neutral, params, neutral_ledger);

    const double acid_dissolved = start_eq - acid.lime_undissolved_eq_per_m2;
    const double neutral_dissolved = start_eq - neutral.lime_undissolved_eq_per_m2;
    REQUIRE(acid_dissolved > neutral_dissolved);
    REQUIRE(acid_ledger.Ca_in_g > neutral_ledger.Ca_in_g);
}

// 마른 흙에서는 석회가 덜 녹는다 (수분 곡선)
TEST_CASE("Lime dissolves slower in drier soil", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    const sim::LimeDef& lime = find_lime(params, "calcium_carbonate");
    sim::SoilCell wet = cell_without_lime();
    sim::SoilCell dry = cell_without_lime();
    wet.ph = 5.0;
    dry.ph = 5.0;
    dry.water_mm = wet.water_mm * 0.2;
    sim::process::apply_lime(wet, lime, 500.0);
    sim::process::apply_lime(dry, lime, 500.0);
    const double start_eq = wet.lime_undissolved_eq_per_m2;
    sim::Ledger ledger;
    sim::process::update_lime_and_ph(wet, params, ledger);
    sim::process::update_lime_and_ph(dry, params, ledger);
    REQUIRE(start_eq - wet.lime_undissolved_eq_per_m2 > start_eq - dry.lime_undissolved_eq_per_m2);
}

// 석회 과다: pH는 용해 곡선이 0이 되는 pH(8 근처)에서 멈추고, 석회는 남는다
TEST_CASE("Excess lime stalls pH near where dissolution stops", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    const sim::SoilChemistryParams& chem = params.soil_chemistry;
    const double stall_ph = first_zero_x(chem.lime_dissolution_ph_curve);
    const sim::LimeDef& lime = find_lime(params, "calcium_carbonate");
    sim::SoilCell cell = cell_without_lime();
    cell.ph = 5.0;
    // 다 녹으면 pH_max를 넘길 만큼 넣어요
    const double needed_eq = (chem.ph_max - cell.ph) * buffer_of(cell, params);
    sim::process::apply_lime(cell, lime, 2.0 * needed_eq / lime.eq_per_kg * kGramsPerKilogram);
    sim::Ledger ledger;
    for (int hour = 0; hour < 24 * 120; ++hour) {
        sim::process::update_lime_and_ph(cell, params, ledger);
    }
    const double ph_after_120_days = cell.ph;
    REQUIRE(ph_after_120_days <= stall_ph);
    REQUIRE(ph_after_120_days > stall_ph - 0.1);
    REQUIRE(ph_after_120_days < chem.ph_max);
    REQUIRE(cell.lime_undissolved_eq_per_m2 > 0.0);  // 석회가 떨어져서가 아니라 pH 때문에 멈춤

    for (int hour = 0; hour < 24 * 30; ++hour) {
        sim::process::update_lime_and_ph(cell, params, ledger);
    }
    REQUIRE(cell.ph <= stall_ph);
    REQUIRE(cell.ph - ph_after_120_days < 0.01);
}

// 극한 산·염기(1000 eq)에도 pH는 ph_min~ph_max 안에 있고 유한
TEST_CASE("Extreme acid or base keeps pH within bounds", "[unit][soil_chemistry][extreme]") {
    const sim::Params params = test::test_params();
    const sim::SoilChemistryParams& chem = params.soil_chemistry;

    sim::SoilCell acid = cell_without_lime();
    acid.acid_H_eq_this_tick = 1000.0;
    sim::Ledger ledger;
    sim::process::update_lime_and_ph(acid, params, ledger);
    REQUIRE(acid.ph == chem.ph_min);

    sim::SoilCell base = cell_without_lime();
    base.pending_H_eq_per_m2 = -1000.0;
    sim::process::update_lime_and_ph(base, params, ledger);
    REQUIRE(base.ph == chem.ph_max);

    // 거대한 석회 + 속도 과다(min(1, …) 경로): 미용해 풀이 정확히 0, 음수 아님
    sim::Params fast = params;
    fast.soil_chemistry.lime_dissolution_rate_per_hour = 1.0e6;
    sim::SoilCell limed = cell_without_lime();
    limed.ph = 5.0;
    sim::process::apply_lime(limed, find_lime(params, "dolomite"), 1.0e6);
    sim::Ledger limed_ledger;
    sim::process::update_lime_and_ph(limed, fast, limed_ledger);
    REQUIRE(limed.lime_undissolved_eq_per_m2 == 0.0);
    REQUIRE(limed.ph == chem.ph_max);
    REQUIRE(std::isfinite(limed.ca_g_per_m2));
    REQUIRE(std::isfinite(limed_ledger.H_eq_net));
    require_pools_non_negative(limed);
}

// ---- 유효량 ----

// 유효량 = 용액 부분 × pH 유효도 곡선 (M3: NH4·K·Ca·Mg는 용액 상태, Mn은 곡선 없이 용액 그대로, NO3 전부, P는 풀 × 용액 비율)
TEST_CASE("Available nutrients equal solution part times pH availability", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    const sim::SoilChemistryParams& chem = params.soil_chemistry;
    const sim::AvailabilityParams& curves = params.availability;
    sim::SoilCell cell = cell_without_lime();
    cell.ph = 5.3;
    const sim::process::NutrientAvailability a = sim::process::available_nutrients(cell, params);

    REQUIRE(a.nh4_N == Catch::Approx(cell.nh4_solution_N_g_per_m2 * curves.n_ph_curve(cell.ph)));
    REQUIRE(a.no3_N == Catch::Approx(cell.no3_N_g_per_m2 * curves.n_ph_curve(cell.ph)));
    REQUIRE(a.p == Catch::Approx(cell.p_g_per_m2 * chem.p_solution_fraction * curves.p_ph_curve(cell.ph)));
    REQUIRE(a.k == Catch::Approx(cell.k_solution_g_per_m2 * curves.k_ph_curve(cell.ph)));
    REQUIRE(a.ca == Catch::Approx(cell.ca_solution_g_per_m2 * curves.ca_ph_curve(cell.ph)));
    REQUIRE(a.mg == Catch::Approx(cell.mg_solution_g_per_m2 * curves.mg_ph_curve(cell.ph)));
    REQUIRE(a.mn == cell.mn_solution_g_per_m2);
    // 유효량은 총량을 넘지 않아요
    REQUIRE(a.p <= cell.p_g_per_m2);
    REQUIRE(a.k <= cell.k_solution_g_per_m2);
}

// 같은 P 풀이라도 pH 8.0에서는 6.5보다 P 유효량이 작다 (S4의 원인)
TEST_CASE("Same P pool is less available at pH 8.0 than at 6.5", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    sim::SoilCell optimal = cell_without_lime();
    sim::SoilCell alkaline = cell_without_lime();
    optimal.ph = 6.5;
    alkaline.ph = 8.0;
    const auto a_opt = sim::process::available_nutrients(optimal, params);
    const auto a_alk = sim::process::available_nutrients(alkaline, params);
    REQUIRE(optimal.p_g_per_m2 == alkaline.p_g_per_m2);
    REQUIRE(a_alk.p < a_opt.p);
    REQUIRE(a_alk.p >= 0.0);
}

// ---- EC ----

// 같은 이온 양, 수분 절반이면 EC 2배 (ec_min_water_mm보다 충분히 젖은 경우)
TEST_CASE("Halving water doubles EC for the same ions", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    sim::SoilCell wet = cell_without_lime();
    sim::SoilCell half = wet;
    half.water_mm = wet.water_mm / 2.0;
    REQUIRE(half.water_mm > params.soil_chemistry.ec_min_water_mm);
    const double ec_wet = sim::process::soil_ec_dS_per_m(wet, params);
    const double ec_half = sim::process::soil_ec_dS_per_m(half, params);
    REQUIRE(ec_wet > 0.0);
    REQUIRE(ec_half == Catch::Approx(2.0 * ec_wet));
}

// EC 한 식: 이온 하나(K)만 있을 때 (용액 meq / 2) / 물 / 환산계수
TEST_CASE("EC of a single ion follows the spec formula", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = cell_without_lime();
    const double k_total = cell.k_g_per_m2;
    const double k_solution = cell.k_solution_g_per_m2;
    zero_cations(cell);
    cell.k_g_per_m2 = k_total;
    cell.k_solution_g_per_m2 = k_solution;  // M3: EC는 용액 상태로 계산
    cell.no2_N_g_per_m2 = 0.0;
    cell.no3_N_g_per_m2 = 0.0;
    cell.p_g_per_m2 = 0.0;
    cell.s_g_per_m2 = 0.0;
    const double k_meq = cell.k_solution_g_per_m2 / params.ions.k_g_per_eq * kMilliPerUnit;
    const double expected = k_meq / 2.0 / cell.water_mm / params.soil_chemistry.ec_meq_per_L_per_dS_per_m;
    REQUIRE(sim::process::soil_ec_dS_per_m(cell, params) == Catch::Approx(expected));
}

// 비료를 주면 EC가 오르고, 이온이 하나도 없으면 0
TEST_CASE("Fertilizer raises EC and an ion-free cell has zero EC", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = cell_without_lime();
    const double ec_before = sim::process::soil_ec_dS_per_m(cell, params);
    sim::Ledger ledger;
    sim::process::apply_fertilizer(cell, find_fertilizer(params, "potassium_sulfate"), 200.0, ledger);
    REQUIRE(sim::process::soil_ec_dS_per_m(cell, params) > ec_before);

    sim::SoilCell empty = cell_without_lime();
    zero_cations(empty);
    empty.no2_N_g_per_m2 = empty.no3_N_g_per_m2 = 0.0;
    empty.p_g_per_m2 = empty.s_g_per_m2 = 0.0;
    REQUIRE(sim::process::soil_ec_dS_per_m(empty, params) == 0.0);
}

// 수분 0에서도 EC는 유한하고, ec_min_water_mm일 때와 같다
TEST_CASE("EC is finite at zero water and capped by minimum water", "[unit][soil_chemistry][extreme]") {
    const sim::Params params = test::test_params();
    sim::SoilCell dry = cell_without_lime();
    dry.water_mm = 0.0;
    sim::SoilCell at_min = dry;
    at_min.water_mm = params.soil_chemistry.ec_min_water_mm;
    const double ec_dry = sim::process::soil_ec_dS_per_m(dry, params);
    REQUIRE(std::isfinite(ec_dry));
    REQUIRE(ec_dry >= 0.0);
    REQUIRE(ec_dry == Catch::Approx(sim::process::soil_ec_dS_per_m(at_min, params)));
    // 마른 흙은 젖은 흙보다 EC가 높아요
    REQUIRE(ec_dry > sim::process::soil_ec_dS_per_m(cell_without_lime(), params));
}

// ---- 용탈 확장 ----

// M3 용탈: K·Ca·Mg·Mn 손실 = 용액 상태 × f (총량과 용액에서 함께 빠짐), P·S는 풀 × 용액 비율 × f. 장부와 같음
TEST_CASE("Leaching removes the solution part of K Ca Mg Mn and P S times f", "[unit][leaching][soil_chemistry]") {
    const sim::Params params = test::test_params();
    const sim::SoilChemistryParams& chem = params.soil_chemistry;
    sim::SoilCell cell = cell_without_lime();
    cell.water_mm = 30.0;
    cell.drained_mm_this_tick = 10.0;
    const sim::SoilCell before = cell;
    const double f = before.drained_mm_this_tick / (before.water_mm + before.drained_mm_this_tick);
    sim::Ledger ledger;
    sim::process::leach(cell, params, ledger);

    REQUIRE(before.k_g_per_m2 - cell.k_g_per_m2 == Catch::Approx(before.k_solution_g_per_m2 * f));
    REQUIRE(before.ca_g_per_m2 - cell.ca_g_per_m2 == Catch::Approx(before.ca_solution_g_per_m2 * f));
    REQUIRE(before.mg_g_per_m2 - cell.mg_g_per_m2 == Catch::Approx(before.mg_solution_g_per_m2 * f));
    REQUIRE(before.mn_g_per_m2 - cell.mn_g_per_m2 == Catch::Approx(before.mn_solution_g_per_m2 * f));
    REQUIRE(before.nh4_N_g_per_m2 - cell.nh4_N_g_per_m2 == Catch::Approx(before.nh4_solution_N_g_per_m2 * f));
    // 용액에서도 같은 양이 빠져요
    REQUIRE(before.k_solution_g_per_m2 - cell.k_solution_g_per_m2 == Catch::Approx(before.k_g_per_m2 - cell.k_g_per_m2));
    REQUIRE(before.mn_solution_g_per_m2 - cell.mn_solution_g_per_m2 ==
            Catch::Approx(before.mn_g_per_m2 - cell.mn_g_per_m2));
    REQUIRE(before.p_g_per_m2 - cell.p_g_per_m2 == Catch::Approx(before.p_g_per_m2 * chem.p_solution_fraction * f));
    REQUIRE(before.s_g_per_m2 - cell.s_g_per_m2 == Catch::Approx(before.s_g_per_m2 * chem.s_solution_fraction * f));
    REQUIRE(ledger.K_leached_g == Catch::Approx(before.k_g_per_m2 - cell.k_g_per_m2));
    REQUIRE(ledger.Ca_leached_g == Catch::Approx(before.ca_g_per_m2 - cell.ca_g_per_m2));
    REQUIRE(ledger.Mg_leached_g == Catch::Approx(before.mg_g_per_m2 - cell.mg_g_per_m2));
    REQUIRE(ledger.Mn_leached_g == Catch::Approx(before.mn_g_per_m2 - cell.mn_g_per_m2));
    REQUIRE(ledger.P_leached_g == Catch::Approx(before.p_g_per_m2 - cell.p_g_per_m2));
    REQUIRE(ledger.S_leached_g == Catch::Approx(before.s_g_per_m2 - cell.s_g_per_m2));
    // 산화물 Mn과 고정 인산은 씻기지 않아요
    REQUIRE(cell.mn_oxide_g_per_m2 == before.mn_oxide_g_per_m2);
    REQUIRE(cell.p_fixed_g_per_m2 == before.p_fixed_g_per_m2);
}

// 같은 총량이라도 용액 비율이 높은(CEC가 낮은) 셀이 더 많이 씻긴다
TEST_CASE("A cell with more of the cation in solution leaches more of it", "[unit][leaching][soil_chemistry][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell low_solution = cell_without_lime();
    sim::SoilCell high_solution = cell_without_lime();
    high_solution.k_solution_g_per_m2 = high_solution.k_g_per_m2 * 0.5;
    low_solution.drained_mm_this_tick = 10.0;
    high_solution.drained_mm_this_tick = 10.0;
    REQUIRE(high_solution.k_solution_g_per_m2 > low_solution.k_solution_g_per_m2);
    sim::Ledger low_ledger;
    sim::Ledger high_ledger;
    sim::process::leach(low_solution, params, low_ledger);
    sim::process::leach(high_solution, params, high_ledger);
    REQUIRE(high_ledger.K_leached_g > low_ledger.K_leached_g);
}

// 배수: 용액 비율이 큰 S가 작은 P보다 상대 손실이 크다
TEST_CASE("Drainage loses relatively more S than P", "[unit][leaching][soil_chemistry]") {
    const sim::Params params = test::test_params();
    REQUIRE(params.soil_chemistry.s_solution_fraction > params.soil_chemistry.p_solution_fraction);
    sim::SoilCell cell = cell_without_lime();
    cell.drained_mm_this_tick = 10.0;
    const sim::SoilCell before = cell;
    sim::Ledger ledger;
    sim::process::leach(cell, params, ledger);
    const double s_rel = 1.0 - cell.s_g_per_m2 / before.s_g_per_m2;
    const double p_rel = 1.0 - cell.p_g_per_m2 / before.p_g_per_m2;
    REQUIRE(p_rel > 0.0);
    REQUIRE(s_rel > p_rel);
}

// 배수가 없으면 K·Ca·Mg·P·S도 빠지지 않는다
TEST_CASE("No drainage leaches no K Ca Mg P S", "[unit][leaching][soil_chemistry]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = cell_without_lime();
    cell.drained_mm_this_tick = 0.0;
    const sim::SoilCell before = cell;
    sim::Ledger ledger;
    sim::process::leach(cell, params, ledger);
    REQUIRE(cell == before);
    REQUIRE(ledger == sim::Ledger{});
}

// 극단 배수(남은 물 0)에도 모든 풀 ≥ 0, 장부 유한
TEST_CASE("Extreme drainage keeps every nutrient pool non-negative", "[unit][leaching][soil_chemistry][extreme]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = cell_without_lime();
    cell.water_mm = 0.0;
    cell.drained_mm_this_tick = 500.0;
    sim::Ledger ledger;
    for (int hour = 0; hour < 100; ++hour) {
        sim::process::leach(cell, params, ledger);
    }
    require_pools_non_negative(cell);
    REQUIRE(std::isfinite(ledger.S_leached_g));
    REQUIRE(std::isfinite(ledger.K_leached_g));
}

// ---- 보존 ----

// 투입(비료·퇴비·석회 용해) + 용탈을 반복해도 원소별: 풀 = 초기 + in − leached
TEST_CASE("Elements are conserved through inputs, lime dissolution and leaching", "[unit][soil_chemistry][conservation]") {
    const sim::Params params = test::test_params();
    const sim::FertilizerDef fert = full_fertilizer();
    sim::SoilCell cell = cell_without_lime();
    cell.ph = 5.0;
    const ElementTotals initial = totals_of(cell);
    sim::Ledger ledger;

    sim::process::apply_lime(cell, find_lime(params, "dolomite"), 300.0);
    sim::process::apply_lime(cell, find_lime(params, "calcium_carbonate"), 200.0);
    for (int hour = 0; hour < 24 * 10; ++hour) {
        if (hour % 24 == 0) {
            sim::process::apply_fertilizer(cell, fert, 40.0, ledger);
            sim::process::apply_compost_minerals(cell, params.composts.back(), 0.5, ledger);
        }
        sim::process::update_lime_and_ph(cell, params, ledger);
        cell.drained_mm_this_tick = (hour % 6 == 0) ? 8.0 : 0.0;
        sim::process::leach(cell, params, ledger);
        require_pools_non_negative(cell);
        REQUIRE(cell.ph >= params.soil_chemistry.ph_min);
        REQUIRE(cell.ph <= params.soil_chemistry.ph_max);
    }

    const ElementTotals now = totals_of(cell);
    REQUIRE(now.k == Catch::Approx(initial.k + ledger.K_in_g - ledger.K_leached_g).epsilon(1e-12));
    REQUIRE(now.ca == Catch::Approx(initial.ca + ledger.Ca_in_g - ledger.Ca_leached_g).epsilon(1e-12));
    REQUIRE(now.mg == Catch::Approx(initial.mg + ledger.Mg_in_g - ledger.Mg_leached_g).epsilon(1e-12));
    REQUIRE(now.p == Catch::Approx(initial.p + ledger.P_in_g - ledger.P_leached_g).epsilon(1e-12));
    REQUIRE(now.s == Catch::Approx(initial.s + ledger.S_in_g - ledger.S_leached_g).epsilon(1e-12));
    REQUIRE(now.n == Catch::Approx(initial.n + ledger.N_in_g - ledger.N_leached_g).epsilon(1e-12));
    REQUIRE(now.mn == Catch::Approx(initial.mn + ledger.Mn_in_g - ledger.Mn_leached_g).epsilon(1e-12));
    // 석회가 실제로 녹아 Ca·Mg 장부에 들어갔어요
    REQUIRE(ledger.Mg_in_g > 0.0);
    REQUIRE(ledger.Ca_leached_g > 0.0);
}

// 완충력이 일정하면 pH 변화 합 × 완충력 = −H_eq_net (제한에 걸리지 않을 때)
TEST_CASE("pH change times constant buffer equals minus net H", "[unit][soil_chemistry][conservation]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = cell_without_lime();
    cell.ph = 5.5;
    sim::process::apply_lime(cell, find_lime(params, "calcium_carbonate"), 50.0);
    const double ph_start = cell.ph;
    const double buffer = buffer_of(cell, params);
    sim::Ledger ledger;
    for (int hour = 0; hour < 48; ++hour) {
        cell.acid_H_eq_this_tick += (hour % 3 == 0) ? 0.05 : 0.0;
        cell.pending_H_eq_per_m2 = (hour % 4 == 0) ? -0.02 : 0.01;
        sim::process::update_lime_and_ph(cell, params, ledger);
    }
    REQUIRE((ph_start - cell.ph) * buffer == Catch::Approx(ledger.H_eq_net).margin(1e-12));
}

// ---- 산성 장해 곡선 (ADR-0007) ----

// pH 5.6 이상에서 0, 낮을수록 손상 비율이 커지거나 같다
TEST_CASE("Acid damage curve is zero from pH 5.6 and grows as pH falls", "[unit][soil_chemistry]") {
    const sim::Params params = test::test_params();
    const sim::PiecewiseLinear& acid = params.root_damage.acid_curve;
    REQUIRE(acid(5.6) == 0.0);
    REQUIRE(acid(6.5) == 0.0);
    REQUIRE(acid(8.0) == 0.0);
    double previous = acid(5.6);
    for (double ph = 5.5; ph >= 3.0; ph -= 0.1) {
        const double damage = acid(ph);
        REQUIRE(damage >= previous);
        previous = damage;
    }
    REQUIRE(acid(4.0) > acid(5.0));
    REQUIRE(acid(5.0) > 0.0);
}

// ---- M3 변경: 투입은 총량과 용액에, EC에 Mn, 완충력의 광물 CEC 항, 유효량의 용액 기준 ----
// (docs/spec/soil-chemistry.md M3 개정, cation-exchange.md "다른 프로세스와의 연결")

// 비료 투입: 양이온(NH4·K·Ca·Mg·Mn)은 총량과 용액에 같은 양, 음이온(NO3·P·S)은 용액 상태가 없다
TEST_CASE("Fertilizer cations go to both the total and the solution", "[unit][soil_chemistry][m3]") {
    const sim::FertilizerDef fert = full_fertilizer();
    const double g_per_m2 = 250.0;
    const double kg = g_per_m2 / kGramsPerKilogram;
    sim::SoilCell cell = cell_without_lime();
    const sim::SoilCell before = cell;
    sim::Ledger ledger;
    sim::process::apply_fertilizer(cell, fert, g_per_m2, ledger);

    CHECK(cell.nh4_solution_N_g_per_m2 - before.nh4_solution_N_g_per_m2 == Catch::Approx(kg * fert.nh4_N_g_per_kg));
    CHECK(cell.k_solution_g_per_m2 - before.k_solution_g_per_m2 == Catch::Approx(kg * fert.k_g_per_kg));
    CHECK(cell.ca_solution_g_per_m2 - before.ca_solution_g_per_m2 == Catch::Approx(kg * fert.ca_g_per_kg));
    CHECK(cell.mg_solution_g_per_m2 - before.mg_solution_g_per_m2 == Catch::Approx(kg * fert.mg_g_per_kg));
    CHECK(cell.mn_solution_g_per_m2 - before.mn_solution_g_per_m2 == Catch::Approx(kg * fert.mn_g_per_kg));
    // 총량 증가분과 용액 증가분이 같아서 교환성(총량 − 용액)은 그대로예요
    CHECK((cell.k_g_per_m2 - cell.k_solution_g_per_m2) ==
          Catch::Approx(before.k_g_per_m2 - before.k_solution_g_per_m2));
    require_pools_non_negative(cell);
}

// 퇴비 무기 성분도 같다
TEST_CASE("Compost cations go to both the total and the solution", "[unit][soil_chemistry][m3]") {
    const sim::Params params = test::test_params();
    const sim::CompostDef& compost = params.composts.front();
    const double kg = 3.0;
    sim::SoilCell cell = cell_without_lime();
    const sim::SoilCell before = cell;
    sim::Ledger ledger;
    sim::process::apply_compost_minerals(cell, compost, kg, ledger);
    CHECK(cell.k_solution_g_per_m2 - before.k_solution_g_per_m2 == Catch::Approx(kg * compost.k_g_per_kg));
    CHECK(cell.ca_solution_g_per_m2 - before.ca_solution_g_per_m2 == Catch::Approx(kg * compost.ca_g_per_kg));
    CHECK(cell.mg_solution_g_per_m2 - before.mg_solution_g_per_m2 == Catch::Approx(kg * compost.mg_g_per_kg));
    CHECK(cell.mn_solution_g_per_m2 - before.mn_solution_g_per_m2 == Catch::Approx(kg * compost.mn_g_per_kg));
    // NH4는 organic_matter 담당(무기화)이라 여기서는 안 바뀌어요
    CHECK(cell.nh4_solution_N_g_per_m2 == before.nh4_solution_N_g_per_m2);
    CHECK(cell.nh4_N_g_per_m2 == before.nh4_N_g_per_m2);
}

// 석회 투입 직후에는 용액도 그대로 (미용해 풀만 늘어요). 용해된 Ca·Mg가 총량·용액에 같은 양 들어가요
TEST_CASE("Lime changes the solution only when it dissolves", "[unit][soil_chemistry][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = cell_without_lime();
    cell.ph = 5.0;
    sim::process::apply_lime(cell, find_lime(params, "dolomite"), 300.0);
    CHECK(cell.ca_solution_g_per_m2 == test::test_cell().ca_solution_g_per_m2);
    CHECK(cell.mg_solution_g_per_m2 == test::test_cell().mg_solution_g_per_m2);
    const sim::SoilCell before = cell;
    sim::Ledger ledger;
    sim::process::update_lime_and_ph(cell, params, ledger);
    CHECK(cell.ca_solution_g_per_m2 - before.ca_solution_g_per_m2 == Catch::Approx(cell.ca_g_per_m2 - before.ca_g_per_m2));
    CHECK(cell.mg_solution_g_per_m2 - before.mg_solution_g_per_m2 == Catch::Approx(cell.mg_g_per_m2 - before.mg_g_per_m2));
    CHECK(cell.ca_solution_g_per_m2 > before.ca_solution_g_per_m2);
    CHECK(cell.mg_solution_g_per_m2 > before.mg_solution_g_per_m2);
    CHECK(ledger.Ca_in_g == Catch::Approx(cell.ca_solution_g_per_m2 - before.ca_solution_g_per_m2));
    CHECK(ledger.Mg_in_g == Catch::Approx(cell.mg_solution_g_per_m2 - before.mg_solution_g_per_m2));
}

// pH 완충력의 광물 CEC 항: 같은 산이라도 항이 있으면(buffer_eq_per_pH_per_eq_cec > 0) pH 변화폭이 정확히 그만큼 작다
TEST_CASE("Buffer capacity includes the mineral CEC term", "[unit][soil_chemistry][m3]") {
    const sim::Params params = test::test_params();
    REQUIRE(params.cation_exchange.buffer_eq_per_pH_per_eq_cec > 0.0);
    sim::Params without_cec_term = params;
    without_cec_term.cation_exchange.buffer_eq_per_pH_per_eq_cec = 0.0;

    sim::SoilCell with_term = cell_without_lime();
    sim::SoilCell without_term = cell_without_lime();
    with_term.acid_H_eq_this_tick = 2.0;
    without_term.acid_H_eq_this_tick = 2.0;
    const double ph_before = with_term.ph;
    sim::Ledger ledger;
    sim::process::update_lime_and_ph(with_term, params, ledger);
    sim::process::update_lime_and_ph(without_term, without_cec_term, ledger);
    CHECK(ph_before - with_term.ph == Catch::Approx(2.0 / buffer_of(with_term, params)));
    CHECK(ph_before - without_term.ph == Catch::Approx(2.0 / buffer_of(without_term, without_cec_term)));
    CHECK(ph_before - with_term.ph < ph_before - without_term.ph);
    // 유기물 몫은 M2의 organic_C 항이 맡아서 CEC의 부식 몫은 완충력에 더하지 않아요 (광물 CEC만)
    sim::SoilCell more_humus = cell_without_lime();
    more_humus.acid_H_eq_this_tick = 2.0;
    more_humus.stable_C_g_per_m2 *= 2.0;  // 유기 C 항은 변하지만 CEC 항은 광물 CEC 그대로
    sim::process::update_lime_and_ph(more_humus, params, ledger);
    CHECK(ph_before - more_humus.ph == Catch::Approx(2.0 / buffer_of(more_humus, params)));
}

// EC에 Mn 용액이 들어간다: 용액 Mn만 있을 때 EC = (Mn meq / 2) / 물 / 환산계수
TEST_CASE("EC includes solution manganese", "[unit][soil_chemistry][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = cell_without_lime();
    zero_cations(cell);
    cell.no2_N_g_per_m2 = 0.0;
    cell.no3_N_g_per_m2 = 0.0;
    cell.p_g_per_m2 = 0.0;
    cell.s_g_per_m2 = 0.0;
    REQUIRE(sim::process::soil_ec_dS_per_m(cell, params) == 0.0);
    cell.mn_g_per_m2 = 6.0;
    cell.mn_solution_g_per_m2 = 4.0;
    const double mn_meq = 4.0 / params.ions.mn_g_per_eq * kMilliPerUnit;
    const double expected = mn_meq / 2.0 / cell.water_mm / params.soil_chemistry.ec_meq_per_L_per_dS_per_m;
    CHECK(sim::process::soil_ec_dS_per_m(cell, params) == Catch::Approx(expected));
    // 산화물·교환성 Mn은 EC에 들어가지 않아요
    sim::SoilCell more_oxide = cell;
    more_oxide.mn_oxide_g_per_m2 += 500.0;
    more_oxide.mn_g_per_m2 += 3.0;  // 총량만 늘고 용액은 그대로
    CHECK(sim::process::soil_ec_dS_per_m(more_oxide, params) == sim::process::soil_ec_dS_per_m(cell, params));
}

// EC의 양이온은 용액 상태를 쓴다: 총량이 같아도 용액이 많으면 EC가 높다
TEST_CASE("EC follows the solution state not the cation total", "[unit][soil_chemistry][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell mostly_bound = cell_without_lime();
    sim::SoilCell mostly_dissolved = cell_without_lime();
    mostly_dissolved.k_solution_g_per_m2 = 0.9 * mostly_dissolved.k_g_per_m2;
    mostly_dissolved.ca_solution_g_per_m2 = 0.9 * mostly_dissolved.ca_g_per_m2;
    REQUIRE(mostly_bound.k_g_per_m2 == mostly_dissolved.k_g_per_m2);
    CHECK(sim::process::soil_ec_dS_per_m(mostly_dissolved, params) > sim::process::soil_ec_dS_per_m(mostly_bound, params));
}

// 유효량: Mn은 pH 유효도 곡선 없이 용액 그대로 (pH 효과는 산화·흡착에 이미 들어 있어요)
TEST_CASE("Available Mn is the solution amount regardless of pH", "[unit][soil_chemistry][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell acid = cell_without_lime();
    sim::SoilCell alkaline = cell_without_lime();
    acid.ph = 4.5;
    alkaline.ph = 8.5;
    CHECK(sim::process::available_nutrients(acid, params).mn == acid.mn_solution_g_per_m2);
    CHECK(sim::process::available_nutrients(alkaline, params).mn == alkaline.mn_solution_g_per_m2);
    // 반면 K·Ca·Mg는 pH 곡선을 거쳐요 (같은 용액이라도 pH 4.5에서 작음)
    CHECK(sim::process::available_nutrients(acid, params).ca < sim::process::available_nutrients(alkaline, params).ca);
}

// 유효량은 용액 상태에서만 나온다: 총량이 커도 용액이 0이면 유효량 0
TEST_CASE("Available cations come only from the solution state", "[unit][soil_chemistry][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = cell_without_lime();
    cell.k_solution_g_per_m2 = 0.0;
    cell.ca_solution_g_per_m2 = 0.0;
    cell.mg_solution_g_per_m2 = 0.0;
    cell.nh4_solution_N_g_per_m2 = 0.0;
    cell.mn_solution_g_per_m2 = 0.0;
    const sim::process::NutrientAvailability a = sim::process::available_nutrients(cell, params);
    CHECK(a.k == 0.0);
    CHECK(a.ca == 0.0);
    CHECK(a.mg == 0.0);
    CHECK(a.nh4_N == 0.0);
    CHECK(a.mn == 0.0);
    CHECK(a.no3_N > 0.0);  // 음이온 NO3는 M2 그대로 (총량 × pH 유효도)
}
