// M5b 지상부 병해충 단위 테스트 (docs/spec/foliar-disease.md 9장 "단위 테스트" 표).
// 기대값은 방향만 써요 (7장 목표값은 차수 보고에서 확인). 프로세스 함수를 직접 부르고, 명령·정식·세이브는 World로 봐요.
// 파라미터는 test::test_params() (명세 3장 초기값).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "m5a_fingerprint.hpp"
#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "test_helpers.hpp"

namespace process = sim::process;
using Catch::Approx;

namespace {

constexpr std::size_t kAnth = static_cast<std::size_t>(sim::FoliarDisease::Anthracnose);

// 등급 B 8포기/m², 활착 1 (지상부 지수는 0에서 시작)
sim::Crop make_crop() { return process::plant_crop(test::test_crop_init(), test::test_params().crop); }

// 지상부 감염 한 틱 (질소 배율 1, 분배 전 잎 = 지금 잎)
void infect(sim::Crop& crop, const sim::SoilCell& cell, const sim::Params& p, const process::CropEnvironment& env,
            double s_N = 1.0) {
    process::FoliarOnsets onsets{};
    process::update_foliar_infection(crop, cell, p, env, s_N, crop.leaf_g_per_m2, 0, onsets);
}

process::CropEnvironment env_at(double air_degC) { return test::crop_env(0.0, air_degC); }

sim::SoilCell cell_with_anthracnose(double index) {
    sim::SoilCell cell = test::test_cell();
    cell.microbe_index[sim::microbe(sim::MicrobeGroup::Anthracnose)] = index;
    return cell;
}

std::size_t mulch_index(const sim::Params& p, const char* id) {
    for (std::size_t i = 0; i < p.mulches.size(); ++i) {
        if (p.mulches[i].id == id) {
            return i;
        }
    }
    return 0;
}

}  // namespace

// ---- 4.1 질소 과다 감수성 ----

// 토양 무기 N 3 대 20 g/m² (2주 뒤): 20 쪽 s_N이 크고 세 병의 증가분이 그 비율만큼 큼. 시들음병 감염에는 영향 없음
TEST_CASE("Nitrogen excess raises foliar infection by the susceptibility ratio only", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    sim::SoilCell low = test::test_cell();
    low.nh4_N_g_per_m2 = 1.0;
    low.no3_N_g_per_m2 = 2.0;
    sim::SoilCell high = test::test_cell();
    high.nh4_N_g_per_m2 = 5.0;
    high.no3_N_g_per_m2 = 15.0;
    sim::Crop crop_low = make_crop();
    sim::Crop crop_high = make_crop();
    crop_low.soil_mineral_N_mean_g_per_m2 = 3.0;
    crop_high.soil_mineral_N_mean_g_per_m2 = 3.0;
    double s_low = 0.0;
    double s_high = 0.0;
    for (int h = 0; h < 14 * 24; ++h) {
        s_low = process::update_n_susceptibility(crop_low, low, p.foliar_disease);
        s_high = process::update_n_susceptibility(crop_high, high, p.foliar_disease);
    }
    CHECK(crop_high.soil_mineral_N_mean_g_per_m2 > 12.0);
    CHECK(s_high > s_low);
    CHECK(s_low == Approx(1.0));

    // 같은 조건에서 세 병의 증가분 비율 = s_N 비율 (작은 증가분이라 상한에 걸리지 않음)
    sim::Crop a = make_crop();
    a.infection_anthracnose_index = 0.05;
    a.infection_powdery_mildew_index = 0.05;
    a.wet_run_hours = 0.0;
    a.canopy_rh_index = 0.85;
    a.rgr_mean_per_day = 0.03;
    sim::Crop b = a;
    process::CropEnvironment env = env_at(20.0);
    env.botrytis_spore_index = 0.5;
    a.wet_run_hours = 24.0;
    b.wet_run_hours = 24.0;
    a.leaf_wetness_index = 0.0;
    b.leaf_wetness_index = 0.0;
    const sim::SoilCell cell = test::test_cell();
    infect(a, cell, p, env, s_low);
    infect(b, cell, p, env, s_high);
    const double ratio = s_high / s_low;
    CHECK(b.last_anthracnose_increase_index == Approx(a.last_anthracnose_increase_index * ratio).epsilon(1e-9));
    CHECK(b.last_botrytis_increase_index == Approx(a.last_botrytis_increase_index * ratio).epsilon(1e-9));
    CHECK(b.last_powdery_mildew_increase_index == Approx(a.last_powdery_mildew_increase_index * ratio).epsilon(1e-9));

    // 시들음병 감염은 질소 이동 평균과 무관
    sim::SoilCell fus_low = low;
    sim::SoilCell fus_high = high;
    for (sim::SoilCell* c : {&fus_low, &fus_high}) {
        c->microbe_index[sim::microbe(sim::MicrobeGroup::Fusarium)] = 0.5;
        c->soil_temp_degC = 28.0;
    }
    sim::Crop f_low = crop_low;
    sim::Crop f_high = crop_high;
    (void)process::update_soil_infection(f_low, fus_low, p, 0);
    (void)process::update_soil_infection(f_high, fus_high, p, 0);
    CHECK(f_low.infection_index[0] == f_high.infection_index[0]);
}

// ---- 4.2 탄저병 ----

// 감염 0.05, 기온 27 대 10 °C: 27 °C만 진행.
// 명세 곡선은 최저 9 °C라 10 °C에서 0.027(최적의 3%)이 남아요. "거의 멈춤"(증가분 5% 미만)으로 확인하고 9 °C는 정확히 0
TEST_CASE("Anthracnose progresses at 27 C but not at 10 C", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    const sim::SoilCell cell = test::test_cell();
    sim::Crop warm = make_crop();
    warm.infection_anthracnose_index = 0.05;
    sim::Crop cold = warm;
    sim::Crop colder = warm;
    infect(warm, cell, p, env_at(27.0));
    infect(cold, cell, p, env_at(10.0));
    infect(colder, cell, p, env_at(9.0));
    CHECK(warm.infection_anthracnose_index > 0.05);
    CHECK(cold.last_anthracnose_increase_index < 0.05 * warm.last_anthracnose_increase_index);
    CHECK(colder.infection_anthracnose_index == 0.05);
}

// 흙 접종원 0.5, 연속 젖음 0 대 14 h: 젖은 쪽만 흙에서 감염
TEST_CASE("Anthracnose soil infection needs a wet run", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    const sim::SoilCell cell = cell_with_anthracnose(0.5);
    sim::Crop dry = make_crop();
    sim::Crop wet = make_crop();
    wet.wet_run_hours = 14.0;
    infect(dry, cell, p, env_at(27.0));
    infect(wet, cell, p, env_at(27.0));
    CHECK(dry.infection_anthracnose_index == 0.0);
    CHECK(wet.infection_anthracnose_index > 0.0);
}

// 두상 관수 틱 대 점적 틱, 같은 조건: 두상 쪽만 물 튐 증가분
TEST_CASE("Anthracnose splash only on overhead irrigation ticks", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    const sim::SoilCell cell = cell_with_anthracnose(0.5);
    sim::Crop drip = make_crop();
    sim::Crop overhead = make_crop();
    process::CropEnvironment env = env_at(27.0);
    infect(drip, cell, p, env);
    env.overhead_irrigated = true;
    infect(overhead, cell, p, env);
    CHECK(drip.infection_anthracnose_index == 0.0);
    CHECK(overhead.infection_anthracnose_index > 0.0);
}

// 포자 원천 > 0, 흙 접종원 0, 두상 관수: 필름 쪽 증가가 큼 (포기 → 포기)
TEST_CASE("Film raises plant to plant anthracnose splash", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    sim::SoilCell bare = cell_with_anthracnose(0.0);
    sim::SoilCell film = bare;
    film.mulch_def_index = static_cast<std::int32_t>(mulch_index(p, "black_film"));
    process::CropEnvironment env = env_at(27.0);
    env.overhead_irrigated = true;
    env.anthracnose_sporulation_index = 0.3;
    sim::Crop on_bare = make_crop();
    sim::Crop on_film = make_crop();
    infect(on_bare, bare, p, env);
    infect(on_film, film, p, env);
    CHECK(on_bare.infection_anthracnose_index > 0.0);
    CHECK(on_film.infection_anthracnose_index > on_bare.infection_anthracnose_index);
}

