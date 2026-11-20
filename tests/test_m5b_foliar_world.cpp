// M5b 지상부 병해충 보존·불변 조건·결정론 (docs/spec/foliar-disease.md 9장 "보존", "결정론" 행).
// data/scenarios/m5b/*.toml (S26~S32)과 극한 변형을 매 틱 검사해요.
//   원소: 토양 + 체내 = 초기 + in − leached − gas − harvested − removed (RemovePlant만 계 밖 항).
//         썩은 과실은 낙엽 규칙으로 토양(계 안), 흰가루 과실은 수확 반출 장부에 들어가요.
//   유기 C: 초기 + C_in + C_root_litter + C_leaf_litter − C_respired (제거한 포기의 C는 토양 밖이라 항 없음).
//   불변: 지상부 지수 0~1, 탄저병 감염은 같은 작물이 이어지는 동안 줄지 않음, 하우스 포자 0~1, 토양 무기 N 평균 ≥ 0, NaN 없음.
// 상대 오차 1e-9, 스케일은 max(1, |값|). 첫 위반에서 멈춰요.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

constexpr double kRelTol = 1e-9;
constexpr std::uint64_t kDay = 24;

struct Totals {
    double water = 0.0, N = 0.0, P = 0.0, K = 0.0, Ca = 0.0, Mg = 0.0, Mn = 0.0, S = 0.0, C = 0.0;
};

Totals totals_of(const sim::Snapshot& snap) {
    Totals t;
    for (std::size_t i = 0; i < snap.soil.size(); ++i) {
        const sim::SoilCell& c = snap.soil[i];
        const sim::Crop& crop = snap.crops[i];
        t.water += c.water_mm;
        t.N += c.total_N_g_per_m2() + crop.plant_N_g_per_m2;
        t.P += c.p_g_per_m2 + c.p_fixed_g_per_m2 + crop.plant_P_g_per_m2;
        t.K += c.k_g_per_m2 + crop.plant_K_g_per_m2;
        t.Ca += c.ca_g_per_m2 + crop.plant_Ca_g_per_m2;
        t.Mg += c.mg_g_per_m2 + crop.plant_Mg_g_per_m2;
        t.Mn += c.mn_g_per_m2 + c.mn_oxide_g_per_m2 + crop.plant_Mn_g_per_m2;
        t.S += c.s_g_per_m2;
        t.C += c.organic_C_g_per_m2();
    }
    return t;
}

double rel_err(double actual, double expected) { return std::abs(actual - expected) / std::max(1.0, std::abs(expected)); }
bool unit(double v) { return std::isfinite(v) && v >= 0.0 && v <= 1.0; }

std::string invalid_reason(const sim::Snapshot& prev, const sim::Snapshot& snap) {
    for (const double v : {snap.house.botrytis_spore_index, snap.house.powdery_mildew_spore_index,
                           snap.house.anthracnose_sporulation_index, snap.house.spider_mite_mean_index}) {
        if (!unit(v)) {
            return "house foliar index out of 0..1";
        }
    }
    if (!std::isfinite(snap.house.inside_solar_mean_W_per_m2) || snap.house.inside_solar_mean_W_per_m2 < 0.0) {
        return "house solar mean invalid";
    }
    for (std::size_t i = 0; i < snap.crops.size(); ++i) {
        const sim::Crop& c = snap.crops[i];
        for (const double v : {c.infection_anthracnose_index, c.infection_botrytis_index, c.infection_powdery_mildew_index,
                               c.spider_mite_index, c.spider_mite_mean_index, c.predator_mite_index, c.leaf_wetness_index}) {
            if (!unit(v)) {
                return std::format("crop {} foliar index out of 0..1", i);
            }
        }
        if (!std::isfinite(c.soil_mineral_N_mean_g_per_m2) || c.soil_mineral_N_mean_g_per_m2 < 0.0) {
            return std::format("crop {} soil mineral N mean invalid", i);
        }
        const sim::Crop& p = prev.crops[i];
        const bool same_crop = p.present && c.present && !(!p.alive && c.alive);
        if (same_crop && c.infection_anthracnose_index < p.infection_anthracnose_index) {
            return std::format("crop {} anthracnose infection decreased", i);
        }
    }
    for (std::size_t i = 0; i < snap.soil.size(); ++i) {
        for (std::size_t g = 0; g < sim::kMicrobeGroupCount; ++g) {
            if (!unit(snap.soil[i].microbe_index[g])) {
                return std::format("cell {} microbe {} out of 0..1", i, g);
            }
        }
    }
    return {};
}

struct Loaded {
    sim::Params params;
    simdata::ScenarioBundle bundle;
};

