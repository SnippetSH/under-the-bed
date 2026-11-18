// M4 단위 테스트: 딸기 개별 과실·분배·호흡·낙엽·수확·품질
// (docs/spec/strawberry-fruit.md "테스트 (단위)", m4-overview.md "구현 세부 규칙" 3·7번)
// 꽃 만들기(과위·개화 지연·변이), 개화 판정(수정률·낙화·기형·크기), 총생산·호흡·분배(따라잡기), 크기 충족도,
// 낙과, 적화·적과, 당도 주·야간 점수, 수확·반출·크기 구간, 낙엽, 요구량
//
// 작기 전체 보존·결정론은 test_m4_conservation.cpp, test_m4_determinism.cpp에 있어요.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "test_helpers.hpp"

namespace {

using Catch::Approx;
using Catch::Matchers::WithinRel;
namespace process = sim::process;

constexpr double kDaySolar_W_per_m2 = 500.0;
constexpr double kAir_degC = 20.0;

// 착과된 어린 과실이 과위(0~4)별로 같은 수만큼 있는 작물 (B x 8, 활착). 진행도 dev, 건물 dry
sim::Crop crop_with_young_fruits(const sim::Params& p, int per_rank, double development = 0.1, double dry_g = 0.0,
                                 std::uint32_t first_rank = 0, std::uint32_t last_rank = 4) {
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    for (std::uint32_t rank = first_rank; rank <= last_rank; ++rank) {
        for (int k = 0; k < per_rank; ++k) {
            sim::Fruit f;
            f.rank = rank;
            f.set = true;
            f.anthesis_tick = static_cast<std::uint64_t>(k);
            f.potential_fresh_g = 20.0;
            f.development_index = development;
            f.dry_g = dry_g;
            crop.fruits.push_back(f);
        }
    }
    return crop;
}

// 한 틱: update_crop를 직접 불러요 (유효량 0 = 흡수 없음)
process::CropTickResult tick_crop(sim::Crop& crop, sim::SoilCell& cell, const sim::Params& p, double solar_W_per_m2,
                                  double air_degC, sim::Pcg32& rng, sim::Ledger& ledger, std::uint64_t tick = 0) {
    return process::update_crop(crop, cell, p, process::NutrientAvailability{},
                                test::crop_env(solar_W_per_m2, air_degC, 1, tick), rng, ledger);
}

struct Totals {
    double N, P, K, Ca, Mg;
};

Totals soil_plus_plant(const sim::SoilCell& cell, const sim::Crop& crop) {
    return {.N = cell.nh4_N_g_per_m2 + cell.no2_N_g_per_m2 + cell.no3_N_g_per_m2 + cell.labile_N_g_per_m2 +
                 crop.plant_N_g_per_m2,
            .P = cell.p_g_per_m2 + crop.plant_P_g_per_m2,
            .K = cell.k_g_per_m2 + crop.plant_K_g_per_m2,
            .Ca = cell.ca_g_per_m2 + crop.plant_Ca_g_per_m2,
            .Mg = cell.mg_g_per_m2 + crop.plant_Mg_g_per_m2};
}

double expected_abortions(const sim::Params& p, double satisfaction, std::uint32_t rank, double n) {
    const double dD = process::fruit_development_increment(p.fruit, kAir_degC);
    return n * p.fruit.abortion_rate_per_development_index * dD * p.fruit.abortion_satisfaction_curve(satisfaction) *
           p.fruit.rank_abortion_weight[rank];
}

}  // namespace

// ---- 꽃 만들기 (1번) ----

// 꽃 수 7.4, 같은 시드 두 번: 같은 개수(7 또는 8). 소수부는 난수로 올림
TEST_CASE("Flowers from 7.4 per m2 are 7 or 8 and the same seed gives the same list", "[unit][m4][fruit]") {
    const sim::Params p = test::test_params();
    auto make = [&](std::uint64_t seed) {
        sim::Crop crop;
        sim::Pcg32 rng = test::biology_rng(seed);
        process::create_flowers(crop, 0, 7.4, p.fruit, 100, rng);
        return crop.fruits;
    };
    const auto a = make(1);
    const auto b = make(1);
    CHECK(a == b);
    CHECK((a.size() == 7 || a.size() == 8));
    bool saw_seven = false;
    bool saw_eight = false;
    for (std::uint64_t seed = 2; seed < 60; ++seed) {
        const auto f = make(seed);
        CHECK((f.size() == 7 || f.size() == 8));
        saw_seven = saw_seven || f.size() == 7;
        saw_eight = saw_eight || f.size() == 8;
    }
    CHECK(saw_seven);  // 소수부 0.4: 두 경우 모두 나와야 해요
    CHECK(saw_eight);
    // 다른 시드는 개화 시점이 달라질 수 있어요
    CHECK_FALSE(make(1) == make(2));
}

// 과위 배분: 합 = 꽃 수 (최대 나머지 방식). 18송이는 1·2·4·7·4, 꽃 0이면 목록 비어 있음
TEST_CASE("Flowers are split over the five ranks by the largest remainder method", "[unit][m4][fruit]") {
    const sim::Params p = test::test_params();
    const auto counts = [&](double flowers) {
        sim::Crop crop;
        sim::Pcg32 rng = test::biology_rng(3);
        process::create_flowers(crop, 0, flowers, p.fruit, 0, rng);
        std::vector<int> per_rank(5, 0);
        for (const sim::Fruit& f : crop.fruits) {
            REQUIRE(f.rank < 5);
            ++per_rank[f.rank];
        }
        return per_rank;
    };
    CHECK(counts(18.0) == std::vector<int>{1, 2, 4, 7, 4});
    CHECK(counts(0.0) == std::vector<int>{0, 0, 0, 0, 0});
    for (const double flowers : {1.0, 5.0, 12.0, 33.0, 100.0, 176.0}) {
        const auto c = counts(flowers);
        int sum = 0;
        for (const int n : c) {
            sum += n;
        }
        INFO("flowers " << flowers);
        CHECK(sum == static_cast<int>(flowers));
        // 과위가 높을수록(늦게 필수록) 같거나 많이 달리는 경향: 4번 과위 이상(3, 4)이 1번 과위보다 많아요
        if (flowers >= 12.0) {
            CHECK(c[3] > c[0]);
        }
    }
}

// 개화 시점: 과위별 지연(0·4·7·10·12일) + 과실마다 편차(± 4일, 시드 기반), 최소 지금. 같은 시드 -> 같은 시점
TEST_CASE("Flower anthesis times follow rank delays with seeded jitter", "[unit][m4][fruit]") {
    const sim::Params p = test::test_params();
    const sim::FruitParams& fp = p.fruit;
    constexpr std::uint64_t kNow = 100;
    sim::Crop crop;
    sim::Pcg32 rng = test::biology_rng(21);
    process::create_flowers(crop, 2, 2000.0, fp, kNow, rng);
    REQUIRE(crop.fruits.size() == 2000);
    std::array<double, 5> sum{};
    std::array<int, 5> count{};
    std::array<std::uint64_t, 5> lo{}, hi{};
    lo.fill(~std::uint64_t{0});
    for (const sim::Fruit& f : crop.fruits) {
        CHECK_FALSE(f.set);
        CHECK(f.truss_index == 2);
        REQUIRE(f.anthesis_tick >= kNow);  // 최소 지금
        const std::uint64_t delay = f.anthesis_tick - kNow;
        sum[f.rank] += static_cast<double>(delay);
        ++count[f.rank];
        lo[f.rank] = std::min(lo[f.rank], delay);
        hi[f.rank] = std::max(hi[f.rank], delay);
    }
    for (std::size_t r = 0; r < 5; ++r) {
        REQUIRE(count[r] > 0);
        const double delay_h = fp.rank_anthesis_delay_days[r] * 24.0;
        const double jitter_h = fp.anthesis_jitter_days * 24.0;
        INFO("rank " << r);
        // 범위: 지연 ± 편차 (음수는 0으로)
        CHECK(static_cast<double>(hi[r]) <= std::round(delay_h + jitter_h));
        CHECK(static_cast<double>(lo[r]) >= std::max(0.0, std::round(delay_h - jitter_h)));
        // 편차가 실제로 있어요 (같은 과위의 시점이 흩어짐)
        CHECK(hi[r] - lo[r] > 24);
        // 하한에 걸리지 않는 과위(지연 >= 편차)는 평균이 지연과 같아요 (균일 분포, 평균 오차 약 6시간 이내)
        if (delay_h >= jitter_h) {
            CHECK(std::abs(sum[r] / count[r] - delay_h) < 8.0);
        }
    }
    // 과위가 높을수록 평균이 늦어요
    for (std::size_t r = 1; r < 5; ++r) {
        CHECK(sum[r] / count[r] > sum[r - 1] / count[r - 1]);
    }
    // 같은 시드 -> 같은 시점, 다른 시드 -> 달라짐
    sim::Crop again;
    sim::Pcg32 rng_again = test::biology_rng(21);
    process::create_flowers(again, 2, 2000.0, fp, kNow, rng_again);
    CHECK(again.fruits == crop.fruits);
    sim::Crop other;
    sim::Pcg32 rng_other = test::biology_rng(22);
    process::create_flowers(other, 2, 2000.0, fp, kNow, rng_other);
    CHECK_FALSE(other.fruits == crop.fruits);
}

// ---- 개화 판정 (2번) ----

