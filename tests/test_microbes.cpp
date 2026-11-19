// docs/spec/soil-microbes.md "테스트 (단위)" (M5a): 개체군 갱신(d2), 활동 계수, 균근균 완화·수용력, 유익균 투입.
// 감염(3번)은 test_soil_disease.cpp, 소독(사멸·무기화)은 test_disinfection.cpp에 있어요.
#include <array>
#include <cmath>
#include <cstddef>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "test_helpers.hpp"

namespace {

using Catch::Approx;
namespace process = sim::process;
using G = sim::MicrobeGroup;

constexpr std::size_t idx(G g) { return sim::microbe(g); }

// 적합도 1(모든 곡선 항등), 분해균 수용력 1인 파라미터. 경쟁 없음.
sim::Params flat_params() {
    sim::Params p = test::test_params();
    const sim::PiecewiseLinear one = {{{{0, 1}}, {{100, 1}}}};
    for (sim::MicrobeGroupParams& g : p.microbes.groups) {
        g.temp_curve = one;
        g.moisture_curve = one;
        g.ph_curve = one;
        g.o2_curve = one;
        g.competition = {};
    }
    p.microbes.decomposer_food_curve = one;
    return p;
}

// 셀 하나를 hours시간 d2만 돌려요 (같은 셀 환경)
sim::SoilCell run_microbes(sim::SoilCell cell, const sim::Params& p, int hours, process::MicrobeHost host = {}) {
    for (int h = 0; h < hours; ++h) {
        process::update_microbes(cell, p, host);
    }
    return cell;
}

bool indices_valid(const sim::MicrobeIndices& v) {
    for (const double x : v) {
        if (!std::isfinite(x) || x < 0.0 || x > 1.0) {
            return false;
        }
    }
    return true;
}

}  // namespace

// 지수 1, 적합도 1, 수용력 1: 거의 변화 없음 (자연 사멸과 성장 균형)
TEST_CASE("Microbe population at capacity stays near one", "[unit][m5a][microbes]") {
    const sim::Params p = flat_params();
    sim::SoilCell cell = test::test_cell();
    cell.microbe_index[idx(G::Decomposer)] = 1.0;
    const sim::SoilCell after = run_microbes(cell, p, 24 * 30);
    CHECK(after.microbe_index[idx(G::Decomposer)] > 0.98);
    CHECK(after.microbe_index[idx(G::Decomposer)] <= 1.0);
}

// 지수 0.05: S자로 늘어 1 근처에 수렴, 넘지 않음
TEST_CASE("Microbe population grows logistically toward capacity", "[unit][m5a][microbes]") {
    const sim::Params p = flat_params();
    sim::SoilCell cell = test::test_cell();
    cell.microbe_index[idx(G::Decomposer)] = 0.05;
    double previous = 0.05;
    double max_step = 0.0;
    int max_step_hour = 0;
    for (int h = 0; h < 24 * 20; ++h) {
        process::update_microbes(cell, p, {});
        const double now = cell.microbe_index[idx(G::Decomposer)];
        CHECK(now >= previous - 1e-12);  // 수용력 아래에서는 단조 증가
        CHECK(now <= 1.0);
        if (now - previous > max_step) {
            max_step = now - previous;
            max_step_hour = h;
        }
        previous = now;
    }
    CHECK(previous > 0.97);
    // S자: 증가 속도가 처음이 아니라 중간에서 가장 커요
    CHECK(max_step_hour > 0);
}

// 수용력이 0.3으로 떨어지면 0.3 쪽으로 감소
TEST_CASE("Microbe population declines toward a lower capacity", "[unit][m5a][microbes]") {
    sim::Params p = flat_params();
    p.microbes.decomposer_food_curve = {{{{0, 0.3}}, {{100, 0.3}}}};
    sim::SoilCell cell = test::test_cell();
    cell.microbe_index[idx(G::Decomposer)] = 1.0;
    const sim::SoilCell after = run_microbes(cell, p, 24 * 30);
    const double v = after.microbe_index[idx(G::Decomposer)];
    CHECK(v < 0.35);
    CHECK(v > 0.25);
}