// 포자 원천 0, 흙 접종원 0.5, 필름 대 맨흙: 필름 쪽 증가가 작음 (흙 → 포기)
TEST_CASE("Film blocks soil to plant anthracnose", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    sim::SoilCell bare = cell_with_anthracnose(0.5);
    sim::SoilCell film = bare;
    film.mulch_def_index = static_cast<std::int32_t>(mulch_index(p, "black_film"));
    sim::Crop on_bare = make_crop();
    sim::Crop on_film = make_crop();
    on_bare.wet_run_hours = 14.0;
    on_film.wet_run_hours = 14.0;
    infect(on_bare, bare, p, env_at(27.0));
    infect(on_film, film, p, env_at(27.0));
    CHECK(on_film.infection_anthracnose_index > 0.0);
    CHECK(on_film.infection_anthracnose_index < on_bare.infection_anthracnose_index);
}

// 길항균 1 대 0: 1 쪽 흙 경로 감염이 작음
TEST_CASE("Antagonist lowers soil path anthracnose", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    sim::SoilCell none = cell_with_anthracnose(0.5);
    sim::SoilCell full = none;
    none.microbe_index[sim::microbe(sim::MicrobeGroup::Antagonist)] = 0.0;
    full.microbe_index[sim::microbe(sim::MicrobeGroup::Antagonist)] = 1.0;
    sim::Crop a = make_crop();
    a.wet_run_hours = 14.0;
    sim::Crop b = a;
    infect(a, none, p, env_at(27.0));
    infect(b, full, p, env_at(27.0));
    CHECK(b.infection_anthracnose_index < a.infection_anthracnose_index);
}

// 탄저병 감염 0.5: 뿌리 손상 증가, 지수는 줄지 않음 (10 °C라 진행 0)
TEST_CASE("Anthracnose infection damages roots and never decreases", "[unit][m5b][foliar]") {
    const sim::Params p = test::quiet_params();
    sim::SoilCell cell_a = test::test_cell();
    sim::SoilCell cell_b = test::test_cell();
    sim::Crop sick = make_crop();
    sick.infection_anthracnose_index = 0.5;
    sim::Crop healthy = make_crop();
    sim::Ledger la;
    sim::Ledger lb;
    for (int h = 0; h < 48; ++h) {
        sim::Pcg32 r1 = test::biology_rng(1);
        sim::Pcg32 r2 = test::biology_rng(1);
        (void)process::update_crop(sick, cell_a, p, process::NutrientAvailability{}, env_at(10.0), r1, la);
        (void)process::update_crop(healthy, cell_b, p, process::NutrientAvailability{}, env_at(10.0), r2, lb);
        CHECK(sick.infection_anthracnose_index >= 0.5);
    }
    CHECK(sick.root_health < healthy.root_health);
}

// 흙 접종원, 숙주 없음 60일: 80% 이상 감소. 병든 포기가 있는 칸은 증가.
// 길항균이 없는 흙으로 봐요 (명세 초기값은 증식 0.003이 사멸 0.002를 조금만 넘어서, 길항균 0.5가 자리 0.2를 차지하면
// 감염 0.6 포기 아래에서도 접종원이 줄어요. 차수 보고 B18에서 확인)
TEST_CASE("Anthracnose soil inoculum fades without a host and builds with one", "[unit][m5b][foliar]") {
    sim::Params p = test::test_params();
    p.microbes.groups[sim::microbe(sim::MicrobeGroup::Antagonist)].immigration_rate_per_hour = 0.0;
    sim::SoilCell bare = cell_with_anthracnose(0.5);
    bare.soil_temp_degC = 27.0;
    // 병든 포기 칸은 낮은 접종원에서 시작해 수용력(부생 + 숙주 × 감염) 쪽으로 늘어요
    sim::SoilCell hosted = cell_with_anthracnose(0.05);
    hosted.soil_temp_degC = 27.0;
    process::MicrobeHost host;
    host.root_g_per_m2 = 12.0;
    host.anthracnose_infection_index = 0.6;
    for (int h = 0; h < 60 * 24; ++h) {
        process::update_microbes(bare, p, process::MicrobeHost{});
        process::update_microbes(hosted, p, host);
    }
    const std::size_t g = sim::microbe(sim::MicrobeGroup::Anthracnose);
    CHECK(bare.microbe_index[g] < 0.5 * 0.2);
    CHECK(hosted.microbe_index[g] > 0.05);
}

// (7차 프로토타입, 4.2) 흙 접종원 0인 칸: 병든 포기(감염 0.5)가 있으면 접종원이 생겨 늘고, 건강한 포기(감염 0)는 0 그대로
TEST_CASE("Diseased plant builds anthracnose soil inoculum from zero and healthy plant does not", "[unit][m5b][foliar]") {
    sim::Params p = test::test_params();
    p.foliar_disease.anthracnose.soil_deposit_per_hour = 0.0003;
    p.microbes.groups[sim::microbe(sim::MicrobeGroup::Antagonist)].immigration_rate_per_hour = 0.0;
    const std::size_t g = sim::microbe(sim::MicrobeGroup::Anthracnose);
    sim::SoilCell sick_cell = cell_with_anthracnose(0.0);
    sick_cell.soil_temp_degC = 22.0;
    sim::SoilCell healthy_cell = sick_cell;
    process::MicrobeHost sick;
    sick.root_g_per_m2 = 12.0;
    sick.anthracnose_infection_index = 0.5;
    process::MicrobeHost healthy;
    healthy.root_g_per_m2 = 12.0;
    double at_week1 = 0.0;
    for (int h = 1; h <= 28 * 24; ++h) {
        process::update_microbes(sick_cell, p, sick);
        process::update_microbes(healthy_cell, p, healthy);
        if (h == 7 * 24) {
            at_week1 = sick_cell.microbe_index[g];
        }
    }
    CHECK(at_week1 > 0.0);
    CHECK(sick_cell.microbe_index[g] > at_week1);
    CHECK(sick_cell.microbe_index[g] <= 1.0);
    CHECK(healthy_cell.microbe_index[g] == 0.0);
    // 길항균 0.5가 있으면 덜 쌓여요 (경쟁으로 증식 몫이 줄어요)
    sim::Params q = test::test_params();
    q.foliar_disease.anthracnose.soil_deposit_per_hour = 0.0003;
    sim::SoilCell guarded = cell_with_anthracnose(0.0);
    guarded.soil_temp_degC = 22.0;
    guarded.microbe_index[sim::microbe(sim::MicrobeGroup::Antagonist)] = 0.5;
    for (int h = 1; h <= 28 * 24; ++h) {
        process::update_microbes(guarded, q, sick);
    }
    CHECK(guarded.microbe_index[g] < sick_cell.microbe_index[g]);
}

// (7차) 병든 포기가 죽으면 잔재가 접종원을 한 번 남기고(residue × 감염), 뽑으면(RemovePlant) 아무것도 남기지 않아요
TEST_CASE("Dead diseased plant leaves anthracnose residue but a removed plant leaves none", "[unit][m5b][foliar]") {
    sim::Params p = test::test_params();
    p.foliar_disease.anthracnose.residue_index = 0.3;
    const std::size_t g = sim::microbe(sim::MicrobeGroup::Anthracnose);
    sim::Crop dying = make_crop();
    dying.infection_anthracnose_index = 0.7;
    dying.root_health = p.crop.death_root_health * 1.0001;  // 이번 틱 탄저병 뿌리 손상으로 고사
    sim::Crop removed = dying;
    sim::SoilCell dead_cell = test::test_cell();
    sim::SoilCell removed_cell = dead_cell;
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(5);
    const process::CropTickResult result = process::update_crop(
        dying, dead_cell, p, process::available_nutrients(dead_cell, p), test::crop_env(0.0, 22.0), rng, ledger);
    REQUIRE(result.died.has_value());
    // 이번 틱 미생물 단계는 부르지 않았으니 잔재 몫만: 0.3 × 감염(이번 틱 진행 뒤, 0.7 이상)
    CHECK(dead_cell.microbe_index[g] >= 0.3 * 0.7);
    CHECK(dead_cell.microbe_index[g] == Approx(0.3 * dying.infection_anthracnose_index));
    process::remove_plant(removed, ledger);
    CHECK(removed_cell.microbe_index[g] == 0.0);
}