// 이동 평균 5 °C 대 18 °C, 꽃 많음: 5 °C 쪽 낙화·기형과 비율 큼, 평균 크기 작음
TEST_CASE("Cold mean temperature at anthesis drops more flowers and makes smaller fruit", "[unit][m4][fruit]") {
    const sim::Params p = test::test_params();
    struct Result {
        double set_share = 0.0;
        double deformed_share = 0.0;
        double mean_size_g = 0.0;
    };
    auto judge = [&](double mean_degC) {
        sim::Crop crop;
        sim::Pcg32 rng = test::biology_rng(7);
        process::create_flowers(crop, 0, 400.0, p.fruit, 0, rng);
        const double flowers = static_cast<double>(crop.fruits.size());
        process::judge_anthesis(crop, p.fruit, mean_degC, 1000, rng);
        Result r;
        double size = 0.0;
        double deformed = 0.0;
        for (const auto& f : crop.fruits) {
            CHECK(f.set);
            size += f.potential_fresh_g;
            deformed += f.deformed ? 1.0 : 0.0;
        }
        const double kept = static_cast<double>(crop.fruits.size());
        r.set_share = kept / flowers;
        r.mean_size_g = kept > 0.0 ? size / kept : 0.0;
        r.deformed_share = kept > 0.0 ? deformed / kept : 0.0;
        return r;
    };
    const Result cold = judge(5.0);
    const Result warm = judge(18.0);
    // 5 °C: 평균 수정률 0.3, 개체별 [0.15, 0.45] -> 착과 문턱 0.3 아래가 절반 떨어짐, 남은 것은 모두 기형(< 0.7)
    CHECK(cold.set_share < warm.set_share);
    CHECK(cold.set_share > 0.4);
    CHECK(cold.set_share < 0.6);
    CHECK(cold.deformed_share == 1.0);
    // 18 °C: 평균 0.92, 개체별 [0.77, 1.0] -> 낙화 0, 기형 0
    CHECK(warm.set_share == 1.0);
    CHECK(warm.deformed_share == 0.0);
    CHECK(cold.mean_size_g < warm.mean_size_g);
    // 고온도 수정을 떨어뜨려요: 35 °C 평균 0.35 -> 개체별 [0.2, 0.5], 30 °C는 [0.5, 0.8]로 낙화는 없고 일부 기형
    const Result hot35 = judge(35.0);
    const Result hot30 = judge(30.0);
    CHECK(hot35.set_share < 0.8);
    CHECK(hot35.set_share > 0.55);
    CHECK(hot30.set_share == 1.0);
    CHECK(hot30.deformed_share > 0.5);
    CHECK(hot30.deformed_share < 0.8);
    CHECK(hot30.deformed_share > warm.deformed_share);
}

// 개별 과실 식: 수정률 p는 [mu - spread, mu + spread], 최대 생체중 = 과위 최대 x 크기 곡선(p) x (1 +- 변이).
// 같은 조건의 과실들은 최대 생체중이 변이 폭 안에서 서로 달라요. 아직 안 핀 꽃은 판정하지 않아요
TEST_CASE("Anthesis judgement sets pollination and varied potential size per fruit", "[unit][m4][fruit]") {
    const sim::Params p = test::test_params();
    const sim::FruitParams& fp = p.fruit;
    sim::Crop crop;
    sim::Pcg32 rng = test::biology_rng(9);
    process::create_flowers(crop, 0, 600.0, fp, 0, rng);
    const std::vector<sim::Fruit> flowers = crop.fruits;
    constexpr std::uint64_t kJudgeTick = 120;  // 5일째: 지연이 짧은 꽃만 핌
    process::judge_anthesis(crop, fp, 18.0, kJudgeTick, rng);
    std::size_t judged = 0;
    std::size_t waiting = 0;
    for (const sim::Fruit& f : crop.fruits) {
        if (!f.set) {
            ++waiting;
            CHECK(f.anthesis_tick > kJudgeTick);
            CHECK(f.pollination_index == 0.0);
            CHECK(f.potential_fresh_g == 0.0);
        } else {
            ++judged;
            CHECK(f.anthesis_tick <= kJudgeTick);
        }
    }
    CHECK(judged > 0);
    CHECK(waiting > 0);
    // 안 핀 꽃은 그대로 목록에 남아 있어요 (18 °C에서는 낙화가 없어서 판정된 꽃도 모두 남음)
    CHECK(crop.fruits.size() == flowers.size());

    process::judge_anthesis(crop, fp, 18.0, 100000, rng);
    REQUIRE(crop.fruits.size() == flowers.size());
    const double mu = fp.pollination_mean_curve(18.0);
    std::array<double, 5> lo;
    std::array<double, 5> hi;
    lo.fill(1.0e9);
    hi.fill(0.0);
    for (const sim::Fruit& f : crop.fruits) {
        REQUIRE(f.set);
        CHECK(f.pollination_index >= std::max(0.0, mu - fp.pollination_spread) - 1e-12);
        CHECK(f.pollination_index <= std::min(1.0, mu + fp.pollination_spread) + 1e-12);
        const double nominal = fp.rank_potential_fresh_g[f.rank] * fp.pollination_size_curve(f.pollination_index);
        CHECK(f.potential_fresh_g >= nominal * (1.0 - fp.size_variation_fraction) - 1e-12);
        CHECK(f.potential_fresh_g <= nominal * (1.0 + fp.size_variation_fraction) + 1e-12);
        CHECK(f.dry_g == 0.0);
        CHECK(f.development_index == 0.0);
        lo[f.rank] = std::min(lo[f.rank], f.potential_fresh_g / nominal);
        hi[f.rank] = std::max(hi[f.rank], f.potential_fresh_g / nominal);
    }
    // 과위 4(가장 많음): 변이가 실제로 있어요 (배율이 한 값이 아님)
    CHECK(hi[3] - lo[3] > 0.1);
    // 과위 순서: 1번과가 가장 크고 갈수록 작아요
    std::array<double, 5> mean_size{};
    std::array<int, 5> n{};
    for (const sim::Fruit& f : crop.fruits) {
        mean_size[f.rank] += f.potential_fresh_g;
        ++n[f.rank];
    }
    for (std::size_t r = 1; r < 5; ++r) {
        CHECK(mean_size[r] / n[r] < mean_size[r - 1] / n[r - 1]);
    }
    // 같은 시드로 같은 판정을 반복하면 같은 결과 (결정론)
    sim::Crop copy_a;
    sim::Crop copy_b;
    for (sim::Crop* c : {&copy_a, &copy_b}) {
        sim::Pcg32 r2 = test::biology_rng(9);
        process::create_flowers(*c, 0, 600.0, fp, 0, r2);
        process::judge_anthesis(*c, fp, 12.0, 100000, r2);
    }
    CHECK(copy_a.fruits == copy_b.fruits);
}

// ---- 진행·크기 구간 도우미 ----

TEST_CASE("Fruit development increment and size classes follow the formulas", "[unit][m4][fruit]") {
    const sim::FruitParams fp = test::test_params().fruit;
    // (기온 - 5) / 24 / 350, 5 °C 이하는 0
    CHECK(process::fruit_development_increment(fp, 20.0) == Approx(15.0 / 24.0 / 350.0));
    CHECK(process::fruit_development_increment(fp, 5.0) == 0.0);
    CHECK(process::fruit_development_increment(fp, -10.0) == 0.0);
    CHECK(process::fruit_development_increment(fp, 15.0) == Approx(0.5 * process::fruit_development_increment(fp, 25.0)));
    // 크기 구간: 경계 [10, 15, 20, 25] -> 5개 구간, 경계 이상이면 다음 구간
    CHECK(process::size_class_index(fp, 0.0) == 0);
    CHECK(process::size_class_index(fp, 9.99) == 0);
    CHECK(process::size_class_index(fp, 10.0) == 1);
    CHECK(process::size_class_index(fp, 14.99) == 1);
    CHECK(process::size_class_index(fp, 15.0) == 2);
    CHECK(process::size_class_index(fp, 24.99) == 3);
    CHECK(process::size_class_index(fp, 25.0) == 4);
    CHECK(process::size_class_index(fp, 100.0) == 4);
}

// ---- 총생산·호흡·분배 (3·4번) ----

// 과실 없음: 생장 전부 영양 기관으로, 비율 0.6/0.15/0.25
TEST_CASE("Without fruit all net growth goes to leaf crown and root in the vegetative fractions",
          "[unit][m4][fruit][allocation]") {
    const sim::Params p = test::quiet_params();  // 호흡·낙엽 0: 순생장 = 총생산
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    const sim::Crop before = crop;
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(1);
    (void)tick_crop(crop, cell, p, kDaySolar_W_per_m2, kAir_degC, rng, ledger);
    const double net = crop.last_growth_g_per_m2;
    REQUIRE(net > 0.0);
    CHECK(crop.last_gross_g_per_m2 == Approx(net));
    CHECK(crop.last_respiration_g_per_m2 == 0.0);
    CHECK_THAT(crop.leaf_g_per_m2 - before.leaf_g_per_m2, WithinRel(net * p.crop.vegetative_leaf_fraction, 1e-12));
    CHECK_THAT(crop.crown_g_per_m2 - before.crown_g_per_m2, WithinRel(net * p.crop.vegetative_crown_fraction, 1e-12));
    CHECK_THAT(crop.root_g_per_m2 - before.root_g_per_m2, WithinRel(net * p.crop.vegetative_root_fraction, 1e-12));
    CHECK(p.crop.vegetative_leaf_fraction + p.crop.vegetative_crown_fraction + p.crop.vegetative_root_fraction == 1.0);
    CHECK(crop.last_supply_ratio == 1.0);  // 수요 0이면 공급 비율 1
    // 총생산에 활착 배율이 곱해져요: 활착 0이면 정식 몸살로 줄어요
    sim::SoilCell cell2 = test::test_cell();
    sim::Crop fresh = process::plant_crop(test::test_crop_init(), p.crop);
    fresh.establishment_index = 0.0;
    sim::Ledger ledger2;
    sim::Pcg32 rng2 = test::biology_rng(1);
    (void)tick_crop(fresh, cell2, p, kDaySolar_W_per_m2, kAir_degC, rng2, ledger2);
    CHECK(fresh.last_gross_g_per_m2 < 0.35 * crop.last_gross_g_per_m2);
    CHECK(fresh.last_gross_g_per_m2 > 0.29 * crop.last_gross_g_per_m2);
}

