// M4 단위 테스트: 탈질 (docs/spec/denitrification.md "테스트 (단위)")

#include <algorithm>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sim/processes.hpp"
#include "test_helpers.hpp"

namespace {

using Catch::Approx;
namespace process = sim::process;

struct Loss {
    double lost_N_g_per_m2 = 0.0;
    double acid_H_eq = 0.0;
};

// 통기 지수·이분해성 C·토양 온도를 정해 한 틱 탈질한 결과
Loss denitrify_once(const sim::Params& p, double aeration, double labile_C_g_per_m2, double soil_temp_degC = 25.0) {
    sim::SoilCell cell = test::test_cell();
    cell.aeration_index = aeration;
    cell.labile_C_g_per_m2 = labile_C_g_per_m2;
    cell.soil_temp_degC = soil_temp_degC;
    const double no3_before = cell.no3_N_g_per_m2;
    const sim::SoilCell before = cell;
    sim::Ledger ledger;
    process::denitrify(cell, p.denitrification, p.ions, ledger);
    CHECK(cell.no3_N_g_per_m2 >= 0.0);
    // NO3만 줄고 다른 질소 풀은 그대로, 줄어든 만큼이 기체 손실 장부와 같아요
    CHECK(cell.nh4_N_g_per_m2 == before.nh4_N_g_per_m2);
    CHECK(cell.no2_N_g_per_m2 == before.no2_N_g_per_m2);
    CHECK(cell.labile_N_g_per_m2 == before.labile_N_g_per_m2);
    CHECK(cell.labile_C_g_per_m2 == before.labile_C_g_per_m2);
    CHECK(no3_before - cell.no3_N_g_per_m2 == Approx(ledger.N_gas_g).margin(1e-15));
    return Loss{.lost_N_g_per_m2 = ledger.N_gas_g, .acid_H_eq = cell.acid_H_eq_this_tick};
}

}  // namespace

// 통기 0.8: 손실 0 (통기가 0.5 이상이면 0)
TEST_CASE("Denitrification is zero in well aerated soil", "[unit][m4][denitrification]") {
    const sim::Params p = test::test_params();
    CHECK(denitrify_once(p, 0.8, 100.0).lost_N_g_per_m2 == 0.0);
    CHECK(denitrify_once(p, 0.5, 200.0).lost_N_g_per_m2 == 0.0);
    CHECK(denitrify_once(p, 1.0, 200.0).lost_N_g_per_m2 == 0.0);
    CHECK(denitrify_once(p, 0.8, 100.0).acid_H_eq == 0.0);
}

// 통기 0.1 대 0.3: 0.1 쪽 손실이 큼. 식 그대로: lost = NO3 x 속도 x 산소 곡선 x 온도 곡선 x 탄소 곡선
TEST_CASE("Denitrification grows as aeration falls and follows the formula", "[unit][m4][denitrification]") {
    const sim::Params p = test::test_params();
    const sim::DenitrificationParams& d = p.denitrification;
    CHECK(denitrify_once(p, 0.1, 100.0).lost_N_g_per_m2 > denitrify_once(p, 0.3, 100.0).lost_N_g_per_m2);
    CHECK(denitrify_once(p, 0.0, 100.0).lost_N_g_per_m2 > denitrify_once(p, 0.1, 100.0).lost_N_g_per_m2);
    const double no3 = test::test_cell().no3_N_g_per_m2;
    for (const double aeration : {0.0, 0.1, 0.2, 0.3, 0.4}) {
        for (const double temp : {10.0, 25.0}) {
            for (const double labile_C : {0.0, 50.0, 200.0}) {
                INFO("aeration " << aeration << " temp " << temp << " labile C " << labile_C);
                const double fraction = d.rate_per_hour * d.o2_curve(aeration) * d.temp_curve(temp) *
                                        d.carbon_curve(labile_C);
                CHECK(denitrify_once(p, aeration, labile_C, temp).lost_N_g_per_m2 == Approx(no3 * std::min(1.0, fraction)));
            }
        }
    }
    // 표의 한 점: 통기 0.2, 10 °C, 이분해성 C 50 -> 0.004 x 0.6 x 0.3 x 0.6
    CHECK(denitrify_once(p, 0.2, 50.0, 10.0).lost_N_g_per_m2 == Approx(no3 * 0.004 * 0.6 * 0.3 * 0.6));
}

// 같은 과습, 이분해성 C 0 대 200: 200 쪽 손실이 큼. 온도: 따뜻할수록 큼, 0 °C 이하는 0
TEST_CASE("Denitrification grows with labile carbon and warmth", "[unit][m4][denitrification]") {
    const sim::Params p = test::test_params();
    CHECK(denitrify_once(p, 0.1, 200.0).lost_N_g_per_m2 > denitrify_once(p, 0.1, 0.0).lost_N_g_per_m2);
    CHECK(denitrify_once(p, 0.1, 0.0).lost_N_g_per_m2 > 0.0);  // 탄소 곡선 하한 0.1
    CHECK(denitrify_once(p, 0.1, 100.0, 25.0).lost_N_g_per_m2 > denitrify_once(p, 0.1, 100.0, 10.0).lost_N_g_per_m2);
    CHECK(denitrify_once(p, 0.1, 100.0, 0.0).lost_N_g_per_m2 == 0.0);
    CHECK(denitrify_once(p, 0.1, 100.0, -5.0).lost_N_g_per_m2 == 0.0);
    // 45 °C는 25 °C의 절반
    CHECK(denitrify_once(p, 0.1, 100.0, 45.0).lost_N_g_per_m2 ==
          Approx(0.5 * denitrify_once(p, 0.1, 100.0, 25.0).lost_N_g_per_m2));
}

