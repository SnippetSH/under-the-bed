// docs/spec/soil-microbes.md 3번 "작물 감염" (M5a) 단위 테스트: 감염 압력·환경 곡선·길항균 보호,
// 감염은 줄지 않음, DiseaseOnset 이벤트 1회, 뿌리 손상 곡선과 고사 원인.
#include <cstddef>
#include <optional>
#include <variant>

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

sim::Crop seedling(const sim::Params& p) { return process::plant_crop(test::test_crop_init(), p.crop); }

// 감염만 hours시간 진행 (셀 고정)
sim::Crop infect(const sim::SoilCell& cell, const sim::Params& p, int hours) {
    sim::Crop crop = seedling(p);
    for (int h = 0; h < hours; ++h) {
        (void)process::update_soil_infection(crop, cell, p, 0);
    }
    return crop;
}

}  // namespace

// 시들음병균 0.5, pH 5.0 대 6.5: 5.0 쪽 감염이 빠름
TEST_CASE("Acid soil speeds fusarium infection", "[unit][m5a][soil_disease]") {
    const sim::Params p = test::test_params();
    sim::SoilCell acid = test::test_cell();
    acid.soil_temp_degC = 28.0;
    acid.microbe_index[idx(G::Fusarium)] = 0.5;
    sim::SoilCell neutral = acid;
    acid.ph = 5.0;
    neutral.ph = 6.5;
    CHECK(process::infection_pressure(D::Fusarium, acid, p) ==
          Approx(process::infection_pressure(D::Fusarium, neutral, p) * 1.7));
    const sim::Crop a = infect(acid, p, 24 * 7);
    const sim::Crop n = infect(neutral, p, 24 * 7);
    CHECK(a.infection_index[dis(D::Fusarium)] > n.infection_index[dis(D::Fusarium)]);
    CHECK(n.infection_index[dis(D::Fusarium)] > 0.0);
}

// 역병균 0.5, 통기 0.1·지온 15 °C 대 통기 0.5: 과습 쪽만 감염
TEST_CASE("Phytophthora infects only waterlogged roots", "[unit][m5a][soil_disease]") {
    const sim::Params p = test::test_params();
    sim::SoilCell wet = test::test_cell();
    wet.soil_temp_degC = 15.0;
    wet.microbe_index[idx(G::Phytophthora)] = 0.5;
    sim::SoilCell aerated = wet;
    wet.aeration_index = 0.1;
    aerated.aeration_index = 0.5;
    const sim::Crop w = infect(wet, p, 24 * 14);
    const sim::Crop a = infect(aerated, p, 24 * 14);
    CHECK(w.infection_index[dis(D::Phytophthora)] > 0.0);
    CHECK(a.infection_index[dis(D::Phytophthora)] == 0.0);
    // 시들음병균이 없으면 시들음병 감염 없음
    CHECK(w.infection_index[dis(D::Fusarium)] == 0.0);
}

// 길항균이 감염 압력을 줄여요 (지수 1 = protection 0.5)
TEST_CASE("Antagonist protects roots from infection pressure", "[unit][m5a][soil_disease]") {
    const sim::Params p = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.soil_temp_degC = 28.0;
    cell.microbe_index[idx(G::Fusarium)] = 0.5;
    cell.microbe_index[idx(G::Antagonist)] = 0.0;
    const double bare = process::infection_pressure(D::Fusarium, cell, p);
    cell.microbe_index[idx(G::Antagonist)] = 1.0;
    const double protected_pressure = process::infection_pressure(D::Fusarium, cell, p);
    CHECK(protected_pressure == Approx(bare * 0.5));
}

// 감염 지수 증가 → 뿌리 손상 증가, 지수는 줄지 않음. DiseaseOnset은 처음 한 번만
TEST_CASE("Infection never decreases and raises root damage", "[unit][m5a][soil_disease]") {
    const sim::Params p = test::quiet_params();
    sim::SoilCell cell = test::test_cell();
    cell.soil_temp_degC = 28.0;
    cell.ph = 5.5;
    cell.microbe_index[idx(G::Fusarium)] = 1.0;
    sim::Crop crop = seedling(p);
    int onsets = 0;
    double previous = 0.0;
    for (int h = 0; h < 24 * 20; ++h) {
        const auto events = process::update_soil_infection(crop, cell, p, 7);
        if (events[dis(D::Fusarium)]) {
            ++onsets;
            CHECK(events[dis(D::Fusarium)]->tile_index == 7);
            CHECK(crop.infection_index[dis(D::Fusarium)] >= p.soil_disease[dis(D::Fusarium)].onset_infection_index);
        }
        CHECK(crop.infection_index[dis(D::Fusarium)] >= previous);
        CHECK(crop.infection_index[dis(D::Fusarium)] <= 1.0);
        previous = crop.infection_index[dis(D::Fusarium)];
        // 병원균이 사라져도 감염은 남아요
        if (h == 24 * 10) {
            cell.microbe_index[idx(G::Fusarium)] = 0.0;
        }
    }
    CHECK(onsets == 1);
    CHECK(crop.disease_onset_reported[dis(D::Fusarium)]);
    CHECK(p.root_damage.fusarium_curve(0.4) > p.root_damage.fusarium_curve(0.1));
    CHECK(p.root_damage.phytophthora_curve(0.4) > p.root_damage.fusarium_curve(0.4));  // 역병은 급성
}