// 과실 수요 > 생장 x 0.65: 공급 비율 < 1, 과실 몫 = 생장 x 0.65, 나머지 0.35는 영양 기관에
TEST_CASE("Fruit demand above the share cap gives a supply ratio below one", "[unit][m4][fruit][allocation]") {
    const sim::Params p = test::quiet_params();
    const sim::FruitParams& fp = p.fruit;
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = crop_with_young_fruits(p, 80, 0.5, 0.0, 0, 4);  // 400개
    const sim::Crop before = crop;
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(1);
    (void)tick_crop(crop, cell, p, kDaySolar_W_per_m2, kAir_degC, rng, ledger);
    const double net = crop.last_growth_g_per_m2;
    REQUIRE(net > 0.0);
    // 수요 = Sigma (새 진행도의 목표 건물 - 지금 건물), 목표 = 최대 생체중 x 건물 비율 x shape(새 진행도)
    const double dD = process::fruit_development_increment(fp, kAir_degC);
    const double target_each = 20.0 * fp.dry_matter_fraction * fp.growth_shape_curve(0.5 + dD);
    const double demand = 400.0 * target_each;
    REQUIRE(demand > net * p.crop.max_fruit_assimilate_fraction);
    const double fruit_supply = net * p.crop.max_fruit_assimilate_fraction;
    CHECK(crop.last_supply_ratio < 1.0);
    CHECK(crop.last_supply_ratio == Approx(fruit_supply / demand));
    CHECK(crop.fruit_g_per_m2() == Approx(fruit_supply));
    // 나머지는 영양 기관에 0.6 / 0.15 / 0.25
    const double rest = net - fruit_supply;
    CHECK(crop.leaf_g_per_m2 - before.leaf_g_per_m2 == Approx(rest * 0.6));
    CHECK(crop.crown_g_per_m2 - before.crown_g_per_m2 == Approx(rest * 0.15));
    CHECK(crop.root_g_per_m2 - before.root_g_per_m2 == Approx(rest * 0.25));
    // 크기 충족도(틱) = Sigma 건물 / Sigma 목표 = 공급 비율 -> 이동 평균이 시작값 1에서 그쪽으로 조금 움직여요
    const double ratio = fruit_supply / demand;
    CHECK(crop.size_satisfaction_mean ==
          Approx(1.0 + (ratio - 1.0) * p.house.temp_mean_relax_rate_per_hour));
    // 과실 부담: 과실이 적으면 과실 하나하나가 더 크게 자라고 영양 기관 몫도 커요
    sim::SoilCell cell_few = test::test_cell();
    sim::Crop few = crop_with_young_fruits(p, 8, 0.5, 0.0, 0, 4);  // 40개
    sim::Ledger ledger_few;
    sim::Pcg32 rng_few = test::biology_rng(1);
    (void)tick_crop(few, cell_few, p, kDaySolar_W_per_m2, kAir_degC, rng_few, ledger_few);
    CHECK(few.fruits[0].dry_g > crop.fruits[0].dry_g);
}

// 진행도 0 -> 1, 공급 1: 과실 건물 = 최대 생체중 x 건물 비율 (수확된 생체중 = 최대 생체중)
TEST_CASE("A fully supplied fruit reaching progress one weighs its potential size", "[unit][m4][fruit][allocation]") {
    const sim::Params p = test::quiet_params();
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    sim::Fruit f;
    f.set = true;
    f.rank = 1;
    f.potential_fresh_g = 2.0;
    f.development_index = 1.0 - 1e-9;  // 이번 틱에 진행도 1
    crop.fruits.push_back(f);
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(1);
    const auto result = tick_crop(crop, cell, p, kDaySolar_W_per_m2, kAir_degC, rng, ledger, 77);
    REQUIRE(result.harvested.has_value());
    CHECK(result.harvested->fruits == 1);
    CHECK(result.harvested->deformed == 0);
    CHECK(result.harvested->fresh_g == Approx(2.0));
    CHECK(crop.harvest.harvested_fresh_g == Approx(2.0));
    CHECK(crop.harvest.harvested_fruits == 1);
    CHECK(crop.fruits.empty());
    CHECK(crop.harvest.has_first_harvest);
    CHECK(crop.harvest.first_harvest_tick == 77);
}

// 한 틱 공급을 못 채움 뒤 다음 틱 공급 충분: 못 채운 몫이 다음 틱 수요에 더해져(따라잡기) 목표 쪽으로 따라잡음.
// 크기 충족도 이동 평균은 순생장 > 0인 틱에만 갱신
TEST_CASE("A fruit catches up its missed supply and satisfaction updates only on positive growth",
          "[unit][m4][fruit][allocation]") {
    const sim::Params p = test::quiet_params();
    const sim::FruitParams& fp = p.fruit;
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    sim::Fruit f;
    f.set = true;
    f.rank = 1;
    f.potential_fresh_g = 2.0;
    f.development_index = 0.5;
    crop.fruits.push_back(f);
    crop.size_satisfaction_mean = 0.5;
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(1);
    const double dD = process::fruit_development_increment(fp, kAir_degC);

    // 1틱째 밤: 순생장 0 -> 공급 0, 건물 그대로, 진행도만 올라가고, 크기 충족도 평균은 그대로
    (void)tick_crop(crop, cell, p, 0.0, kAir_degC, rng, ledger, 0);
    CHECK(crop.last_growth_g_per_m2 <= 0.0);
    CHECK(crop.fruits[0].dry_g == 0.0);
    CHECK(crop.fruits[0].development_index == Approx(0.5 + dD));
    CHECK(crop.size_satisfaction_mean == 0.5);

    // 2틱째 낮: 공급이 충분하면 2틱 분(새 진행도 기준 목표 - 지금 0)을 한 번에 따라잡아요
    (void)tick_crop(crop, cell, p, kDaySolar_W_per_m2, kAir_degC, rng, ledger, 1);
    REQUIRE(crop.last_growth_g_per_m2 > 0.0);
    const double target = 2.0 * fp.dry_matter_fraction * fp.growth_shape_curve(0.5 + 2.0 * dD);
    CHECK(crop.fruits[0].dry_g == Approx(target));
    // 틱 증가분 몫만 달라고 하는 식이었다면 shape 차이만큼만 컸을 거예요
    const double increment_only = 2.0 * fp.dry_matter_fraction *
                                  (fp.growth_shape_curve(0.5 + 2.0 * dD) - fp.growth_shape_curve(0.5 + dD));
    CHECK(crop.fruits[0].dry_g > 5.0 * increment_only);
    CHECK(crop.last_supply_ratio == 1.0);
    // 충족도(틱) = 1 -> 평균이 0.5에서 1쪽으로 temp_mean_relax_rate만큼
    CHECK(crop.size_satisfaction_mean == Approx(0.5 + 0.5 * p.house.temp_mean_relax_rate_per_hour));
}

// 구현 규칙 7: 호흡 부족분이 영양 기관 건물보다 크면 잎·관부·뿌리를 0까지만 줄이고 나머지는 버려요 (CO2 수지는 총생산 - 호흡)
TEST_CASE("A respiration deficit larger than the vegetative mass empties the organs but never goes negative",
          "[unit][m4][fruit][allocation][extreme]") {
    sim::Params p = test::quiet_params();
    p.crop.maintenance_rate_leaf_per_hour = 1.0e6;
    p.crop.maintenance_rate_crown_per_hour = 1.0e6;
    p.crop.maintenance_rate_root_per_hour = 1.0e6;
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    sim::Fruit f;  // 과실은 줄이지 않아요
    f.set = true;
    f.potential_fresh_g = 20.0;
    f.development_index = 0.5;
    f.dry_g = 0.6;
    crop.fruits.push_back(f);
    const double plant_N = crop.plant_N_g_per_m2;
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(1);
    const double vegetative = crop.leaf_g_per_m2 + crop.crown_g_per_m2 + crop.root_g_per_m2;
    (void)tick_crop(crop, cell, p, 0.0, kAir_degC, rng, ledger);
    CHECK(crop.last_respiration_g_per_m2 > 100.0 * vegetative);
    CHECK(crop.leaf_g_per_m2 == 0.0);
    CHECK(crop.crown_g_per_m2 == 0.0);
    CHECK(crop.root_g_per_m2 == 0.0);
    CHECK(crop.fruits[0].dry_g == 0.6);
    CHECK(crop.plant_N_g_per_m2 == plant_N);  // 체내 양분은 그대로
    // CO2 수지는 총생산 - 호흡 그대로 (버려지는 몫까지 포함해서 음수 순생장)
    CHECK(crop.last_growth_g_per_m2 == Approx(crop.last_gross_g_per_m2 - crop.last_respiration_g_per_m2));
    CHECK(crop.last_growth_g_per_m2 < -100.0 * vegetative);
    CHECK(std::isfinite(crop.last_growth_g_per_m2));
    // 한 번 더: 건물이 이미 0이라도 유한하고 음수 없음
    (void)tick_crop(crop, cell, p, 0.0, kAir_degC, rng, ledger);
    CHECK(crop.leaf_g_per_m2 == 0.0);
    CHECK(std::isfinite(crop.last_growth_g_per_m2));
    CHECK(crop.alive);
}

