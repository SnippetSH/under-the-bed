// M3 단위 테스트: 양이온 교환 (docs/spec/cation-exchange.md "테스트 (단위)")
// 원칙: 명세가 말하는 방향·식·허용오차만 검증하고, 현재 파라미터 값에 기대는 매직 넘버는 쓰지 않아요.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

#include "sim/processes.hpp"
#include "test_helpers.hpp"

namespace {

using sim::process::ExchangeEquilibrium;

// 교환성 합 [eq/m²] = Σ (총량 − 평형 용액) / 당량 무게
double exchangeable_eq(const sim::SoilCell& cell, const sim::Params& params, const ExchangeEquilibrium& eq) {
    const sim::IonParams& ions = params.ions;
    return (cell.nh4_N_g_per_m2 - eq.nh4_solution_N_g_per_m2) / ions.nh4_N_g_per_eq +
           (cell.k_g_per_m2 - eq.k_solution_g_per_m2) / ions.k_g_per_eq +
           (cell.ca_g_per_m2 - eq.ca_solution_g_per_m2) / ions.ca_g_per_eq +
           (cell.mg_g_per_m2 - eq.mg_solution_g_per_m2) / ions.mg_g_per_eq +
           (cell.mn_g_per_m2 - eq.mn_solution_g_per_m2) / ions.mn_g_per_eq;
}

// 용액 비율 = 평형 용액 / 총량
struct Fractions {
    double nh4, k, ca, mg, mn;
};

Fractions fractions_of(const sim::SoilCell& cell, const ExchangeEquilibrium& eq) {
    return Fractions{.nh4 = eq.nh4_solution_N_g_per_m2 / cell.nh4_N_g_per_m2,
                     .k = eq.k_solution_g_per_m2 / cell.k_g_per_m2,
                     .ca = eq.ca_solution_g_per_m2 / cell.ca_g_per_m2,
                     .mg = eq.mg_solution_g_per_m2 / cell.mg_g_per_m2,
                     .mn = eq.mn_solution_g_per_m2 / cell.mn_g_per_m2};
}

// 망간 산화·환원을 끄고 교환만 보고 싶을 때 (교환 평형이 틱마다 흔들리지 않게)
sim::Params params_without_redox() {
    sim::Params params = test::test_params();
    params.manganese.oxidation_rate_per_hour = 0.0;
    params.manganese.reduction_rate_per_hour = 0.0;
    return params;
}

const sim::CompostDef& find_compost(const sim::Params& params, const std::string& id) {
    for (const auto& compost : params.composts) {
        if (compost.id == id) {
            return compost;
        }
    }
    throw std::runtime_error("compost not found: " + id);
}

const sim::FertilizerDef& find_fertilizer(const sim::Params& params, const std::string& id) {
    for (const auto& fertilizer : params.fertilizers) {
        if (fertilizer.id == id) {
            return fertilizer;
        }
    }
    throw std::runtime_error("fertilizer not found: " + id);
}

// 용액 상태를 비운 채(전부 붙은 것처럼) 시작하는 셀: 완화가 어느 쪽으로 움직이는지 보려고 써요.
sim::SoilCell cell_with_empty_solution() {
    sim::SoilCell cell = test::test_cell();
    cell.nh4_solution_N_g_per_m2 = 0.0;
    cell.k_solution_g_per_m2 = 0.0;
    cell.ca_solution_g_per_m2 = 0.0;
    cell.mg_solution_g_per_m2 = 0.0;
    cell.mn_solution_g_per_m2 = 0.0;
    return cell;
}

}  // namespace

