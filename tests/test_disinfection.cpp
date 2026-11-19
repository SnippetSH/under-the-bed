// docs/spec/disinfection.md "테스트 (단위)" (M5a): 멀칭(증발·지온·반사광), 태양열 사멸, 약제 잔류·사멸,
// 경운, 약제 뿌리 손상, 소독 무기화·망간 환원, Plant 명령. 보존·극한·직렬화도 여기서 확인해요.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
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

constexpr std::size_t idx(G g) { return sim::microbe(g); }
constexpr std::int32_t kClear = 0;  // test_params().mulches 순서
constexpr std::int32_t kBlack = 1;
constexpr std::int32_t kWhite = 2;
constexpr std::size_t kDazomet = 0;  // test_params().disinfectants 순서
constexpr std::size_t kChloropicrin = 1;
const sim::TileArea kAll{.row = 0, .col = 0, .rows = 2, .cols = 3};

// 소독용 셀: 젖은 흙(relative_water 0.75), 모든 무리 1, 병원균 0.5
sim::SoilCell disinfection_cell() {
    sim::SoilCell cell = test::test_cell();
    cell.microbe_index = {1.0, 1.0, 1.0, 1.0, 1.0, 0.5, 0.5};
    return cell;
}

struct Exposure {
    sim::SoilCell cell;
    sim::MicrobeIndices killed{};  // 무리별 소독 사멸 누적
};

// 지온·수분을 고정한 채 hours시간 d1 → d2를 돌려요 (작물 없음)
Exposure expose(sim::SoilCell cell, const sim::Params& p, int hours) {
    Exposure e;
    for (int h = 0; h < hours; ++h) {
        process::decay_fumigant(cell, p);
        process::update_microbes(cell, p, {});
        for (std::size_t g = 0; g < sim::kMicrobeGroupCount; ++g) {
            e.killed[g] += cell.disinfection_kill_index[g];
        }
    }
    e.cell = cell;
    return e;
}

struct Totals {
    double N = 0, C = 0, Mn = 0, P = 0, K = 0, Ca = 0, Mg = 0, water = 0;
};

Totals totals_of(const sim::Snapshot& s) {
    Totals t;
    for (std::size_t i = 0; i < s.soil.size(); ++i) {
        const sim::SoilCell& c = s.soil[i];
        const sim::Crop& crop = s.crops[i];
        t.N += c.total_N_g_per_m2() + crop.plant_N_g_per_m2;
        t.C += c.organic_C_g_per_m2();
        t.Mn += c.mn_g_per_m2 + c.mn_oxide_g_per_m2 + crop.plant_Mn_g_per_m2;
        t.P += c.p_g_per_m2 + c.p_fixed_g_per_m2 + crop.plant_P_g_per_m2;
        t.K += c.k_g_per_m2 + crop.plant_K_g_per_m2;
        t.Ca += c.ca_g_per_m2 + crop.plant_Ca_g_per_m2;
        t.Mg += c.mg_g_per_m2 + crop.plant_Mg_g_per_m2;
        t.water += c.water_mm;
    }
    return t;
}

double rel_err(double a, double b) { return std::abs(a - b) / std::max(1.0, std::abs(b)); }

// 보존식 (M4 보존 테스트와 같은 형태, C는 토양 유기 C)
bool conserved(const Totals& init, const sim::Snapshot& s, double tol = 1e-9) {
    const Totals now = totals_of(s);
    const sim::Ledger& l = s.ledger;
    return rel_err(now.N, init.N + l.N_in_g - l.N_leached_g - l.N_gas_g - l.N_harvested_g) <= tol &&
           rel_err(now.C, init.C + l.C_in_g + l.C_root_litter_g + l.C_leaf_litter_g - l.C_respired_g) <= tol &&
           rel_err(now.Mn, init.Mn + l.Mn_in_g - l.Mn_leached_g - l.Mn_harvested_g) <= tol &&
           rel_err(now.P, init.P + l.P_in_g - l.P_leached_g - l.P_harvested_g) <= tol &&
           rel_err(now.K, init.K + l.K_in_g - l.K_leached_g - l.K_harvested_g) <= tol &&
           rel_err(now.Ca, init.Ca + l.Ca_in_g - l.Ca_leached_g - l.Ca_harvested_g) <= tol &&
           rel_err(now.Mg, init.Mg + l.Mg_in_g - l.Mg_leached_g - l.Mg_harvested_g) <= tol &&
           rel_err(now.water, init.water + l.water_in_mm - l.water_evaporated_mm - l.water_transpired_mm -
                                  l.water_drained_mm) <= tol;
}

