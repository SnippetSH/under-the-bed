// M4 단위 테스트: 딸기 생물계절 (docs/spec/strawberry-phenology.md "테스트 (단위)")
// 활착, 화아분화(낮 길이·기온·체내 N), 화방 추가와 꽃 수, 화방 발달·개화, 모종 등급, 활착 시작값

#include <cmath>
#include <cstdint>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "test_helpers.hpp"

namespace {

using Catch::Approx;
namespace process = sim::process;

// 낮 길이 약 11 h(day 288)와 약 14.3 h(day 151). induction_day_length_curve가 1과 0인 날
constexpr int kShortDay_doy = 288;
constexpr int kLongDay_doy = 151;
constexpr double kOptimalAir_degC = 18.0;  // 화아분화 기온 곡선 1 (12~22 °C)

sim::Crop grown_crop(const sim::Params& p) {
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);  // 활착 1, 체내 양분 = 요구량
    crop.induction_index = 0.1;
    return crop;
}

// 한 틱 생물계절만 돌려요 (update_phenology). 돌려받은 값은 활착 배율
double phenology_step(sim::Crop& crop, const sim::Params& p, double air_degC, int day_of_year, std::uint64_t tick = 0) {
    sim::Pcg32 rng = test::biology_rng(5);
    const auto env = test::crop_env(0.0, air_degC, day_of_year, tick);
    return process::update_phenology(crop, p, env, crop.fruit_g_per_m2(), rng);
}

}  // namespace

// ---- 활착 ----

// 활착 시작값 1 대 0: 1은 활착 배율 1로 바로 생장·흡수, 0은 약 10일에 걸쳐 올라감
TEST_CASE("Establishment starts at the scenario value and rises over about ten days", "[unit][m4][phenology]") {
    const sim::Params p = test::test_params();
    // 시작값 1: 곧바로 배율 1
    sim::Crop full = grown_crop(p);
    CHECK(phenology_step(full, p, 20.0, 1) == 1.0);
    CHECK(full.establishment_index == 1.0);

    // 시작값 0: 첫 틱에 활착 속도 x 기온 곡선만큼 올라가고, 그 값의 활착 배율을 돌려줘요 (정식 몸살 0.3에서 시작)
    sim::Crop fresh = grown_crop(p);
    fresh.establishment_index = 0.0;
    const double rate = p.phenology.establishment_rate_per_hour;
    const double first_factor = phenology_step(fresh, p, 20.0, 1);
    CHECK(fresh.establishment_index == Approx(rate * p.phenology.establishment_temp_curve(20.0)));
    CHECK(first_factor == Approx(p.phenology.establishment_growth_curve(rate)));
    CHECK(first_factor < 0.31);
    CHECK(first_factor > 0.3);
    // 배율은 계속 올라가 1에서 멈춰요: 최적 기온에서 1 / 0.0042 = 238.1 -> 239틱(약 10일)
    int ticks = 1;
    double last_factor = first_factor;
    while (fresh.establishment_index < 1.0 && ticks < 1000) {
        const double factor = phenology_step(fresh, p, 20.0, 1);
        CHECK(factor >= last_factor);
        last_factor = factor;
        ++ticks;
    }
    CHECK(ticks == 239);
    CHECK(ticks / 24.0 > 9.5);
    CHECK(ticks / 24.0 < 10.5);
    CHECK(last_factor == 1.0);
    CHECK(phenology_step(fresh, p, 20.0, 1) == 1.0);
    CHECK(fresh.establishment_index == 1.0);  // 1에서 멈춤
}

// 활착 속도는 기온 곡선을 따라가요: 5 °C 이하는 0, 10 °C는 절반
TEST_CASE("Establishment speed follows the temperature curve", "[unit][m4][phenology]") {
    const sim::Params p = test::test_params();
    for (const double air : {5.0, 2.0}) {
        sim::Crop crop = grown_crop(p);
        crop.establishment_index = 0.0;
        for (int t = 0; t < 100; ++t) {
            phenology_step(crop, p, air, 1);
        }
        CHECK(crop.establishment_index == 0.0);
    }
    sim::Crop cool = grown_crop(p);
    cool.establishment_index = 0.0;
    phenology_step(cool, p, 10.0, 1);
    CHECK(cool.establishment_index == Approx(0.5 * p.phenology.establishment_rate_per_hour));
}

// ---- 화아분화 ----