// CEC = 광물 CEC + 부식(난분해성) C × 계수. 이분해성 C는 세지 않아요.
TEST_CASE("CEC is mineral CEC plus organic CEC from stable carbon only", "[unit][cation_exchange][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    const double expected = cell.cec_mineral_eq_per_m2 + params.cation_exchange.cec_organic_eq_per_g_C * cell.stable_C_g_per_m2;
    REQUIRE(sim::process::cec_eq_per_m2(cell, params.cation_exchange) == Catch::Approx(expected));

    // 이분해성 C를 늘려도 CEC는 그대로
    sim::SoilCell more_labile = cell;
    more_labile.labile_C_g_per_m2 += 1000.0;
    REQUIRE(sim::process::cec_eq_per_m2(more_labile, params.cation_exchange) ==
            sim::process::cec_eq_per_m2(cell, params.cation_exchange));

    // 난분해성 C가 늘면 CEC 증가
    sim::SoilCell more_stable = cell;
    more_stable.stable_C_g_per_m2 += 1000.0;
    REQUIRE(sim::process::cec_eq_per_m2(more_stable, params.cation_exchange) >
            sim::process::cec_eq_per_m2(cell, params.cation_exchange));
}

// 기준 토양 평형: 용액 비율이 M2 고정값(NH4 0.1, K 0.03, Ca 0.01, Mg 0.02)과 ±10% 안
TEST_CASE("Reference soil equilibrium matches the M2 fixed solution fractions within 10 percent",
          "[unit][cation_exchange][m3]") {
    const sim::Params params = test::test_params();
    const sim::SoilCell cell = test::test_cell();  // 기준 토양: K 60, Ca 400, Mg 60, NH4 2, 부식 C 3600, 광물 CEC 25
    const ExchangeEquilibrium eq = sim::process::exchange_equilibrium(cell, params);
    const Fractions f = fractions_of(cell, eq);
    CHECK(f.nh4 == Catch::Approx(0.1).epsilon(0.10));
    CHECK(f.k == Catch::Approx(0.03).epsilon(0.10));
    CHECK(f.ca == Catch::Approx(0.01).epsilon(0.10));
    CHECK(f.mg == Catch::Approx(0.02).epsilon(0.10));
}

// 평형 해: cec × (1 − φ) = Σ 교환성 잔차가 cec의 1e-9 이하
TEST_CASE("Equilibrium solves cec times one minus phi equals exchangeable sum", "[unit][cation_exchange][m3]") {
    const sim::Params params = test::test_params();
    struct Variant {
        double cec_mineral_scale;
        double k_scale;
        double ph;
    };
    const std::array<Variant, 6> variants{{{1.0, 1.0, 6.5}, {0.04, 1.0, 6.5}, {1.0, 20.0, 5.0}, {0.2, 5.0, 8.0},
                                           {3.0, 0.1, 4.5}, {0.01, 50.0, 7.0}}};
    for (const Variant& v : variants) {
        INFO("cec scale " << v.cec_mineral_scale << " k scale " << v.k_scale << " ph " << v.ph);
        sim::SoilCell cell = test::test_cell();
        cell.cec_mineral_eq_per_m2 *= v.cec_mineral_scale;
        cell.k_g_per_m2 *= v.k_scale;
        cell.ph = v.ph;
        const ExchangeEquilibrium eq = sim::process::exchange_equilibrium(cell, params);
        const double cec = eq.cec_eq_per_m2;
        REQUIRE(cec > 0.0);
        const double residual = cec * (1.0 - eq.exchange_free_site_index) - exchangeable_eq(cell, params, eq);
        REQUIRE(std::abs(residual) <= 1e-9 * cec);
        // 불변 조건: 0 ≤ φ ≤ 1, 0 ≤ 용액 ≤ 총량, 교환성 합 ≤ CEC
        REQUIRE(eq.exchange_free_site_index >= 0.0);
        REQUIRE(eq.exchange_free_site_index <= 1.0);
        REQUIRE(eq.k_solution_g_per_m2 >= 0.0);
        REQUIRE(eq.k_solution_g_per_m2 <= cell.k_g_per_m2);
        REQUIRE(eq.ca_solution_g_per_m2 <= cell.ca_g_per_m2);
        REQUIRE(eq.mg_solution_g_per_m2 <= cell.mg_g_per_m2);
        REQUIRE(eq.nh4_solution_N_g_per_m2 <= cell.nh4_N_g_per_m2);
        REQUIRE(eq.mn_solution_g_per_m2 <= cell.mn_g_per_m2);
        REQUIRE(exchangeable_eq(cell, params, eq) <= cec * (1.0 + 1e-9));
    }
}