// 손실 1 eq: pH 단계에 -1 eq(염기)가 들어감 (탈질은 H+를 소비해 pH를 올려요)
TEST_CASE("Denitrification consumes H plus and raises pH", "[unit][m4][denitrification]") {
    const sim::Params p = test::test_params();
    REQUIRE(p.denitrification.H_eq_per_eq_N == -1.0);
    const Loss loss = denitrify_once(p, 0.0, 200.0);
    REQUIRE(loss.lost_N_g_per_m2 > 0.0);
    // 손실 N을 eq로 바꾼 만큼 -1 eq/eq (음수 = 염기)
    CHECK(loss.acid_H_eq == Approx(-loss.lost_N_g_per_m2 / p.ions.no3_N_g_per_eq));
    CHECK(loss.acid_H_eq < 0.0);
    // 1 eq 손실이면 정확히 -1 eq: 14.007 g N을 모두 잃는 셀
    sim::Params huge = p;
    huge.denitrification.rate_per_hour = 100.0;
    sim::SoilCell cell = test::test_cell();
    cell.aeration_index = 0.0;
    cell.soil_temp_degC = 25.0;
    cell.no3_N_g_per_m2 = p.ions.no3_N_g_per_eq;  // 1 eq
    sim::Ledger ledger;
    process::denitrify(cell, huge.denitrification, huge.ions, ledger);
    CHECK(ledger.N_gas_g == Approx(p.ions.no3_N_g_per_eq));
    CHECK(cell.acid_H_eq_this_tick == Approx(-1.0));
    // pH 단계(g)가 이 -1 eq를 소비해 pH가 올라가요
    sim::SoilCell neutral = test::test_cell();
    neutral.acid_H_eq_this_tick = -0.5;
    const double ph_before = neutral.ph;
    sim::Ledger ph_ledger;
    process::update_lime_and_ph(neutral, p, ph_ledger);
    CHECK(neutral.ph > ph_before);
    CHECK(neutral.acid_H_eq_this_tick == 0.0);
    CHECK(ph_ledger.H_eq_net == Approx(-0.5));
}

// 속도 x 곡선 >= 1: NO3가 0이 되고 음수가 아님, 장부와 일치. NO3가 없으면 아무 일도 없음
TEST_CASE("Denitrification is capped by the nitrate pool and matches the ledger", "[unit][m4][denitrification][extreme]") {
    const sim::Params p = test::test_params();
    sim::Params huge = p;
    huge.denitrification.rate_per_hour = 100.0;
    sim::SoilCell cell = test::test_cell();
    cell.aeration_index = 0.0;
    cell.soil_temp_degC = 25.0;
    const double no3_before = cell.no3_N_g_per_m2;
    sim::Ledger ledger;
    process::denitrify(cell, huge.denitrification, huge.ions, ledger);
    CHECK(cell.no3_N_g_per_m2 == 0.0);
    CHECK(ledger.N_gas_g == Approx(no3_before));
    // 한 번 더: 남은 NO3가 없으니 변화 없음, 장부 그대로
    const double gas_after_first = ledger.N_gas_g;
    const double acid_after_first = cell.acid_H_eq_this_tick;
    process::denitrify(cell, huge.denitrification, huge.ions, ledger);
    CHECK(cell.no3_N_g_per_m2 == 0.0);
    CHECK(ledger.N_gas_g == gas_after_first);
    CHECK(cell.acid_H_eq_this_tick == acid_after_first);
    // 극단 입력(NO3 1e9, 음수 통기·고온)에서도 유한하고 음수 없음
    sim::SoilCell extreme = test::test_cell();
    extreme.no3_N_g_per_m2 = 1.0e9;
    extreme.aeration_index = 0.0;
    extreme.soil_temp_degC = 90.0;
    extreme.labile_C_g_per_m2 = 1.0e6;
    sim::Ledger extreme_ledger;
    process::denitrify(extreme, p.denitrification, p.ions, extreme_ledger);
    CHECK(std::isfinite(extreme.no3_N_g_per_m2));
    CHECK(extreme.no3_N_g_per_m2 >= 0.0);
    CHECK(std::isfinite(extreme_ledger.N_gas_g));
    CHECK(extreme_ledger.N_gas_g >= 0.0);
}

// 장부는 누적: 연속 틱의 손실을 더해 가고, 질소는 NO3 + 기체 손실로 보존돼요
TEST_CASE("Denitrification accumulates in the ledger and conserves nitrogen", "[unit][m4][denitrification][conservation]") {
    const sim::Params p = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.aeration_index = 0.05;
    cell.soil_temp_degC = 25.0;
    const double no3_before = cell.no3_N_g_per_m2;
    sim::Ledger ledger;
    double previous = 0.0;
    for (int t = 0; t < 72; ++t) {
        process::denitrify(cell, p.denitrification, p.ions, ledger);
        CHECK(ledger.N_gas_g >= previous);
        previous = ledger.N_gas_g;
        CHECK(cell.no3_N_g_per_m2 + ledger.N_gas_g == Approx(no3_before).epsilon(1e-12));
    }
    CHECK(ledger.N_gas_g > 0.0);
    CHECK(cell.no3_N_g_per_m2 < no3_before);
}
