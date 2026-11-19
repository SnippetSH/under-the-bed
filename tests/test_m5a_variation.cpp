// M5a 프로토타입 3차 (2026-11-19 디렉터 결정) 단위 테스트:
//   A. 깊은 흙 온도 항 (soil-temperature): 완화 목표 = (1 − f) × (하우스 기온 + 멀칭 이득) + f × 깊은 흙 온도,
//      근권 갱신 뒤 깊은 흙 온도가 근권 온도를 deep_relax_rate_per_hour로 따라감. f = 0이면 이전 식 그대로.
//   B. 셀 간 차이 (soil-microbes 1·3번): 병원균 시작 지수 × (1 + s × (2U − 1)) (월드 생성 때만),
//      포기 감수성 1 + s × (2U − 1) (정식 때)이 감염 증가분에 곱해짐. 차이가 0이면 난수를 뽑지 않음.
// biology 난수 순서: 타일 행 우선, 타일마다 화아분화 → 감수성 → 병원균(MicrobeGroup 순서).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "test_helpers.hpp"

namespace {

using Catch::Approx;
namespace process = sim::process;
using G = sim::MicrobeGroup;
using D = sim::SoilDisease;

constexpr std::size_t idx(G g) { return sim::microbe(g); }
constexpr std::size_t dis(D d) { return static_cast<std::size_t>(d); }

sim::World create(const sim::Params& p, const sim::Scenario& s) {
    auto world = sim::World::create(p, s);
    REQUIRE(world.has_value());
    return std::move(*world);
}

// 깊은 흙 온도를 0으로 지운 셀 목록 (깊은 흙 외의 상태 비교용)
std::vector<sim::SoilCell> without_deep(std::vector<sim::SoilCell> soil) {
    for (sim::SoilCell& c : soil) {
        c.deep_soil_temp_degC = 0.0;
    }
    return soil;
}

double mean_index(const sim::Snapshot& snap, G g) {
    double sum = 0.0;
    for (const sim::SoilCell& c : snap.soil) {
        sum += c.microbe_index[idx(g)];
    }
    return sum / static_cast<double>(snap.soil.size());
}

}  // namespace

// ---- A. 깊은 흙 온도 ----

// 결합 비율 0이면 깊은 흙 온도가 무엇이든(움직여도) 근권 온도는 이전 식과 비트 단위로 같아요
TEST_CASE("Deep coupling zero reproduces the old soil temperature exactly", "[unit][m5a][temperature][deep]") {
    sim::Params p = test::test_params();
    p.soil_temperature.evap_cooling_degC_per_mm = 2.5;
    p.soil_temperature.deep_coupling_fraction = 0.0;
    p.soil_temperature.deep_relax_rate_per_hour = 0.01;
    sim::SoilCell cell = test::test_cell();
    cell.soil_temp_degC = 12.0;
    cell.deep_soil_temp_degC = -40.0;  // 극단값이어도 영향 없음
    double old_temp = 12.0;
    for (int h = 0; h < 24 * 5; ++h) {
        const double air = 20.0 + 8.0 * std::sin(2.0 * std::numbers::pi * h / 24.0);
        const double evaporated = (h % 24 >= 8 && h % 24 < 16) ? 0.1 : 0.0;
        process::relax_soil_temperature(cell, p.soil_temperature, air, evaporated);
        old_temp += (air - old_temp) * std::min(1.0, p.soil_temperature.relax_rate_per_hour);
        old_temp -= std::max(0.0, evaporated) * p.soil_temperature.evap_cooling_degC_per_mm;
        REQUIRE(cell.soil_temp_degC == old_temp);
    }
    CHECK(cell.deep_soil_temp_degC > -40.0);  // 깊은 흙은 따라 움직였어요
}