// 같은 양이온, CEC 절반 → 모든 이온의 용액 비율 증가
TEST_CASE("Half the CEC raises the solution fraction of every cation", "[unit][cation_exchange][m3]") {
    const sim::Params params = test::test_params();
    const sim::SoilCell full = test::test_cell();
    sim::SoilCell half = full;
    half.cec_mineral_eq_per_m2 *= 0.5;
    half.stable_C_g_per_m2 *= 0.5;  // 부식 몫도 절반이라 전체 CEC가 정확히 절반
    REQUIRE(sim::process::cec_eq_per_m2(half, params.cation_exchange) ==
            Catch::Approx(0.5 * sim::process::cec_eq_per_m2(full, params.cation_exchange)));

    const Fractions f_full = fractions_of(full, sim::process::exchange_equilibrium(full, params));
    const Fractions f_half = fractions_of(half, sim::process::exchange_equilibrium(half, params));
    CHECK(f_half.nh4 > f_full.nh4);
    CHECK(f_half.k > f_full.k);
    CHECK(f_half.ca > f_full.ca);
    CHECK(f_half.mg > f_full.mg);
    CHECK(f_half.mn > f_full.mn);
}

// 양이온 합 > CEC: 교환성 합 ≤ CEC, 남는 양은 용액으로, 용액 비율은 NH4 > K > Mg > Ca
TEST_CASE("Cations beyond the CEC stay in solution weakest first", "[unit][cation_exchange][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.cec_mineral_eq_per_m2 = 2.0;
    cell.stable_C_g_per_m2 = 0.0;  // CEC = 2 eq/m²
    const double cec = sim::process::cec_eq_per_m2(cell, params.cation_exchange);
    const sim::IonParams& ions = params.ions;
    const double total_eq = cell.nh4_N_g_per_m2 / ions.nh4_N_g_per_eq + cell.k_g_per_m2 / ions.k_g_per_eq +
                            cell.ca_g_per_m2 / ions.ca_g_per_eq + cell.mg_g_per_m2 / ions.mg_g_per_eq +
                            cell.mn_g_per_m2 / ions.mn_g_per_eq;
    REQUIRE(total_eq > cec);

    const ExchangeEquilibrium eq = sim::process::exchange_equilibrium(cell, params);
    const double exchangeable = exchangeable_eq(cell, params, eq);
    CHECK(exchangeable <= cec * (1.0 + 1e-9));
    // 남는 양(총합 − 교환성)이 전부 용액에 있고 CEC를 넘어선 만큼은 최소한 용액이에요
    const double solution_eq = eq.nh4_solution_N_g_per_m2 / ions.nh4_N_g_per_eq + eq.k_solution_g_per_m2 / ions.k_g_per_eq +
                               eq.ca_solution_g_per_m2 / ions.ca_g_per_eq + eq.mg_solution_g_per_m2 / ions.mg_g_per_eq +
                               eq.mn_solution_g_per_m2 / ions.mn_g_per_eq;
    CHECK(solution_eq >= total_eq - cec - 1e-9);

    const Fractions f = fractions_of(cell, eq);
    CHECK(f.nh4 > f.k);
    CHECK(f.k > f.mg);
    CHECK(f.mg > f.ca);
}

// K 대량 투입 → Ca·Mg 용액 양 증가 (자리에서 밀려남)
TEST_CASE("Heavy potassium pushes calcium and magnesium into solution", "[unit][cation_exchange][m3]") {
    const sim::Params params = test::test_params();
    const sim::SoilCell base = test::test_cell();
    sim::SoilCell heavy_k = base;
    heavy_k.k_g_per_m2 *= 8.0;
    const ExchangeEquilibrium eq_base = sim::process::exchange_equilibrium(base, params);
    const ExchangeEquilibrium eq_heavy = sim::process::exchange_equilibrium(heavy_k, params);
    CHECK(eq_heavy.ca_solution_g_per_m2 > eq_base.ca_solution_g_per_m2);
    CHECK(eq_heavy.mg_solution_g_per_m2 > eq_base.mg_solution_g_per_m2);
    CHECK(eq_heavy.k_solution_g_per_m2 > eq_base.k_solution_g_per_m2);
    CHECK(eq_heavy.exchange_free_site_index < eq_base.exchange_free_site_index);
}