// 활착 전에는 화아분화 진행 0 (활착이 끝나야 진행)
TEST_CASE("Induction does not progress before establishment is complete", "[unit][m4][phenology]") {
    const sim::Params p = test::test_params();
    sim::Crop crop = grown_crop(p);
    crop.establishment_index = 0.99;  // 활착 전 (이번 틱에 1이 되지 못해요: 기온 5 °C에서 활착 속도 0)
    phenology_step(crop, p, 5.0, kShortDay_doy);
    CHECK(crop.induction_index == 0.1);
    // 활착이 끝난 뒤(1.0)에는 진행해요: 낮 11 h, 18 °C, 체내 N 요구량 그대로 -> 0.002
    sim::Crop done = grown_crop(p);
    phenology_step(done, p, kOptimalAir_degC, kShortDay_doy);
    CHECK(done.induction_index == Approx(0.1 + p.phenology.induction_rate_per_hour));
}

// 낮 길이 11 h 대 14 h, 같은 기온: 11 h에서 진행, 14 h에서 0
TEST_CASE("Induction needs short days", "[unit][m4][phenology]") {
    const sim::Params p = test::test_params();
    REQUIRE(p.calendar.day_length_curve(kShortDay_doy) == Approx(11.05).margin(0.05));
    REQUIRE(p.calendar.day_length_curve(kLongDay_doy) > 14.0);
    sim::Crop short_day = grown_crop(p);
    sim::Crop long_day = grown_crop(p);
    phenology_step(short_day, p, kOptimalAir_degC, kShortDay_doy);
    phenology_step(long_day, p, kOptimalAir_degC, kLongDay_doy);
    CHECK(short_day.induction_index > 0.1);
    CHECK(long_day.induction_index == 0.1);
    // 식: 진행 = 속도 x 낮 길이 곡선 x 기온 곡선 x 체내 N 곡선 (여기서 곡선 셋이 모두 1)
    CHECK(short_day.induction_index - 0.1 ==
          Approx(p.phenology.induction_rate_per_hour * p.phenology.induction_day_length_curve(
                                                           p.calendar.day_length_curve(kShortDay_doy))));
}

// 기온 18 °C 대 30 °C: 30 °C에서 진행 0. 26 °C는 곡선 값(0.3)만큼
TEST_CASE("Induction is blocked by high temperature", "[unit][m4][phenology]") {
    const sim::Params p = test::test_params();
    sim::Crop cool = grown_crop(p);
    sim::Crop hot = grown_crop(p);
    sim::Crop warm = grown_crop(p);
    phenology_step(cool, p, 18.0, kShortDay_doy);
    phenology_step(hot, p, 30.0, kShortDay_doy);
    phenology_step(warm, p, 26.0, kShortDay_doy);
    CHECK(cool.induction_index > 0.1);
    CHECK(hot.induction_index == 0.1);
    CHECK(warm.induction_index - 0.1 == Approx(0.3 * (cool.induction_index - 0.1)));
    // 저온은 촉진(10~20 °C 곡선 1), 3 °C 이하는 0
    sim::Crop frozen = grown_crop(p);
    phenology_step(frozen, p, 3.0, kShortDay_doy);
    CHECK(frozen.induction_index == 0.1);
}

// 체내 N 비율 1.0 대 1.3: 1.3에서 진행이 느림 (곡선 1.3 -> 0.6)
TEST_CASE("Induction slows when the plant nitrogen ratio is high", "[unit][m4][phenology]") {
    const sim::Params p = test::test_params();
    const double required_N = process::nutrient_requirement(grown_crop(p), p.crop).N;
    sim::Crop balanced = grown_crop(p);
    sim::Crop rich = grown_crop(p);
    sim::Crop very_rich = grown_crop(p);
    balanced.plant_N_g_per_m2 = 1.0 * required_N;
    rich.plant_N_g_per_m2 = 1.3 * required_N;
    very_rich.plant_N_g_per_m2 = 1.5 * required_N;
    phenology_step(balanced, p, kOptimalAir_degC, kShortDay_doy);
    phenology_step(rich, p, kOptimalAir_degC, kShortDay_doy);
    phenology_step(very_rich, p, kOptimalAir_degC, kShortDay_doy);
    const double base = balanced.induction_index - 0.1;
    REQUIRE(base == Approx(p.phenology.induction_rate_per_hour));
    CHECK(rich.induction_index - 0.1 < base);
    CHECK(rich.induction_index - 0.1 == Approx(0.6 * base));
    CHECK(very_rich.induction_index - 0.1 == Approx(0.25 * base));
    // 요구량에 과실 N 몫이 들어가요: 과실이 있으면 같은 체내 N의 비율이 낮아져 진행이 빨라져요 (분모 = 요구량)
    sim::Crop fruity = grown_crop(p);
    sim::Fruit fruit;
    fruit.set = true;
    fruit.development_index = 0.5;
    fruit.dry_g = 10.0;
    fruity.fruits.push_back(fruit);
    fruity.plant_N_g_per_m2 = 1.3 * required_N;  // 과실 없는 요구량 기준 1.3배지만 과실 포함 요구량 기준으로는 더 낮음
    phenology_step(fruity, p, kOptimalAir_degC, kShortDay_doy);
    CHECK(fruity.induction_index - 0.1 > rich.induction_index - 0.1);
}