// 월드 단위: 결합 0이면 깊은 흙 속도가 달라도 깊은 흙 외의 상태가 모두 같아요
TEST_CASE("Deep coupling zero leaves every other world state unchanged", "[unit][m5a][temperature][deep]") {
    sim::Scenario s = test::test_scenario(200);
    s.crop = test::test_crop_init();
    sim::Params moving = test::test_params();
    moving.soil_temperature.deep_relax_rate_per_hour = 0.01;
    sim::World a = create(test::test_params(), s);
    sim::World b = create(moving, s);
    a.step(24 * 3);
    b.step(24 * 3);
    const sim::Snapshot sa = a.snapshot();
    const sim::Snapshot sb = b.snapshot();
    CHECK(without_deep(sa.soil) == without_deep(sb.soil));
    CHECK(sa.crops == sb.crops);
    CHECK(sa.ledger == sb.ledger);
    CHECK(sa.soil[0].deep_soil_temp_degC == s.soil.soil_temp_degC);  // 속도 0: 그대로
    CHECK(sb.soil[0].deep_soil_temp_degC != s.soil.soil_temp_degC);
}

// 목표 식과 깊은 흙 갱신이 정확히 명세대로예요 (근권: 완화 → 증발 냉각, 그다음 깊은 흙)
TEST_CASE("Deep soil temperature target blend and update follow the formula", "[unit][m5a][temperature][deep]") {
    sim::Params p = test::test_params();
    p.soil_temperature.deep_coupling_fraction = 0.4;
    p.soil_temperature.deep_relax_rate_per_hour = 0.004;
    p.soil_temperature.evap_cooling_degC_per_mm = 2.5;
    sim::SoilCell cell = test::test_cell();
    cell.soil_temp_degC = 20.0;
    cell.deep_soil_temp_degC = 10.0;
    CHECK(process::soil_relax_blend_target_degC(cell, p.soil_temperature, 30.0) == Approx(0.6 * 30.0 + 0.4 * 10.0));
    process::relax_soil_temperature(cell, p.soil_temperature, 30.0, 0.2);
    const double expected_soil = 20.0 + (22.0 - 20.0) * 0.04 - 0.2 * 2.5;
    CHECK(cell.soil_temp_degC == Approx(expected_soil).epsilon(1e-12));
    CHECK(cell.deep_soil_temp_degC == Approx(10.0 + (expected_soil - 10.0) * 0.004).epsilon(1e-12));
}

// 깊은 흙은 근권을 느리게 따라가고(지연), 결합이 있으면 근권 일교차가 줄어요
TEST_CASE("Deep soil temperature lags and damps the daily swing", "[unit][m5a][temperature][deep]") {
    const auto run = [](double coupling) {
        sim::Params p = test::test_params();
        p.soil_temperature.deep_coupling_fraction = coupling;
        p.soil_temperature.deep_relax_rate_per_hour = 0.004;
        sim::SoilCell cell = test::test_cell();
        cell.soil_temp_degC = 10.0;
        cell.deep_soil_temp_degC = 10.0;
        double deep_after_day1 = 0.0;
        double lo = 1e9;
        double hi = -1e9;
        double deep_lo = 1e9;
        double deep_hi = -1e9;
        const int days = 60;
        for (int h = 0; h < 24 * days; ++h) {
            const double air = 20.0 + 8.0 * std::sin(2.0 * std::numbers::pi * h / 24.0);
            process::relax_soil_temperature(cell, p.soil_temperature, air);
            if (h == 23) {
                deep_after_day1 = cell.deep_soil_temp_degC;
            }
            if (h >= 24 * (days - 1)) {
                lo = std::min(lo, cell.soil_temp_degC);
                hi = std::max(hi, cell.soil_temp_degC);
                deep_lo = std::min(deep_lo, cell.deep_soil_temp_degC);
                deep_hi = std::max(deep_hi, cell.deep_soil_temp_degC);
            }
        }
        struct Result {
            double deep_after_day1, soil_swing, deep_swing, deep_end;
        };
        return Result{deep_after_day1, hi - lo, deep_hi - deep_lo, cell.deep_soil_temp_degC};
    };
    const auto coupled = run(0.4);
    const auto plain = run(0.0);
    // 하루 뒤 깊은 흙은 아주 조금만 올라요 (근권은 이미 하우스 쪽으로 이동)
    CHECK(coupled.deep_after_day1 > 10.0);
    CHECK(coupled.deep_after_day1 < 11.0);
    // 60일 뒤 하우스 평균(20 °C) 가까이
    CHECK(coupled.deep_end == Approx(20.0).margin(1.0));
    // 깊은 흙의 일교차는 근권보다 훨씬 작고, 결합이 있으면 근권 일교차가 줄어요
    CHECK(coupled.deep_swing < 0.1 * coupled.soil_swing);
    CHECK(coupled.soil_swing < plain.soil_swing);
}