Loaded load(const std::string& name) {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m5b/" + name + ".toml"));
    REQUIRE(bundle.has_value());
    return Loaded{std::move(*params), std::move(*bundle)};
}

struct Options {
    std::optional<sim::FoliarDisease> remove_on_onset;  // sim-cli --remove-on-onset과 같은 동작
    std::uint64_t remove_all_at_tick = 0;               // 0이 아니면 그 틱에 하우스 전체 RemovePlant
};

struct Result {
    double max_err = 0.0;
    sim::Ledger ledger;
    std::uint64_t foliar_onsets = 0;
    std::uint64_t final_hash = 0;
};

Result run(Loaded loaded, std::uint64_t ticks, const Options& options = {}, bool check = true) {
    auto world = sim::World::create(std::move(loaded.params), std::move(loaded.bundle.scenario));
    REQUIRE(world.has_value());
    const auto& schedule = loaded.bundle.schedule;
    const int cols = world->scenario().bed_cols;
    const int rows = world->scenario().bed_rows;
    sim::Snapshot prev = world->snapshot();
    const Totals init = totals_of(prev);
    Result result;
    std::size_t next = 0;
    std::vector<sim::Command> removals;
    for (std::uint64_t t = 0; t < ticks; ++t) {
        for (sim::Command& c : removals) {
            (void)world->enqueue(std::move(c));
        }
        removals.clear();
        while (next < schedule.size() && schedule[next].tick == t) {
            REQUIRE(world->enqueue(schedule[next].command).has_value());
            ++next;
        }
        if (options.remove_all_at_tick != 0 && t == options.remove_all_at_tick) {
            REQUIRE(world->enqueue(sim::RemovePlantCommand{.area = sim::whole_grid(rows, cols)}).has_value());
        }
        world->step(1);
        for (const sim::Event& e : world->events()) {
            if (const auto* onset = std::get_if<sim::FoliarDiseaseOnsetEvent>(&e.payload)) {
                ++result.foliar_onsets;
                if (options.remove_on_onset && onset->disease == *options.remove_on_onset) {
                    const int r = static_cast<int>(onset->tile_index) / cols;
                    const int c = static_cast<int>(onset->tile_index) % cols;
                    removals.emplace_back(sim::RemovePlantCommand{.area = {.row = r, .col = c, .rows = 1, .cols = 1}});
                }
            }
        }
        if (!check) {
            continue;
        }
        sim::Snapshot snap = world->snapshot();
        const Totals now = totals_of(snap);
        const sim::Ledger& l = snap.ledger;
        const std::array<double, 9> errs = {
            rel_err(now.water, init.water + l.water_in_mm - l.water_evaporated_mm - l.water_transpired_mm -
                                   l.water_drained_mm),
            rel_err(now.N, init.N + l.N_in_g - l.N_leached_g - l.N_gas_g - l.N_harvested_g - l.N_removed_g),
            rel_err(now.P, init.P + l.P_in_g - l.P_leached_g - l.P_harvested_g - l.P_removed_g),
            rel_err(now.K, init.K + l.K_in_g - l.K_leached_g - l.K_harvested_g - l.K_removed_g),
            rel_err(now.Ca, init.Ca + l.Ca_in_g - l.Ca_leached_g - l.Ca_harvested_g - l.Ca_removed_g),
            rel_err(now.Mg, init.Mg + l.Mg_in_g - l.Mg_leached_g - l.Mg_harvested_g - l.Mg_removed_g),
            rel_err(now.Mn, init.Mn + l.Mn_in_g - l.Mn_leached_g - l.Mn_harvested_g - l.Mn_removed_g),
            rel_err(now.S, init.S + l.S_in_g - l.S_leached_g),
            rel_err(now.C, init.C + l.C_in_g + l.C_root_litter_g + l.C_leaf_litter_g - l.C_respired_g),
        };
        static constexpr const char* kNames[9] = {"water", "N", "P", "K", "Ca", "Mg", "Mn", "S", "C"};
        const double worst = *std::max_element(errs.begin(), errs.end());
        result.max_err = std::max(result.max_err, worst);
        const std::string reason = invalid_reason(prev, snap);
        if (worst > kRelTol || !reason.empty()) {
            INFO("tick " << (t + 1) << " " << reason);
            for (std::size_t i = 0; i < errs.size(); ++i) {
                INFO(kNames[i] << "_err=" << errs[i]);
                CHECK(errs[i] <= kRelTol);
            }
            CHECK(reason.empty());
            result.ledger = snap.ledger;
            return result;
        }
        prev = std::move(snap);
    }
    result.ledger = world->snapshot().ledger;
    result.final_hash = world->state_hash();
    return result;
}