// 진행도가 1을 넘음: 화방 1개 추가, 남는 진행도 이월 (활착이 끝난 뒤). 화방 추가는 물질을 옮기지 않아요
TEST_CASE("Induction overflow adds a truss and carries the remainder", "[unit][m4][phenology]") {
    const sim::Params p = test::test_params();
    const double inc = p.phenology.induction_rate_per_hour;
    sim::Crop crop = grown_crop(p);
    crop.induction_index = 1.0 - inc / 2.0;
    const sim::Crop before = crop;
    phenology_step(crop, p, kOptimalAir_degC, kShortDay_doy, 777);
    REQUIRE(crop.trusses.size() == 1);
    CHECK(crop.induction_index == Approx(before.induction_index + inc - 1.0));
    CHECK(crop.induction_index > 0.0);
    CHECK(crop.induction_index < inc);
    const sim::Truss& truss = crop.trusses[0];
    CHECK(truss.initiated_tick == 777);
    CHECK_FALSE(truss.opened);
    // 생물계절 자체는 물질을 옮기지 않아요: 건물·체내 양분 그대로, 과실 목록 그대로
    CHECK(crop.leaf_g_per_m2 == before.leaf_g_per_m2);
    CHECK(crop.crown_g_per_m2 == before.crown_g_per_m2);
    CHECK(crop.root_g_per_m2 == before.root_g_per_m2);
    CHECK(crop.plant_N_g_per_m2 == before.plant_N_g_per_m2);
    CHECK(crop.plant_K_g_per_m2 == before.plant_K_g_per_m2);
    CHECK(crop.fruits.empty());

    // 진행도가 한 번에 여러 번 넘으면 화방이 여러 개 (같은 틱에 개수만큼)
    sim::Crop many = grown_crop(p);
    many.induction_index = 2.5;
    phenology_step(many, p, kOptimalAir_degC, kShortDay_doy);
    CHECK(many.trusses.size() == 2);
    CHECK(many.induction_index == Approx(0.5 + inc));
    // 1에 못 미치면 화방 없음
    sim::Crop none = grown_crop(p);
    none.induction_index = 0.5;
    phenology_step(none, p, kOptimalAir_degC, kShortDay_doy);
    CHECK(none.trusses.empty());
}

// 관부가 큼: 새 화방의 꽃 수가 많음 (분화할 때의 포기당 관부 g로 정해짐)
TEST_CASE("A bigger crown gives a new truss more flowers", "[unit][m4][phenology]") {
    const sim::Params p = test::test_params();
    const auto flowers_for_crown = [&](double crown_g_per_plant) {
        sim::Crop crop = grown_crop(p);
        crop.crown_g_per_m2 = crown_g_per_plant * crop.plants_per_m2;
        crop.induction_index = 1.0;
        phenology_step(crop, p, kOptimalAir_degC, kShortDay_doy);
        REQUIRE(crop.trusses.size() == 1);
        return crop.trusses[0].flowers_per_m2;
    };
    const double small = flowers_for_crown(1.0);
    const double medium = flowers_for_crown(2.0);
    const double large = flowers_for_crown(6.0);
    CHECK(small < medium);
    CHECK(medium < large);
    // 식: flowers_per_m2 = flowers_per_truss_curve(관부 / 포기 수) x 포기 수 (포기 8/m²)
    CHECK(small == Approx(p.phenology.flowers_per_truss_curve(1.0) * 8.0));
    CHECK(medium == Approx(p.phenology.flowers_per_truss_curve(2.0) * 8.0));
    CHECK(large == Approx(p.phenology.flowers_per_truss_curve(6.0) * 8.0));
    CHECK(large == Approx(18.0 * 8.0));
}

// ---- 화방 발달과 개화 ----