// 월드 생성 때 깊은 흙 온도 = 시나리오 지온
TEST_CASE("World initialises deep soil temperature from the scenario", "[unit][m5a][temperature][deep][world]") {
    sim::Scenario s = test::test_scenario();
    s.soil.soil_temp_degC = 17.5;
    sim::Params p = test::test_params();
    p.soil_temperature.deep_coupling_fraction = 0.4;
    p.soil_temperature.deep_relax_rate_per_hour = 0.004;
    const sim::World world = create(p, s);
    for (const sim::SoilCell& c : world.snapshot().soil) {
        CHECK(c.deep_soil_temp_degC == 17.5);
        CHECK(c.soil_temp_degC == 17.5);
    }
}

// ---- B. 병원균 시작 지수 차이 ----

TEST_CASE("Pathogen spread varies start indices across cells around the start value",
          "[unit][m5a][microbes][variation]") {
    sim::Params p = test::test_params();
    p.microbes.pathogen_spread_index = 0.5;
    sim::Scenario s = test::test_scenario(1, 10, 10);
    s.soil.microbe_index = {1.0, 1.0, 1.0, 0.5, 0.1, 0.4, 0.0};
    const sim::World world = create(p, s);
    const sim::Snapshot snap = world.snapshot();
    double lo = 1.0;
    double hi = 0.0;
    for (const sim::SoilCell& c : snap.soil) {
        const double f = c.microbe_index[idx(G::Fusarium)];
        CHECK(f >= 0.4 * 0.5);
        CHECK(f <= 0.4 * 1.5);
        lo = std::min(lo, f);
        hi = std::max(hi, f);
        // 시작 0인 병원균은 0 그대로, 병원균이 아닌 무리는 시작값 그대로
        CHECK(c.microbe_index[idx(G::Phytophthora)] == 0.0);
        CHECK(c.microbe_index[idx(G::Decomposer)] == 1.0);
        CHECK(c.microbe_index[idx(G::Antagonist)] == 0.5);
        CHECK(c.microbe_index[idx(G::Mycorrhiza)] == 0.1);
    }
    CHECK(hi - lo > 0.1);
    CHECK(mean_index(snap, G::Fusarium) == Approx(0.4).margin(0.05));
}

// 높은 시작값 + 큰 차이: [0, 1]로 잘려요
TEST_CASE("Pathogen spread is clamped to the unit range", "[unit][m5a][microbes][variation]") {
    sim::Params p = test::test_params();
    p.microbes.pathogen_spread_index = 1.0;
    sim::Scenario s = test::test_scenario(1, 10, 10);
    s.soil.microbe_index = {1.0, 1.0, 1.0, 0.5, 0.1, 0.9, 0.9};
    const sim::Snapshot snap = create(p, s).snapshot();
    bool clamped = false;
    for (const sim::SoilCell& c : snap.soil) {
        for (const G g : {G::Fusarium, G::Phytophthora}) {
            const double v = c.microbe_index[idx(g)];
            CHECK(v >= 0.0);
            CHECK(v <= 1.0);
            clamped = clamped || v == 1.0;
        }
    }
    CHECK(clamped);
}

// 같은 시드면 같은 배치, 다른 시드면 다른 배치
TEST_CASE("Pathogen spread layout is reproducible from the seed", "[unit][m5a][microbes][variation][determinism]") {
    sim::Params p = test::test_params();
    p.microbes.pathogen_spread_index = 0.5;
    sim::Scenario s = test::test_scenario(1, 4, 5);
    s.soil.microbe_index = {1.0, 1.0, 1.0, 0.5, 0.1, 0.4, 0.3};
    const sim::Snapshot a = create(p, s).snapshot();
    const sim::Snapshot b = create(p, s).snapshot();
    CHECK(a.soil == b.soil);
    s.seed = 999;
    const sim::Snapshot c = create(p, s).snapshot();
    CHECK(a.soil != c.soil);
}