// ---- 낙과 (6b) ----

// 크기 충족도 0.4 대 1.0: 확률 = 속도 x 진행도 증가분 x 충족도 곡선 x 과위 배율. 0.4 쪽이 훨씬 많이 떨어지고,
// 높은 과위(늦게 핀 작은 과실)가 더 잘 떨어짐
TEST_CASE("Young fruit abortion follows the per development index formula with rank weights",
          "[unit][m4][fruit][abortion]") {
    const sim::Params p = test::quiet_params();
    constexpr int kPerRank = 6000;
    const auto run = [&](double satisfaction) {
        sim::Crop crop = crop_with_young_fruits(p, kPerRank);
        crop.size_satisfaction_mean = satisfaction;
        sim::SoilCell cell = test::test_cell();
        sim::Ledger ledger;
        sim::Pcg32 rng = test::biology_rng(3);
        (void)tick_crop(crop, cell, p, 0.0, kAir_degC, rng, ledger);  // 밤: 공급 0, 진행도만 올라감
        std::array<double, 5> aborted{};
        aborted.fill(static_cast<double>(kPerRank));
        for (const sim::Fruit& f : crop.fruits) {
            aborted[f.rank] -= 1.0;
        }
        CHECK(crop.size_satisfaction_mean == satisfaction);  // 순생장 <= 0이라 평균 그대로
        double total = 0.0;
        for (const double a : aborted) {
            total += a;
        }
        CHECK(static_cast<double>(crop.harvest.aborted_fruits) == total);
        return aborted;
    };
    const auto low = run(0.4);
    const auto high = run(1.0);
    double low_total = 0.0;
    double high_total = 0.0;
    for (std::uint32_t rank = 0; rank < 5; ++rank) {
        for (const auto& [sat, counts] : {std::pair{0.4, low}, std::pair{1.0, high}}) {
            const double n = kPerRank;
            const double expected = expected_abortions(p, sat, rank, n);
            const double p_each = expected / n;
            INFO("rank " << rank << " satisfaction " << sat << " expected " << expected);
            CHECK(std::abs(counts[rank] - expected) <= 5.0 * std::sqrt(n * p_each * (1.0 - p_each)) + 2.0);
        }
        low_total += low[rank];
        high_total += high[rank];
    }
    // 충족도 1.0에서도 곡선 하한(0.1)만큼은 떨어져서 명세 표의 "0.4 쪽만"은 "0.4 쪽이 훨씬 많이"로 바뀌어요
    CHECK(low_total > 5.0 * high_total);
    // 높은 과위일수록 잘 떨어짐: 과위 0(가중 0.1) < 과위 2(1.0) < 과위 4(1.8)
    CHECK(low[0] < low[2]);
    CHECK(low[2] < low[4]);
    CHECK(low[1] < low[3]);
}

// 진행도가 낙과 구간(0.35) 이상이면 충족도가 낮아도 떨어지지 않음. 꽃(set = false)도 떨어지지 않음
TEST_CASE("Abortion only affects set fruit below the abortion window", "[unit][m4][fruit][abortion]") {
    const sim::Params p = test::quiet_params();
    sim::Crop crop = crop_with_young_fruits(p, 2000, 0.5);  // 진행도 0.5: 구간 밖
    for (int k = 0; k < 500; ++k) {                         // 아직 안 핀 꽃
        sim::Fruit flower;
        flower.rank = 4;
        flower.anthesis_tick = 100000;
        crop.fruits.push_back(flower);
    }
    const std::size_t before = crop.fruits.size();
    crop.size_satisfaction_mean = 0.0;  // 가장 높은 확률 배율
    sim::SoilCell cell = test::test_cell();
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(3);
    (void)tick_crop(crop, cell, p, 0.0, kAir_degC, rng, ledger);
    CHECK(crop.fruits.size() == before);
    CHECK(crop.harvest.aborted_fruits == 0);
}

// 떨어진 과실의 건물·양분은 토양으로 (원소·C 보존, 수확 아님)
TEST_CASE("Aborted fruit returns its dry matter and nutrients to the soil", "[unit][m4][fruit][abortion][conservation]") {
    const sim::Params p = test::quiet_params();
    sim::Crop crop = crop_with_young_fruits(p, 2000, 0.1, 0.3);
    crop.size_satisfaction_mean = 0.0;
    sim::SoilCell cell = test::test_cell();
    const sim::SoilCell cell_before = cell;
    const sim::Crop before = crop;
    const Totals initial = soil_plus_plant(cell, crop);
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(3);
    (void)tick_crop(crop, cell, p, 0.0, kAir_degC, rng, ledger);
    REQUIRE(crop.harvest.aborted_fruits > 0);
    const double lost_dry = before.fruit_g_per_m2() - crop.fruit_g_per_m2();
    CHECK(lost_dry == Approx(0.3 * static_cast<double>(crop.harvest.aborted_fruits)));
    CHECK(ledger.C_leaf_litter_g == Approx(lost_dry * p.crop.dry_matter_C_fraction).epsilon(1e-9));
    CHECK(cell.labile_C_g_per_m2 - cell_before.labile_C_g_per_m2 == Approx(ledger.C_leaf_litter_g).epsilon(1e-9));
    const Totals now = soil_plus_plant(cell, crop);
    CHECK(now.N == Approx(initial.N).epsilon(1e-12));
    CHECK(now.K == Approx(initial.K).epsilon(1e-12));
    CHECK(now.Ca == Approx(initial.Ca).epsilon(1e-12));
    CHECK(ledger.N_harvested_g == 0.0);
    CHECK(ledger.K_harvested_g == 0.0);
    CHECK(cell.labile_N_g_per_m2 > cell_before.labile_N_g_per_m2);
}

// 같은 충족도 부족, 진행 속도 2배 차이: 어린 과실이 떨어지는 비율은 거의 같음 (확률이 진행도 증가분 기준)
TEST_CASE("Abortion share does not depend on how fast the fruit progresses", "[unit][m4][fruit][abortion]") {
    const sim::Params p = test::quiet_params();
    const sim::FruitParams& fp = p.fruit;
    constexpr int kPerRank = 3000;
    const auto aborted_share = [&](double air_degC, std::uint32_t rank) {
        sim::Crop crop = crop_with_young_fruits(p, kPerRank, 0.1, 0.0, rank, rank);
        crop.size_satisfaction_mean = 0.4;
        sim::SoilCell cell = test::test_cell();
        sim::Ledger ledger;
        sim::Pcg32 rng = test::biology_rng(5);
        // 모든 과실의 진행도가 낙과 구간(0.35)을 지날 때까지 밤 틱을 돌려요
        int ticks = 0;
        const auto still_in_window = [&] {
            return std::any_of(crop.fruits.begin(), crop.fruits.end(),
                               [&](const sim::Fruit& f) { return f.development_index < fp.abortion_window_index; });
        };
        while (still_in_window() && ticks < 2000) {
            (void)tick_crop(crop, cell, p, 0.0, air_degC, rng, ledger, static_cast<std::uint64_t>(ticks));
            ++ticks;
        }
        REQUIRE(ticks < 2000);
        return std::pair{static_cast<double>(crop.harvest.aborted_fruits) / kPerRank, ticks};
    };
    for (const std::uint32_t rank : {2u, 4u}) {
        const auto [slow, slow_ticks] = aborted_share(10.0, rank);  // 적산온도 5: 진행 속도 절반
        const auto [fast, fast_ticks] = aborted_share(15.0, rank);  // 적산온도 10
        INFO("rank " << rank << " slow " << slow << " fast " << fast);
        CHECK(slow_ticks > static_cast<int>(1.8 * fast_ticks));  // 진행 속도가 정말 2배 차이
        CHECK(slow > 0.1);
        CHECK(std::abs(slow - fast) < 0.05);
    }
}

// ---- 적화·적과 (6c) ----