// (7차) 쌓인 접종원도 태양열(지온 45 °C)·약제(다조멧 잔류 1)로 줄어요 (M5a 사멸 경로 그대로)
TEST_CASE("Solarization and fumigant reduce deposited anthracnose inoculum", "[unit][m5b][foliar]") {
    sim::Params p = test::test_params();
    p.foliar_disease.anthracnose.soil_deposit_per_hour = 0.0003;
    const std::size_t g = sim::microbe(sim::MicrobeGroup::Anthracnose);
    sim::SoilCell built = cell_with_anthracnose(0.0);
    built.soil_temp_degC = 22.0;
    process::MicrobeHost sick;
    sick.root_g_per_m2 = 12.0;
    sick.anthracnose_infection_index = 0.6;
    for (int h = 0; h < 28 * 24; ++h) {
        process::update_microbes(built, p, sick);
    }
    REQUIRE(built.microbe_index[g] > 0.0);
    // 작물을 치운 뒤 2주: 그냥 둔 칸 대 태양열 대 약제
    sim::SoilCell idle = built;
    sim::SoilCell solar = built;
    solar.soil_temp_degC = 45.0;
    sim::SoilCell fumigated = built;
    fumigated.fumigant_residue_index.assign(p.disinfectants.size(), 0.0);
    REQUIRE_FALSE(fumigated.fumigant_residue_index.empty());
    fumigated.fumigant_residue_index[0] = 1.0;
    for (int h = 0; h < 14 * 24; ++h) {
        process::update_microbes(idle, p, process::MicrobeHost{});
        solar.soil_temp_degC = 45.0;
        process::update_microbes(solar, p, process::MicrobeHost{});
        fumigated.fumigant_residue_index[0] = 1.0;  // 잔류 유지 (분해는 따로 보지 않음)
        process::update_microbes(fumigated, p, process::MicrobeHost{});
    }
    CHECK(solar.microbe_index[g] < idle.microbe_index[g] * 0.5);
    CHECK(fumigated.microbe_index[g] < idle.microbe_index[g] * 0.5);
}

// ---- 4.3 잿빛곰팡이병 ----

// 포자 0.5, 20 °C, 연속 젖음 6 대 24 h: 24 h 쪽 증가가 5배 이상
TEST_CASE("Botrytis infection rises over five times from 6 to 24 wet hours", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    const sim::SoilCell cell = test::test_cell();
    process::CropEnvironment env = env_at(20.0);
    env.botrytis_spore_index = 0.5;
    sim::Crop short_wet = make_crop();
    sim::Crop long_wet = make_crop();
    short_wet.wet_run_hours = 6.0;
    long_wet.wet_run_hours = 24.0;
    infect(short_wet, cell, p, env);
    infect(long_wet, cell, p, env);
    CHECK(short_wet.last_botrytis_increase_index > 0.0);
    CHECK(long_wet.last_botrytis_increase_index >= 5.0 * short_wet.last_botrytis_increase_index);
}

// 같은 젖음, 20 대 30 °C: 20 °C가 큼
TEST_CASE("Botrytis infects faster at 20 C than at 30 C", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    const sim::SoilCell cell = test::test_cell();
    sim::Crop mild = make_crop();
    mild.wet_run_hours = 24.0;
    sim::Crop hot = mild;
    process::CropEnvironment env20 = env_at(20.0);
    process::CropEnvironment env30 = env_at(30.0);
    env20.botrytis_spore_index = 0.5;
    env30.botrytis_spore_index = 0.5;
    infect(mild, cell, p, env20);
    infect(hot, cell, p, env30);
    CHECK(mild.infection_botrytis_index > hot.infection_botrytis_index);
}

// 젖음이 끊긴 뒤: 감염 압력이 서서히 감소
TEST_CASE("Botrytis pressure decays slowly after wetness stops", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    const sim::SoilCell cell = test::test_cell();
    sim::Crop crop = make_crop();
    crop.infection_botrytis_index = 0.4;
    process::CropEnvironment env = env_at(20.0);
    env.botrytis_spore_index = 0.5;  // 젖음 0이라 감염 증가 없음
    double prev = crop.infection_botrytis_index;
    for (int h = 0; h < 24; ++h) {
        infect(crop, cell, p, env);
        CHECK(crop.infection_botrytis_index < prev);
        prev = crop.infection_botrytis_index;
    }
    CHECK(crop.infection_botrytis_index > 0.9 * 0.4);  // 하루에 10% 안쪽 (반감기 약 2주)
}

// 감염 0.5, 과실 100개: 썩은 수의 기대값이 확률 합과 맞고, 썩은 과실의 원소가 토양으로 (보존)
TEST_CASE("Botrytis fruit rot matches probabilities and conserves elements", "[unit][m5b][foliar]") {
    sim::Params p = test::quiet_params();
    p.foliar_disease.botrytis.fruit_rot_prob_per_hour = 0.2;  // 한 틱 표본을 키우려고 확률만 올려요
    const auto make = [&]() {
        sim::Crop crop = make_crop();
        crop.infection_botrytis_index = 0.5;
        crop.trusses.push_back(sim::Truss{.opened = true});
        for (int i = 0; i < 100; ++i) {
            sim::Fruit f;
            f.set = true;
            f.potential_fresh_g = 15.0;
            f.dry_g = 0.5;
            f.development_index = 0.01 * i;  // 0~0.99 (수확 없음)
            crop.fruits.push_back(f);
        }
        return crop;
    };
    double expected = 0.0;
    {
        const sim::Crop crop = make();
        for (const sim::Fruit& f : crop.fruits) {
            expected += 0.2 * 0.5 * p.foliar_disease.botrytis.fruit_stage_curve(f.development_index);
        }
    }
    constexpr int kTrials = 200;
    double rotten_sum = 0.0;
    for (int trial = 0; trial < kTrials; ++trial) {
        sim::Crop crop = make();
        sim::SoilCell cell = test::test_cell();
        sim::Ledger ledger;
        const double N_before = crop.plant_N_g_per_m2 + cell.total_N_g_per_m2();
        const double K_before = crop.plant_K_g_per_m2 + cell.k_g_per_m2;
        const double C_before = cell.organic_C_g_per_m2();
        sim::Pcg32 bio = test::biology_rng(7);
        sim::Pcg32 foliar = sim::make_stream(static_cast<std::uint64_t>(trial),
                                             static_cast<std::uint64_t>(sim::RngStream::Foliar));
        // 밤 4 °C: 과실 진행 0, 광합성 0, 흡수 없음
        (void)process::update_crop(crop, cell, p, process::NutrientAvailability{}, env_at(4.0), bio, ledger, &foliar);
        rotten_sum += static_cast<double>(ledger.fruit_rot_count);
        CHECK(static_cast<double>(ledger.fruit_rot_count) == crop.last_fruit_rot_count);
        CHECK(crop.plant_N_g_per_m2 + cell.total_N_g_per_m2() == Approx(N_before).epsilon(1e-12));
        CHECK(crop.plant_K_g_per_m2 + cell.k_g_per_m2 == Approx(K_before).epsilon(1e-12));
        CHECK(cell.organic_C_g_per_m2() - C_before == Approx(ledger.C_leaf_litter_g + ledger.C_root_litter_g));
    }
    const double mean = rotten_sum / kTrials;
    // 기대 약 7.7개. 표준오차 약 0.19라 ±1이면 넉넉해요
    CHECK(mean == Approx(expected).margin(1.0));
}