// 적산온도: 발달 = (기온 - 5) / 24 / 350 (5 °C 이하는 0). 350 °C·일에 도달하면 개화하고 꽃(과실)이 생김
TEST_CASE("Truss development accumulates degree days and opens into flowers", "[unit][m4][phenology]") {
    const sim::Params p = test::test_params();
    sim::Crop crop = grown_crop(p);
    crop.induction_index = 0.0;
    sim::Truss truss;
    truss.flowers_per_m2 = 18.0;
    truss.development_index = 0.5;
    crop.trusses.push_back(truss);
    // 기온 5 °C 이하: 발달 없음
    for (const double air : {5.0, 0.0, -4.0}) {
        phenology_step(crop, p, air, 1);
        CHECK(crop.trusses[0].development_index == 0.5);
    }
    // 20 °C: 15 / 24 / 350
    phenology_step(crop, p, 20.0, 1);
    CHECK(crop.trusses[0].development_index == Approx(0.5 + 15.0 / 24.0 / 350.0));
    CHECK_FALSE(crop.trusses[0].opened);
    CHECK(crop.fruits.empty());
    // 350 °C·일 = 20 °C에서 560틱: 처음 0에서 시작해 그 시점에 개화
    sim::Crop fresh = grown_crop(p);
    fresh.induction_index = 0.0;
    sim::Truss new_truss;
    new_truss.flowers_per_m2 = 18.0;
    fresh.trusses.push_back(new_truss);
    int ticks = 0;
    while (!fresh.trusses[0].opened && ticks < 2000) {
        phenology_step(fresh, p, 20.0, 1, static_cast<std::uint64_t>(ticks));
        ++ticks;
    }
    CHECK(ticks >= 559);  // 부동소수점 누적으로 한 틱 정도 오차를 허용
    CHECK(ticks <= 561);
    const sim::Truss& opened = fresh.trusses[0];
    CHECK(opened.development_index == 1.0);
    CHECK(opened.opened_tick == static_cast<std::uint64_t>(ticks - 1));
    // 꽃(개별 과실) 생성: 꽃 수 18.0 -> 정확히 18개, 아직 안 핀 꽃(set = false), 과위 배분 1·2·4·7·4
    REQUIRE(fresh.fruits.size() == 18);
    std::vector<int> per_rank(5, 0);
    for (const sim::Fruit& f : fresh.fruits) {
        CHECK_FALSE(f.set);
        CHECK(f.truss_index == 0);
        CHECK(f.anthesis_tick >= opened.opened_tick);
        ++per_rank[f.rank];
    }
    CHECK(per_rank == std::vector<int>{1, 2, 4, 7, 4});
    // 개화한 화방은 더 계산하지 않아요: 다시 돌려도 꽃이 늘지 않고 진행도 그대로
    phenology_step(fresh, p, 20.0, 1, 600);
    CHECK(fresh.fruits.size() == 18);
    CHECK(fresh.trusses[0].development_index == 1.0);
}

// 분화한 순서대로 개화: 먼저 분화한 화방이 먼저 열려요
TEST_CASE("Trusses open in the order they were initiated", "[unit][m4][phenology]") {
    const sim::Params p = test::test_params();
    sim::Crop crop = grown_crop(p);
    crop.induction_index = 0.0;
    for (const double development : {0.9, 0.4}) {
        sim::Truss truss;
        truss.flowers_per_m2 = 8.0;
        truss.development_index = development;
        crop.trusses.push_back(truss);
    }
    int ticks = 0;
    int first_open_tick = -1;
    int second_open_tick = -1;
    while (second_open_tick < 0 && ticks < 2000) {
        phenology_step(crop, p, 20.0, 1, static_cast<std::uint64_t>(ticks));
        if (first_open_tick < 0 && crop.trusses[0].opened) {
            first_open_tick = ticks;
        }
        if (crop.trusses[1].opened) {
            second_open_tick = ticks;
        }
        ++ticks;
    }
    CHECK(first_open_tick >= 0);
    CHECK(first_open_tick < second_open_tick);
    // 두 화방의 개화 간격 = 진행도 차이 0.5 / 틱당 0.0017857 = 280틱 (부동소수점 오차 1틱 허용)
    CHECK(second_open_tick - first_open_tick >= 279);
    CHECK(second_open_tick - first_open_tick <= 281);
}

// ---- 모종 등급 ----