// ThinFlowers 포기당 3개: 칸마다 24개를 높은 과위·늦게 핀 것부터 제거, 진행도 0.3 이상은 남음
TEST_CASE("ThinFlowers removes about per_plant times plants from the highest rank and keeps developed fruit",
          "[unit][m4][fruit][thin]") {
    const sim::Params p = test::test_params();
    sim::Crop crop = crop_with_young_fruits(p, 10);  // 과위 0~4, 과위별 10개 (개화 틱 0~9)
    for (auto& f : crop.fruits) {
        if (f.rank == 4 && f.anthesis_tick < 5) {
            f.development_index = 0.5;  // 딸 수 없는 과실
        }
    }
    const std::size_t before = crop.fruits.size();
    sim::SoilCell cell = test::test_cell();
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(3);
    const auto removed = process::thin_flowers(crop, cell, p, 3.0, rng, ledger);
    CHECK(removed == 24);  // 3.0 x 8포기
    CHECK(crop.fruits.size() == before - 24);
    CHECK(crop.harvest.thinned_fruits == 24);
    std::array<int, 5> left{};
    int rank4_old = 0;
    int rank4_young = 0;
    for (const auto& f : crop.fruits) {
        ++left[f.rank];
        if (f.rank == 4) {
            (f.development_index >= p.fruit.thinnable_max_index ? rank4_old : rank4_young) += 1;
        }
    }
    CHECK(rank4_young == 0);  // 과위 4의 어린 것 5개를 먼저
    CHECK(rank4_old == 5);    // 진행도 0.3 이상은 남음
    CHECK(left[3] == 0);      // 다음 과위 3의 10개
    CHECK(left[2] == 1);      // 남은 9개는 과위 2에서 늦게 핀 것부터: 가장 일찍 핀 1개만 남음
    CHECK(left[1] == 10);
    CHECK(left[0] == 10);
    for (const auto& f : crop.fruits) {
        if (f.rank == 2) {
            CHECK(f.anthesis_tick == 0);
        }
    }
}

// 포기당 개수 x 포기 수가 정수가 아니면 난수 반올림. 0이면 아무것도 안 따고, 아주 크면 딸 수 있는 전부(진행도 0.3 미만)
TEST_CASE("ThinFlowers rounds fractional counts with the biology stream and respects the limit",
          "[unit][m4][fruit][thin]") {
    const sim::Params p = test::test_params();
    bool saw_two = false;
    bool saw_three = false;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        sim::Crop crop = crop_with_young_fruits(p, 10);
        sim::SoilCell cell = test::test_cell();
        sim::Ledger ledger;
        sim::Pcg32 rng = test::biology_rng(seed);
        const auto removed = process::thin_flowers(crop, cell, p, 0.3125, rng, ledger);  // 2.5개
        CHECK((removed == 2 || removed == 3));
        saw_two = saw_two || removed == 2;
        saw_three = saw_three || removed == 3;
    }
    CHECK(saw_two);
    CHECK(saw_three);

    sim::Crop none = crop_with_young_fruits(p, 10);
    sim::SoilCell cell = test::test_cell();
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(1);
    CHECK(process::thin_flowers(none, cell, p, 0.0, rng, ledger) == 0);
    CHECK(none.fruits.size() == 50);

    sim::Crop all = crop_with_young_fruits(p, 10);
    for (std::size_t i = 0; i < all.fruits.size(); i += 2) {
        all.fruits[i].development_index = 0.6;  // 딸 수 없는 절반
    }
    CHECK(process::thin_flowers(all, cell, p, 1000.0, rng, ledger) == 25);
    CHECK(all.fruits.size() == 25);
    for (const auto& f : all.fruits) {
        CHECK(f.development_index >= p.fruit.thinnable_max_index);
    }
}

// 구현 규칙 3: 딸 수 있는 것은 개화가 시작된 화방의 개별 꽃(아직 안 핀 것 포함)과 진행도 0.3 미만의 어린 과실.
// 아직 개화하지 않은 화방의 꽃은 개체가 생기기 전이라 딸 수 없어요
TEST_CASE("ThinFlowers takes only individual flowers of opened trusses not unopened truss flowers",
          "[unit][m4][fruit][thin]") {
    const sim::Params p = test::test_params();
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    // 안 핀 화방 (꽃 수만 있고 개체 없음)
    sim::Truss unopened;
    unopened.flowers_per_m2 = 20.0;
    crop.trusses.push_back(unopened);
    sim::Truss opened;
    opened.flowers_per_m2 = 10.0;
    opened.opened = true;
    crop.trusses.push_back(opened);
    // 개화한 화방(1번)의 개체: 늦게 필 꽃 5개(set = false, 개화 틱 100 이후)와 착과된 어린 과실 5개(개화 틱 0~4), 모두 과위 4
    for (int k = 0; k < 5; ++k) {
        sim::Fruit flower;
        flower.truss_index = 1;
        flower.rank = 4;
        flower.anthesis_tick = 100 + static_cast<std::uint64_t>(k);
        crop.fruits.push_back(flower);
        sim::Fruit young;
        young.truss_index = 1;
        young.rank = 4;
        young.anthesis_tick = static_cast<std::uint64_t>(k);
        young.set = true;
        young.potential_fresh_g = 10.0;
        young.development_index = 0.1;
        young.dry_g = 0.1;
        crop.fruits.push_back(young);
    }
    sim::SoilCell cell = test::test_cell();
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(1);
    // 5개(= 0.625 x 8): 가장 늦게 핀 것부터라서 아직 안 핀 꽃 5개가 먼저 나가요
    CHECK(process::thin_flowers(crop, cell, p, 0.625, rng, ledger) == 5);
    REQUIRE(crop.fruits.size() == 5);
    for (const auto& f : crop.fruits) {
        CHECK(f.set);
    }
    CHECK(ledger.C_leaf_litter_g == 0.0);  // 꽃은 건물이 없어서 돌려줄 것이 없어요
    // 다시 아주 많이: 남은 어린 과실만 모두 나가고, 안 핀 화방의 꽃 수는 그대로
    CHECK(process::thin_flowers(crop, cell, p, 1000.0, rng, ledger) == 5);
    CHECK(crop.fruits.empty());
    REQUIRE(crop.trusses.size() == 2);
    CHECK(crop.trusses[0].flowers_per_m2 == 20.0);
    CHECK(crop.trusses[0].development_index == 0.0);
    CHECK(crop.harvest.thinned_fruits == 10);
    CHECK(ledger.C_leaf_litter_g == Approx(5 * 0.1 * p.crop.dry_matter_C_fraction));
}

// 딴 꽃·어린 과실의 건물·양분은 낙엽 규칙으로 토양에 (원소·C 보존, 수확이 아님)
TEST_CASE("Thinned fruit returns to the soil under the litter rule", "[unit][m4][fruit][thin][conservation]") {
    const sim::Params p = test::test_params();
    sim::Crop crop = crop_with_young_fruits(p, 10, 0.1, 0.4);
    sim::SoilCell cell = test::test_cell();
    const sim::SoilCell cell_before = cell;
    const Totals initial = soil_plus_plant(cell, crop);
    const double dry_before = crop.fruit_g_per_m2();
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(1);
    const auto removed = process::thin_flowers(crop, cell, p, 2.0, rng, ledger);  // 16개
    REQUIRE(removed == 16);
    const double lost_dry = dry_before - crop.fruit_g_per_m2();
    CHECK(lost_dry == Approx(16.0 * 0.4));
    CHECK(ledger.C_leaf_litter_g == Approx(lost_dry * p.crop.dry_matter_C_fraction).epsilon(1e-9));
    CHECK(cell.labile_C_g_per_m2 - cell_before.labile_C_g_per_m2 == Approx(ledger.C_leaf_litter_g).epsilon(1e-9));
    const Totals now = soil_plus_plant(cell, crop);
    CHECK(now.N == Approx(initial.N).epsilon(1e-12));
    CHECK(now.K == Approx(initial.K).epsilon(1e-12));
    CHECK(ledger.N_harvested_g == 0.0);
}

// 적과한 칸 대 안 한 칸: 적과 쪽 남은 과실 평균 크기가 큼 (공급이 남은 과실에 나뉘어요).
// 낙과는 따로 보므로 끄고, 적과 효과만 비교해요.
TEST_CASE("A thinned tile grows its remaining fruit larger", "[unit][m4][fruit][thin]") {
    sim::Params p = test::quiet_params();
    p.fruit.abortion_rate_per_development_index = 0.0;
    const auto mean_dry_per_fruit_after_three_days = [&](bool thin) {
        sim::Crop crop = crop_with_young_fruits(p, 30, 0.2);  // 150개, 진행도 0.2 (딸 수 있음)
        sim::SoilCell cell = test::test_cell();
        sim::Ledger ledger;
        sim::Pcg32 rng = test::biology_rng(4);
        if (thin) {
            CHECK(process::thin_flowers(crop, cell, p, 8.0, rng, ledger) == 64);  // 8 x 8포기
        }
        for (int t = 0; t < 72; ++t) {
            const double solar = (t % 24 >= 6 && t % 24 < 18) ? kDaySolar_W_per_m2 : 0.0;
            (void)tick_crop(crop, cell, p, solar, kAir_degC, rng, ledger, static_cast<std::uint64_t>(t));
        }
        REQUIRE_FALSE(crop.fruits.empty());
        CHECK(crop.harvest.aborted_fruits == 0);
        return crop.fruit_g_per_m2() / static_cast<double>(crop.fruits.size());
    };
    CHECK(mean_dry_per_fruit_after_three_days(true) > mean_dry_per_fruit_after_three_days(false));
}

TEST_CASE("ThinFlowers command is validated at enqueue", "[unit][m4][fruit][thin][world]") {
    sim::World world = test::make_world();  // 2 x 3 칸
    CHECK(world.enqueue(sim::ThinFlowersCommand{.area = sim::whole_grid(2, 3), .per_plant = 3.0}));
    CHECK(world.enqueue(sim::ThinFlowersCommand{.area = sim::whole_grid(2, 3), .per_plant = 0.0}));
    CHECK_FALSE(world.enqueue(sim::ThinFlowersCommand{.area = sim::whole_grid(2, 3), .per_plant = -1.0}));
    CHECK_FALSE(world.enqueue(sim::ThinFlowersCommand{.area = sim::TileArea{0, 0, 3, 3}, .per_plant = 1.0}));
    CHECK_FALSE(world.enqueue(sim::ThinFlowersCommand{.area = sim::TileArea{0, 0, 0, 1}, .per_plant = 1.0}));
    world.step(1);  // 작물이 없어도 안전하게 적용
    CHECK(world.snapshot().crops[0].harvest.thinned_fruits == 0);
}