// 작물 갱신(h6): 감염된 포기는 뿌리 건강도가 줄고 결국 고사, 원인은 해당 병
TEST_CASE("Infected crop loses root health and dies of the disease", "[unit][m5a][soil_disease]") {
    const sim::Params p = test::quiet_params();
    sim::SoilCell cell = test::test_cell();
    cell.soil_temp_degC = 22.0;
    cell.aeration_index = 0.6;  // 침수 손상 없음
    cell.microbe_index[idx(G::Phytophthora)] = 1.0;
    sim::Crop crop = seedling(p);
    crop.infection_index[dis(D::Phytophthora)] = 0.8;
    sim::Crop healthy = seedling(p);
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(2);
    sim::Pcg32 rng2 = test::biology_rng(2);
    sim::SoilCell cell2 = cell;
    cell2.microbe_index[idx(G::Phytophthora)] = 0.0;
    (void)process::update_crop(crop, cell, p, process::available_nutrients(cell, p), test::crop_env(0.0, 20.0), rng,
                               ledger);
    (void)process::update_crop(healthy, cell2, p, process::available_nutrients(cell2, p), test::crop_env(0.0, 20.0),
                               rng2, ledger);
    CHECK(crop.root_health < healthy.root_health);
    std::optional<sim::PlantDiedEvent> died;
    for (int h = 0; h < 24 * 30 && !died; ++h) {
        died = process::update_crop(crop, cell, p, process::available_nutrients(cell, p), test::crop_env(0.0, 20.0),
                                    rng, ledger)
                   .died;
    }
    REQUIRE(died.has_value());
    CHECK(died->cause == sim::RootDamageCause::Phytophthora);
}

// 월드: 오염 흙에 심은 작물에서 DiseaseOnset 이벤트가 포기마다 한 번, 감염 포기가 흙 속 병원균을 늘림(숙주 되먹임)
TEST_CASE("World reports disease onset once per plant", "[unit][m5a][soil_disease]") {
    sim::Scenario s = test::test_scenario(240, 1, 2);
    s.soil.microbe_index = {1.0, 1.0, 1.0, 0.0, 0.1, 0.5, 0.0};
    s.soil.ph = 5.3;
    s.soil.soil_temp_degC = 28.0;
    s.crop = test::test_crop_init();
    sim::Params p = test::test_params();
    // 지온을 고정하려고 하우스 영향을 없애요 (완화 0, 발열 0)
    p.soil_temperature.relax_rate_per_hour = 0.0;
    p.soil_temperature.heat_degC_per_g_C = 0.0;
    auto created = sim::World::create(p, s);
    REQUIRE(created.has_value());
    sim::World world = std::move(*created);
    int onsets = 0;
    for (int day = 0; day < 20; ++day) {
        world.step(24);
        for (const sim::Event& e : world.events()) {
            if (const auto* onset = std::get_if<sim::DiseaseOnsetEvent>(&e.payload)) {
                CHECK(onset->disease == D::Fusarium);
                ++onsets;
            }
        }
    }
    CHECK(onsets == 2);
    const sim::Snapshot snap = world.snapshot();
    for (std::size_t i = 0; i < snap.crops.size(); ++i) {
        if (snap.crops[i].alive) {
            CHECK(snap.crops[i].infection_index[dis(D::Fusarium)] > 0.15);
        }
        // 감염 숙주가 수용력을 올려 부생 수용력(0.3)보다 많이 유지돼요 (포기가 죽으면 길항균 경쟁으로 다시 줄어요)
        CHECK(snap.soil[i].microbe_index[idx(G::Fusarium)] > 0.3);
    }
}