// 병원균 0, 유입 0: 계속 0 (숙주가 있어도)
TEST_CASE("Pathogen at zero without immigration stays zero", "[unit][m5a][microbes]") {
    const sim::Params p = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.microbe_index[idx(G::Fusarium)] = 0.0;
    cell.microbe_index[idx(G::Phytophthora)] = 0.0;
    cell.soil_temp_degC = 28.0;
    const process::MicrobeHost host{.root_g_per_m2 = 12.0, .infection_index = {0.5, 0.5}};
    const sim::SoilCell after = run_microbes(cell, p, 24 * 60, host);
    CHECK(after.microbe_index[idx(G::Fusarium)] == 0.0);
    CHECK(after.microbe_index[idx(G::Phytophthora)] == 0.0);
}

// 길항균 1 대 0, 같은 병원균: 길항균 1 쪽 병원균 평형이 낮다
TEST_CASE("Antagonist lowers the pathogen equilibrium", "[unit][m5a][microbes]") {
    const sim::Params p = test::test_params();
    sim::SoilCell base = test::test_cell();
    base.soil_temp_degC = 28.0;
    base.ph = 6.0;
    base.labile_C_g_per_m2 = 60.0;  // 길항균 수용력 1
    base.microbe_index[idx(G::Fusarium)] = 0.3;
    sim::SoilCell with = base;
    with.microbe_index[idx(G::Antagonist)] = 1.0;
    sim::SoilCell without = base;
    without.microbe_index[idx(G::Antagonist)] = 0.0;  // 유입(0.0002/h)으로만 아주 천천히 늘어요
    const sim::SoilCell a = run_microbes(with, p, 24 * 60);
    const sim::SoilCell b = run_microbes(without, p, 24 * 60);
    CHECK(a.microbe_index[idx(G::Fusarium)] < b.microbe_index[idx(G::Fusarium)]);
}

// 분해균 1 대 0.2: 0.2 쪽 분해 C가 activity 비율만큼 작다
TEST_CASE("Decomposer index scales decomposition by the activity curve", "[unit][m5a][microbes]") {
    const sim::Params p = test::test_params();
    sim::SoilCell high = test::test_cell();
    high.microbe_index[idx(G::Decomposer)] = 1.0;
    sim::SoilCell low = high;
    low.microbe_index[idx(G::Decomposer)] = 0.2;
    const double f_high = process::decomposer_activity_factor(high, p.microbes);
    const double f_low = process::decomposer_activity_factor(low, p.microbes);
    CHECK(f_high == 1.0);
    CHECK(f_low == Approx(p.microbes.decomposer_activity_curve(0.2)));

    sim::Ledger lh;
    sim::Ledger ll;
    const double labile_before = high.labile_C_g_per_m2;
    process::decompose_organic_matter(high, p.organic_matter, p.soil_temperature, lh, f_high);
    process::decompose_organic_matter(low, p.organic_matter, p.soil_temperature, ll, f_low);
    const double d_high = labile_before - high.labile_C_g_per_m2;
    const double d_low = labile_before - low.labile_C_g_per_m2;
    CHECK(d_low == Approx(d_high * f_low / f_high).epsilon(1e-9));

    // 지수 1이면 M4와 같음 (기본 인자 1과 비트 단위로 같음)
    sim::SoilCell plain = test::test_cell();
    sim::Ledger lp;
    process::decompose_organic_matter(plain, p.organic_matter, p.soil_temperature, lp);
    CHECK(plain.labile_C_g_per_m2 == high.labile_C_g_per_m2);
}