// 포자 압력: 원천 0, 환기 대 밀폐 → 환기 쪽이 빨리 줄어듦. 배경값이 있으면 그 값으로 수렴
TEST_CASE("Spore pressure vents faster and converges to background", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    sim::HouseState closed;
    closed.botrytis_spore_index = 0.5;
    closed.powdery_mildew_spore_index = 0.5;
    closed.vent_path_index = 0.0;
    sim::HouseState vented = closed;
    vented.vent_path_index = 1.0;
    const sim::FoliarInit none{};
    for (int h = 0; h < 24; ++h) {
        process::update_foliar_house(closed, p, none, process::FoliarHouseSources{}, 1);
        process::update_foliar_house(vented, p, none, process::FoliarHouseSources{}, 1);
    }
    CHECK(vented.botrytis_spore_index < closed.botrytis_spore_index);
    CHECK(vented.powdery_mildew_spore_index < closed.powdery_mildew_spore_index);
    CHECK(closed.botrytis_spore_index < 0.5);

    // 배경값 (5차 프로토타입): 배경 원천 bg × L ÷ (1 − bg)라서 원천이 없으면 평형이 정확히 배경값이에요.
    // (4차의 bg × 감쇠 원천은 bg·d / (bg·d + d + vent·ach)로 수렴해 배경값보다 낮았어요)
    const sim::FoliarInit background{.botrytis_background_index = 0.05, .powdery_mildew_background_index = 0.02};
    sim::HouseState h;  // 창 닫힘 (ach = 틈새)
    for (int t = 0; t < 24 * 120; ++t) {
        process::update_foliar_house(h, p, background, process::FoliarHouseSources{}, 1);
    }
    CHECK(h.botrytis_spore_index == Approx(0.05).epsilon(1e-6));
    CHECK(h.powdery_mildew_spore_index == Approx(0.02).epsilon(1e-6));
}

// 배경 0.05, 원천 0: 0에서 시작해도, 0.5에서 시작해도, 창 닫힘·전부 개방 모두 0.05로 수렴
TEST_CASE("Spore pressure without source converges to background 0.05 closed and vented", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    const sim::FoliarInit background{.botrytis_background_index = 0.05, .powdery_mildew_background_index = 0.05};
    for (const double vent : {0.0, 1.0}) {
        for (const double start : {0.0, 0.5}) {
            sim::HouseState h;
            h.vent_path_index = vent;
            h.botrytis_spore_index = start;
            h.powdery_mildew_spore_index = start;
            for (int t = 0; t < 24 * 365; ++t) {
                process::update_foliar_house(h, p, background, process::FoliarHouseSources{}, 1);
            }
            CHECK(h.botrytis_spore_index == Approx(0.05).epsilon(1e-6));
            CHECK(h.powdery_mildew_spore_index == Approx(0.05).epsilon(1e-6));
        }
    }
}

// ---- 4.4 흰가루병 ----

namespace {

sim::Crop mildew_crop() {
    sim::Crop crop = make_crop();
    crop.infection_powdery_mildew_index = 0.05;
    crop.canopy_rh_index = 0.85;
    crop.leaf_wetness_index = 0.0;
    crop.rgr_mean_per_day = 0.03;
    return crop;
}

}  // namespace

// 잎 사이 습도 0.5 대 0.85: 0.5는 증가 0
TEST_CASE("Powdery mildew does not grow in dry canopy air", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    const sim::SoilCell cell = test::test_cell();
    sim::Crop dry = mildew_crop();
    dry.canopy_rh_index = 0.5;
    sim::Crop humid = mildew_crop();
    infect(dry, cell, p, env_at(20.0));
    infect(humid, cell, p, env_at(20.0));
    CHECK(dry.last_powdery_mildew_increase_index == 0.0);
    CHECK(humid.last_powdery_mildew_increase_index > 0.0);
}

// 잎 젖음 1 대 0, 같은 습도: 젖은 쪽 증가가 작음
TEST_CASE("Free water on leaves inhibits powdery mildew", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    const sim::SoilCell cell = test::test_cell();
    sim::Crop wet = mildew_crop();
    wet.leaf_wetness_index = 1.0;
    sim::Crop dry = mildew_crop();
    infect(wet, cell, p, env_at(20.0));
    infect(dry, cell, p, env_at(20.0));
    CHECK(wet.last_powdery_mildew_increase_index < dry.last_powdery_mildew_increase_index);
}

// RGR 0.03 대 0: 생장이 빠른 쪽 증가가 큼
TEST_CASE("Young fast growing leaves get more powdery mildew", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    const sim::SoilCell cell = test::test_cell();
    sim::Crop fast = mildew_crop();
    sim::Crop still = mildew_crop();
    still.rgr_mean_per_day = 0.0;
    infect(fast, cell, p, env_at(20.0));
    infect(still, cell, p, env_at(20.0));
    CHECK(fast.last_powdery_mildew_increase_index > still.last_powdery_mildew_increase_index);
}

// 잎 건물이 2배로: 병든 면적 비율이 절반 (희석). 45 °C라 감염 증가 0
TEST_CASE("New clean leaves dilute the powdery mildew fraction", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    const sim::SoilCell cell = test::test_cell();
    sim::Crop crop = mildew_crop();
    crop.infection_powdery_mildew_index = 0.2;
    const double before_leaf = crop.leaf_g_per_m2;
    crop.leaf_g_per_m2 = 2.0 * before_leaf;
    process::FoliarOnsets onsets{};
    process::update_foliar_infection(crop, cell, p, env_at(45.0), 1.0, before_leaf, 0, onsets);
    CHECK(crop.infection_powdery_mildew_index == Approx(0.1));
}

// 흰가루병 0.17: 총생산 약 −5%
TEST_CASE("Powdery mildew at 0.17 cuts gross production by about five percent", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    sim::Crop crop = make_crop();
    crop.infection_powdery_mildew_index = 0.17;
    CHECK(process::foliar_photosynthesis_factor(crop, p.foliar_disease) == Approx(0.95));
    // 실제 h3 총생산도 같은 비율 (낮 25 °C, 같은 셀)
    const sim::Params q = test::quiet_params();
    sim::Crop healthy = make_crop();
    sim::Crop sick = healthy;
    sick.infection_powdery_mildew_index = 0.17;
    sim::SoilCell c1 = test::test_cell();
    sim::SoilCell c2 = test::test_cell();
    sim::Ledger l1;
    sim::Ledger l2;
    sim::Pcg32 r1 = test::biology_rng(3);
    sim::Pcg32 r2 = test::biology_rng(3);
    (void)process::update_crop(healthy, c1, q, process::available_nutrients(c1, q), test::crop_env(300.0, 22.0), r1, l1);
    (void)process::update_crop(sick, c2, q, process::available_nutrients(c2, q), test::crop_env(300.0, 22.0), r2, l2);
    REQUIRE(healthy.last_gross_g_per_m2 > 0.0);
    CHECK(sick.last_gross_g_per_m2 / healthy.last_gross_g_per_m2 == Approx(0.95));
}

// ---- 4.5 응애·칠레이리응애 ----

namespace {

process::CropEnvironment mite_env(double air_degC, double rh_mean) {
    process::CropEnvironment env = env_at(air_degC);
    env.house_rh_mean_index = rh_mean;
    return env;
}

void mites(sim::Crop& crop, const sim::Params& p, const process::CropEnvironment& env, bool same_mean = true) {
    process::CropEnvironment e = env;
    if (same_mean) {
        e.house_spider_mite_mean_index = crop.spider_mite_index;  // 하우스 전체가 같은 밀도 (이동 0)
    }
    process::FoliarOnsets onsets{};
    process::update_mites(crop, p.foliar_disease, e, 0, onsets);
}

}  // namespace