Result report(const std::string& label, const Result& r) {
    WARN(std::format("m5b conservation [{}] max rel err {:.3e}; rot {} mildew {} removed {} onsets {}", label,
                     r.max_err, r.ledger.fruit_rot_count, r.ledger.harvested_mildew, r.ledger.plants_removed,
                     r.foliar_onsets));
    CHECK(r.max_err <= kRelTol);
    return r;
}

}  // namespace

// ---- 보존: S26~S32 전부 (README 실행 일수, 매 틱) ----

TEST_CASE("M5b S26 and S27 scenarios conserve every budget every tick", "[conservation][m5b][foliar][s26][s27]") {
    for (const char* name : {"s26_closed", "s26_vented", "s27_closed_fan"}) {
        report(name, run(load(name), 62 * kDay));
    }
}

TEST_CASE("M5b S28 scenarios conserve every budget every tick", "[conservation][m5b][foliar][s28]") {
    for (const char* name : {"s28_drip", "s28_overhead", "s28_overhead_bare"}) {
        report(name, run(load(name), 90 * kDay));
    }
    // s28_overhead_remove: 탄저병 발병 칸을 다음 틱에 뽑아요 (sim-cli --remove-on-onset anthracnose)
    const Result r = report("s28_overhead_remove",
                            run(load("s28_overhead_remove"), 90 * kDay, {.remove_on_onset = sim::FoliarDisease::Anthracnose}));
    CHECK(r.ledger.plants_removed > 0);
    CHECK(r.ledger.N_removed_g > 0.0);
}

TEST_CASE("M5b S30 to S32 scenarios conserve every budget every tick", "[conservation][m5b][foliar][s30][s31][s32]") {
    for (const char* name : {"s30_hot", "s30_cool", "s31_predator", "s31_predator_late", "s31_predator_dry"}) {
        report(name, run(load(name), 45 * kDay));
    }
    for (const char* name : {"s32_bright", "s32_dark", "s32_overhead", "s32_dry"}) {
        report(name, run(load(name), 76 * kDay));
    }
}

// ---- 극한: 두상 관수 매 시간, 포자 1 시작(배경 1), 천적 1000마리/m², 전 포기 제거 ----
TEST_CASE("M5b extreme foliar inputs keep conservation", "[conservation][m5b][foliar][extreme]") {
    Loaded l = load("s28_overhead");
    // 5차: 배경은 1 미만만 허용 (배경 원천 bg × L ÷ (1 − bg))
    l.bundle.scenario.foliar = {.botrytis_background_index = 0.999, .powdery_mildew_background_index = 0.999};
    l.bundle.scenario.soil.microbe_index[sim::microbe(sim::MicrobeGroup::Anthracnose)] = 1.0;
    const sim::TileArea all = sim::whole_grid(l.bundle.scenario.bed_rows, l.bundle.scenario.bed_cols);
    for (std::uint64_t t = 0; t < 60 * kDay; ++t) {
        l.bundle.schedule.push_back({.tick = t, .command = sim::IrrigateCommand{.area = all, .amount_mm = 1.0}});
    }
    l.bundle.schedule.push_back(
        {.tick = 5, .command = sim::ReleasePredatorsCommand{.area = all, .predator_id = "persimilis", .per_m2 = 1000.0}});
    std::stable_sort(l.bundle.schedule.begin(), l.bundle.schedule.end(),
                     [](const auto& a, const auto& b) { return a.tick < b.tick; });
    const Result r = report("extreme overhead every hour + spores 1 + 1000 predators + remove all",
                            run(std::move(l), 70 * kDay, {.remove_all_at_tick = 60 * kDay}));
    CHECK(r.ledger.plants_removed > 0);
    CHECK(r.ledger.predators_released_per_m2 == 1000.0);
}

// ---- 결정론: S26, S28, S31, s29_standard 같은 시드 → 같은 해시 ----
TEST_CASE("M5b scenarios are deterministic", "[determinism][m5b][foliar]") {
    struct Case {
        const char* name;
        std::uint64_t days;
    };
    for (const Case c : {Case{"s26_closed", 62}, Case{"s28_overhead", 90}, Case{"s31_predator", 45},
                         Case{"s29_standard", 263}}) {
        const Result a = run(load(c.name), c.days * kDay, {}, false);
        const Result b = run(load(c.name), c.days * kDay, {}, false);
        INFO(c.name);
        CHECK(a.final_hash == b.final_hash);
        CHECK(a.ledger == b.ledger);
    }
}