// 2단계균 0.1, 1단계균 1: NO2- 축적
TEST_CASE("Low NOB with healthy AOB accumulates nitrite", "[unit][m5a][microbes]") {
    const sim::Params p = test::test_params();
    sim::SoilCell healthy = test::test_cell();
    healthy.nh4_N_g_per_m2 = 20.0;
    test::settle(healthy, p);
    healthy.microbe_index[idx(G::NitrifierAob)] = 1.0;
    healthy.microbe_index[idx(G::NitrifierNob)] = 1.0;
    sim::SoilCell broken = healthy;
    broken.microbe_index[idx(G::NitrifierNob)] = 0.1;
    for (int h = 0; h < 24 * 3; ++h) {
        process::nitrify(healthy, p.nitrification, p.soil_chemistry, p.ions,
                         process::aob_activity_factor(healthy, p.microbes),
                         process::nob_activity_factor(healthy, p.microbes));
        process::nitrify(broken, p.nitrification, p.soil_chemistry, p.ions,
                         process::aob_activity_factor(broken, p.microbes),
                         process::nob_activity_factor(broken, p.microbes));
    }
    CHECK(broken.no2_N_g_per_m2 > healthy.no2_N_g_per_m2 * 2.0);
    // 1단계 속도는 같아서 NH4+ 감소는 같아요
    CHECK(broken.nh4_N_g_per_m2 == Approx(healthy.nh4_N_g_per_m2));
}

// 균근균 0 대 1, 같은 수분 결손: 1 쪽 수분 계수가 큼
TEST_CASE("Mycorrhiza relieves water and salt stress factors", "[unit][m5a][microbes]") {
    const sim::Params p = test::test_params();
    sim::SoilCell dry = test::test_cell();
    dry.water_mm = dry.field_capacity_mm * 0.4;
    dry.ec_dS_per_m = 4.0;
    const process::WaterSaltFactors raw = process::water_salt_factors(dry, p.crop);
    REQUIRE(raw.water < 1.0);
    REQUIRE(raw.salt < 1.0);
    sim::SoilCell none = dry;
    none.microbe_index[idx(G::Mycorrhiza)] = 0.0;
    sim::SoilCell full = dry;
    full.microbe_index[idx(G::Mycorrhiza)] = 1.0;
    const process::WaterSaltFactors r0 = process::mycorrhiza_relieved(raw, none, p.microbes);
    const process::WaterSaltFactors r1 = process::mycorrhiza_relieved(raw, full, p.microbes);
    CHECK(r0.water == raw.water);
    CHECK(r0.salt == raw.salt);
    CHECK(r1.water == Approx(raw.water + (1.0 - raw.water) * 0.3));
    CHECK(r1.salt == Approx(raw.salt + (1.0 - raw.salt) * 0.3));

    // 작물 갱신(h2 스트레스)에서도: 균근균 1 쪽 총생산이 큼
    const sim::Crop crop0 = process::plant_crop(test::test_crop_init(), p.crop);
    sim::Crop c_none = crop0;
    sim::Crop c_full = crop0;
    sim::Ledger l_none;
    sim::Ledger l_full;
    sim::Pcg32 rng_a = test::biology_rng(1);
    sim::Pcg32 rng_b = test::biology_rng(1);
    (void)process::update_crop(c_none, none, p, process::available_nutrients(none, p), test::crop_env(500.0, 20.0),
                               rng_a, l_none);
    (void)process::update_crop(c_full, full, p, process::available_nutrients(full, p), test::crop_env(500.0, 20.0),
                               rng_b, l_full);
    CHECK(c_full.last_gross_g_per_m2 > c_none.last_gross_g_per_m2);
}

// 유효 P 100 g/m²: 균근균 수용력 ≤ 0.1. 뿌리가 없으면 숙주 곡선 하한
TEST_CASE("High available phosphorus caps mycorrhiza capacity", "[unit][m5a][microbes]") {
    const sim::Params p = test::test_params();
    sim::SoilCell cell = test::test_cell();
    const process::MicrobeHost roots{.root_g_per_m2 = 20.0};
    cell.p_g_per_m2 = 100.0;
    CHECK(process::microbe_capacity(cell, p.microbes, roots)[idx(G::Mycorrhiza)] <= 0.1);
    cell.p_g_per_m2 = 25.0;
    CHECK(process::microbe_capacity(cell, p.microbes, roots)[idx(G::Mycorrhiza)] == Approx(0.6));
    CHECK(process::microbe_capacity(cell, p.microbes, {})[idx(G::Mycorrhiza)] == Approx(0.05 * 0.6));
    // 균근균 가용화 배율: 1 + 2 × 지수
    cell.microbe_index[idx(G::Mycorrhiza)] = 0.5;
    CHECK(process::mycorrhiza_p_release_factor(cell, p.microbes) == Approx(2.0));
    sim::SoilCell slow = cell;
    sim::SoilCell fast = cell;
    process::fix_phosphorus(slow, p.phosphorus);
    process::fix_phosphorus(fast, p.phosphorus, process::mycorrhiza_p_release_factor(cell, p.microbes));
    CHECK(fast.p_g_per_m2 > slow.p_g_per_m2);
    CHECK(fast.p_g_per_m2 + fast.p_fixed_g_per_m2 == Approx(slow.p_g_per_m2 + slow.p_fixed_g_per_m2));
}