// 응애: 25 대 10 °C → 10 °C는 증가 0
TEST_CASE("Spider mites grow at 25 C and stop at 10 C", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    sim::Crop warm = make_crop();
    warm.spider_mite_index = 0.01;
    sim::Crop cold = warm;
    mites(warm, p, mite_env(25.0, 0.7));
    mites(cold, p, mite_env(10.0, 0.7));
    CHECK(warm.spider_mite_index > 0.01);
    CHECK(cold.spider_mite_index == 0.01);
}

// 응애 0.01, 25 °C, 천적 없음: S자로 증가, 1을 넘지 않음
TEST_CASE("Spider mites grow logistically without predators", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    sim::Crop crop = make_crop();
    crop.spider_mite_index = 0.01;
    double prev = crop.spider_mite_index;
    double max_step = 0.0;
    double max_step_at = 0.0;
    for (int h = 0; h < 24 * 120; ++h) {
        mites(crop, p, mite_env(25.0, 0.7));
        const double step = crop.spider_mite_index - prev;
        CHECK(step >= 0.0);
        CHECK(crop.spider_mite_index <= 1.0);
        if (step > max_step) {
            max_step = step;
            max_step_at = prev;
        }
        prev = crop.spider_mite_index;
    }
    CHECK(crop.spider_mite_index > 0.9);
    CHECK(max_step_at > 0.3);  // 가장 빠른 증가는 중간 밀도 (S자)
    CHECK(max_step_at < 0.7);
}

// 응애, 25 °C, 습도 평균 0.5 대 0.9: 0.5 쪽 증가가 조금 빠름
TEST_CASE("Spider mites grow a little faster in dry air", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    sim::Crop dry = make_crop();
    dry.spider_mite_index = 0.01;
    sim::Crop humid = dry;
    for (int h = 0; h < 24 * 7; ++h) {
        mites(dry, p, mite_env(25.0, 0.5));
        mites(humid, p, mite_env(25.0, 0.9));
    }
    CHECK(dry.spider_mite_index > humid.spider_mite_index);
    CHECK(dry.spider_mite_index < 2.0 * humid.spider_mite_index);  // 약한 곡선 (최대 1.15배)
}

// 응애 0.05 + 천적, 습도 평균 0.8 대 0.45: 습한 쪽이 응애를 더 누르고 천적이 더 남아요.
// (명세 초기값에서는 응애 0.05의 먹이로 천적이 늘지 못해(배부름 낮음 → 굶음 사망) 0.8 쪽도 천적이 줄어요.
//  절대 방향 "0.8 쪽 천적 증가"는 차수 보고 B8에서 확인하고, 여기서는 습도의 방향만 봐요)
TEST_CASE("Predatory mites control spider mites better in humid air", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    sim::Crop humid = make_crop();
    humid.spider_mite_index = 0.05;
    humid.predator_mite_index = 0.05;
    sim::Crop dry = humid;
    for (int h = 0; h < 24 * 21; ++h) {
        mites(humid, p, mite_env(25.0, 0.8));
        mites(dry, p, mite_env(25.0, 0.45));
    }
    WARN("after 21 days: humid mite " << humid.spider_mite_index << " predator " << humid.predator_mite_index
                                      << "; dry mite " << dry.spider_mite_index << " predator "
                                      << dry.predator_mite_index);
    CHECK(humid.predator_mite_index > dry.predator_mite_index);
    CHECK(humid.spider_mite_index < dry.spider_mite_index);
    CHECK(dry.predator_mite_index < 0.05);
}

// 응애 0, 천적 0.01: 천적이 굶어 감소
TEST_CASE("Predatory mites starve without prey", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    sim::Crop crop = make_crop();
    crop.predator_mite_index = 0.01;
    for (int h = 0; h < 24 * 5; ++h) {
        mites(crop, p, mite_env(25.0, 0.8));
    }
    CHECK(crop.predator_mite_index < 0.01 * 0.5);
    CHECK(crop.spider_mite_index == 0.0);
}

// (7차 프로토타입) 사망 기온 배율: 먹이 없는 천적이 10 °C에서 25 °C보다 천천히 줄어요 (추우면 대사가 느려요).
// 배율 곡선이 평평하면(test_params) 두 기온의 굶주림이 같아요
TEST_CASE("Predatory mites starve more slowly in the cold", "[unit][m5b][foliar]") {
    sim::Params p = test::test_params();
    sim::Crop flat_cold = make_crop();
    flat_cold.predator_mite_index = 0.01;
    sim::Crop flat_warm = flat_cold;
    for (int h = 0; h < 24 * 5; ++h) {
        mites(flat_cold, p, mite_env(10.0, 0.8));
        mites(flat_warm, p, mite_env(25.0, 0.8));
    }
    CHECK(flat_cold.predator_mite_index == Approx(flat_warm.predator_mite_index));
    p.foliar_disease.mites.predator_death_temp_curve = {
        {{{5, 0.25}}, {{10, 0.35}}, {{15, 0.5}}, {{20, 0.7}}, {{25, 1}}, {{40, 1}}}};
    sim::Crop cold = make_crop();
    cold.predator_mite_index = 0.01;
    sim::Crop warm = cold;
    for (int h = 0; h < 24 * 5; ++h) {
        mites(cold, p, mite_env(10.0, 0.8));
        mites(warm, p, mite_env(25.0, 0.8));
    }
    CHECK(warm.predator_mite_index == Approx(flat_warm.predator_mite_index));  // 25 °C 배율 1 = 옛 값
    CHECK(cold.predator_mite_index > warm.predator_mite_index);
    CHECK(cold.predator_mite_index < 0.01);  // 그래도 굶으면 줄어요
}

// ---- (5차 프로토타입) 천적 하우스 단위 탐색 ----

namespace {

// 포기 여럿: 틱 시작 합 → 천적 탐색 → 포기마다 h7 (응애 이동은 틱 시작 하우스 평균 응애)
void mites_house(std::vector<sim::Crop>& crops, const sim::Params& p, double air_degC, double rh_mean) {
    process::PredatorSearchSums sums;
    for (const sim::Crop& c : crops) {
        process::add_predator_search_sums(sums, c);
    }
    const double mite_mean = sums.live_crops > 0 ? sums.mite_sum / static_cast<double>(sums.live_crops) : 0.0;
    for (sim::Crop& c : crops) {
        process::apply_predator_search(c, sums, p.foliar_disease.mites);
    }
    process::CropEnvironment env = mite_env(air_degC, rh_mean);
    env.house_spider_mite_mean_index = mite_mean;
    for (sim::Crop& c : crops) {
        process::FoliarOnsets onsets{};
        process::update_mites(c, p.foliar_disease, env, 0, onsets);
    }
}

double sum_predators(const std::vector<sim::Crop>& crops) {
    double s = 0.0;
    for (const sim::Crop& c : crops) {
        s += c.predator_mite_index;
    }
    return s;
}

double mean_mites(const std::vector<sim::Crop>& crops) {
    double s = 0.0;
    for (const sim::Crop& c : crops) {
        s += c.spider_mite_index;
    }
    return s / static_cast<double>(crops.size());
}

}  // namespace