// ---- B. 포기 감수성 ----

// 감수성은 두 병해의 감염 증가분에 정확히 곱해져요
TEST_CASE("Susceptibility factor scales the infection increment exactly", "[unit][m5a][soil_disease][variation]") {
    const sim::Params p = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.soil_temp_degC = 22.0;
    cell.aeration_index = 0.1;
    cell.microbe_index[idx(G::Fusarium)] = 0.5;
    cell.microbe_index[idx(G::Phytophthora)] = 0.5;
    sim::Crop base = process::plant_crop(test::test_crop_init(), p.crop);
    REQUIRE(base.disease_susceptibility_factor == 1.0);
    sim::Crop weak = base;
    weak.disease_susceptibility_factor = 1.3;
    (void)process::update_soil_infection(base, cell, p, 0);
    (void)process::update_soil_infection(weak, cell, p, 0);
    for (const D d : {D::Fusarium, D::Phytophthora}) {
        const double pressure = process::infection_pressure(d, cell, p);
        const double rate = p.soil_disease[dis(d)].infection_rate_per_hour;
        REQUIRE(pressure > 0.0);
        CHECK(base.infection_index[dis(d)] == rate * pressure);
        CHECK(weak.infection_index[dis(d)] == rate * pressure * 1.3);
        CHECK(weak.infection_index[dis(d)] == Approx(1.3 * base.infection_index[dis(d)]).epsilon(1e-12));
    }
    // 두 번째 틱: 증가분 × 감수성 × (1 − 감염)
    const double before = weak.infection_index[dis(D::Fusarium)];
    (void)process::update_soil_infection(weak, cell, p, 0);
    const double rate = p.soil_disease[dis(D::Fusarium)].infection_rate_per_hour;
    CHECK(weak.infection_index[dis(D::Fusarium)] ==
          Approx(before + rate * process::infection_pressure(D::Fusarium, cell, p) * 1.3 * (1.0 - before))
              .epsilon(1e-12));
}

// 월드 생성 정식: 감수성은 [1 − s, 1 + s] 안에서 포기마다 달라요
TEST_CASE("Planted crops get susceptibility factors within the spread", "[unit][m5a][soil_disease][variation]") {
    sim::Params p = test::test_params();
    p.microbes.susceptibility_spread_index = 0.5;
    sim::Scenario s = test::test_scenario(1, 4, 5);
    s.crop = test::test_crop_init();
    const sim::Snapshot snap = create(p, s).snapshot();
    double lo = 2.0;
    double hi = 0.0;
    for (const sim::Crop& c : snap.crops) {
        CHECK(c.disease_susceptibility_factor >= 0.5);
        CHECK(c.disease_susceptibility_factor <= 1.5);
        lo = std::min(lo, c.disease_susceptibility_factor);
        hi = std::max(hi, c.disease_susceptibility_factor);
    }
    CHECK(hi - lo > 0.1);
}

// 난수 순서 (월드 생성): 타일마다 biology 스트림에서 화아분화, disease 스트림에서 감수성 → 시들음병균 → 역병균
TEST_CASE("World creation draws biology random numbers in the documented order",
          "[unit][m5a][variation][determinism]") {
    sim::Params p = test::test_params();
    p.phenology.induction_spread_index = 0.4;
    p.microbes.susceptibility_spread_index = 0.5;
    p.microbes.pathogen_spread_index = 0.5;
    sim::Scenario s = test::test_scenario(1, 2, 3);
    s.soil.microbe_index = {1.0, 1.0, 1.0, 0.5, 0.1, 0.4, 0.25};
    s.crop = test::test_crop_init();
    const sim::SeedlingGradeDef* grade = process::find_seedling_grade(p, s.crop->seedling_grade);
    REQUIRE(grade != nullptr);
    const sim::Snapshot snap = create(p, s).snapshot();
    sim::Pcg32 rng = test::biology_rng(s.seed);
    sim::Pcg32 disease = sim::make_stream(s.seed, static_cast<std::uint64_t>(sim::RngStream::Disease));
    for (std::size_t t = 0; t < snap.crops.size(); ++t) {
        const double u_induction = rng.next_unit_double();
        const double u_susceptibility = disease.next_unit_double();
        const double u_fusarium = disease.next_unit_double();
        const double u_phytophthora = disease.next_unit_double();
        CHECK(snap.crops[t].induction_index == std::max(0.0, grade->initial_induction_index - 0.4 * u_induction));
        CHECK(snap.crops[t].disease_susceptibility_factor == 1.0 + 0.5 * (2.0 * u_susceptibility - 1.0));
        CHECK(snap.soil[t].microbe_index[idx(G::Fusarium)] ==
              std::clamp(0.4 * (1.0 + 0.5 * (2.0 * u_fusarium - 1.0)), 0.0, 1.0));
        CHECK(snap.soil[t].microbe_index[idx(G::Phytophthora)] ==
              std::clamp(0.25 * (1.0 + 0.5 * (2.0 * u_phytophthora - 1.0)), 0.0, 1.0));
    }
}