// 비료 투입 직후 투입분이 전부 용액에 있고, 이후 틱마다 평형 쪽으로 움직이되 넘어서지 않는다
TEST_CASE("Fertilizer arrives fully in solution then relaxes toward equilibrium without overshoot",
          "[unit][cation_exchange][m3]") {
    const sim::Params params = params_without_redox();
    sim::SoilCell cell = test::test_cell();
    // 평형으로 맞춘 직후 상태에서 황산칼륨을 줘요
    sim::process::set_solution_to_equilibrium(cell, params);
    const double k_solution_before = cell.k_solution_g_per_m2;
    const double k_total_before = cell.k_g_per_m2;
    sim::Ledger ledger;
    const sim::FertilizerDef& potassium_sulfate = find_fertilizer(params, "potassium_sulfate");
    const double g_per_m2 = 200.0;
    const double added_k = g_per_m2 / 1000.0 * potassium_sulfate.k_g_per_kg;
    sim::process::apply_fertilizer(cell, potassium_sulfate, g_per_m2, ledger);

    // 투입분이 전부 용액으로
    CHECK(cell.k_g_per_m2 - k_total_before == Catch::Approx(added_k));
    CHECK(cell.k_solution_g_per_m2 - k_solution_before == Catch::Approx(added_k));

    const double eq_k = sim::process::exchange_equilibrium(cell, params).k_solution_g_per_m2;
    REQUIRE(cell.k_solution_g_per_m2 > eq_k);  // 평형보다 많이 녹아 있어요 (붙을 자리가 모자람)
    double previous = cell.k_solution_g_per_m2;
    for (int hour = 0; hour < 48; ++hour) {
        sim::process::update_cation_exchange(cell, params);
        // 평형 쪽으로 단조 감소하고 평형 아래로 내려가지 않아요
        CHECK(cell.k_solution_g_per_m2 <= previous);
        CHECK(cell.k_solution_g_per_m2 >= eq_k - 1e-9);
        previous = cell.k_solution_g_per_m2;
    }
    CHECK(cell.k_solution_g_per_m2 < eq_k + 0.01 * (k_solution_before + added_k - eq_k));  // 48시간이면 거의 평형
}

// 반대쪽: 용액이 평형보다 적으면 늘어나고, 역시 넘어서지 않는다
TEST_CASE("Solution below equilibrium relaxes upward without overshoot", "[unit][cation_exchange][m3]") {
    const sim::Params params = params_without_redox();
    sim::SoilCell cell = cell_with_empty_solution();
    const ExchangeEquilibrium eq = sim::process::exchange_equilibrium(cell, params);
    REQUIRE(eq.ca_solution_g_per_m2 > 0.0);
    double previous = cell.ca_solution_g_per_m2;
    for (int hour = 0; hour < 24; ++hour) {
        sim::process::update_cation_exchange(cell, params);
        CHECK(cell.ca_solution_g_per_m2 >= previous);
        CHECK(cell.ca_solution_g_per_m2 <= eq.ca_solution_g_per_m2 + 1e-12);
        previous = cell.ca_solution_g_per_m2;
    }
}

