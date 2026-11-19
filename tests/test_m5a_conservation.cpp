// M5a 보존 법칙·불변 조건 테스트 (docs/spec/m5-overview.md 9장 "보존" 행, disinfection "보존·불변 조건",
// soil-microbes "보존·불변 조건").
// data/scenarios/m5a/*.toml 전부와 극한 변형(소독 중 관수 0, 약제 100배, 필름 + 난방 30 °C)을 매 틱 검사해요.
//   원소: 토양 + 체내 = 초기 + in − leached − gas − harvested (유기 C: 초기 + C_in + C_root_litter + C_leaf_litter − C_respired).
//   소독 무기화(N_flush: 유기 N → NH4+)와 망간 환원(Mn_flush: 산화물 → 유효태)은 계 안 이동이라 식에 항이 없어요.
//   불변: 미생물 지수·이번 틱 소독 사멸 0~1, 약제 잔류 ≥ 0, 감염 지수 0~1이고 줄지 않음, 멀칭 id는 데이터에 있는 것,
//   모든 풀 ≥ 0, 용액 ≤ 총량, 온도 유한(NaN 없음), 장부 진단값(N_flush·Mn_flush) ≥ 0이고 줄지 않음.
// 상대 오차 1e-9, 스케일은 max(1, |값|). 첫 위반에서 멈춰요.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
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

bool finite_nonneg(double v) { return std::isfinite(v) && v >= 0.0; }
bool unit(double v) { return std::isfinite(v) && v >= 0.0 && v <= 1.0; }