// ---- 당도 온도 점수 (주·야간 이중 가중) ----

// 곡선: 낮 23 °C 대 30 °C(밤 같음): 23 °C 쪽 점수 높음. 밤 8.5 °C 대 15 °C(낮 같음): 8.5 °C 쪽 높음. 밤 2 °C: 낮은 점수
TEST_CASE("Brix temperature score prefers 23 C days and 8.5 C nights", "[unit][m4][fruit][brix]") {
    const sim::FruitParams fp = test::test_params().fruit;
    auto k_temp = [&](double day_degC, double night_degC) {
        const double day = fp.brix_day_temp_score_curve(day_degC);
        const double night = fp.brix_night_temp_score_curve(night_degC);
        return fp.brix_temp_score_curve(fp.brix_day_weight * day + fp.brix_night_weight * night);
    };
    CHECK(k_temp(23.0, 10.0) > k_temp(30.0, 10.0));
    CHECK(k_temp(23.0, 8.5) > k_temp(23.0, 15.0));
    CHECK(fp.brix_night_temp_score_curve(2.0) < 0.2);  // 전류 저하
    CHECK(fp.brix_day_temp_score_curve(23.0) == 1.0);
    CHECK(fp.brix_night_temp_score_curve(8.5) == 1.0);
    CHECK(fp.brix_night_temp_score_curve(17.0) == 0.0);  // 호흡 소모: 약 17 °C 이상 0
    CHECK(fp.brix_day_weight + fp.brix_night_weight == 1.0);
}

namespace {

struct Ripened {
    sim::Crop crop;
    std::uint64_t ticks = 0;
    double brix = 0.0;
    double firmness = 0.0;
    double k_temp = 0.0;
    double day_score = 0.0;
    double night_score = 0.0;
    double sufficiency_K = 0.0;
    double sufficiency_Ca = 0.0;
    double relative_water = 0.0;
};

// 익기 직전(진행도 start)의 과실 하나를 낮/밤 기온 패턴으로 익혀요. 호흡·낙엽을 끄고 물을 0으로 둬서(총생산 0)
// 작물이 자라지 않으므로 품질 계수(체내 양분 충족도 등)가 틱마다 같고, 당도 차이는 온도 점수에서만 나와요.
// tweak: 시작 작물을 바꾸는 함수 (체내 K·Ca 충족도 등)
Ripened ripen_one_fruit(double day_degC, double night_degC, double start_development = 0.8,
                        const std::function<void(sim::Crop&, const sim::CropParams&)>& tweak = nullptr) {
    const sim::Params p = test::quiet_params();
    sim::SoilCell cell = test::test_cell();
    cell.water_mm = 0.0;
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    sim::Fruit f;
    f.set = true;
    f.rank = 1;
    f.potential_fresh_g = 20.0;
    f.dry_g = 1.0;  // 생체중 약 11.1 g, 공급이 0이라 그대로
    f.development_index = start_development;
    crop.fruits.push_back(f);
    if (tweak) {
        tweak(crop, p.crop);
    }
    Ripened result;
    const auto status = process::nutrient_status(crop, p.crop);
    result.sufficiency_K = status.sufficiency_K;
    result.sufficiency_Ca = status.sufficiency_Ca;
    result.relative_water = cell.relative_water();
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(8);
    std::uint64_t tick = 0;
    while (crop.harvest.harvested_fruits == 0 && tick < 6000) {
        const int hour = static_cast<int>(tick % 24);
        const bool day = hour >= 6 && hour < 18;
        (void)tick_crop(crop, cell, p, day ? kDaySolar_W_per_m2 : 0.0, day ? day_degC : night_degC, rng, ledger, tick);
        ++tick;
    }
    REQUIRE(crop.harvest.harvested_fruits == 1);
    result.ticks = tick;
    const double fresh = crop.harvest.harvested_fresh_g;
    result.brix = crop.harvest.harvested_brix_weighted / fresh;
    result.firmness = crop.harvest.harvested_firmness_weighted / fresh;
    result.k_temp = crop.harvest.harvested_temp_score_weighted / fresh;
    result.day_score = crop.harvest.harvested_day_score_weighted / fresh;
    result.night_score = crop.harvest.harvested_night_score_weighted / fresh;
    result.crop = std::move(crop);
    return result;
}

}  // namespace

// 수확 기록의 낮·밤·합산 온도 점수가 식 그대로: K_temp = 0.6 x 낮 평균 + 0.4 x 밤 평균 (낮은 일사 가중, 밤은 시간 가중).
// 당도 = 기준 당도 x 품질 계수 x 점수 곡선(K_temp). 낮 23 °C가 30 °C보다, 밤 8.5 °C가 15 °C보다 달아요
TEST_CASE("Harvested brix follows the dual day and night temperature score", "[unit][m4][fruit][brix]") {
    const sim::FruitParams fp = test::test_params().fruit;
    const auto expect_scores = [&](const Ripened& r, double day_degC, double night_degC) {
        CHECK(r.day_score == Approx(fp.brix_day_temp_score_curve(day_degC)).epsilon(1e-9));
        CHECK(r.night_score == Approx(fp.brix_night_temp_score_curve(night_degC)).epsilon(1e-9));
        CHECK(r.k_temp == Approx(fp.brix_day_weight * r.day_score + fp.brix_night_weight * r.night_score).epsilon(1e-9));
        // 품질 계수 q = 공급(크기 충족도 1) x K 곡선 x N 과잉 곡선(0) x 물 곡선, 시작부터 끝까지 같아요
        const double q = fp.brix_supply_curve(1.0) * fp.brix_k_curve(r.sufficiency_K) * fp.brix_n_excess_curve(0.0) *
                         fp.brix_water_curve(r.relative_water);
        CHECK(r.brix == Approx(fp.base_brix * q * fp.brix_temp_score_curve(r.k_temp)).epsilon(1e-9));
    };
    const Ripened day23 = ripen_one_fruit(23.0, 10.0);
    const Ripened day30 = ripen_one_fruit(30.0, 10.0);
    const Ripened night85 = ripen_one_fruit(23.0, 8.5);
    const Ripened night15 = ripen_one_fruit(23.0, 15.0);
    expect_scores(day23, 23.0, 10.0);
    expect_scores(day30, 30.0, 10.0);
    expect_scores(night85, 23.0, 8.5);
    expect_scores(night15, 23.0, 15.0);
    CHECK(day23.brix > day30.brix);
    CHECK(night85.brix > night15.brix);
    // 낮 23 °C / 밤 8.5 °C가 최고 점수 1
    CHECK(night85.k_temp == Approx(1.0).epsilon(1e-9));
    // 밤 2 °C: 전류 저하로 밤 점수가 낮아 당도가 떨어져요
    const Ripened frozen_night = ripen_one_fruit(23.0, 2.0);
    CHECK(frozen_night.night_score < 0.2);
    CHECK(frozen_night.brix < night85.brix);
}

// 같은 조건, 기온 15 °C 대 25 °C: 15 °C 쪽 수확이 늦음 (적산온도). 경도는 온도 곡선을 따라가요.
// (명세 표의 "15 °C 쪽 당도 높음"은 주·야간 이중 가중(디렉터 결정 10번) 뒤로는 낮밤 같은 기온에서 성립하지 않아서,
//  당도는 위 테스트가 점수 식으로 확인해요. 보고 항목.)
TEST_CASE("A cooler house ripens fruit later", "[unit][m4][fruit]") {
    const Ripened cool = ripen_one_fruit(15.0, 15.0, 0.2);
    const Ripened warm = ripen_one_fruit(25.0, 25.0, 0.2);
    CHECK(cool.ticks > warm.ticks);
    // 적산온도 350: (1 - 0.2) x 350 = 280 °C·일 -> 15 °C에서 28일 = 672틱, 25 °C에서 14일 = 336틱
    CHECK(cool.ticks == Approx(672.0).margin(2.0));
    CHECK(warm.ticks == Approx(336.0).margin(2.0));
    const sim::FruitParams fp = test::test_params().fruit;
    CHECK(cool.firmness > warm.firmness);
    CHECK(cool.firmness == Approx(fp.firmness_ca_curve(cool.sufficiency_Ca) * fp.firmness_temp_curve(15.0) *
                                  fp.firmness_n_excess_curve(0.0))
                               .epsilon(1e-9));
    CHECK(warm.firmness == Approx(fp.firmness_ca_curve(warm.sufficiency_Ca) * fp.firmness_temp_curve(25.0) *
                                  fp.firmness_n_excess_curve(0.0))
                               .epsilon(1e-9));
}