// 모종 A 대 C: 초기 화아분화 진행도·건물이 A가 큼. B는 잎 28, 관부 16, 뿌리 12 g/m² (8포기)
TEST_CASE("Seedling grades set initial biomass and induction and A is bigger than C", "[unit][m4][phenology]") {
    const sim::Params p = test::test_params();
    const sim::SeedlingGradeDef* a = process::find_seedling_grade(p, "A");
    const sim::SeedlingGradeDef* b = process::find_seedling_grade(p, "B");
    const sim::SeedlingGradeDef* c = process::find_seedling_grade(p, "C");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    CHECK(process::find_seedling_grade(p, "Z") == nullptr);
    const sim::Crop crop_a = process::plant_crop(*a, 8.0, p);
    const sim::Crop crop_b = process::plant_crop(*b, 8.0, p);
    const sim::Crop crop_c = process::plant_crop(*c, 8.0, p);
    CHECK(crop_a.induction_index > crop_c.induction_index);
    CHECK(crop_a.leaf_g_per_m2 > crop_c.leaf_g_per_m2);
    CHECK(crop_a.crown_g_per_m2 > crop_c.crown_g_per_m2);
    CHECK(crop_a.root_g_per_m2 > crop_c.root_g_per_m2);
    // 정식 시 기관별 건물 = 포기당 값 x 재식 밀도
    CHECK(crop_b.leaf_g_per_m2 == Approx(3.5 * 8.0));
    CHECK(crop_b.crown_g_per_m2 == Approx(2.0 * 8.0));
    CHECK(crop_b.root_g_per_m2 == Approx(1.5 * 8.0));
    CHECK(crop_b.plants_per_m2 == 8.0);
    CHECK(crop_a.induction_index == 1.0);
    CHECK(crop_b.induction_index == 0.8);
    CHECK(crop_c.induction_index == 0.3);
    // 체내 양분 = 요구량 (적정 농도 x 관부 포함 건물, M2 규칙 그대로), 정식 직후 건강하고 살아 있어요
    const process::NutrientRequirement required = process::nutrient_requirement(crop_b, p.crop);
    CHECK(crop_b.plant_N_g_per_m2 == required.N);
    CHECK(crop_b.plant_K_g_per_m2 == required.K);
    CHECK(crop_b.plant_Mn_g_per_m2 == required.Mn);
    CHECK(crop_b.present);
    CHECK(crop_b.alive);
    CHECK(crop_b.root_health == 1.0);
    CHECK(crop_b.fruits.empty());
    CHECK(crop_b.trusses.empty());
    CHECK(crop_b.harvest.harvested_size_class_counts.size() == p.fruit.size_class_bounds_g.size() + 1);
    // 좋은 모종은 활착이 끝나면 곧바로 첫 화방 (initial_induction_index 1.0 + 진행)
    sim::Crop grade_a = crop_a;
    grade_a.establishment_index = 1.0;
    phenology_step(grade_a, p, kOptimalAir_degC, kShortDay_doy);
    CHECK(grade_a.trusses.size() == 1);
    sim::Crop grade_c = crop_c;
    grade_c.establishment_index = 1.0;
    phenology_step(grade_c, p, kOptimalAir_degC, kShortDay_doy);
    CHECK(grade_c.trusses.empty());
}

// 월드 생성: 모종 등급과 시나리오의 initial_establishment_index가 작물 상태에 들어가요
TEST_CASE("World planting uses the seedling grade and the scenario establishment index", "[unit][m4][phenology][world]") {
    for (const double establishment : {0.0, 0.4, 1.0}) {
        sim::Scenario scenario = test::test_scenario(1, 1, 1);
        scenario.crop = sim::CropInit{.seedling_grade = "A", .plants_per_m2 = 6.0, .initial_establishment_index = establishment};
        sim::World world = test::make_world(std::move(scenario));
        const sim::Snapshot snap = world.snapshot();
        const sim::Crop& crop = snap.crops[0];
        INFO("establishment " << establishment);
        CHECK(crop.present);
        CHECK(crop.establishment_index == establishment);
        CHECK(crop.induction_index == 1.0);
        CHECK(crop.leaf_g_per_m2 == Approx(4.0 * 6.0));
        CHECK(crop.crown_g_per_m2 == Approx(3.0 * 6.0));
        CHECK(crop.plants_per_m2 == 6.0);
    }
}

// initial_induction_index 1.2 또는 -0.1: 검증에서 거부 (범위 [0, 1]). 경계값 0과 1은 통과
TEST_CASE("Seedling grade initial induction index must be within zero and one", "[unit][m4][phenology][validation]") {
    const auto creates_with = [](double initial_induction_index) {
        sim::Params p = test::test_params();
        p.seedling_grades[1].initial_induction_index = initial_induction_index;
        return sim::World::create(p, test::test_scenario(1, 1, 1)).has_value();
    };
    CHECK(creates_with(0.0));
    CHECK(creates_with(0.8));
    CHECK(creates_with(1.0));
    CHECK_FALSE(creates_with(1.2));
    CHECK_FALSE(creates_with(-0.1));
}
