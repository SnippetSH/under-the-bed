// M3 단위 테스트: 인산 고정 (docs/spec/phosphorus.md "테스트 (단위)")
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "sim/processes.hpp"
#include "test_helpers.hpp"

namespace {

// 고정 풀을 비운 셀: 한 틱 동안 새로 고정된 양을 p_fixed로 바로 읽을 수 있어요.
sim::SoilCell cell_with_no_fixed_p(double ph) {
    sim::SoilCell cell = test::test_cell();
    cell.p_fixed_g_per_m2 = 0.0;
    cell.ph = ph;
    return cell;
}

double total_p(const sim::SoilCell& cell) { return cell.p_g_per_m2 + cell.p_fixed_g_per_m2; }

}  // namespace

// pH 6.5 대 5.0 대 8.0, 같은 유효 P → 고정량은 5.0·8.0 쪽이 6.5보다 크다
TEST_CASE("Phosphorus fixes more at pH 5.0 and 8.0 than at pH 6.5", "[unit][phosphorus][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell neutral = cell_with_no_fixed_p(6.5);
    sim::SoilCell acid = cell_with_no_fixed_p(5.0);
    sim::SoilCell alkaline = cell_with_no_fixed_p(8.0);
    REQUIRE(neutral.p_g_per_m2 == acid.p_g_per_m2);
    REQUIRE(neutral.p_g_per_m2 == alkaline.p_g_per_m2);
    sim::process::fix_phosphorus(neutral, params.phosphorus);
    sim::process::fix_phosphorus(acid, params.phosphorus);
    sim::process::fix_phosphorus(alkaline, params.phosphorus);
    CHECK(neutral.p_fixed_g_per_m2 > 0.0);
    CHECK(acid.p_fixed_g_per_m2 > neutral.p_fixed_g_per_m2);
    CHECK(alkaline.p_fixed_g_per_m2 > neutral.p_fixed_g_per_m2);
    // 유효 풀은 반대로 줄어요
    CHECK(acid.p_g_per_m2 < neutral.p_g_per_m2);
    CHECK(alkaline.p_g_per_m2 < neutral.p_g_per_m2);
}

// 한 틱 식: fixed = p × min(1, 속도 × 곡선), released = p_fixed × min(1, 방출 속도), 두 이동량은 틱 시작 값 기준
TEST_CASE("One tick of fixation follows the spec formula from start-of-tick values", "[unit][phosphorus][m3]") {
    const sim::Params params = test::test_params();
    const sim::PhosphorusParams& pp = params.phosphorus;
    sim::SoilCell cell = test::test_cell();
    cell.ph = 5.2;
    cell.p_g_per_m2 = 40.0;
    cell.p_fixed_g_per_m2 = 90.0;
    const double fixed = 40.0 * std::min(1.0, pp.fixation_rate_per_hour * pp.fixation_ph_curve(5.2));
    const double released = 90.0 * std::min(1.0, pp.release_rate_per_hour);
    sim::process::fix_phosphorus(cell, pp);
    CHECK(cell.p_g_per_m2 == Catch::Approx(40.0 - fixed + released));
    CHECK(cell.p_fixed_g_per_m2 == Catch::Approx(90.0 + fixed - released));
}

// 방출은 pH와 무관하다
TEST_CASE("Release from the fixed pool does not depend on pH", "[unit][phosphorus][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell acid = test::test_cell();
    sim::SoilCell alkaline = test::test_cell();
    for (sim::SoilCell* cell : {&acid, &alkaline}) {
        cell->p_g_per_m2 = 0.0;  // 유효 풀이 비면 고정은 0이라 방출만 보여요
        cell->p_fixed_g_per_m2 = 200.0;
    }
    acid.ph = 4.5;
    alkaline.ph = 8.5;
    sim::process::fix_phosphorus(acid, params.phosphorus);
    sim::process::fix_phosphorus(alkaline, params.phosphorus);
    CHECK(acid.p_g_per_m2 > 0.0);
    CHECK(acid.p_g_per_m2 == alkaline.p_g_per_m2);
    CHECK(acid.p_g_per_m2 == Catch::Approx(200.0 * params.phosphorus.release_rate_per_hour));
}

// 고정 0, 유효 0 → 변화 없음, NaN 없음
TEST_CASE("Empty phosphorus pools do not change and stay finite", "[unit][phosphorus][m3][extreme]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.p_g_per_m2 = 0.0;
    cell.p_fixed_g_per_m2 = 0.0;
    for (const double ph : {3.0, 6.5, 10.0}) {
        cell.ph = ph;
        sim::process::fix_phosphorus(cell, params.phosphorus);
        CHECK(cell.p_g_per_m2 == 0.0);
        CHECK(cell.p_fixed_g_per_m2 == 0.0);
        CHECK(std::isfinite(cell.p_g_per_m2));
        CHECK(std::isfinite(cell.p_fixed_g_per_m2));
    }
}