// 완화 속도 1 → 한 틱에 평형 도달
TEST_CASE("Relaxation rate one reaches equilibrium in a single tick", "[unit][cation_exchange][m3]") {
    sim::Params params = params_without_redox();
    params.cation_exchange.exchange_rate_per_hour = 1.0;
    sim::SoilCell cell = cell_with_empty_solution();
    cell.k_solution_g_per_m2 = cell.k_g_per_m2;  // 일부는 평형보다 많이, 일부는 적게
    const ExchangeEquilibrium eq = sim::process::exchange_equilibrium(cell, params);
    sim::process::update_cation_exchange(cell, params);
    CHECK(cell.k_solution_g_per_m2 == Catch::Approx(eq.k_solution_g_per_m2));
    CHECK(cell.ca_solution_g_per_m2 == Catch::Approx(eq.ca_solution_g_per_m2));
    CHECK(cell.mg_solution_g_per_m2 == Catch::Approx(eq.mg_solution_g_per_m2));
    CHECK(cell.nh4_solution_N_g_per_m2 == Catch::Approx(eq.nh4_solution_N_g_per_m2));
    CHECK(cell.mn_solution_g_per_m2 == Catch::Approx(eq.mn_solution_g_per_m2));
    CHECK(cell.exchange_free_site_index == Catch::Approx(eq.exchange_free_site_index));
    CHECK(cell.cec_eq_per_m2 == Catch::Approx(eq.cec_eq_per_m2));
}

// 완화 속도가 1보다 커도 한 틱에 평형을 넘지 않는다 (min(1, rate))
TEST_CASE("Relaxation rate above one does not overshoot equilibrium", "[unit][cation_exchange][m3][extreme]") {
    sim::Params params = params_without_redox();
    params.cation_exchange.exchange_rate_per_hour = 50.0;
    sim::SoilCell cell = cell_with_empty_solution();
    const ExchangeEquilibrium eq = sim::process::exchange_equilibrium(cell, params);
    sim::process::update_cation_exchange(cell, params);
    CHECK(cell.k_solution_g_per_m2 == Catch::Approx(eq.k_solution_g_per_m2));
    CHECK(cell.k_solution_g_per_m2 <= cell.k_g_per_m2);
}

// 교환은 계 안의 이동: 완화해도 NH4·K·Ca·Mg 총량은 그대로, Mn은 유효태 + 산화물이 보존
TEST_CASE("Relaxation moves cations between bound and dissolved without changing totals",
          "[unit][cation_exchange][conservation][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = cell_with_empty_solution();
    const sim::SoilCell before = cell;
    for (int hour = 0; hour < 72; ++hour) {
        sim::process::update_cation_exchange(cell, params);
    }
    CHECK(cell.nh4_N_g_per_m2 == before.nh4_N_g_per_m2);
    CHECK(cell.k_g_per_m2 == before.k_g_per_m2);
    CHECK(cell.ca_g_per_m2 == before.ca_g_per_m2);
    CHECK(cell.mg_g_per_m2 == before.mg_g_per_m2);
    CHECK(cell.mn_g_per_m2 + cell.mn_oxide_g_per_m2 ==
          Catch::Approx(before.mn_g_per_m2 + before.mn_oxide_g_per_m2).epsilon(1e-12));
    CHECK(cell.k_solution_g_per_m2 > 0.0);  // 실제로 움직였어요
}

// 숙성 퇴비 투입 → 같은 틱 이후 CEC 증가. 미숙 퇴비는 즉시 증가분이 더 작다
TEST_CASE("Mature compost raises CEC more than immature compost right after application",
          "[unit][cation_exchange][m3]") {
    const sim::Params params = test::test_params();
    const double kg_per_m2 = 5.0;
    sim::SoilCell base = test::test_cell();
    sim::process::update_cation_exchange(base, params);
    const double cec_before = base.cec_eq_per_m2;
    REQUIRE(cec_before > 0.0);

    sim::SoilCell mature = base;
    sim::SoilCell immature = base;
    sim::Ledger ledger;
    sim::process::apply_compost(mature, find_compost(params, "mature_manure"), kg_per_m2, ledger);
    sim::process::apply_compost(immature, find_compost(params, "immature_manure"), kg_per_m2, ledger);
    sim::process::update_cation_exchange(mature, params);
    sim::process::update_cation_exchange(immature, params);

    const double mature_gain = mature.cec_eq_per_m2 - cec_before;
    const double immature_gain = immature.cec_eq_per_m2 - cec_before;
    CHECK(mature_gain > 0.0);
    CHECK(immature_gain >= 0.0);
    CHECK(mature_gain > immature_gain);
}