// M5a 상태 범위: 지수 0~1, 잔류 >= 0, 감염 0~1, 유한
bool m5a_state_valid(const sim::Snapshot& s) {
    for (const sim::SoilCell& c : s.soil) {
        for (const double x : c.microbe_index) {
            if (!std::isfinite(x) || x < 0.0 || x > 1.0) {
                return false;
            }
        }
        for (const double x : c.fumigant_residue_index) {
            if (!std::isfinite(x) || x < 0.0) {
                return false;
            }
        }
        const double pools[] = {c.nh4_N_g_per_m2, c.stable_N_g_per_m2, c.stable_C_g_per_m2, c.labile_C_g_per_m2,
                                c.mn_g_per_m2,    c.mn_oxide_g_per_m2, c.mn_solution_g_per_m2, c.water_mm};
        for (const double x : pools) {
            if (!std::isfinite(x) || x < 0.0) {
                return false;
            }
        }
        if (!std::isfinite(c.soil_temp_degC) || c.mn_solution_g_per_m2 > c.mn_g_per_m2 ||
            c.nh4_solution_N_g_per_m2 > c.nh4_N_g_per_m2) {
            return false;
        }
    }
    for (const sim::Crop& crop : s.crops) {
        for (const double x : crop.infection_index) {
            if (!std::isfinite(x) || x < 0.0 || x > 1.0) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

// 멀칭 없음: 지온 식이 M1~M4와 같음
TEST_CASE("No mulch keeps the M4 soil temperature equation", "[unit][m5a][disinfection]") {
    const sim::Params p = test::test_params();
    sim::SoilCell cell = test::test_cell();
    CHECK(process::soil_relax_target_degC(cell, p, 27.5, 600.0, 0.0) == 27.5);
    CHECK(process::mulch_evaporation_factor(cell, p) == 1.0);
    CHECK(process::mulch_canopy_reflect_factor(cell, p) == 0.0);
    sim::SoilCell a = cell;
    sim::SoilCell b = cell;
    process::relax_soil_temperature(a, p.soil_temperature, 27.5, 0.3);
    process::relax_soil_temperature(b, p.soil_temperature, process::soil_relax_target_degC(b, p, 27.5, 600.0, 0.0),
                                    0.3);
    CHECK(a.soil_temp_degC == b.soil_temp_degC);
}

// 투명 필름, 일사 600, 작물 없음: 완화 목표 = 하우스 기온 + 15 °C. 투명 > 흑색 > 없음 > 백색
TEST_CASE("Film raises the soil relaxation target in the expected order", "[unit][m5a][disinfection]") {
    const sim::Params p = test::test_params();
    sim::SoilCell cell = test::test_cell();
    const double none = process::soil_relax_target_degC(cell, p, 30.0, 600.0, 0.0);
    cell.mulch_def_index = kClear;
    const double clear = process::soil_relax_target_degC(cell, p, 30.0, 600.0, 0.0);
    CHECK(clear == Approx(30.0 + 15.0));
    // 수관이 가리면 이득이 줄어요
    CHECK(process::soil_relax_target_degC(cell, p, 30.0, 600.0, 0.5) == Approx(30.0 + 7.5));
    cell.mulch_def_index = kBlack;
    const double black = process::soil_relax_target_degC(cell, p, 30.0, 600.0, 0.0);
    cell.mulch_def_index = kWhite;
    const double white = process::soil_relax_target_degC(cell, p, 30.0, 600.0, 0.0);
    CHECK(clear > black);
    CHECK(black > none);
    CHECK(none > white);
}

// 투명 필름: 증발이 0.1배
TEST_CASE("Clear film cuts soil evaporation to a tenth", "[unit][m5a][disinfection]") {
    const sim::Params p = test::test_params();
    sim::SoilCell bare = test::test_cell();
    sim::SoilCell film = bare;
    film.mulch_def_index = kClear;
    sim::Ledger lb;
    sim::Ledger lf;
    const double e_bare = process::update_soil_water(bare, p.soil_water, 600.0, 0.0, 0.0, lb);
    const double e_film = process::update_soil_water(film, p.soil_water, 600.0, 0.0, 0.0, lf,
                                                     process::mulch_evaporation_factor(film, p));
    REQUIRE(e_bare > 0.0);
    CHECK(e_film == Approx(e_bare * 0.1));
}

// 같은 온도 시계열, 2주 대 4주: 4주 쪽 사멸 누적이 큼 (완료 기준: 지속 시간)
TEST_CASE("Longer solarization kills more", "[unit][m5a][disinfection]") {
    const sim::Params p = test::test_params();
    sim::SoilCell cell = disinfection_cell();
    cell.soil_temp_degC = 42.0;
    const Exposure two = expose(cell, p, 24 * 14);
    const Exposure four = expose(cell, p, 24 * 28);
    CHECK(four.killed[idx(G::Fusarium)] > two.killed[idx(G::Fusarium)]);
    CHECK(four.cell.microbe_index[idx(G::Fusarium)] < two.cell.microbe_index[idx(G::Fusarium)]);
    CHECK(four.cell.microbe_index[idx(G::Phytophthora)] < two.cell.microbe_index[idx(G::Phytophthora)]);
}

// 지온 50 °C 대 42 °C, 같은 기간: 50 °C 쪽 사멸이 큼 (완료 기준: 토양 온도)
TEST_CASE("Hotter solarization kills more", "[unit][m5a][disinfection]") {
    const sim::Params p = test::test_params();
    sim::SoilCell hot = disinfection_cell();
    hot.soil_temp_degC = 50.0;
    sim::SoilCell warm = disinfection_cell();
    warm.soil_temp_degC = 42.0;
    const Exposure h = expose(hot, p, 24 * 7);
    const Exposure w = expose(warm, p, 24 * 7);
    for (const G g : {G::Fusarium, G::Phytophthora, G::NitrifierAob, G::Decomposer}) {
        CHECK(h.cell.microbe_index[idx(g)] < w.cell.microbe_index[idx(g)]);
    }
    // 유익과 유해가 함께 줄어요
    CHECK(h.cell.microbe_index[idx(G::Antagonist)] < 1.0);
    CHECK(h.cell.microbe_index[idx(G::Fusarium)] < 0.5);
}

// 마른 흙 대 젖은 흙: 마른 쪽 열 사멸이 작음
TEST_CASE("Dry soil reduces heat kill", "[unit][m5a][disinfection]") {
    const sim::Params p = test::test_params();
    sim::SoilCell wet = disinfection_cell();
    wet.soil_temp_degC = 45.0;
    sim::SoilCell dry = wet;
    dry.water_mm = dry.field_capacity_mm * 0.2;
    const Exposure w = expose(wet, p, 24 * 3);
    const Exposure d = expose(dry, p, 24 * 3);
    CHECK(d.killed[idx(G::Fusarium)] < w.killed[idx(G::Fusarium)]);
    CHECK(d.cell.microbe_index[idx(G::Fusarium)] > w.cell.microbe_index[idx(G::Fusarium)]);
}

// 약제 30 g/m², 필름 대 없음: 필름 쪽 잔류가 오래가고 사멸이 큼
TEST_CASE("Film retains fumigant and increases its kill", "[unit][m5a][disinfection]") {
    const sim::Params p = test::test_params();
    sim::SoilCell bare = disinfection_cell();
    bare.soil_temp_degC = 25.0;
    sim::Ledger ledger;
    ledger.disinfectant_applied_g.assign(p.disinfectants.size(), 0.0);
    process::apply_disinfectant(bare, kDazomet, p.disinfectants[kDazomet], 30.0, ledger);
    CHECK(bare.fumigant_residue_index[kDazomet] == Approx(1.0));
    CHECK(ledger.disinfectant_applied_g[kDazomet] == 30.0);
    sim::SoilCell film = bare;
    film.mulch_def_index = kClear;
    const Exposure b = expose(bare, p, 24 * 7);
    const Exposure f = expose(film, p, 24 * 7);
    CHECK(f.cell.fumigant_residue_index[kDazomet] > b.cell.fumigant_residue_index[kDazomet]);
    CHECK(f.killed[idx(G::Fusarium)] > b.killed[idx(G::Fusarium)]);
    CHECK(f.cell.microbe_index[idx(G::Fusarium)] < b.cell.microbe_index[idx(G::Fusarium)]);
}

// 다조멧 대 클로로피크린 표준량: 클로로피크린 쪽 질산화균 감소가 크고 가스가 오래 남음
TEST_CASE("Chloropicrin kills nitrifiers harder and lingers longer than dazomet", "[unit][m5a][disinfection]") {
    const sim::Params p = test::test_params();
    sim::Ledger ledger;
    sim::SoilCell daz = disinfection_cell();
    daz.soil_temp_degC = 25.0;
    daz.mulch_def_index = kClear;
    sim::SoilCell chl = daz;
    process::apply_disinfectant(daz, kDazomet, p.disinfectants[kDazomet], 30.0, ledger);
    process::apply_disinfectant(chl, kChloropicrin, p.disinfectants[kChloropicrin], 4.0, ledger);
    CHECK(chl.fumigant_residue_index[kChloropicrin] == Approx(1.0));
    const Exposure d = expose(daz, p, 24 * 10);
    const Exposure c = expose(chl, p, 24 * 10);
    CHECK(c.cell.microbe_index[idx(G::NitrifierAob)] < d.cell.microbe_index[idx(G::NitrifierAob)]);
    CHECK(c.cell.microbe_index[idx(G::NitrifierNob)] < d.cell.microbe_index[idx(G::NitrifierNob)]);
    CHECK(c.cell.fumigant_residue_index[kChloropicrin] > d.cell.fumigant_residue_index[kDazomet]);
}

// 마른 흙(상대 수분 0.2)에서 두 약제: 다조멧 사멸이 크게 줄고 클로로피크린은 덜 줄어듦
TEST_CASE("Dry soil weakens dazomet more than chloropicrin", "[unit][m5a][disinfection]") {
    const sim::Params p = test::test_params();
    sim::SoilCell wet = disinfection_cell();
    wet.fumigant_residue_index = {1.0, 1.0};
    sim::SoilCell dry = wet;
    dry.water_mm = dry.field_capacity_mm * 0.2;
    const double daz_ratio =
        process::fumigant_activation(p.disinfectants[kDazomet], dry) / process::fumigant_activation(p.disinfectants[kDazomet], wet);
    const double chl_ratio = process::fumigant_activation(p.disinfectants[kChloropicrin], dry) /
                             process::fumigant_activation(p.disinfectants[kChloropicrin], wet);
    CHECK(daz_ratio < chl_ratio);
    CHECK(daz_ratio < 0.5);
    sim::SoilCell daz_wet = wet;
    daz_wet.fumigant_residue_index = {1.0, 0.0};
    sim::SoilCell daz_dry = dry;
    daz_dry.fumigant_residue_index = {1.0, 0.0};
    CHECK(process::fumigant_kill_per_hour(daz_dry, p.disinfectants)[idx(G::Fusarium)] <
          process::fumigant_kill_per_hour(daz_wet, p.disinfectants)[idx(G::Fusarium)] * 0.5);
}

// 두 약제를 함께: 약제별 잔류가 따로(각자의 분해 속도로) 분해되고, 사멸·뿌리 손상(노출 지수)은 합이에요
TEST_CASE("Two disinfectants decay separately and their kill and root exposure add up", "[unit][m5a][disinfection]") {
    const sim::Params p = test::quiet_params();
    sim::SoilCell both = disinfection_cell();
    both.soil_temp_degC = 25.0;
    both.mulch_def_index = kBlack;
    both.fumigant_residue_index = {0.15, 0.15};
    sim::SoilCell daz = both;
    daz.fumigant_residue_index = {0.15, 0.0};
    sim::SoilCell chl = both;
    chl.fumigant_residue_index = {0.0, 0.15};

    // 사멸률과 노출 지수는 약제별 값의 합
    const sim::MicrobeIndices k_both = process::fumigant_kill_per_hour(both, p.disinfectants);
    const sim::MicrobeIndices k_daz = process::fumigant_kill_per_hour(daz, p.disinfectants);
    const sim::MicrobeIndices k_chl = process::fumigant_kill_per_hour(chl, p.disinfectants);
    for (std::size_t g = 0; g < sim::kMicrobeGroupCount; ++g) {
        CHECK(k_both[g] == Approx(k_daz[g] + k_chl[g]).epsilon(1e-12));
    }
    REQUIRE(process::fumigant_exposure_index(daz, p) > 0.0);
    CHECK(process::fumigant_exposure_index(both, p) ==
          Approx(process::fumigant_exposure_index(daz, p) + process::fumigant_exposure_index(chl, p)).epsilon(1e-12));

    // 분해: 약제마다 자기 속도로, 다른 약제가 있어도 같은 값
    sim::SoilCell decayed_both = both;
    sim::SoilCell decayed_daz = daz;
    sim::SoilCell decayed_chl = chl;
    for (int h = 0; h < 24 * 3; ++h) {
        process::decay_fumigant(decayed_both, p);
        process::decay_fumigant(decayed_daz, p);
        process::decay_fumigant(decayed_chl, p);
    }
    CHECK(decayed_both.fumigant_residue_index[kDazomet] == decayed_daz.fumigant_residue_index[kDazomet]);
    CHECK(decayed_both.fumigant_residue_index[kChloropicrin] == decayed_chl.fumigant_residue_index[kChloropicrin]);
    CHECK(decayed_daz.fumigant_residue_index[kChloropicrin] == 0.0);
    // 한 틱 분해 = 잔류 × decay × decay_temp_curve(지온) × 필름 보존 (약제별 계수)
    sim::SoilCell one = both;
    process::decay_fumigant(one, p);
    const double retention = p.mulches[kBlack].fumigant_retention_factor;
    for (const std::size_t d : {kDazomet, kChloropicrin}) {
        const sim::DisinfectantDef& def = p.disinfectants[d];
        CHECK(one.fumigant_residue_index[d] ==
              Approx(0.15 * (1.0 - def.decay_rate_per_hour * def.decay_temp_curve(25.0) * retention)));
    }
    // 두 약제의 분해 속도가 달라서 잔류가 갈라져요 (클로로피크린이 오래 남음)
    CHECK(decayed_both.fumigant_residue_index[kChloropicrin] > decayed_both.fumigant_residue_index[kDazomet]);

    // 뿌리 손상: 두 약제 노출의 합이 곡선에 들어가서 함께 쓴 쪽 뿌리 건강도가 더 줄어요
    const auto root_health_after_tick = [&p](sim::SoilCell cell) {
        sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
        sim::Ledger ledger;
        sim::Pcg32 rng = test::biology_rng(5);
        (void)process::update_crop(crop, cell, p, process::available_nutrients(cell, p), test::crop_env(0.0, 20.0), rng,
                                   ledger);
        return crop.root_health;
    };
    const double h_both = root_health_after_tick(both);
    const double h_daz = root_health_after_tick(daz);
    const double h_chl = root_health_after_tick(chl);
    CHECK(h_daz < 1.0);
    CHECK(h_both < h_daz);
    CHECK(h_both < h_chl);
}

// 경운: 잔류 절반
TEST_CASE("Tillage releases half the fumigant residue", "[unit][m5a][disinfection]") {
    sim::World world = test::make_world();
    test::must_enqueue(world, sim::ApplyDisinfectantCommand{.area = kAll, .disinfectant_id = "dazomet", .g_per_m2 = 30.0});
    world.step(1);
    const double before = world.snapshot().soil[0].fumigant_residue_index[kDazomet];
    REQUIRE(before > 0.0);
    test::must_enqueue(world, sim::TillCommand{.area = kAll});
    world.step(1);
    // 명령 단계에서 절반, 그 뒤 같은 틱 d1 분해가 한 번 더 일어나요
    const double after = world.snapshot().soil[0].fumigant_residue_index[kDazomet];
    CHECK(after < before * 0.5);
    CHECK(after > before * 0.5 * 0.9);

    sim::SoilCell cell = test::test_cell();
    cell.fumigant_residue_index = {0.8, 0.4};
    process::release_fumigant_on_till(cell, test::test_params().disinfection);
    CHECK(cell.fumigant_residue_index[0] == Approx(0.4));
    CHECK(cell.fumigant_residue_index[1] == Approx(0.2));
}

// 잔류 0.3에서 작물 있음: 뿌리 손상, 고사 원인 fumigant
TEST_CASE("Fumigant residue damages roots and kills with cause fumigant", "[unit][m5a][disinfection]") {
    const sim::Params p = test::quiet_params();
    sim::SoilCell cell = test::test_cell();
    cell.fumigant_residue_index = {0.0, 0.3};  // 클로로피크린 활성 1 (relative_water 0.75)
    REQUIRE(process::fumigant_exposure_index(cell, p) == Approx(0.3));
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    sim::Ledger ledger;
    sim::Pcg32 rng = test::biology_rng(3);
    std::optional<sim::PlantDiedEvent> died;
    double first_health = -1.0;
    for (int h = 0; h < 24 * 30 && !died; ++h) {
        const process::CropTickResult r = process::update_crop(crop, cell, p, process::available_nutrients(cell, p),
                                                               test::crop_env(0.0, 20.0), rng, ledger);
        if (first_health < 0.0) {
            first_health = crop.root_health;
        }
        died = r.died;
    }
    CHECK(first_health < 1.0);
    REQUIRE(died.has_value());
    CHECK(died->cause == sim::RootDamageCause::Fumigant);
}

// 분해균 소독 사멸 0.5: NH4+ 3 g/m² 증가, stable_N 같은 양 감소, 총 N 불변. 산화물 100: 유효태 Mn 1.5 증가, 총 Mn 불변
TEST_CASE("Disinfection flush moves organic N to ammonium and reduces manganese oxide",
          "[unit][m5a][disinfection][conservation]") {
    const sim::Params p = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.disinfection_kill_index[idx(G::Decomposer)] = 0.5;
    const sim::SoilCell before = cell;
    sim::Ledger ledger;
    process::apply_disinfection_flush(cell, p.disinfection, ledger);
    CHECK(cell.nh4_N_g_per_m2 - before.nh4_N_g_per_m2 == Approx(3.0));
    CHECK(before.stable_N_g_per_m2 - cell.stable_N_g_per_m2 == Approx(3.0));
    CHECK(cell.nh4_solution_N_g_per_m2 - before.nh4_solution_N_g_per_m2 == Approx(3.0));
    CHECK(cell.total_N_g_per_m2() == Approx(before.total_N_g_per_m2()).epsilon(1e-14));
    CHECK(cell.labile_C_g_per_m2 - before.labile_C_g_per_m2 == Approx(18.0));
    CHECK(cell.organic_C_g_per_m2() == Approx(before.organic_C_g_per_m2()).epsilon(1e-14));
    CHECK(cell.mn_g_per_m2 - before.mn_g_per_m2 == Approx(1.5));
    CHECK(cell.mn_solution_g_per_m2 - before.mn_solution_g_per_m2 == Approx(1.5));
    CHECK(cell.mn_g_per_m2 + cell.mn_oxide_g_per_m2 ==
          Approx(before.mn_g_per_m2 + before.mn_oxide_g_per_m2).epsilon(1e-14));
    CHECK(ledger.N_flush_g == Approx(3.0));
    CHECK(ledger.Mn_flush_g == Approx(1.5));

    // 원천 풀로 제한: 난분해성 N이 모자라면 있는 만큼만
    sim::SoilCell poor = test::test_cell();
    poor.stable_N_g_per_m2 = 1.0;
    poor.stable_C_g_per_m2 = 2.0;
    poor.disinfection_kill_index[idx(G::Decomposer)] = 1.0;
    sim::Ledger l2;
    process::apply_disinfection_flush(poor, p.disinfection, l2);
    CHECK(poor.stable_N_g_per_m2 == 0.0);
    CHECK(poor.stable_C_g_per_m2 == 0.0);
}

// 백색 대 없음, LAI 1, 같은 일사: 백색 쪽 작물 광량·총생산이 큼. 하우스 기온은 같음
TEST_CASE("White film reflects light to the canopy without changing house air", "[unit][m5a][disinfection]") {
    const sim::Params p = test::quiet_params();
    sim::SoilCell cell = test::test_cell();
    sim::Crop crop = process::plant_crop(test::test_crop_init(), p.crop);
    crop.leaf_g_per_m2 = 1.0 / p.crop.specific_leaf_area_m2_per_g;  // LAI 1
    sim::Crop none = crop;
    sim::Crop white = crop;
    sim::SoilCell cell_none = cell;
    sim::SoilCell cell_white = cell;
    cell_white.mulch_def_index = kWhite;
    sim::Ledger l1;
    sim::Ledger l2;
    sim::Pcg32 r1 = test::biology_rng(4);
    sim::Pcg32 r2 = test::biology_rng(4);
    process::CropEnvironment env = test::crop_env(400.0, 20.0);
    (void)process::update_crop(none, cell_none, p, process::available_nutrients(cell_none, p), env, r1, l1);
    env.canopy_reflect_factor = process::mulch_canopy_reflect_factor(cell_white, p);
    CHECK(env.canopy_reflect_factor == 0.25);
    (void)process::update_crop(white, cell_white, p, process::available_nutrients(cell_white, p), env, r2, l2);
    CHECK(white.last_gross_g_per_m2 > none.last_gross_g_per_m2);
    // 광량 배율 = 1 + 0.25 × (1 − interception(LAI 1) = 0.5)
    CHECK(white.last_gross_g_per_m2 == Approx(none.last_gross_g_per_m2 * 1.125));

    // 월드: 백색 필름이 하우스 기온을 바꾸지 않아요
    sim::World bare = test::make_world();
    sim::World filmed = test::make_world();
    test::must_enqueue(filmed, sim::SetMulchCommand{.area = kAll, .mulch_id = "white_film"});
    bare.step(48);
    filmed.step(48);
    CHECK(bare.snapshot().house_air_temp_degC == filmed.snapshot().house_air_temp_degC);
    CHECK(filmed.snapshot().soil[0].mulch_def_index == kWhite);
}

// 명령 검증: 모르는 멀칭·약제 id, 음수 약제량 거부. "none"은 걷기
TEST_CASE("Mulch and disinfectant commands validate their ids", "[unit][m5a][disinfection]") {
    sim::World world = test::make_world();
    CHECK_FALSE(world.enqueue(sim::SetMulchCommand{.area = kAll, .mulch_id = "silver_film"}));
    CHECK_FALSE(world.enqueue(sim::ApplyDisinfectantCommand{.area = kAll, .disinfectant_id = "x", .g_per_m2 = 1.0}));
    CHECK_FALSE(
        world.enqueue(sim::ApplyDisinfectantCommand{.area = kAll, .disinfectant_id = "dazomet", .g_per_m2 = -1.0}));
    test::must_enqueue(world, sim::SetMulchCommand{.area = kAll, .mulch_id = "clear_film"});
    world.step(1);
    CHECK(world.snapshot().soil[5].mulch_def_index == kClear);
    test::must_enqueue(world, sim::SetMulchCommand{.area = kAll, .mulch_id = "none"});
    world.step(1);
    CHECK(world.snapshot().soil[5].mulch_def_index == sim::kNoMulch);
    // 장부의 약제별 누적은 첫 투입 때 만들어져요
    CHECK(world.snapshot().ledger.disinfectant_applied_g.empty());
    test::must_enqueue(world, sim::ApplyDisinfectantCommand{.area = kAll, .disinfectant_id = "chloropicrin", .g_per_m2 = 4.0});
    world.step(1);
    const sim::Ledger l = world.snapshot().ledger;
    REQUIRE(l.disinfectant_applied_g.size() == 2);
    CHECK(l.disinfectant_applied_g[kChloropicrin] == Approx(24.0));
    CHECK(l.disinfectant_applied_g[kDazomet] == 0.0);
}

// 살아 있는 작물이 있을 때 Plant: enqueue에서 거부. 모르는 등급·0 이하 밀도 거부
TEST_CASE("Plant is rejected while a live crop exists", "[unit][m5a][disinfection][plant]") {
    sim::Scenario s = test::test_scenario();
    s.crop = test::test_crop_init();
    sim::World world = test::make_world(s);
    CHECK_FALSE(world.enqueue(sim::PlantCommand{.seedling_grade = "B", .plants_per_m2 = 8.0}));
    sim::World empty = test::make_world();
    CHECK_FALSE(empty.enqueue(sim::PlantCommand{.seedling_grade = "Z", .plants_per_m2 = 8.0}));
    CHECK_FALSE(empty.enqueue(sim::PlantCommand{.seedling_grade = "B", .plants_per_m2 = 0.0}));
    CHECK(empty.enqueue(sim::PlantCommand{.seedling_grade = "B", .plants_per_m2 = 8.0}));
}

// 작물 없는 시나리오에 Plant: 모종 등급 초기값, 활착 0, 체내 원소는 *_in_g 투입. 같은 틱 두 번째 Plant는 무시
TEST_CASE("Plant creates seedlings with zero establishment and books their elements",
          "[unit][m5a][disinfection][plant][conservation]") {
    sim::World world = test::make_world();
    const Totals init = totals_of(world.snapshot());
    test::must_enqueue(world, sim::PlantCommand{.seedling_grade = "A", .plants_per_m2 = 6.0});
    test::must_enqueue(world, sim::PlantCommand{.seedling_grade = "B", .plants_per_m2 = 8.0});
    world.step(1);
    const sim::Snapshot s = world.snapshot();
    const sim::SeedlingGradeDef& a = *process::find_seedling_grade(world.params(), "A");
    for (const sim::Crop& crop : s.crops) {
        REQUIRE(crop.present);
        CHECK(crop.alive);
        CHECK(crop.plants_per_m2 == 6.0);
        CHECK(crop.crown_g_per_m2 == Approx(a.crown_g_per_plant * 6.0).epsilon(1e-3));  // 한 틱 호흡
        CHECK(crop.establishment_index < 0.01);  // 0에서 시작해 한 틱 진행
        CHECK(crop.induction_index >= a.initial_induction_index);  // 퍼짐 0: 등급 값 그대로에서 진행
        CHECK(crop.infection_index == std::array<double, sim::kSoilDiseaseCount>{});
    }
    CHECK(s.ledger.N_in_g > 0.0);
    CHECK(conserved(init, s));
    world.step(24 * 3);
    CHECK(conserved(init, world.snapshot()));
}

// 죽은 작물만 있는 셀에 Plant: 죽은 작물의 원소는 토양으로(보존), 새 작물로 바뀜.
// 포기 간 차이 > 0이면 월드 생성과 같은 규칙으로 생물 난수를 타일 순서로 뽑아요
TEST_CASE("Plant replaces dead crops and returns their elements to the soil",
          "[unit][m5a][disinfection][plant][conservation]") {
    sim::Params p = test::test_params();
    p.phenology.induction_spread_index = 0.4;
    sim::Scenario s = test::test_scenario();
    s.crop = test::test_crop_init();
    auto created = sim::World::create(p, s);
    REQUIRE(created.has_value());
    sim::World world = std::move(*created);
    const Totals init = totals_of(world.snapshot());
    // 약제 과다로 모든 포기를 죽여요
    test::must_enqueue(world, sim::ApplyDisinfectantCommand{.area = kAll, .disinfectant_id = "chloropicrin", .g_per_m2 = 40.0});
    int hours = 0;
    while (hours < 24 * 20) {
        world.step(1);
        ++hours;
        const sim::Snapshot snap = world.snapshot();
        if (std::none_of(snap.crops.begin(), snap.crops.end(), [](const sim::Crop& c) { return c.alive; })) {
            break;
        }
    }
    const sim::Snapshot dead = world.snapshot();
    REQUIRE(std::none_of(dead.crops.begin(), dead.crops.end(), [](const sim::Crop& c) { return c.alive; }));
    CHECK(conserved(init, dead));
    test::must_enqueue(world, sim::PlantCommand{.seedling_grade = "C", .plants_per_m2 = 8.0});
    world.step(1);
    const sim::Snapshot replanted = world.snapshot();
    const sim::SeedlingGradeDef& c = *process::find_seedling_grade(p, "C");
    std::vector<double> inductions;
    for (const sim::Crop& crop : replanted.crops) {
        CHECK(crop.alive);
        CHECK(crop.root_g_per_m2 < c.root_g_per_plant * 8.0 * 1.05);  // 모종 값에서 한 틱 진행
        CHECK(crop.induction_index <= c.initial_induction_index + 0.01);
        inductions.push_back(crop.induction_index);
    }
    // 포기 간 차이가 실제로 적용됨
    CHECK(*std::max_element(inductions.begin(), inductions.end()) >
          *std::min_element(inductions.begin(), inductions.end()));
    CHECK(conserved(init, replanted));
}

// 보존·극한: 소독 중 관수 0, 약제 100배, 필름 + 난방 30 °C에서 NaN·음수 없음, 원소 보존
TEST_CASE("Disinfection extremes keep pools valid and elements conserved",
          "[unit][m5a][disinfection][extreme][conservation]") {
    sim::Scenario s = test::test_scenario(191);
    s.soil.microbe_index = {1.0, 1.0, 1.0, 0.5, 0.1, 0.5, 0.5};
    sim::World world = test::make_world(s);
    const Totals init = totals_of(world.snapshot());
    test::must_enqueue(world, sim::SetMulchCommand{.area = kAll, .mulch_id = "clear_film"});
    test::must_enqueue(world, sim::SetHeaterCommand{.on = true, .setpoint_degC = 30.0});
    test::must_enqueue(world, sim::ApplyDisinfectantCommand{.area = kAll, .disinfectant_id = "dazomet", .g_per_m2 = 3000.0});
    test::must_enqueue(world, sim::ApplyDisinfectantCommand{.area = kAll, .disinfectant_id = "chloropicrin", .g_per_m2 = 400.0});
    test::must_enqueue(world, sim::AddInoculantCommand{.area = kAll, .inoculant_id = "mycorrhiza_product", .g_per_m2 = 1000.0});
    bool ok = true;
    double max_flush = 0.0;
    for (int h = 0; h < 24 * 21 && ok; ++h) {
        if (h == 24 * 14) {
            test::must_enqueue(world, sim::TillCommand{.area = kAll});
            test::must_enqueue(world, sim::PlantCommand{.seedling_grade = "B", .plants_per_m2 = 8.0});
        }
        world.step(1);
        const sim::Snapshot snap = world.snapshot();
        ok = m5a_state_valid(snap) && conserved(init, snap);
        max_flush = snap.ledger.N_flush_g;
    }
    CHECK(ok);
    CHECK(max_flush > 0.0);

    // 셀 하나 극한: 지온 70 °C, 수분 0, pH 3, 약제 100배
    const sim::Params p = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.soil_temp_degC = 70.0;
    cell.water_mm = 0.0;
    cell.ph = 3.0;
    cell.microbe_index = {1, 1, 1, 1, 1, 1, 1};
    cell.fumigant_residue_index = {100.0, 100.0};
    cell.mulch_def_index = kClear;
    const double N0 = cell.total_N_g_per_m2();
    const double C0 = cell.organic_C_g_per_m2();
    const double Mn0 = cell.mn_g_per_m2 + cell.mn_oxide_g_per_m2;
    sim::Ledger ledger;
    for (int h = 0; h < 24 * 5; ++h) {
        process::decay_fumigant(cell, p);
        process::update_microbes(cell, p, {});
        process::apply_disinfection_flush(cell, p.disinfection, ledger);
    }
    sim::Snapshot one{};
    one.soil = {cell};
    one.crops = {sim::Crop{}};
    CHECK(m5a_state_valid(one));
    CHECK(cell.total_N_g_per_m2() == Approx(N0).epsilon(1e-12));
    CHECK(cell.organic_C_g_per_m2() == Approx(C0).epsilon(1e-12));
    CHECK(cell.mn_g_per_m2 + cell.mn_oxide_g_per_m2 == Approx(Mn0).epsilon(1e-12));
}

// 직렬화: M5a 상태(지수, 멀칭, 잔류, 감염, 장부)가 저장·복원되고 이어서 돌려도 해시가 같아요
TEST_CASE("M5a state survives a save and load round trip", "[unit][m5a][disinfection][serialization]") {
    sim::Scenario s = test::test_scenario(250);
    s.soil.microbe_index = {1.0, 1.0, 1.0, 0.5, 0.1, 0.5, 0.5};
    s.crop = test::test_crop_init();
    sim::World world = test::make_world(s);
    test::must_enqueue(world, sim::SetMulchCommand{.area = kAll, .mulch_id = "black_film"});
    test::must_enqueue(world, sim::ApplyDisinfectantCommand{.area = kAll, .disinfectant_id = "dazomet", .g_per_m2 = 3.0});
    test::must_enqueue(world, sim::AddInoculantCommand{.area = kAll, .inoculant_id = "bacillus_product", .g_per_m2 = 5.0});
    world.step(24 * 3);
    test::must_enqueue(world, sim::SetMulchCommand{.area = kAll, .mulch_id = "white_film"});  // 대기 명령도 저장
    const std::vector<std::byte> bytes = world.serialize();
    auto loaded = sim::World::deserialize(bytes);
    REQUIRE(loaded.has_value());
    CHECK(loaded->state_hash() == world.state_hash());
    CHECK(loaded->snapshot().soil == world.snapshot().soil);
    CHECK(loaded->snapshot().crops == world.snapshot().crops);
    CHECK(loaded->snapshot().ledger == world.snapshot().ledger);
    CHECK(loaded->params() == world.params());
    CHECK(loaded->scenario() == world.scenario());
    world.step(24);
    loaded->step(24);
    CHECK(loaded->state_hash() == world.state_hash());
}

// 결정론: 같은 시드·같은 명령(정식 포함) → 같은 해시
TEST_CASE("M5a commands are deterministic", "[unit][m5a][disinfection][determinism]") {
    const auto run = [] {
        sim::Params p = test::test_params();
        p.phenology.induction_spread_index = 0.4;
        sim::Scenario s = test::test_scenario(191);
        s.soil.microbe_index = {1.0, 1.0, 1.0, 0.5, 0.1, 0.5, 0.5};
        auto w = sim::World::create(p, s);
        REQUIRE(w.has_value());
        test::must_enqueue(*w, sim::SetMulchCommand{.area = kAll, .mulch_id = "clear_film"});
        w->step(24 * 5);
        test::must_enqueue(*w, sim::PlantCommand{.seedling_grade = "B", .plants_per_m2 = 8.0});
        w->step(24 * 5);
        return w->state_hash();
    };
    CHECK(run() == run());
}