// 응애가 몇 포기에만 있으면 천적이 그 포기로 모이고, 이동만으로는 천적 합이 그대로
TEST_CASE("Predators move toward mite infested plants and the move conserves the total", "[unit][m5b][foliar]") {
    sim::Params p = test::test_params();
    p.foliar_disease.mites.predator_search_rate_per_hour = 0.05;
    std::vector<sim::Crop> crops(10, make_crop());
    for (std::size_t i = 0; i < crops.size(); ++i) {
        crops[i].predator_mite_index = 0.001 * static_cast<double>(i + 1);
    }
    crops[2].spider_mite_index = 0.05;
    crops[7].spider_mite_index = 0.01;
    const std::vector<sim::Crop> before = crops;

    process::PredatorSearchSums sums;
    for (const sim::Crop& c : crops) {
        process::add_predator_search_sums(sums, c);
    }
    // 셀 순서와 무관: 거꾸로 적용해도 같은 결과
    std::vector<sim::Crop> reversed = crops;
    for (sim::Crop& c : crops) {
        process::apply_predator_search(c, sums, p.foliar_disease.mites);
    }
    for (auto it = reversed.rbegin(); it != reversed.rend(); ++it) {
        process::apply_predator_search(*it, sums, p.foliar_disease.mites);
    }
    for (std::size_t i = 0; i < crops.size(); ++i) {
        CHECK(crops[i].predator_mite_index == reversed[i].predator_mite_index);
    }
    CHECK(sum_predators(crops) == Approx(sum_predators(before)).epsilon(1e-12));
    CHECK(crops[2].predator_mite_index > before[2].predator_mite_index);
    CHECK(crops[7].predator_mite_index > before[7].predator_mite_index);
    CHECK(crops[2].predator_mite_index - before[2].predator_mite_index >
          crops[7].predator_mite_index - before[7].predator_mite_index);
    CHECK(crops[9].predator_mite_index < before[9].predator_mite_index);  // 응애 없는 포기에서 빠져나감

    // 응애가 하나도 없으면 하우스 평균으로 고르게 퍼짐 (합 그대로)
    std::vector<sim::Crop> clean(4, make_crop());
    clean[0].predator_mite_index = 0.004;
    process::PredatorSearchSums clean_sums;
    for (const sim::Crop& c : clean) {
        process::add_predator_search_sums(clean_sums, c);
    }
    for (sim::Crop& c : clean) {
        process::apply_predator_search(c, clean_sums, p.foliar_disease.mites);
    }
    CHECK(clean[0].predator_mite_index == Approx(0.004 - 0.05 * 0.003));
    CHECK(clean[1].predator_mite_index == Approx(0.05 * 0.001));
    CHECK(sum_predators(clean) == Approx(0.004).epsilon(1e-12));
}

// 80포기 중 4포기에만 응애 0.01 (등급 B), 25 °C, 습도 평균 0.8: 전 포기에 15마리/m² 방사하면
// 4주 뒤 응애가 방사 안 한 쪽보다 낮고 천적이 사라지지 않아요 (4차에서는 다른 포기의 천적이 굶어 사라졌어요)
TEST_CASE("Release on all plants controls mites on a few plants within four weeks", "[unit][m5b][foliar]") {
    sim::Params p = test::test_params();
    p.foliar_disease.mites.predator_search_rate_per_hour = 0.05;
    const double released = 15.0 * p.predators.front().index_per_individual_per_m2;
    std::vector<sim::Crop> base(80, make_crop());
    for (const std::size_t i : {5, 27, 48, 70}) {
        base[i].spider_mite_index = 0.01;
    }
    std::vector<sim::Crop> without = base;
    std::vector<sim::Crop> with = base;
    for (sim::Crop& c : with) {
        c.predator_mite_index = released;
    }
    std::vector<sim::Crop> no_search = with;
    sim::Params p_no_search = p;
    p_no_search.foliar_disease.mites.predator_search_rate_per_hour = 0.0;
    for (int h = 0; h < 24 * 28; ++h) {
        mites_house(without, p, 25.0, 0.8);
        mites_house(with, p, 25.0, 0.8);
        mites_house(no_search, p_no_search, 25.0, 0.8);
    }
    WARN("day 28 mean mite: without " << mean_mites(without) << ", release " << mean_mites(with)
                                      << " (predator sum " << sum_predators(with) << " of released "
                                      << released * 80.0 << "), release without search " << mean_mites(no_search)
                                      << " (predator sum " << sum_predators(no_search) << ")");
    CHECK(mean_mites(with) < mean_mites(without));
    // 방향만: 천적이 0이 되지 않음. (5차 값에서는 응애 밀도가 낮아 굶음 사망이 커서 28일에 방사량의 약 1만분의 1만 남아요.
    //  크기는 차수 보고에서 판단)
    CHECK(sum_predators(with) > 0.0);
}

// 응애 밀도 급감: 이동 평균과 광합성 피해는 서서히 회복
TEST_CASE("Mite damage recovers slowly after a crash", "[unit][m5b][foliar]") {
    const sim::Params p = test::test_params();
    sim::Crop crop = make_crop();
    crop.spider_mite_index = 0.0;
    crop.spider_mite_mean_index = 0.3;
    const double damaged = process::foliar_photosynthesis_factor(crop, p.foliar_disease);
    mites(crop, p, mite_env(25.0, 0.7));
    CHECK(crop.spider_mite_mean_index > 0.29);
    for (int h = 0; h < 24 * 14; ++h) {
        mites(crop, p, mite_env(25.0, 0.7));
    }
    const double later = process::foliar_photosynthesis_factor(crop, p.foliar_disease);
    CHECK(crop.spider_mite_mean_index < 0.3 * 0.6);
    CHECK(crop.spider_mite_mean_index > 0.0);
    CHECK(later > damaged);
    CHECK(later < 1.0);
}

// ---- 명령·정식 (World) ----

namespace {

sim::Scenario crop_scenario(const char* grade, int rows = 2, int cols = 3, int day = 260) {
    sim::Scenario s = test::test_scenario(day, rows, cols);
    s.crop = sim::CropInit{.seedling_grade = grade, .plants_per_m2 = 8.0, .initial_establishment_index = 1.0};
    return s;
}

// 세이브 버퍼 안에서 Foliar 스트림의 원시 상태(시작값)를 찾아요 (그대로면 한 번도 안 뽑은 것)
bool foliar_stream_untouched(const sim::World& world) {
    const sim::Pcg32 fresh =
        sim::make_stream(world.scenario().seed, static_cast<std::uint64_t>(sim::RngStream::Foliar));
    std::array<std::byte, 16> needle{};
    const std::uint64_t state = fresh.raw_state();
    const std::uint64_t inc = fresh.raw_increment();
    for (std::size_t i = 0; i < 8; ++i) {
        needle[i] = static_cast<std::byte>((state >> (8 * i)) & 0xFF);
        needle[8 + i] = static_cast<std::byte>((inc >> (8 * i)) & 0xFF);
    }
    const std::vector<std::byte> bytes = world.serialize();
    return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
}

}  // namespace

// ReleasePredators 15마리/m²: 천적 지수 +0.00045, 장부 증가
TEST_CASE("Releasing 15 predators per square meter adds 0.00045", "[unit][m5b][foliar][command]") {
    sim::World world = test::make_world(crop_scenario("A"));
    test::must_enqueue(world, sim::ReleasePredatorsCommand{.area = sim::whole_grid(2, 3), .predator_id = "persimilis",
                                                           .per_m2 = 15.0});
    CHECK_FALSE(world.enqueue(sim::ReleasePredatorsCommand{.area = sim::whole_grid(2, 3), .predator_id = "cucumeris",
                                                           .per_m2 = 15.0})
                    .has_value());
    world.step(1);
    const sim::Snapshot s = world.snapshot();
    CHECK(s.ledger.predators_released_per_m2 == Approx(15.0));
    for (const sim::Crop& c : s.crops) {
        // 방사 뒤 그 틱의 h7에서 조금 줄어요 (먹이 없음)
        CHECK(c.predator_mite_index == Approx(0.00045).epsilon(0.02));
        CHECK(c.predator_mite_index > 0.0);
    }
}