// Plant 명령: 타일마다 화아분화(biology) → 감수성(disease) (병원균 차이는 생성 때만)
TEST_CASE("Plant command draws induction then susceptibility per tile", "[unit][m5a][variation][plant]") {
    sim::Params p = test::test_params();
    p.phenology.induction_spread_index = 0.4;
    p.microbes.susceptibility_spread_index = 0.5;
    p.microbes.pathogen_spread_index = 0.5;  // 시작 지수 0이라 생성 때 뽑지 않아요
    sim::Scenario s = test::test_scenario(250, 2, 3);
    s.soil.microbe_index = {1.0, 1.0, 1.0, 0.5, 0.1, 0.0, 0.0};
    sim::World world = create(p, s);
    test::must_enqueue(world, sim::PlantCommand{.seedling_grade = "B", .plants_per_m2 = 8.0});
    world.step(1);
    const sim::Snapshot snap = world.snapshot();
    sim::Pcg32 disease = sim::make_stream(s.seed, static_cast<std::uint64_t>(sim::RngStream::Disease));
    for (const sim::Crop& crop : snap.crops) {
        REQUIRE(crop.present);
        const double u = disease.next_unit_double();
        CHECK(crop.disease_susceptibility_factor == 1.0 + 0.5 * (2.0 * u - 1.0));
    }
}

// ---- 차이가 0이면 이전과 같음 ----

// test_params(차이 0): 감수성 1, 병원균 시작값 그대로, 화아분화 차이는 이전 규칙 그대로(다른 난수가 끼지 않음)
TEST_CASE("Zero spreads keep the previous world exactly", "[unit][m5a][variation][determinism]") {
    sim::Params p = test::test_params();
    p.phenology.induction_spread_index = 0.4;  // 이전부터 있던 규칙만 켬
    sim::Scenario s = test::test_scenario(1, 2, 3);
    s.soil.microbe_index = {1.0, 1.0, 1.0, 0.5, 0.1, 0.4, 0.25};
    s.crop = test::test_crop_init();
    const sim::SeedlingGradeDef* grade = process::find_seedling_grade(p, s.crop->seedling_grade);
    REQUIRE(grade != nullptr);
    const sim::Snapshot snap = create(p, s).snapshot();
    sim::Pcg32 rng = test::biology_rng(s.seed);
    for (std::size_t t = 0; t < snap.crops.size(); ++t) {
        CHECK(snap.crops[t].induction_index ==
              std::max(0.0, grade->initial_induction_index - 0.4 * rng.next_unit_double()));
        CHECK(snap.crops[t].disease_susceptibility_factor == 1.0);
        CHECK(snap.soil[t].microbe_index == s.soil.microbe_index);
    }
}