// 토양 풀·M5a 상태의 불변 조건. 실패하면 이유를 돌려줘요 (통과면 빈 문자열)
std::string invalid_reason(const sim::Snapshot& snap, const sim::Params& params) {
    for (std::size_t i = 0; i < snap.soil.size(); ++i) {
        const sim::SoilCell& c = snap.soil[i];
        const double pools[] = {c.water_mm,           c.labile_C_g_per_m2,    c.labile_N_g_per_m2,
                                c.stable_C_g_per_m2,  c.stable_N_g_per_m2,    c.nh4_N_g_per_m2,
                                c.no2_N_g_per_m2,     c.no3_N_g_per_m2,       c.k_g_per_m2,
                                c.ca_g_per_m2,        c.mg_g_per_m2,          c.p_g_per_m2,
                                c.s_g_per_m2,         c.mn_g_per_m2,          c.mn_oxide_g_per_m2,
                                c.p_fixed_g_per_m2,   c.nh4_solution_N_g_per_m2, c.mn_solution_g_per_m2,
                                c.ec_dS_per_m};
        for (const double p : pools) {
            if (!finite_nonneg(p)) {
                return std::format("cell {} has a negative or non-finite pool", i);
            }
        }
        if (c.nh4_solution_N_g_per_m2 > c.nh4_N_g_per_m2 || c.mn_solution_g_per_m2 > c.mn_g_per_m2) {
            return std::format("cell {} solution exceeds total", i);
        }
        if (!std::isfinite(c.soil_temp_degC) || !std::isfinite(c.deep_soil_temp_degC) || !std::isfinite(c.ph)) {
            return std::format("cell {} temperature or pH is not finite", i);
        }
        for (std::size_t g = 0; g < sim::kMicrobeGroupCount; ++g) {
            if (!unit(c.microbe_index[g]) || !unit(c.disinfection_kill_index[g])) {
                return std::format("cell {} microbe group {} index out of 0..1", i, g);
            }
        }
        if (c.fumigant_residue_index.size() != params.disinfectants.size()) {
            return std::format("cell {} residue list size", i);
        }
        for (const double r : c.fumigant_residue_index) {
            if (!finite_nonneg(r)) {
                return std::format("cell {} fumigant residue negative or non-finite", i);
            }
        }
        if (c.mulch_def_index != sim::kNoMulch &&
            (c.mulch_def_index < 0 || static_cast<std::size_t>(c.mulch_def_index) >= params.mulches.size())) {
            return std::format("cell {} mulch id not in data", i);
        }
    }
    for (std::size_t i = 0; i < snap.crops.size(); ++i) {
        const sim::Crop& crop = snap.crops[i];
        for (const double x : crop.infection_index) {
            if (!unit(x)) {
                return std::format("crop {} infection out of 0..1", i);
            }
        }
        if (!unit(crop.root_health) || !finite_nonneg(crop.plant_N_g_per_m2) || !finite_nonneg(crop.root_g_per_m2) ||
            !finite_nonneg(crop.leaf_g_per_m2)) {
            return std::format("crop {} root health or biomass invalid", i);
        }
    }
    const sim::Ledger& l = snap.ledger;
    if (!finite_nonneg(l.N_flush_g) || !finite_nonneg(l.Mn_flush_g) || !finite_nonneg(l.inoculant_applied_g)) {
        return "ledger diagnostics negative or non-finite";
    }
    for (const double x : l.disinfectant_applied_g) {
        if (!finite_nonneg(x)) {
            return "ledger disinfectant_applied_g negative or non-finite";
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
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m5a/" + name + ".toml"));
    REQUIRE(bundle.has_value());
    return Loaded{std::move(*params), std::move(*bundle)};
}

struct Result {
    double max_err = 0.0;
    sim::Ledger ledger;  // 끝 시점
    std::size_t died = 0;
};

// ticks 틱을 매 틱 돌며 보존식과 불변 조건을 검사해요
Result run_and_check(Loaded loaded, std::uint64_t ticks) {
    std::stable_sort(loaded.bundle.schedule.begin(), loaded.bundle.schedule.end(),
                     [](const auto& a, const auto& b) { return a.tick < b.tick; });
    const sim::Params params = loaded.params;
    auto world = sim::World::create(std::move(loaded.params), std::move(loaded.bundle.scenario));
    REQUIRE(world.has_value());
    const auto& schedule = loaded.bundle.schedule;

    sim::Snapshot prev = world->snapshot();
    const Totals init = totals_of(prev);
    {
        const std::string reason = invalid_reason(prev, params);
        INFO(reason);
        REQUIRE(reason.empty());
    }
    Result result;
    std::size_t next = 0;
    for (std::uint64_t t = 0; t < ticks; ++t) {
        while (next < schedule.size() && schedule[next].tick == t) {
            REQUIRE(world->enqueue(schedule[next].command).has_value());
            ++next;
        }
        world->step(1);
        for (const sim::Event& e : world->events()) {
            result.died += std::holds_alternative<sim::PlantDiedEvent>(e.payload) ? 1 : 0;
        }
        sim::Snapshot snap = world->snapshot();
        const Totals now = totals_of(snap);
        const sim::Ledger& l = snap.ledger;
        const std::array<double, 9> errs = {
            rel_err(now.water, init.water + l.water_in_mm - l.water_evaporated_mm - l.water_transpired_mm -
                                   l.water_drained_mm),
            rel_err(now.N, init.N + l.N_in_g - l.N_leached_g - l.N_gas_g - l.N_harvested_g),
            rel_err(now.P, init.P + l.P_in_g - l.P_leached_g - l.P_harvested_g),
            rel_err(now.K, init.K + l.K_in_g - l.K_leached_g - l.K_harvested_g),
            rel_err(now.Ca, init.Ca + l.Ca_in_g - l.Ca_leached_g - l.Ca_harvested_g),
            rel_err(now.Mg, init.Mg + l.Mg_in_g - l.Mg_leached_g - l.Mg_harvested_g),
            rel_err(now.Mn, init.Mn + l.Mn_in_g - l.Mn_leached_g - l.Mn_harvested_g),
            rel_err(now.S, init.S + l.S_in_g - l.S_leached_g),
            rel_err(now.C, init.C + l.C_in_g + l.C_root_litter_g + l.C_leaf_litter_g - l.C_respired_g),
        };
        static constexpr const char* kNames[9] = {"water", "N", "P", "K", "Ca", "Mg", "Mn", "S", "C"};
        const double worst = *std::max_element(errs.begin(), errs.end());
        result.max_err = std::max(result.max_err, worst);

        std::string reason = invalid_reason(snap, params);
        // 감염은 줄지 않아요 (같은 작물이 이어지는 동안. 정식으로 새 작물이 생기면 0부터)
        for (std::size_t i = 0; reason.empty() && i < snap.crops.size(); ++i) {
            if (!prev.crops[i].present || !snap.crops[i].present || (!prev.crops[i].alive && snap.crops[i].alive)) {
                continue;
            }
            for (std::size_t d = 0; d < sim::kSoilDiseaseCount; ++d) {
                if (snap.crops[i].infection_index[d] < prev.crops[i].infection_index[d]) {
                    reason = std::format("crop {} infection decreased", i);
                }
            }
        }
        if (reason.empty() && (l.N_flush_g < prev.ledger.N_flush_g || l.Mn_flush_g < prev.ledger.Mn_flush_g)) {
            reason = "ledger flush diagnostics decreased";
        }
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
    result.ledger = prev.ledger;
    return result;
}

Result report(const std::string& label, const Result& r) {
    WARN(std::format("conservation max relative error [{}] = {:.3e}, N_flush {:.1f} g, Mn_flush {:.2f} g, died {}", label,
                     r.max_err, r.ledger.N_flush_g, r.ledger.Mn_flush_g, r.died));
    CHECK(r.max_err <= kRelTol);
    return r;
}

Result scenario_case(const std::string& name, std::uint64_t days) {
    return report(name, run_and_check(load(name), days * kDay));
}

// 일정표에서 조건에 맞는 명령을 지워요
template <typename Cmd>
void erase_commands(Loaded& l) {
    std::erase_if(l.bundle.schedule, [](const auto& item) { return std::holds_alternative<Cmd>(item.command); });
}

// 약제 투입량을 factor배로
std::size_t scale_disinfectant(Loaded& l, double factor) {
    std::size_t scaled = 0;
    for (auto& item : l.bundle.schedule) {
        if (auto* d = std::get_if<sim::ApplyDisinfectantCommand>(&item.command)) {
            d->g_per_m2 *= factor;
            ++scaled;
        }
    }
    return scaled;
}

}  // namespace

// ---- M5a 시나리오 전부 (README "실행 일수" 그대로, 매 틱) ----

TEST_CASE("M5a S19 scenarios conserve every budget every tick", "[conservation][m5a][s19]") {
    for (const char* name : {"s19_solar_4w", "s19_closed_nofilm", "s19_vented", "s19_solar_2w", "s19_solar_dry"}) {
        const Result r = scenario_case(name, 28);
        if (std::string(name) == "s19_solar_4w") {
            // 소독 무기화·망간 환원이 실제로 일어난 실행에서도 보존식이 그대로예요
            CHECK(r.ledger.N_flush_g > 0.0);
            CHECK(r.ledger.Mn_flush_g > 0.0);
        }
    }
}

TEST_CASE("M5a S20 scenarios conserve every budget every tick", "[conservation][m5a][s20]") {
    for (const char* name : {"s20_none", "s20_solar", "s20_dazomet", "s20_chloropicrin"}) {
        const Result r = scenario_case(name, 70);
        if (std::string(name) != "s20_none") {
            CHECK(r.ledger.N_flush_g > 0.0);
            CHECK(r.ledger.Mn_flush_g > 0.0);
        }
    }
}

TEST_CASE("M5a S21 scenarios conserve every budget every tick", "[conservation][m5a][s21]") {
    for (const char* name : {"s21_plant_day0", "s21_plant_day42", "s21_plant_day0_alkaline"}) {
        scenario_case(name, 98);
    }
    for (const char* name : {"s21_dazomet_day0", "s21_dazomet_day3", "s21_dazomet_day21", "s21_chloropicrin_day21"}) {
        scenario_case(name, 77);
    }
}

TEST_CASE("M5a S22 scenarios conserve every budget every tick", "[conservation][m5a][s22]") {
    for (const char* name : {"s22_fusarium_none", "s22_fusarium_acid", "s22_fusarium_antagonist"}) {
        scenario_case(name, 120);
    }
    scenario_case("s22_fusarium_solar", 182);
}

TEST_CASE("M5a S23 to S25 scenarios conserve every budget every tick", "[conservation][m5a][s23][s24][s25]") {
    for (const char* name : {"s23_proper", "s23_waterlogged_mild", "s23_waterlogged_hot", "s23_waterlogged_cold"}) {
        scenario_case(name, 28);
    }
    for (const char* name : {"s24_no_compost", "s24_compost"}) {
        scenario_case(name, 60);
    }
    for (const char* name : {"s25_standard_none", "s25_standard_inoc", "s25_dry_none", "s25_dry_inoc", "s25_highp_none",
                             "s25_highp_inoc"}) {
        scenario_case(name, 42);
    }
}

TEST_CASE("M5a S33 scenarios conserve every budget every tick", "[conservation][m5a][s33]") {
    for (const char* name : {"s33_disinfect_clear", "s33_disinfect_black", "s33_disinfect_white", "s33_disinfect_none"}) {
        scenario_case(name, 28);
    }
    for (const char* name : {"s33_planted_clear", "s33_planted_black", "s33_planted_white", "s33_planted_none"}) {
        scenario_case(name, 60);
    }
}

// ---- 극한 ----

// 극한: 소독 중 관수 0 (태양열 당일 정식, 다조멧). 마른 흙에서도 풀이 음수가 되지 않고 보존식이 그대로예요
TEST_CASE("M5a extreme disinfection without irrigation keeps conservation", "[conservation][m5a][extreme]") {
    Loaded solar = load("s21_plant_day0");
    erase_commands<sim::IrrigateCommand>(solar);
    report("s21_plant_day0 without irrigation", run_and_check(std::move(solar), 98 * kDay));
    Loaded daz = load("s20_dazomet");
    erase_commands<sim::IrrigateCommand>(daz);
    report("s20_dazomet without irrigation", run_and_check(std::move(daz), 70 * kDay));
}

// 극한: 약제 100배 (작물 없음 2종, 잔류가 남은 채 정식하는 다조멧 당일 정식). 무기화가 원천 풀로 제한되고,
// 고사한 작물의 원소는 토양으로 돌아가요
TEST_CASE("M5a extreme 100x fumigant dose keeps conservation", "[conservation][m5a][extreme]") {
    for (const char* name : {"s20_dazomet", "s20_chloropicrin"}) {
        Loaded l = load(name);
        REQUIRE(scale_disinfectant(l, 100.0) > 0);
        const Result r = report(std::string(name) + " x100", run_and_check(std::move(l), 70 * kDay));
        CHECK(r.ledger.N_flush_g > 0.0);
    }
    Loaded planted = load("s21_dazomet_day0");
    REQUIRE(scale_disinfectant(planted, 100.0) > 0);
    const Result r = report("s21_dazomet_day0 x100", run_and_check(std::move(planted), 77 * kDay));
    CHECK(r.died > 0);  // 잔류 속에 심은 포기는 약제로 죽어요
}

// 극한: 필름 + 난방 30 °C (7월 태양열 소독, 겨울 정식 후 투명 필름)
TEST_CASE("M5a extreme film with heating at 30 C keeps conservation", "[conservation][m5a][extreme]") {
    for (const auto& [name, days] : {std::pair{"s19_solar_4w", 28}, std::pair{"s33_planted_clear", 60}}) {
        Loaded l = load(name);
        erase_commands<sim::SetHeaterCommand>(l);
        l.bundle.schedule.push_back({0, sim::SetHeaterCommand{.on = true, .setpoint_degC = 30.0}});
        report(std::string(name) + " + heater 30 C", run_and_check(std::move(l), static_cast<std::uint64_t>(days) * kDay));
    }
}