// RemovePlant: 그 셀 작물 없음, 원소가 removed 장부로(보존), 하우스 포자 원천 감소. 빈 셀이면 거부
TEST_CASE("RemovePlant empties the tile and exports its elements", "[unit][m5b][foliar][command]") {
    sim::Params p = test::test_params();
    sim::World world = [&] {
        auto w = sim::World::create(p, crop_scenario("C", 1, 2));
        REQUIRE(w.has_value());
        return std::move(*w);
    }();
    world.step(24);
    const sim::Snapshot before = world.snapshot();
    const sim::Crop& victim = before.crops[1];
    REQUIRE(victim.present);
    const sim::TileArea tile1{.row = 0, .col = 1, .rows = 1, .cols = 1};
    test::must_enqueue(world, sim::RemovePlantCommand{.area = tile1});
    world.step(1);
    const sim::Snapshot after = world.snapshot();
    CHECK_FALSE(after.crops[1].present);
    CHECK(after.crops[0].present);
    CHECK(after.ledger.plants_removed == 1);
    CHECK(after.ledger.N_removed_g == Approx(victim.plant_N_g_per_m2));
    CHECK(after.ledger.K_removed_g == Approx(victim.plant_K_g_per_m2));
    CHECK(after.ledger.dry_matter_removed_g > 0.0);
    CHECK(after.crops[1].spider_mite_index == 0.0);
    CHECK(after.crops[1].leaf_wetness_index == 0.0);
    // 빈 칸은 거부
    CHECK_FALSE(world.enqueue(sim::RemovePlantCommand{.area = tile1}).has_value());
    // 원소 보존: 토양 + 체내 + removed (한 틱 동안)
    const auto total_N = [](const sim::Snapshot& s) {
        double n = 0.0;
        for (std::size_t i = 0; i < s.soil.size(); ++i) {
            n += s.soil[i].total_N_g_per_m2() + s.crops[i].plant_N_g_per_m2;
        }
        return n + s.ledger.N_removed_g + s.ledger.N_leached_g + s.ledger.N_gas_g + s.ledger.N_harvested_g -
               s.ledger.N_in_g;
    };
    CHECK(total_N(after) == Approx(total_N(before)).epsilon(1e-10));
}

// RemovePlant: 병든 포기를 뽑으면 하우스 흰가루 포자 원천이 줄어요
TEST_CASE("Removing diseased plants lowers the house spore source", "[unit][m5b][foliar][command]") {
    const sim::Params p = test::test_params();
    process::FoliarHouseSources all;
    process::FoliarHouseSources half;
    sim::Crop sick = make_crop();
    sick.infection_powdery_mildew_index = 0.2;
    sim::Crop removed = sick;
    sim::Ledger ledger;
    process::remove_plant(removed, ledger);
    for (const sim::Crop* c : {&sick, &sick}) {
        process::add_foliar_house_sources(all, *c, p, 20.0);
    }
    for (const sim::Crop* c : {&sick, &removed}) {
        process::add_foliar_house_sources(half, *c, p, 20.0);
    }
    CHECK(half.powdery_mildew_source_sum < all.powdery_mildew_source_sum);
    CHECK(half.live_crops == 1);
}

// SetFan·InstallDrip: 팬 시간 장부, 점적 설치 뒤 관수는 두상 표시 없음, 이미 깔린 점적은 거부
TEST_CASE("SetFan and InstallDrip change the house and irrigation kind", "[unit][m5b][foliar][command]") {
    sim::Scenario s = crop_scenario("A");
    s.drip_installed = false;
    sim::World world = test::make_world(s);
    test::must_enqueue(world, sim::SetFanCommand{.on = true});
    test::must_enqueue(world, sim::IrrigateCommand{.area = sim::whole_grid(2, 3), .amount_mm = 2.0});
    world.step(1);
    {
        const sim::Snapshot snap = world.snapshot();
        CHECK(snap.house.fan_on);
        CHECK(snap.house.fan_hours == 1.0);
        for (std::size_t i = 0; i < snap.soil.size(); ++i) {
            CHECK(snap.soil[i].overhead_irrigated_this_tick);
            CHECK(snap.crops[i].leaf_wetness_index == Approx(test::test_params().humidity.overhead_wet_index));
        }
    }
    test::must_enqueue(world, sim::InstallDripCommand{});
    test::must_enqueue(world, sim::IrrigateCommand{.area = sim::whole_grid(2, 3), .amount_mm = 2.0});
    world.step(1);
    const sim::Snapshot snap = world.snapshot();
    CHECK(snap.house.drip_installed);
    CHECK(snap.house.drip_installations == 1.0);
    CHECK(snap.house.fan_hours == 2.0);
    for (const sim::SoilCell& c : snap.soil) {
        CHECK_FALSE(c.overhead_irrigated_this_tick);
    }
    CHECK_FALSE(world.enqueue(sim::InstallDripCommand{}).has_value());
}

// 정식: 등급 C → 잠복 포기 비율 약 25%, 응애 포기 약 15%, 흰가루 시작 0.02. 같은 시드면 같은 배치
TEST_CASE("Grade C seedlings carry latent anthracnose mites and mildew", "[unit][m5b][foliar][planting]") {
    const sim::World a = test::make_world(crop_scenario("C", 20, 20));
    const sim::World b = test::make_world(crop_scenario("C", 20, 20));
    const sim::Snapshot sa = a.snapshot();
    const sim::Snapshot sb = b.snapshot();
    double latent = 0.0;
    double mite = 0.0;
    for (std::size_t i = 0; i < sa.crops.size(); ++i) {
        const sim::Crop& c = sa.crops[i];
        latent += c.infection_anthracnose_index > 0.0 ? 1.0 : 0.0;
        mite += c.spider_mite_index > 0.0 ? 1.0 : 0.0;
        CHECK(c.infection_powdery_mildew_index == 0.02);
        CHECK(c.infection_anthracnose_index == sb.crops[i].infection_anthracnose_index);
        CHECK(c.spider_mite_index == sb.crops[i].spider_mite_index);
        CHECK(c.soil_mineral_N_mean_g_per_m2 == Approx(sa.soil[i].nh4_N_g_per_m2 + sa.soil[i].no3_N_g_per_m2));
    }
    const double n = static_cast<double>(sa.crops.size());
    CHECK(latent / n == Approx(0.25).margin(0.06));
    CHECK(mite / n == Approx(0.15).margin(0.05));
    CHECK_FALSE(foliar_stream_untouched(a));
}

// 정식: 등급 A → Foliar 난수를 뽑지 않음 (월드 생성과 Plant 명령)
TEST_CASE("Grade A planting draws nothing from the foliar stream", "[unit][m5b][foliar][planting]") {
    sim::World world = test::make_world(crop_scenario("A"));
    CHECK(foliar_stream_untouched(world));
    world.step(48);  // 배경 포자 0, 병 없음: 썩음·흰가루 판정 없음
    CHECK(foliar_stream_untouched(world));
    // Plant 명령 (작물 없는 하우스)
    sim::World empty = test::make_world(test::test_scenario(260, 2, 3));
    test::must_enqueue(empty, sim::PlantCommand{.seedling_grade = "A", .plants_per_m2 = 8.0});
    empty.step(1);
    CHECK(empty.snapshot().crops[0].present);
    CHECK(foliar_stream_untouched(empty));
}

// 정식: 등급 B 대 등급 A, 같은 시드 → biology·Disease 스트림 수열(화아분화 차이·감수성·병원균 편차)이 같음
TEST_CASE("Seedling disease does not shift biology or disease streams", "[unit][m5b][foliar][planting]") {
    sim::Params p = test::test_params();
    p.phenology.induction_spread_index = 0.4;
    p.microbes.susceptibility_spread_index = 0.5;
    p.microbes.pathogen_spread_index = 0.5;
    // 등급 B의 크기·화아분화를 A와 같게 두고 병해 값만 B로 (두 월드의 차이가 모종 병해뿐이게)
    sim::SeedlingGradeDef& grade_b = p.seedling_grades[1];
    const sim::SeedlingGradeDef& grade_a = p.seedling_grades[0];
    grade_b.leaf_g_per_plant = grade_a.leaf_g_per_plant;
    grade_b.crown_g_per_plant = grade_a.crown_g_per_plant;
    grade_b.root_g_per_plant = grade_a.root_g_per_plant;
    grade_b.initial_induction_index = grade_a.initial_induction_index;
    sim::Scenario sa = crop_scenario("A", 6, 6);
    sim::Scenario sb = crop_scenario("B", 6, 6);
    sa.soil.microbe_index[sim::microbe(sim::MicrobeGroup::Fusarium)] = 0.3;
    sb.soil.microbe_index[sim::microbe(sim::MicrobeGroup::Fusarium)] = 0.3;
    auto wa = sim::World::create(p, sa);
    auto wb = sim::World::create(p, sb);
    REQUIRE(wa.has_value());
    REQUIRE(wb.has_value());
    const sim::Snapshot a = wa->snapshot();
    const sim::Snapshot b = wb->snapshot();
    bool any_disease = false;
    for (std::size_t i = 0; i < a.crops.size(); ++i) {
        CHECK(a.crops[i].induction_index == b.crops[i].induction_index);
        CHECK(a.crops[i].disease_susceptibility_factor == b.crops[i].disease_susceptibility_factor);
        CHECK(a.soil[i].microbe_index == b.soil[i].microbe_index);
        any_disease = any_disease || b.crops[i].infection_anthracnose_index > 0.0 || b.crops[i].spider_mite_index > 0.0;
    }
    CHECK(any_disease);
}