// 병원균 수용력 = 부생 + 숙주 × 감염 (숙주 되먹임)
TEST_CASE("Pathogen capacity grows with crop infection", "[unit][m5a][microbes]") {
    const sim::Params p = test::test_params();
    const sim::SoilCell cell = test::test_cell();
    const sim::MicrobeIndices k0 = process::microbe_capacity(cell, p.microbes, {});
    const sim::MicrobeIndices k1 =
        process::microbe_capacity(cell, p.microbes, {.root_g_per_m2 = 12.0, .infection_index = {0.5, 1.0}});
    CHECK(k0[idx(G::Fusarium)] == Approx(0.3));
    CHECK(k0[idx(G::Phytophthora)] == Approx(0.2));
    CHECK(k1[idx(G::Fusarium)] == Approx(0.3 + 0.7 * 0.5));
    CHECK(k1[idx(G::Phytophthora)] == Approx(1.0));
}

// 무리 순서를 바꿔 계산해도 같은 결과 (단계 시작 값으로 계산)
TEST_CASE("Microbe update does not depend on group order", "[unit][m5a][microbes]") {
    const sim::Params p = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.soil_temp_degC = 41.0;
    cell.microbe_index = {0.7, 0.6, 0.4, 0.8, 0.3, 0.5, 0.45};
    cell.fumigant_residue_index = {0.5, 0.2};
    const process::MicrobeHost host{.root_g_per_m2 = 8.0, .infection_index = {0.2, 0.1}};

    sim::SoilCell forward = cell;
    process::update_microbes(forward, p, host);

    // 역순으로 하나씩 계산해 바로 써 넣어도(시작 값은 따로 보관) 같아야 해요
    const sim::MicrobeIndices start = cell.microbe_index;
    const sim::MicrobeIndices suit = process::microbe_suitability(cell, p.microbes);
    const sim::MicrobeIndices cap = process::microbe_capacity(cell, p.microbes, host);
    const sim::MicrobeIndices fumi = process::fumigant_kill_per_hour(cell, p.disinfectants);
    sim::SoilCell reversed = cell;
    for (std::size_t k = sim::kMicrobeGroupCount; k-- > 0;) {
        const process::MicrobeGroupStep s = process::microbe_group_step(k, start, suit, cap, fumi, cell, p.microbes);
        reversed.microbe_index[k] = s.next_index;
        reversed.disinfection_kill_index[k] = s.disinfection_kill_index;
    }
    CHECK(reversed.microbe_index == forward.microbe_index);
    CHECK(reversed.disinfection_kill_index == forward.disinfection_kill_index);
}

// 극한: 지온 70 °C, 수분 0, pH 3에서 지수 0~1, NaN 없음
TEST_CASE("Microbe update stays bounded under extreme soil", "[unit][m5a][microbes][extreme]") {
    const sim::Params p = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.soil_temp_degC = 70.0;
    cell.water_mm = 0.0;
    cell.ph = 3.0;
    cell.aeration_index = 0.0;
    cell.labile_C_g_per_m2 = 0.0;
    cell.microbe_index = {1, 1, 1, 1, 1, 1, 1};
    cell.fumigant_residue_index = {100.0, 100.0};
    for (int h = 0; h < 24 * 10; ++h) {
        process::update_microbes(cell, p, {.root_g_per_m2 = 1e6, .infection_index = {1.0, 1.0}});
        REQUIRE(indices_valid(cell.microbe_index));
        REQUIRE(indices_valid(cell.disinfection_kill_index));
    }
}