// K 충족도 0.6 대 1.0: 0.6 쪽 당도 낮음. Ca 충족도 0.6 대 1.0: 0.6 쪽 경도 낮음
TEST_CASE("Low potassium lowers brix and low calcium lowers firmness", "[unit][m4][fruit][brix]") {
    const sim::FruitParams fp = test::test_params().fruit;
    const auto scaled = [](double k_share, double ca_share) {
        return [k_share, ca_share](sim::Crop& crop, const sim::CropParams& cp) {
            const auto required = process::nutrient_requirement(crop, cp);
            crop.plant_K_g_per_m2 = k_share * required.K;
            crop.plant_Ca_g_per_m2 = ca_share * required.Ca;
        };
    };
    const Ripened full = ripen_one_fruit(20.0, 10.0, 0.8, scaled(1.0, 1.0));
    const Ripened low_k = ripen_one_fruit(20.0, 10.0, 0.8, scaled(0.6, 1.0));
    const Ripened low_ca = ripen_one_fruit(20.0, 10.0, 0.8, scaled(1.0, 0.6));
    REQUIRE(low_k.sufficiency_K == Approx(0.6));
    REQUIRE(low_ca.sufficiency_Ca == Approx(0.6));
    CHECK(low_k.brix < full.brix);
    CHECK(low_k.brix / full.brix == Approx(fp.brix_k_curve(0.6) / fp.brix_k_curve(1.0)).epsilon(1e-9));
    CHECK(low_ca.firmness < full.firmness);
    CHECK(low_ca.firmness / full.firmness == Approx(fp.firmness_ca_curve(0.6) / fp.firmness_ca_curve(1.0)).epsilon(1e-9));
    // 서로 독립: K 부족은 경도를, Ca 부족은 당도를 바꾸지 않아요
    CHECK(low_k.firmness == Approx(full.firmness).epsilon(1e-9));
    CHECK(low_ca.brix == Approx(full.brix).epsilon(1e-9));
    // 질소 과다는 당도와 경도를 낮춰요 (과잉 지수 0.5: 사치 상한 1.2배를 넘는 쪽은 N 흡수 상한 내 최대 0.2 -> 곡선 직접 확인)
    CHECK(fp.brix_n_excess_curve(0.5) < fp.brix_n_excess_curve(0.0));
    CHECK(fp.firmness_n_excess_curve(0.3) < fp.firmness_n_excess_curve(0.0));
}

// ---- 수확 (6번) ----

// 수확: 수확 기록·크기 구간·장부 반출 증가, 체내 양분 감소, 과실 제거. 기형과는 따로 세요
TEST_CASE("Harvest records fruit size classes deformed fruit and exports nutrients", "[unit][m4][fruit][harvest]") {
    const sim::Params p = test::quiet_params();
    const sim::FruitParams& fp = p.fruit;
    const sim::CropParams& cp = p.crop;
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    const auto add = [&](double fresh_g, bool deformed, double development) {
        sim::Fruit f;
        f.set = true;
        f.rank = 1;
        f.deformed = deformed;
        f.potential_fresh_g = 40.0;
        f.dry_g = fresh_g * fp.dry_matter_fraction;
        f.development_index = development;
        f.brix_factor_sum = 0.9;
        f.firmness_factor_sum = 0.8;
        f.weight_sum = 1.0;
        f.night_score_sum = 5.0;
        f.night_weight_sum = 10.0;
        crop.fruits.push_back(f);
    };
    add(8.0, false, 1.0);    // 구간 0 (< 10)
    add(12.0, false, 1.0);   // 구간 1
    add(30.0, false, 1.0);   // 구간 4 (>= 25)
    add(15.0, true, 1.0);    // 기형과
    add(5.0, false, 0.5);    // 아직 안 익음: 남아요
    const sim::Crop before = crop;
    const sim::SoilCell cell_before = cell;
    const auto required = process::nutrient_requirement(before, cp);
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(2);
    // 기온 5 °C: 진행도 증가분 0이라 품질 합이 그대로이고, 밤에는 점수만 한 번 더해져요
    const auto result = tick_crop(crop, cell, p, 0.0, 5.0, rng, ledger, 321);
    REQUIRE(result.harvested.has_value());
    const auto& event = *result.harvested;
    CHECK(event.fruits == 3);
    CHECK(event.deformed == 1);
    CHECK(event.fresh_g == Approx(8.0 + 12.0 + 30.0 + 15.0));
    // 기록: 정상과와 기형과 분리, 크기 구간 [10, 15, 20, 25] -> 5개 구간
    const sim::HarvestRecord& h = crop.harvest;
    CHECK(h.harvested_fruits == 3);
    CHECK(h.harvested_fresh_g == Approx(50.0));
    CHECK(h.harvested_deformed == 1);
    CHECK(h.harvested_deformed_fresh_g == Approx(15.0));
    REQUIRE(h.harvested_size_class_counts.size() == 5);
    CHECK(h.harvested_size_class_counts == std::vector<std::uint64_t>{1, 1, 0, 0, 1});
    CHECK(h.has_first_harvest);
    CHECK(h.first_harvest_tick == 321);
    // 당도·경도: 과실 하나의 식 = 기준 당도 x (brix_factor_sum / weight_sum) x 점수 곡선(K_temp), 밤만 있으면 K_temp = 밤 평균
    const double night_avg = (5.0 + fp.brix_night_temp_score_curve(5.0)) / 11.0;
    const double brix_each = fp.base_brix * 0.9 * fp.brix_temp_score_curve(night_avg);
    CHECK(event.mean_brix == Approx(brix_each).epsilon(1e-9));
    CHECK(event.mean_firmness == Approx(0.8).epsilon(1e-9));
    CHECK(h.harvested_brix_weighted == Approx(brix_each * 50.0).epsilon(1e-9));
    CHECK(h.harvested_firmness_weighted == Approx(0.8 * 50.0).epsilon(1e-9));
    // 익은 과실은 목록에서 빠지고 안 익은 과실은 남음
    REQUIRE(crop.fruits.size() == 1);
    CHECK(crop.fruits[0].development_index == 0.5);
    // 양분 반출: min(체내량, 수확 건물 x 과실 적정 농도 x min(1, 체내량 / 요구량)) (수확 전 요구량 기준)
    const double harvested_dry = (8.0 + 12.0 + 30.0 + 15.0) * fp.dry_matter_fraction;
    struct Row {
        double plant_before, plant_after, fruit_target, required, ledger;
    };
    const Row rows[] = {
        {before.plant_N_g_per_m2, crop.plant_N_g_per_m2, cp.fruit_target_N_g_per_g, required.N, ledger.N_harvested_g},
        {before.plant_P_g_per_m2, crop.plant_P_g_per_m2, cp.fruit_target_P_g_per_g, required.P, ledger.P_harvested_g},
        {before.plant_K_g_per_m2, crop.plant_K_g_per_m2, cp.fruit_target_K_g_per_g, required.K, ledger.K_harvested_g},
        {before.plant_Ca_g_per_m2, crop.plant_Ca_g_per_m2, cp.fruit_target_Ca_g_per_g, required.Ca, ledger.Ca_harvested_g},
        {before.plant_Mg_g_per_m2, crop.plant_Mg_g_per_m2, cp.fruit_target_Mg_g_per_g, required.Mg, ledger.Mg_harvested_g},
        {before.plant_Mn_g_per_m2, crop.plant_Mn_g_per_m2, cp.fruit_target_Mn_g_per_g, required.Mn, ledger.Mn_harvested_g},
    };
    for (const Row& row : rows) {
        const double expected =
            std::min(row.plant_before, harvested_dry * row.fruit_target * std::min(1.0, row.plant_before / row.required));
        CHECK(expected > 0.0);
        CHECK(row.ledger == Approx(expected).epsilon(1e-12));
        CHECK(row.plant_before - row.plant_after == Approx(expected).epsilon(1e-12));
    }
    // 반출은 토양과 무관: 토양 풀은 그대로, 장부 반출만 늘어요 (보존식: 토양 + 체내 + 반출 = 처음)
    CHECK(cell.k_g_per_m2 == cell_before.k_g_per_m2);
    CHECK(cell.nh4_N_g_per_m2 == cell_before.nh4_N_g_per_m2);
    CHECK(ledger.K_harvested_g + crop.plant_K_g_per_m2 == Approx(before.plant_K_g_per_m2).epsilon(1e-12));
}

// 기형과만 수확된 틱 (구현 규칙 8): 이벤트의 평균 당도·경도 0, 첫 수확 틱은 기형과 수확으로도 정해짐
TEST_CASE("A tick that harvests only deformed fruit reports zero mean brix and still sets the first harvest",
          "[unit][m4][fruit][harvest]") {
    const sim::Params p = test::quiet_params();
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    sim::Fruit f;
    f.set = true;
    f.deformed = true;
    f.potential_fresh_g = 10.0;
    f.dry_g = 10.0 * p.fruit.dry_matter_fraction;
    f.development_index = 1.0;
    f.weight_sum = 1.0;
    f.brix_factor_sum = 1.0;
    f.firmness_factor_sum = 1.0;
    crop.fruits.push_back(f);
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(2);
    const auto result = tick_crop(crop, cell, p, 0.0, 5.0, rng, ledger, 9);
    REQUIRE(result.harvested.has_value());
    CHECK(result.harvested->fruits == 0);
    CHECK(result.harvested->deformed == 1);
    CHECK(result.harvested->mean_brix == 0.0);
    CHECK(result.harvested->mean_firmness == 0.0);
    CHECK(crop.harvest.harvested_fruits == 0);
    CHECK(crop.harvest.harvested_deformed == 1);
    CHECK(crop.harvest.has_first_harvest);
    CHECK(crop.harvest.first_harvest_tick == 9);
}

// ---- 낙엽 (7번) ----