// 등급 A, 흙 접종원 0, 배경 포자 0, 증산 습도 곡선 평탄, 틈새 0.3, 밤 기본 증산 없음 →
// 지상부 지수가 작기 내내 0이고 결과가 M5a와 비트 단위로 같음.
// 기대값은 M5a 커밋 d6dff07의 코어에 같은 지문 코드(tests/m5a_fingerprint.hpp)를 붙여 빌드해 기록했어요.
TEST_CASE("Grade A without inoculum is bit identical to M5a", "[m5b][foliar][determinism]") {
    constexpr std::uint64_t kM5aFingerprint = 0x36b7dc7c59d09278ULL;
    const sim::Params p = test::m5a_fingerprint::params();
    REQUIRE(p.crop.transpiration_humidity_curve(0.95) == 1.0);
    REQUIRE(p.house.infiltration_air_changes_per_hour == 0.3);
    REQUIRE(p.crop.basal_transpiration_mm_per_hour_per_lai == 0.0);
    CHECK(test::m5a_fingerprint::run() == kM5aFingerprint);
    // 지상부 지수 0 확인 (같은 실행을 다시)
    auto world = sim::World::create(p, test::m5a_fingerprint::scenario());
    REQUIRE(world.has_value());
    for (int day = 0; day < 120; ++day) {
        world->step(24);
        for (const sim::Crop& c : world->snapshot().crops) {
            REQUIRE(c.infection_anthracnose_index == 0.0);
            REQUIRE(c.infection_botrytis_index == 0.0);
            REQUIRE(c.infection_powdery_mildew_index == 0.0);
            REQUIRE(c.spider_mite_index == 0.0);
            REQUIRE(c.predator_mite_index == 0.0);
        }
    }
    CHECK(foliar_stream_untouched(*world));
}

// 세이브 왕복: 모든 상태와 Foliar 스트림 상태가 같음
TEST_CASE("Foliar state and stream survive a save round trip", "[unit][m5b][foliar][serialization]") {
    sim::Scenario s = crop_scenario("C", 3, 4);
    s.drip_installed = false;
    s.foliar = {.botrytis_background_index = 0.05, .powdery_mildew_background_index = 0.02};
    s.soil.microbe_index[sim::microbe(sim::MicrobeGroup::Anthracnose)] = 0.2;
    sim::World world = test::make_world(s);
    test::must_enqueue(world, sim::SetFanCommand{.on = true});
    test::must_enqueue(world, sim::IrrigateCommand{.area = sim::whole_grid(3, 4), .amount_mm = 3.0});
    test::must_enqueue(world, sim::ReleasePredatorsCommand{.area = sim::whole_grid(3, 4), .predator_id = "persimilis",
                                                           .per_m2 = 15.0});
    world.step(24 * 10);
    test::must_enqueue(world, sim::RemovePlantCommand{.area = {.row = 0, .col = 0, .rows = 1, .cols = 1}});
    test::must_enqueue(world, sim::InstallDripCommand{});  // 대기 명령도 세이브에 들어가요
    auto loaded = sim::World::deserialize(world.serialize());
    REQUIRE(loaded.has_value());
    CHECK(loaded->state_hash() == world.state_hash());
    const sim::Snapshot a = world.snapshot();
    const sim::Snapshot b = loaded->snapshot();
    CHECK(a.house == b.house);
    CHECK(a.crops == b.crops);
    CHECK(a.soil == b.soil);
    CHECK(a.ledger == b.ledger);
    world.step(24 * 5);
    loaded->step(24 * 5);
    CHECK(loaded->state_hash() == world.state_hash());
    CHECK(loaded->snapshot().ledger == world.snapshot().ledger);
}

// 극한: 포자 1·연속 젖음 1000 h·N 100배·기온 45 °C → 지수 0~1, NaN 없음
TEST_CASE("Foliar indices stay bounded under extreme inputs", "[unit][m5b][foliar][extreme]") {
    const sim::Params p = test::test_params();
    sim::SoilCell cell = cell_with_anthracnose(1.0);
    cell.nh4_N_g_per_m2 = 200.0;
    cell.no3_N_g_per_m2 = 1000.0;
    for (const double air : {-10.0, 20.0, 27.0, 45.0}) {
        sim::Crop crop = make_crop();
        crop.infection_anthracnose_index = 0.9;
        crop.infection_botrytis_index = 0.9;
        crop.infection_powdery_mildew_index = 0.9;
        crop.spider_mite_index = 0.9;
        crop.predator_mite_index = 1.0;
        crop.wet_run_hours = 1000.0;
        crop.canopy_rh_index = 0.85;
        crop.rgr_mean_per_day = 1.0;
        crop.disease_susceptibility_factor = 1.5;
        process::CropEnvironment env = mite_env(air, 1.0);
        env.botrytis_spore_index = 1.0;
        env.powdery_mildew_spore_index = 1.0;
        env.anthracnose_sporulation_index = 1.0;
        env.house_spider_mite_mean_index = 1.0;
        env.overhead_irrigated = true;
        for (int h = 0; h < 500; ++h) {
            const double s_N = process::update_n_susceptibility(crop, cell, p.foliar_disease);
            process::FoliarOnsets onsets{};
            process::update_foliar_infection(crop, cell, p, env, s_N * 100.0, crop.leaf_g_per_m2 * 0.5, 0, onsets);
            process::update_mites(crop, p.foliar_disease, env, 0, onsets);
            for (const double v : {crop.infection_anthracnose_index, crop.infection_botrytis_index,
                                   crop.infection_powdery_mildew_index, crop.spider_mite_index,
                                   crop.spider_mite_mean_index, crop.predator_mite_index}) {
                REQUIRE(std::isfinite(v));
                REQUIRE(v >= 0.0);
                REQUIRE(v <= 1.0);
            }
            REQUIRE(std::isfinite(crop.soil_mineral_N_mean_g_per_m2));
            REQUIRE(crop.soil_mineral_N_mean_g_per_m2 >= 0.0);
        }
        CHECK(crop.foliar_onset_reported[kAnth]);
    }
    // 하우스 포자: 원천 최대, 환기 최대에서도 0~1
    sim::HouseState h;
    h.vent_path_index = 1.0;
    process::FoliarHouseSources big;
    big.botrytis_source_sum = 1e6;
    big.powdery_mildew_source_sum = 1e6;
    big.anthracnose_sporulation_sum = 1e6;
    for (int t = 0; t < 10; ++t) {
        process::update_foliar_house(h, p, {.botrytis_background_index = 0.999, .powdery_mildew_background_index = 0.999},
                                     big, 1);
        for (const double v : {h.botrytis_spore_index, h.powdery_mildew_spore_index, h.anthracnose_sporulation_index}) {
            REQUIRE(std::isfinite(v));
            REQUIRE(v >= 0.0);
            REQUIRE(v <= 1.0);
        }
    }
}