// 병원균 차이가 켜져 있어도 시작 지수가 0이면 난수를 뽑지 않아, 이후 실행 전체가 끈 것과 같아요
TEST_CASE("Pathogen spread with zero start indices leaves the run unchanged", "[unit][m5a][variation][determinism]") {
    sim::Params off = test::test_params();
    off.phenology.induction_spread_index = 0.4;
    sim::Params on = off;
    on.microbes.pathogen_spread_index = 0.5;
    sim::Scenario s = test::test_scenario(250, 2, 3);
    s.crop = test::test_crop_init();
    REQUIRE(s.soil.microbe_index[idx(G::Fusarium)] == 0.0);
    REQUIRE(s.soil.microbe_index[idx(G::Phytophthora)] == 0.0);
    sim::World a = create(off, s);
    sim::World b = create(on, s);
    CHECK(a.state_hash() == b.state_hash());
    a.step(24 * 2);
    b.step(24 * 2);
    CHECK(a.state_hash() == b.state_hash());
}

// ---- 직렬화 ----

TEST_CASE("Deep soil temperature and susceptibility survive a save and load round trip",
          "[unit][m5a][variation][serialization]") {
    sim::Params p = test::test_params();
    p.soil_temperature.deep_coupling_fraction = 0.4;
    p.soil_temperature.deep_relax_rate_per_hour = 0.004;
    p.microbes.pathogen_spread_index = 0.5;
    p.microbes.susceptibility_spread_index = 0.5;
    sim::Scenario s = test::test_scenario(250);
    s.soil.microbe_index = {1.0, 1.0, 1.0, 0.5, 0.1, 0.4, 0.3};
    s.crop = test::test_crop_init();
    sim::World world = create(p, s);
    world.step(24 * 2);
    const sim::Snapshot before = world.snapshot();
    REQUIRE(before.soil[0].deep_soil_temp_degC != before.soil[0].soil_temp_degC);
    REQUIRE(before.crops[0].disease_susceptibility_factor != 1.0);
    auto loaded = sim::World::deserialize(world.serialize());
    REQUIRE(loaded.has_value());
    CHECK(loaded->params() == world.params());
    CHECK(loaded->state_hash() == world.state_hash());
    CHECK(loaded->snapshot().soil == before.soil);
    CHECK(loaded->snapshot().crops == before.crops);
    world.step(24);
    loaded->step(24);
    CHECK(loaded->state_hash() == world.state_hash());
}

// state_hash가 새 상태를 덮어요
TEST_CASE("State hash covers deep soil temperature and susceptibility", "[unit][m5a][variation][determinism]") {
    sim::Scenario s = test::test_scenario();
    s.crop = test::test_crop_init();
    sim::Params p = test::test_params();
    sim::World base = create(p, s);
    // 결합 0 + 깊은 흙 속도만 다름: 깊은 흙 외의 상태는 같으므로(위 테스트) 해시 차이는 깊은 흙 온도에서 와요
    sim::Params moving = p;
    moving.soil_temperature.deep_relax_rate_per_hour = 0.01;
    sim::World deep = create(moving, s);
    CHECK(deep.state_hash() == base.state_hash());
    base.step(24);
    deep.step(24);
    CHECK(without_deep(deep.snapshot().soil) == without_deep(base.snapshot().soil));
    CHECK(deep.state_hash() != base.state_hash());
    p.microbes.susceptibility_spread_index = 0.5;  // 생성 직후 감수성만 바뀜 (화아분화 차이 0)
    CHECK(create(p, s).state_hash() != create(test::test_params(), s).state_hash());
}

// ---- 검증 ----

TEST_CASE("Validation rejects out-of-range deep soil and spread parameters", "[unit][m5a][variation][validation]") {
    const sim::Scenario s = test::test_scenario();
    const auto rejected = [&s](auto edit) {
        sim::Params p = test::test_params();
        edit(p);
        return !sim::World::create(p, s).has_value();
    };
    CHECK(rejected([](sim::Params& p) { p.soil_temperature.deep_coupling_fraction = 1.5; }));
    CHECK(rejected([](sim::Params& p) { p.soil_temperature.deep_relax_rate_per_hour = -0.1; }));
    CHECK(rejected([](sim::Params& p) { p.microbes.pathogen_spread_index = 1.2; }));
    CHECK(rejected([](sim::Params& p) { p.microbes.susceptibility_spread_index = -0.5; }));
    CHECK_FALSE(rejected([](sim::Params& p) {
        p.soil_temperature.deep_coupling_fraction = 1.0;
        p.microbes.susceptibility_spread_index = 1.0;
    }));
}