// 평형 비(고정 / 유효 = 6)에서 시작하고 pH 적정 → 유효 풀이 1주에 ±5% 안
TEST_CASE("At the equilibrium ratio and optimal pH the labile pool stays within 5 percent in a week",
          "[unit][phosphorus][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.ph = 6.5;
    cell.p_g_per_m2 = 25.0;
    cell.p_fixed_g_per_m2 = 150.0;  // 25 × 6
    // 평형 비는 명세 식 속도 × 곡선 / 방출 속도예요 (곡선이 적정 pH에서 0.2)
    const double ratio = params.phosphorus.fixation_rate_per_hour * params.phosphorus.fixation_ph_curve(6.5) /
                         params.phosphorus.release_rate_per_hour;
    REQUIRE(ratio == Catch::Approx(6.0));
    for (int hour = 0; hour < 24 * 7; ++hour) {
        sim::process::fix_phosphorus(cell, params.phosphorus);
    }
    CHECK(cell.p_g_per_m2 == Catch::Approx(25.0).epsilon(0.05));
}

// 고정 속도 극단(곡선 × 속도 ≥ 1) → 유효 풀이 0이 되고 음수가 아니다
TEST_CASE("Extreme fixation rate drains the labile pool to exactly zero without going negative",
          "[unit][phosphorus][m3][extreme]") {
    sim::Params params = test::test_params();
    params.phosphorus.fixation_rate_per_hour = 1.0e6;
    params.phosphorus.release_rate_per_hour = 0.0;  // 방출이 있으면 같은 틱에 조금 돌아와요
    sim::SoilCell cell = test::test_cell();
    const double total_before = total_p(cell);
    sim::process::fix_phosphorus(cell, params.phosphorus);
    CHECK(cell.p_g_per_m2 == 0.0);
    CHECK(cell.p_fixed_g_per_m2 >= 0.0);
    CHECK(total_p(cell) == Catch::Approx(total_before).epsilon(1e-12));

    // 방출 속도도 극단이면 두 풀이 서로 바뀌되 음수는 없어요
    params.phosphorus.release_rate_per_hour = 1.0e6;
    sim::SoilCell swapped = test::test_cell();
    const double p_before = swapped.p_g_per_m2;
    const double fixed_before = swapped.p_fixed_g_per_m2;
    sim::process::fix_phosphorus(swapped, params.phosphorus);
    CHECK(swapped.p_g_per_m2 >= 0.0);
    CHECK(swapped.p_fixed_g_per_m2 >= 0.0);
    CHECK(swapped.p_g_per_m2 == Catch::Approx(fixed_before));
    CHECK(swapped.p_fixed_g_per_m2 == Catch::Approx(p_before));
}

// 총량: 유효 + 고정은 흡수·용탈이 없으면 보존된다
TEST_CASE("Labile plus fixed phosphorus is conserved without uptake or leaching", "[unit][phosphorus][conservation][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    const double initial = total_p(cell);
    for (int hour = 0; hour < 24 * 60; ++hour) {
        cell.ph = 4.5 + 4.0 * ((hour / 24) % 9) / 8.0;  // 4.5 ~ 8.5를 오가며
        sim::process::fix_phosphorus(cell, params.phosphorus);
        REQUIRE(cell.p_g_per_m2 >= 0.0);
        REQUIRE(cell.p_fixed_g_per_m2 >= 0.0);
        REQUIRE(total_p(cell) == Catch::Approx(initial).epsilon(1e-12));
    }
}

// 방향: 산성·알칼리 토양에서 유효 풀이 몇 주에 걸쳐 줄고, 적정 pH에서는 거의 그대로 (S12의 셀 단위 버전)
TEST_CASE("Labile phosphorus declines over weeks outside the neutral window", "[unit][phosphorus][m3]") {
    const sim::Params params = test::test_params();
    sim::SoilCell neutral = cell_with_no_fixed_p(6.5);
    sim::SoilCell acid = cell_with_no_fixed_p(5.0);
    sim::SoilCell alkaline = cell_with_no_fixed_p(8.0);
    const double start = neutral.p_g_per_m2;
    for (int hour = 0; hour < 24 * 28; ++hour) {
        sim::process::fix_phosphorus(neutral, params.phosphorus);
        sim::process::fix_phosphorus(acid, params.phosphorus);
        sim::process::fix_phosphorus(alkaline, params.phosphorus);
    }
    CHECK(neutral.p_g_per_m2 > acid.p_g_per_m2);
    CHECK(neutral.p_g_per_m2 > alkaline.p_g_per_m2);
    CHECK(acid.p_g_per_m2 < start);
    CHECK(alkaline.p_g_per_m2 < start);
    CHECK(neutral.p_g_per_m2 < start);  // 적정 pH에서도 고정이 있어서 조금은 줄어요 (고정 0에서 출발)
}