// 낙엽: 잎 감소, C·양분이 토양으로. 몫(share)의 분모 = 잎 + 관부 + 뿌리 + 과실 건물 (떨어지기 전 기준)
TEST_CASE("Leaf senescence moves leaf mass nutrients and carbon to the soil", "[unit][m4][fruit][senescence][conservation]") {
    sim::Params p = test::test_params();
    p.crop.maintenance_rate_leaf_per_hour = 0.0;  // 낙엽만 보려고 호흡을 꺼요 (수축과 낙엽은 같은 호출에서 차례로 일어나요)
    p.crop.maintenance_rate_crown_per_hour = 0.0;
    p.crop.maintenance_rate_root_per_hour = 0.0;
    p.crop.maintenance_rate_fruit_per_hour = 0.0;
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    sim::Fruit f;
    f.set = true;
    f.potential_fresh_g = 20.0;
    f.development_index = 0.5;
    f.dry_g = 6.0;
    crop.fruits.push_back(f);
    const sim::Crop before = crop;
    const sim::SoilCell cell_before = cell;
    const Totals initial = soil_plus_plant(cell, crop);
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(1);
    (void)tick_crop(crop, cell, p, 0.0, kAir_degC, rng, ledger);

    const double dead_leaf = before.leaf_g_per_m2 * p.crop.leaf_senescence_rate_per_hour;
    const double total_before = before.leaf_g_per_m2 + before.crown_g_per_m2 + before.root_g_per_m2 + before.fruit_g_per_m2();
    const double share = dead_leaf / total_before;
    CHECK(crop.leaf_g_per_m2 == Approx(before.leaf_g_per_m2 - dead_leaf).epsilon(1e-12));
    CHECK(crop.crown_g_per_m2 == before.crown_g_per_m2);
    CHECK(crop.root_g_per_m2 == before.root_g_per_m2);
    CHECK(crop.fruits[0].dry_g == 6.0);
    CHECK(crop.plant_N_g_per_m2 == Approx(before.plant_N_g_per_m2 * (1.0 - share)).epsilon(1e-12));
    CHECK(crop.plant_K_g_per_m2 == Approx(before.plant_K_g_per_m2 * (1.0 - share)).epsilon(1e-12));
    // 토양: N은 이분해성 유기 N, P·양이온은 총량(양이온은 용액에도), C는 이분해성 C
    CHECK(cell.labile_N_g_per_m2 - cell_before.labile_N_g_per_m2 == Approx(before.plant_N_g_per_m2 * share).epsilon(1e-9));
    CHECK(cell.p_g_per_m2 - cell_before.p_g_per_m2 == Approx(before.plant_P_g_per_m2 * share).epsilon(1e-9));
    CHECK(cell.k_g_per_m2 - cell_before.k_g_per_m2 == Approx(before.plant_K_g_per_m2 * share).epsilon(1e-9));
    CHECK(cell.k_solution_g_per_m2 - cell_before.k_solution_g_per_m2 == Approx(before.plant_K_g_per_m2 * share).epsilon(1e-9));
    CHECK(cell.ca_g_per_m2 - cell_before.ca_g_per_m2 == Approx(before.plant_Ca_g_per_m2 * share).epsilon(1e-9));
    CHECK(cell.mg_g_per_m2 - cell_before.mg_g_per_m2 == Approx(before.plant_Mg_g_per_m2 * share).epsilon(1e-9));
    const double litter_C = dead_leaf * p.crop.dry_matter_C_fraction;
    CHECK(cell.labile_C_g_per_m2 - cell_before.labile_C_g_per_m2 == Approx(litter_C).epsilon(1e-9));
    CHECK(ledger.C_leaf_litter_g == Approx(litter_C).epsilon(1e-9));
    CHECK(ledger.C_root_litter_g == 0.0);
    // 원소 보존 (낙엽은 계 안의 이동: 장부 흡수·반출 없음)
    const Totals now = soil_plus_plant(cell, crop);
    CHECK(now.N == Approx(initial.N).epsilon(1e-12));
    CHECK(now.P == Approx(initial.P).epsilon(1e-12));
    CHECK(now.K == Approx(initial.K).epsilon(1e-12));
    CHECK(now.Ca == Approx(initial.Ca).epsilon(1e-12));
    CHECK(now.Mg == Approx(initial.Mg).epsilon(1e-12));
    CHECK(ledger.N_uptake_g == 0.0);
    CHECK(ledger.N_harvested_g == 0.0);
}

// 호흡 수축과 낙엽은 같은 호출 안에서 차례로 (strawberry-fruit 7번): 잎 = 잎 x 수축 계수 x (1 - 낙엽 비율)
TEST_CASE("Respiration shrinkage and leaf senescence happen in the same call in order", "[unit][m4][fruit][senescence]") {
    const sim::Params p = test::test_params();
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    const sim::Crop before = crop;
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(1);
    (void)tick_crop(crop, cell, p, 0.0, kAir_degC, rng, ledger);
    const double vegetative = before.leaf_g_per_m2 + before.crown_g_per_m2 + before.root_g_per_m2;
    const double keep = 1.0 - crop.last_respiration_g_per_m2 / vegetative;
    CHECK(crop.leaf_g_per_m2 == Approx(before.leaf_g_per_m2 * keep * (1.0 - p.crop.leaf_senescence_rate_per_hour)).epsilon(1e-12));
    CHECK(crop.root_g_per_m2 == Approx(before.root_g_per_m2 * keep).epsilon(1e-12));
    // 낙엽 몫의 분모는 수축한 뒤의 건물 합
    const double dead_leaf = before.leaf_g_per_m2 * keep * p.crop.leaf_senescence_rate_per_hour;
    CHECK(ledger.C_leaf_litter_g == Approx(dead_leaf * p.crop.dry_matter_C_fraction).epsilon(1e-9));
}

// ---- 요구량 (8번) ----

// 요구량 = 적정 농도 x (잎 + 관부 + 뿌리) + 과실 적정 농도 x 과실. 과실이 늘면 K 요구량이 증가
TEST_CASE("Nutrient requirement includes crown and fruit and K demand rises with fruit", "[unit][m4][fruit][requirement]") {
    const sim::Params p = test::test_params();
    const sim::CropParams& cp = p.crop;
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    const process::NutrientRequirement none = process::nutrient_requirement(crop, cp);
    const double vegetative = crop.leaf_g_per_m2 + crop.crown_g_per_m2 + crop.root_g_per_m2;
    CHECK(vegetative == Approx(56.0));  // 28 + 16 + 12
    CHECK(none.N == Approx(cp.target_N_g_per_g * 56.0));
    CHECK(none.K == Approx(cp.target_K_g_per_g * 56.0));
    CHECK(none.Mn == Approx(cp.target_Mn_g_per_g * 56.0));
    // 관부 포함: 관부만 늘려도 요구량이 늘어요
    sim::Crop thick = crop;
    thick.crown_g_per_m2 += 10.0;
    CHECK(process::nutrient_requirement(thick, cp).K == Approx(none.K + 10.0 * cp.target_K_g_per_g));
    // 과실 포함: 과실 건물 20 g 추가 -> 과실 적정 농도 x 20
    sim::Fruit f;
    f.set = true;
    f.dry_g = 20.0;
    f.development_index = 0.5;
    crop.fruits.push_back(f);
    const process::NutrientRequirement with_fruit = process::nutrient_requirement(crop, cp);
    CHECK(with_fruit.K == Approx(none.K + cp.fruit_target_K_g_per_g * 20.0));
    CHECK(with_fruit.N == Approx(none.N + cp.fruit_target_N_g_per_g * 20.0));
    CHECK(with_fruit.Ca == Approx(none.Ca + cp.fruit_target_Ca_g_per_g * 20.0));
    CHECK(with_fruit.Mg == Approx(none.Mg + cp.fruit_target_Mg_g_per_g * 20.0));
    CHECK(with_fruit.P == Approx(none.P + cp.fruit_target_P_g_per_g * 20.0));
    CHECK(with_fruit.Mn == Approx(none.Mn + cp.fruit_target_Mn_g_per_g * 20.0));
    CHECK(with_fruit.K > none.K);
    // 과실의 K 적정 농도가 높아(0.025) 과실은 영양 기관(0.020)보다 g당 K를 더 요구해요
    CHECK(cp.fruit_target_K_g_per_g > cp.target_K_g_per_g);
    // 같은 값을 건물 합으로 넘기는 형태와 비트 단위로 같아요
    const process::NutrientRequirement explicit_sum = process::nutrient_requirement(crop, cp, crop.fruit_g_per_m2());
    CHECK(explicit_sum.K == with_fruit.K);
    CHECK(explicit_sum.Mn == with_fruit.Mn);
    // K 충족도: 체내 K가 그대로면 과실이 달리는 만큼 충족도가 내려가요 (K 공급이 판단이 되는 이유)
    const auto status_none = process::nutrient_status(process::plant_crop(test::test_crop_init(), p.crop), cp);
    const auto status_fruit = process::nutrient_status(crop, cp);
    CHECK(status_none.sufficiency_K == 1.0);
    CHECK(status_fruit.sufficiency_K < 1.0);
    CHECK(status_fruit.sufficiency_K == Approx(none.K / with_fruit.K));
    // 총 건물량 도우미
    CHECK(process::vegetative_biomass_g_per_m2(crop) == Approx(56.0));
    CHECK(process::total_biomass_g_per_m2(crop) == Approx(76.0));
}