// 질산화로 NH4 총량이 절반 → NH4 용액도 절반 (비례 차감)
TEST_CASE("Halving NH4 total halves NH4 solution proportionally", "[unit][cation_exchange][m3]") {
    sim::SoilCell cell = test::test_cell();
    const double solution_before = cell.nh4_solution_N_g_per_m2;
    const double total_before = cell.nh4_N_g_per_m2;
    REQUIRE(solution_before > 0.0);
    cell.nh4_N_g_per_m2 = 0.5 * total_before;
    sim::process::sync_nh4_solution(cell, total_before);
    CHECK(cell.nh4_solution_N_g_per_m2 == Catch::Approx(0.5 * solution_before));
    CHECK(cell.nh4_solution_N_g_per_m2 <= cell.nh4_N_g_per_m2);
}

// 무기화로 NH4 총량이 늘면 증가분은 그대로 용액에
TEST_CASE("NH4 added by mineralization goes to solution", "[unit][cation_exchange][m3]") {
    sim::SoilCell cell = test::test_cell();
    const double solution_before = cell.nh4_solution_N_g_per_m2;
    const double total_before = cell.nh4_N_g_per_m2;
    cell.nh4_N_g_per_m2 = total_before + 3.0;
    sim::process::sync_nh4_solution(cell, total_before);
    CHECK(cell.nh4_solution_N_g_per_m2 == Catch::Approx(solution_before + 3.0));
}

// 실제 질산화 프로세스도 NH4 용액을 비례 차감한다 (용액/총량 비율이 그대로)
TEST_CASE("Nitrification reduces NH4 solution in proportion to the total", "[unit][cation_exchange][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.nh4_N_g_per_m2 = 6.0;
    sim::process::set_solution_to_equilibrium(cell, params);
    const double ratio_before = cell.nh4_solution_N_g_per_m2 / cell.nh4_N_g_per_m2;
    const double total_before = cell.nh4_N_g_per_m2;
    sim::process::nitrify(cell, params.nitrification, params.soil_chemistry, params.ions);
    REQUIRE(cell.nh4_N_g_per_m2 < total_before);
    CHECK(cell.nh4_solution_N_g_per_m2 / cell.nh4_N_g_per_m2 == Catch::Approx(ratio_before));
}

// 같은 산, 광물 CEC 큼 → pH 변화폭 작음 (완충력의 광물 CEC 항)
TEST_CASE("Same acid moves pH less when mineral CEC is larger", "[unit][cation_exchange][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell low = test::test_cell();
    sim::SoilCell high = test::test_cell();
    low.cec_mineral_eq_per_m2 = 5.0;
    high.cec_mineral_eq_per_m2 = 50.0;
    low.lime_undissolved_eq_per_m2 = 0.0;
    high.lime_undissolved_eq_per_m2 = 0.0;
    low.acid_H_eq_this_tick = 3.0;
    high.acid_H_eq_this_tick = 3.0;
    const double ph_before = low.ph;
    sim::Ledger low_ledger;
    sim::Ledger high_ledger;
    sim::process::update_lime_and_ph(low, params, low_ledger);
    sim::process::update_lime_and_ph(high, params, high_ledger);
    CHECK(ph_before - low.ph > 0.0);
    CHECK(ph_before - high.ph > 0.0);
    CHECK(ph_before - high.ph < ph_before - low.ph);
    // 변화폭은 정확히 H / (base + b × 광물 CEC + 유기 C 항)
    const double organic = params.soil_chemistry.organic_C_buffer_eq_per_g_C_per_pH * low.organic_C_g_per_m2();
    CHECK(ph_before - low.ph ==
          Catch::Approx(3.0 / (low.base_buffer_eq_per_m2_per_pH +
                               params.cation_exchange.buffer_eq_per_pH_per_eq_cec * 5.0 + organic)));
    CHECK(ph_before - high.ph ==
          Catch::Approx(3.0 / (high.base_buffer_eq_per_m2_per_pH +
                               params.cation_exchange.buffer_eq_per_pH_per_eq_cec * 50.0 + organic)));
}