// 유익균 투입: P += (1 − P) × min(1, g × gain), 담체 C·N은 이분해성 풀과 장부로
TEST_CASE("Inoculant raises the target group and books carrier C and N", "[unit][m5a][microbes][conservation]") {
    const sim::Params p = test::test_params();
    const sim::InoculantDef& tricho = p.inoculants[1];
    sim::SoilCell empty = test::test_cell();
    empty.microbe_index[idx(G::Antagonist)] = 0.0;
    sim::SoilCell high = test::test_cell();
    high.microbe_index[idx(G::Antagonist)] = 0.9;
    sim::Ledger le;
    sim::Ledger lh;
    const double C_before = empty.organic_C_g_per_m2();
    const double N_before = empty.total_N_g_per_m2();
    process::apply_inoculant(empty, tricho, 20.0, le);
    process::apply_inoculant(high, tricho, 20.0, lh);
    CHECK(empty.microbe_index[idx(G::Antagonist)] == Approx(0.5));  // 20 × 0.025
    CHECK(high.microbe_index[idx(G::Antagonist)] == Approx(0.95));
    // 빈 흙이 더 많이 늘어요
    CHECK(empty.microbe_index[idx(G::Antagonist)] > high.microbe_index[idx(G::Antagonist)] - 0.9);
    // 담체: 20 g = 0.02 kg × 300 g C/kg = 6 g C, × 10 g N/kg = 0.2 g N
    CHECK(le.C_in_g == Approx(6.0));
    CHECK(le.N_in_g == Approx(0.2));
    CHECK(le.inoculant_applied_g == 20.0);
    CHECK(empty.organic_C_g_per_m2() == Approx(C_before + le.C_in_g).epsilon(1e-12));
    CHECK(empty.total_N_g_per_m2() == Approx(N_before + le.N_in_g).epsilon(1e-12));
    // 대상이 아닌 무리는 그대로
    CHECK(empty.microbe_index[idx(G::Mycorrhiza)] == test::test_cell().microbe_index[idx(G::Mycorrhiza)]);
}

// 월드: 시나리오 [microbes] 시작값이 모든 셀에. 기존 시나리오(유익균 1, 병원균 0, 지온 40 °C 미만)에서
// 질산화균·분해균이 1 근처에 머물러요
TEST_CASE("World microbes start from the scenario and stay near one in a plain scenario", "[unit][m5a][microbes]") {
    sim::World world = test::make_world();
    for (const sim::SoilCell& c : world.snapshot().soil) {
        CHECK(c.microbe_index == test::test_soil().microbe_index);
        CHECK(c.fumigant_residue_index.size() == world.params().disinfectants.size());
        CHECK(c.mulch_def_index == sim::kNoMulch);
    }
    world.step(24 * 30);
    for (const sim::SoilCell& c : world.snapshot().soil) {
        CHECK(c.microbe_index[idx(G::NitrifierAob)] > 0.9);
        CHECK(c.microbe_index[idx(G::NitrifierNob)] > 0.9);
        CHECK(c.microbe_index[idx(G::Decomposer)] > 0.9);
        CHECK(c.microbe_index[idx(G::Fusarium)] == 0.0);
        CHECK(indices_valid(c.microbe_index));
    }
}

// AddInoculant 명령: 모르는 id·음수 거부
TEST_CASE("AddInoculant command validates its id and amount", "[unit][m5a][microbes]") {
    sim::World world = test::make_world();
    const sim::TileArea all{.row = 0, .col = 0, .rows = 2, .cols = 3};
    CHECK_FALSE(world.enqueue(sim::AddInoculantCommand{.area = all, .inoculant_id = "nope", .g_per_m2 = 1.0}));
    CHECK_FALSE(
        world.enqueue(sim::AddInoculantCommand{.area = all, .inoculant_id = "bacillus_product", .g_per_m2 = -1.0}));
    CHECK(world.enqueue(sim::AddInoculantCommand{.area = all, .inoculant_id = "bacillus_product", .g_per_m2 = 10.0}));
    world.step(1);
    CHECK(world.snapshot().ledger.inoculant_applied_g == Approx(60.0));
}