// 길항 지수 0.2 대 1.0, 나머지 같음 → Ca·Mg 흡수 능력은 1.0 쪽이 작고, K는 같다
TEST_CASE("Higher antagonism index lowers Ca and Mg uptake capacity but not K", "[unit][cation_exchange][m3]") {
    const sim::Params params = test::test_params();
    const auto build = [&](double antagonism_index) {
        sim::SoilCell cell = test::test_cell();
        cell.nh4_solution_N_g_per_m2 = 0.0;
        cell.ca_solution_g_per_m2 = 20.0;
        cell.mg_solution_g_per_m2 = 5.0;
        const double divalent_eq = cell.ca_solution_g_per_m2 / params.ions.ca_g_per_eq +
                                   cell.mg_solution_g_per_m2 / params.ions.mg_g_per_eq;
        cell.k_solution_g_per_m2 = antagonism_index * divalent_eq * params.ions.k_g_per_eq;
        cell.k_g_per_m2 = std::max(cell.k_g_per_m2, 2.0 * cell.k_solution_g_per_m2);
        return cell;
    };
    // 능력이 병목이 되도록 유효량은 작게 (두 쪽 똑같이 직접 지정)
    const sim::process::NutrientAvailability small_available{
        .nh4_N = 0.0, .no3_N = 0.0, .p = 0.0, .k = 5.0, .ca = 8.0, .mg = 3.0, .mn = 0.0};

    sim::SoilCell low_cell = build(0.2);
    sim::SoilCell high_cell = build(1.0);
    sim::Crop low_crop = sim::process::plant_crop(test::test_crop_init(), params.crop);
    sim::Crop high_crop = low_crop;
    // 수요가 충분히 크도록 체내 양분을 비워요
    for (sim::Crop* crop : {&low_crop, &high_crop}) {
        crop->plant_Ca_g_per_m2 = 0.0;
        crop->plant_Mg_g_per_m2 = 0.0;
        crop->plant_K_g_per_m2 = 0.0;
    }
    sim::Ledger low_ledger;
    sim::Ledger high_ledger;
    (void)sim::process::update_crop(low_crop, low_cell, params, small_available, 0.0, 20.0,
                                    sim::StressCombination::Minimum, 0, low_ledger);
    (void)sim::process::update_crop(high_crop, high_cell, params, small_available, 0.0, 20.0,
                                    sim::StressCombination::Minimum, 0, high_ledger);

    CHECK(low_crop.antagonism_Ca_factor == Catch::Approx(params.antagonism.ca_curve(0.2)));
    CHECK(high_crop.antagonism_Ca_factor == Catch::Approx(params.antagonism.ca_curve(1.0)));
    CHECK(low_crop.antagonism_Mg_factor == Catch::Approx(params.antagonism.mg_curve(0.2)));
    CHECK(high_crop.antagonism_Mg_factor == Catch::Approx(params.antagonism.mg_curve(1.0)));
    CHECK(high_crop.antagonism_Ca_factor < low_crop.antagonism_Ca_factor);
    CHECK(high_crop.antagonism_Mg_factor < low_crop.antagonism_Mg_factor);
    CHECK(high_ledger.Ca_uptake_g < low_ledger.Ca_uptake_g);
    CHECK(high_ledger.Mg_uptake_g < low_ledger.Mg_uptake_g);
    CHECK(high_ledger.Ca_uptake_g > 0.0);
    CHECK(low_ledger.K_uptake_g > 0.0);
    CHECK(high_ledger.K_uptake_g == low_ledger.K_uptake_g);  // K 흡수 능력은 길항을 받지 않아요
}

// 극단: cec ≤ 0 → φ = 0, 용액 = 총량, NaN 없음. 양이온이 하나도 없으면 φ = 1, 용액 0
TEST_CASE("Zero CEC and zero cations give safe equilibria without NaN", "[unit][cation_exchange][m3][extreme]") {
    const sim::Params params = test::test_params();
    for (const double mineral_cec : {0.0, -5.0}) {
        sim::SoilCell no_sites = test::test_cell();
        no_sites.cec_mineral_eq_per_m2 = mineral_cec;
        no_sites.stable_C_g_per_m2 = 0.0;
        const ExchangeEquilibrium eq = sim::process::exchange_equilibrium(no_sites, params);
        CHECK(eq.exchange_free_site_index == 0.0);
        CHECK(eq.nh4_solution_N_g_per_m2 == no_sites.nh4_N_g_per_m2);
        CHECK(eq.k_solution_g_per_m2 == no_sites.k_g_per_m2);
        CHECK(eq.ca_solution_g_per_m2 == no_sites.ca_g_per_m2);
        CHECK(eq.mg_solution_g_per_m2 == no_sites.mg_g_per_m2);
        CHECK(eq.mn_solution_g_per_m2 == no_sites.mn_g_per_m2);
        // 틱을 돌려도 유한하고 용액 ≤ 총량
        for (int hour = 0; hour < 10; ++hour) {
            sim::process::update_cation_exchange(no_sites, params);
        }
        CHECK(std::isfinite(no_sites.k_solution_g_per_m2));
        CHECK(no_sites.k_solution_g_per_m2 <= no_sites.k_g_per_m2);
        CHECK(std::isfinite(no_sites.exchange_free_site_index));
    }

    sim::SoilCell no_cations = test::test_cell();
    no_cations.nh4_N_g_per_m2 = no_cations.k_g_per_m2 = no_cations.ca_g_per_m2 = no_cations.mg_g_per_m2 = 0.0;
    no_cations.mn_g_per_m2 = 0.0;
    no_cations.nh4_solution_N_g_per_m2 = no_cations.k_solution_g_per_m2 = no_cations.ca_solution_g_per_m2 = 0.0;
    no_cations.mg_solution_g_per_m2 = no_cations.mn_solution_g_per_m2 = 0.0;
    const ExchangeEquilibrium eq = sim::process::exchange_equilibrium(no_cations, params);
    CHECK(eq.exchange_free_site_index == 1.0);
    CHECK(eq.k_solution_g_per_m2 == 0.0);
    CHECK(eq.ca_solution_g_per_m2 == 0.0);
    sim::process::update_cation_exchange(no_cations, params);
    CHECK(no_cations.k_solution_g_per_m2 == 0.0);
    CHECK(std::isfinite(no_cations.exchange_free_site_index));
}

// 극단: 비료 100배 투입에도 불변 조건 유지 (0 ≤ 용액 ≤ 총량, 0 ≤ φ ≤ 1, 교환성 합 ≤ CEC, NaN 없음)
TEST_CASE("Extreme cation loading keeps exchange invariants", "[unit][cation_exchange][m3][extreme]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.cec_mineral_eq_per_m2 = 0.01;
    cell.stable_C_g_per_m2 = 0.0;
    sim::Ledger ledger;
    sim::process::apply_fertilizer(cell, find_fertilizer(params, "potassium_sulfate"), 20000.0, ledger);
    sim::process::apply_fertilizer(cell, find_fertilizer(params, "ammonium_sulfate"), 20000.0, ledger);
    for (int hour = 0; hour < 100; ++hour) {
        sim::process::update_cation_exchange(cell, params);
        REQUIRE(std::isfinite(cell.exchange_free_site_index));
        REQUIRE(cell.exchange_free_site_index >= 0.0);
        REQUIRE(cell.exchange_free_site_index <= 1.0);
        REQUIRE(cell.k_solution_g_per_m2 >= 0.0);
        REQUIRE(cell.k_solution_g_per_m2 <= cell.k_g_per_m2);
        REQUIRE(cell.nh4_solution_N_g_per_m2 <= cell.nh4_N_g_per_m2);
        REQUIRE(cell.ca_solution_g_per_m2 <= cell.ca_g_per_m2);
        REQUIRE(cell.mn_solution_g_per_m2 <= cell.mn_g_per_m2);
    }
    // CEC가 거의 없으니 대부분이 용액에 남아요
    CHECK(cell.k_solution_g_per_m2 > 0.9 * cell.k_g_per_m2);
}
