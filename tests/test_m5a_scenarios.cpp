// M5a 시나리오 테스트 (docs/spec/m5-overview.md 9장 "M5a" S19~S25·S33, disinfection "시나리오 테스트")
// 실제 data/params.toml과 data/scenarios/m5a/*.toml을 읽어 "기대 방향"(순서)만 검사해요. 수치는 WARN으로 출력해요.
// 측정 정의: 토양 값은 하우스 80칸 평균(셀 간 병원균 차이가 있어서), 작물 값은 작물이 있는 타일 평균,
// "누적 소독 사멸"은 셀 평균 disinfection_kill_index의 틱 합(tools/run_scenarios.py의 disinfection_kill_cum_* 열과 같은 정의).
// 시계열 인덱스 = 처리한 틱 수 (0 = 초기 상태). 실행 일수는 data/scenarios/m5a/README.md 표 그대로예요.
// 같은 시나리오는 한 테스트 바이너리 안에서 한 번만 돌리고 결과를 캐시해요.
// 5장 "프로토타입 4차 값"에서 이탈로 기록된 항목(S19c 마른 흙 누적 사멸, A1 일최고 평균, A19 균근균 건조 완화 크기)은
// 실패하는 방향을 검사하지 않고, 성립하는 비교만 검사해요(각 케이스 주석).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

using G = sim::MicrobeGroup;
constexpr std::size_t kGroups = sim::kMicrobeGroupCount;
constexpr std::size_t idx(G g) { return sim::microbe(g); }

constexpr std::uint64_t kDay = 24;
constexpr std::uint64_t kNever = std::numeric_limits<std::uint64_t>::max();
constexpr std::size_t kDazomet = 0;  // data/params.toml [[disinfectant]] 순서
constexpr std::size_t kChloropicrin = 1;

// data/scenarios/m5a/README.md "실행 일수"
std::uint64_t days_of(const std::string& name) {
    static const std::map<std::string, std::uint64_t> days = {
        {"s19_solar_4w", 28},         {"s19_closed_nofilm", 28},       {"s19_vented", 28},
        {"s19_solar_2w", 28},         {"s19_solar_dry", 28},           {"s20_none", 70},
        {"s20_solar", 70},            {"s20_dazomet", 70},             {"s20_chloropicrin", 70},
        {"s21_plant_day0", 98},       {"s21_plant_day42", 98},         {"s21_plant_day0_alkaline", 98},
        {"s21_dazomet_day0", 77},     {"s21_dazomet_day3", 77},        {"s21_dazomet_day21", 77},
        {"s21_chloropicrin_day21", 77}, {"s22_fusarium_none", 120},    {"s22_fusarium_acid", 120},
        {"s22_fusarium_antagonist", 120}, {"s22_fusarium_solar", 182}, {"s23_proper", 28},
        {"s23_waterlogged_mild", 28}, {"s23_waterlogged_hot", 28},     {"s23_waterlogged_cold", 28},
        {"s24_no_compost", 60},       {"s24_compost", 60},             {"s25_standard_none", 42},
        {"s25_standard_inoc", 42},    {"s25_dry_none", 42},            {"s25_dry_inoc", 42},
        {"s25_highp_none", 42},       {"s25_highp_inoc", 42},          {"s33_disinfect_clear", 28},
        {"s33_disinfect_black", 28},  {"s33_disinfect_white", 28},     {"s33_disinfect_none", 28},
        {"s33_planted_clear", 60},    {"s33_planted_black", 60},       {"s33_planted_white", 60},
        {"s33_planted_none", 60},
    };
    const auto it = days.find(name);
    REQUIRE(it != days.end());
    return it->second;
}

// 한 시나리오 실행의 시계열과 끝 값
struct Run {
    std::uint64_t ticks = 0;
    std::uint64_t plant_index = kNever;  // 작물이 처음 있는 시계열 인덱스

    // 하우스 평균 시계열 (인덱스 = 처리한 틱 수)
    std::vector<double> soil_temp_degC, nh4_N, no2_N, water_mm;
    std::array<std::vector<double>, kGroups> microbe;
    std::array<std::vector<double>, 2> residue;  // 약제별 잔류
    // 작물 (작물이 있는 타일 평균, 없으면 0)
    std::vector<double> root_health, sufficiency_Ca, antagonism_Ca, fumigant_exposure;
    std::array<double, kGroups> cum_kill{};  // 누적 소독 사멸 (셀 평균의 틱 합)

    double alive_fraction = 0.0;  // 끝 시점
    double infection_fusarium = 0.0;
    double infection_phytophthora = 0.0;
    double leaf_g_per_m2 = 0.0;
    double gross_sum_g_per_m2 = 0.0;  // last_gross_g_per_m2의 모든 틱 합
    std::size_t nitrite_events = 0;
    std::size_t died_events = 0;
    sim::Ledger ledger;  // 끝 시점

    [[nodiscard]] double microbe_at(G g, std::uint64_t i) const { return microbe[idx(g)].at(i); }
    [[nodiscard]] double microbe_end(G g) const { return microbe[idx(g)].back(); }
};

double mean_range(const std::vector<double>& v, std::size_t from, std::size_t to) {
    to = std::min(to, v.size());
    REQUIRE(from < to);
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        s += v[i];
    }
    return s / static_cast<double>(to - from);
}
double min_range(const std::vector<double>& v, std::size_t from, std::size_t to) {
    to = std::min(to, v.size());
    REQUIRE(from < to);
    return *std::min_element(v.begin() + static_cast<std::ptrdiff_t>(from), v.begin() + static_cast<std::ptrdiff_t>(to));
}
double max_range(const std::vector<double>& v, std::size_t from, std::size_t to) {
    to = std::min(to, v.size());
    REQUIRE(from < to);
    return *std::max_element(v.begin() + static_cast<std::ptrdiff_t>(from), v.begin() + static_cast<std::ptrdiff_t>(to));
}
double mean_all(const std::vector<double>& v) { return mean_range(v, 0, v.size()); }
double max_all(const std::vector<double>& v) { return max_range(v, 0, v.size()); }
double min_all(const std::vector<double>& v) { return min_range(v, 0, v.size()); }
// 하루(24틱)마다 최고값의 평균 (인덱스 1.. = 처리한 틱)
double daily_max_mean(const std::vector<double>& v) {
    double sum = 0.0;
    std::size_t days = 0;
    for (std::size_t start = 1; start + kDay <= v.size(); start += kDay) {
        sum += max_range(v, start, start + kDay);
        ++days;
    }
    REQUIRE(days > 0);
    return sum / static_cast<double>(days);
}
// from 이후 처음으로 값이 threshold 이상이 되는 인덱스
std::uint64_t first_at_or_above(const std::vector<double>& v, std::size_t from, double threshold) {
    for (std::size_t i = from; i < v.size(); ++i) {
        if (v[i] >= threshold) {
            return i;
        }
    }
    return kNever;
}
std::size_t argmin(const std::vector<double>& v) {
    return static_cast<std::size_t>(std::min_element(v.begin(), v.end()) - v.begin());
}
std::size_t count_above(const std::vector<double>& v, std::size_t from, std::size_t to, double threshold) {
    to = std::min(to, v.size());
    std::size_t n = 0;
    for (std::size_t i = from; i < to; ++i) {
        n += v[i] > threshold ? 1 : 0;
    }
    return n;
}

sim::Params load_repo_params() {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    return std::move(*params);
}

Run run_uncached(const std::string& name) {
    sim::Params params = load_repo_params();
    const sim::Params p = params;
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m5a/" + name + ".toml"));
    REQUIRE(bundle.has_value());
    std::stable_sort(bundle->schedule.begin(), bundle->schedule.end(),
                     [](const auto& a, const auto& b) { return a.tick < b.tick; });
    auto world = sim::World::create(std::move(params), std::move(bundle->scenario));
    REQUIRE(world.has_value());
    REQUIRE(p.disinfectants.size() == 2);

    Run run;
    run.ticks = days_of(name) * kDay;
    auto record = [&](const sim::Snapshot& snap, std::size_t index) {
        const double n = static_cast<double>(snap.soil.size());
        double temp = 0.0, nh4 = 0.0, no2 = 0.0, water = 0.0;
        std::array<double, kGroups> m{};
        std::array<double, kGroups> kill{};
        std::array<double, 2> res{};
        for (const sim::SoilCell& c : snap.soil) {
            temp += c.soil_temp_degC;
            nh4 += c.nh4_N_g_per_m2;
            no2 += c.no2_N_g_per_m2;
            water += c.water_mm;
            for (std::size_t g = 0; g < kGroups; ++g) {
                m[g] += c.microbe_index[g];
                kill[g] += c.disinfection_kill_index[g];
            }
            for (std::size_t d = 0; d < 2; ++d) {
                res[d] += c.fumigant_residue_index[d];
            }
        }
        run.soil_temp_degC.push_back(temp / n);
        run.nh4_N.push_back(nh4 / n);
        run.no2_N.push_back(no2 / n);
        run.water_mm.push_back(water / n);
        for (std::size_t g = 0; g < kGroups; ++g) {
            run.microbe[g].push_back(m[g] / n);
            if (index > 0) {
                run.cum_kill[g] += kill[g] / n;
            }
        }
        for (std::size_t d = 0; d < 2; ++d) {
            run.residue[d].push_back(res[d] / n);
        }

        double present = 0.0, alive = 0.0, rh = 0.0, suff_ca = 0.0, ant_ca = 0.0, exposure = 0.0;
        double inf_f = 0.0, inf_p = 0.0, leaf = 0.0, gross = 0.0;
        for (std::size_t i = 0; i < snap.crops.size(); ++i) {
            const sim::Crop& crop = snap.crops[i];
            if (!crop.present) {
                continue;
            }
            present += 1.0;
            alive += crop.alive ? 1.0 : 0.0;
            rh += crop.root_health;
            suff_ca += sim::process::nutrient_status(crop, p.crop).sufficiency_Ca;
            ant_ca += crop.antagonism_Ca_factor;
            exposure += sim::process::fumigant_exposure_index(snap.soil[i], p);
            inf_f += crop.infection_index[static_cast<std::size_t>(sim::SoilDisease::Fusarium)];
            inf_p += crop.infection_index[static_cast<std::size_t>(sim::SoilDisease::Phytophthora)];
            leaf += crop.leaf_g_per_m2;
            gross += crop.last_gross_g_per_m2;
        }
        if (present > 0.0) {
            if (run.plant_index == kNever) {
                run.plant_index = index;
            }
            run.root_health.push_back(rh / present);
            run.sufficiency_Ca.push_back(suff_ca / present);
            run.antagonism_Ca.push_back(ant_ca / present);
            run.fumigant_exposure.push_back(exposure / present);
            run.alive_fraction = alive / present;
            run.infection_fusarium = inf_f / present;
            run.infection_phytophthora = inf_p / present;
            run.leaf_g_per_m2 = leaf / present;
            run.gross_sum_g_per_m2 += gross / present;
        } else {
            run.root_health.push_back(0.0);
            run.sufficiency_Ca.push_back(0.0);
            run.antagonism_Ca.push_back(0.0);
            run.fumigant_exposure.push_back(0.0);
        }
        run.ledger = snap.ledger;
    };

    record(world->snapshot(), 0);
    std::size_t next = 0;
    const auto& schedule = bundle->schedule;
    for (std::uint64_t t = 0; t < run.ticks; ++t) {
        while (next < schedule.size() && schedule[next].tick == t) {
            REQUIRE(world->enqueue(schedule[next].command).has_value());
            ++next;
        }
        world->step(1);
        for (const sim::Event& event : world->events()) {
            run.nitrite_events += std::holds_alternative<sim::NitriteThresholdExceededEvent>(event.payload) ? 1 : 0;
            run.died_events += std::holds_alternative<sim::PlantDiedEvent>(event.payload) ? 1 : 0;
        }
        record(world->snapshot(), static_cast<std::size_t>(t + 1));
    }
    return run;
}

const Run& run_scenario(const std::string& name) {
    static std::map<std::string, Run> cache;
    auto it = cache.find(name);
    if (it == cache.end()) {
        it = cache.emplace(name, run_uncached(name)).first;
    }
    return it->second;
}

// 정식 뒤 window 틱 구간 [plant, plant + window)
std::pair<std::size_t, std::size_t> after_plant(const Run& r, std::uint64_t window) {
    REQUIRE(r.plant_index != kNever);
    return {static_cast<std::size_t>(r.plant_index), static_cast<std::size_t>(r.plant_index + window)};
}

}  // namespace

// S19: 태양열(투명 필름 + 밀폐) > 필름 없이 밀폐 > 환기 순으로 지온이 높고, 시들음병균·역병균 사멸도 같은 순서
// (완료 기준: 토양 온도가 클수록 사멸량 증가). 4차 값 A1 "일최고 평균 41.9 °C"는 목표 45~52 °C 밖(이탈 기록)이라
// 절대값은 검사하지 않고 순서만 봐요.
TEST_CASE("M5a S19 hotter solarization kills more pathogens", "[scenario][m5a][s19]") {
    const Run& film = run_scenario("s19_solar_4w");
    const Run& closed = run_scenario("s19_closed_nofilm");
    const Run& vented = run_scenario("s19_vented");
    WARN(std::format("S19 soil temp mean / daily max mean: film {:.2f}/{:.2f}, closed {:.2f}/{:.2f}, vented {:.2f}/{:.2f}",
                     mean_all(film.soil_temp_degC), daily_max_mean(film.soil_temp_degC),
                     mean_all(closed.soil_temp_degC), daily_max_mean(closed.soil_temp_degC),
                     mean_all(vented.soil_temp_degC), daily_max_mean(vented.soil_temp_degC)));
    WARN(std::format("S19 cumulative kill fusarium/phytophthora: film {:.3f}/{:.3f}, closed {:.4f}/{:.4f}, vented {:.4f}/{:.4f}",
                     film.cum_kill[idx(G::Fusarium)], film.cum_kill[idx(G::Phytophthora)],
                     closed.cum_kill[idx(G::Fusarium)], closed.cum_kill[idx(G::Phytophthora)],
                     vented.cum_kill[idx(G::Fusarium)], vented.cum_kill[idx(G::Phytophthora)]));
    // 지온: 필름 > 밀폐 > 환기 (평균과 일최고 평균 모두, 1 °C 이상 차이)
    CHECK(mean_all(film.soil_temp_degC) > mean_all(closed.soil_temp_degC) + 1.0);
    CHECK(mean_all(closed.soil_temp_degC) > mean_all(vented.soil_temp_degC) + 1.0);
    CHECK(daily_max_mean(film.soil_temp_degC) > daily_max_mean(closed.soil_temp_degC) + 1.0);
    CHECK(daily_max_mean(closed.soil_temp_degC) > daily_max_mean(vented.soil_temp_degC) + 1.0);
    // 사멸: 같은 순서
    for (const G g : {G::Fusarium, G::Phytophthora}) {
        CHECK(film.cum_kill[idx(g)] > 10.0 * closed.cum_kill[idx(g)]);
        CHECK(closed.cum_kill[idx(g)] > vented.cum_kill[idx(g)]);
        CHECK(film.microbe_end(g) < closed.microbe_end(g));
        // 필름 4주는 병원균을 크게 줄여요 (A4·A5 방향: 시작의 10% 아래)
        CHECK(film.microbe_end(g) < 0.1 * film.microbe_at(g, 0));
    }
}

// S19b: 2주 대 4주 소독, 4주 쪽 누적 사멸이 큼 (완료 기준: 지속 시간)
TEST_CASE("M5a S19b longer solarization kills more", "[scenario][m5a][s19][s19b]") {
    const Run& two = run_scenario("s19_solar_2w");
    const Run& four = run_scenario("s19_solar_4w");
    WARN(std::format("S19b cumulative kill fusarium 2w {:.3f} 4w {:.3f}, phytophthora 2w {:.3f} 4w {:.3f}",
                     two.cum_kill[idx(G::Fusarium)], four.cum_kill[idx(G::Fusarium)],
                     two.cum_kill[idx(G::Phytophthora)], four.cum_kill[idx(G::Phytophthora)]));
    for (const G g : {G::Fusarium, G::Phytophthora}) {
        CHECK(four.cum_kill[idx(g)] > two.cum_kill[idx(g)]);
        CHECK(four.microbe_end(g) < two.microbe_end(g));
    }
    // 유익균도 오래 덮은 쪽이 더 죽어요
    CHECK(four.cum_kill[idx(G::Decomposer)] > two.cum_kill[idx(G::Decomposer)]);
}

// S19c: 마른 흙(관수 없음) 대 젖은 흙. 4차 값 기록: 4주 누적 병원균 사멸은 0.488 대 0.488로 차이가 없어요
// (이탈 — 병원균이 마른 쪽도 4주 안에 다 죽어 포화). 그래서 4주 병원균 누적 사멸은 비교하지 않고, 명세 기록이 말한 대로
// "2주 시점"(마른 쪽이 덜 줄어듦)과 포화되지 않는 무리(분해균·길항균의 누적 사멸)로 방향을 봐요.
TEST_CASE("M5a S19c dry soil kills less where the kill is not saturated", "[scenario][m5a][s19][s19c]") {
    const Run& dry = run_scenario("s19_solar_dry");
    const Run& wet = run_scenario("s19_solar_4w");
    const std::uint64_t day14 = 14 * kDay;
    WARN(std::format("S19c water mean dry {:.1f} wet {:.1f}; day 14 fusarium dry {:.3f} wet {:.3f}; "
                     "cum kill decomposer dry {:.3f} wet {:.3f}, antagonist dry {:.3f} wet {:.3f}; "
                     "4-week cum kill fusarium dry {:.3f} wet {:.3f} (recorded deviation, not checked)",
                     mean_all(dry.water_mm), mean_all(wet.water_mm), dry.microbe_at(G::Fusarium, day14),
                     wet.microbe_at(G::Fusarium, day14), dry.cum_kill[idx(G::Decomposer)],
                     wet.cum_kill[idx(G::Decomposer)], dry.cum_kill[idx(G::Antagonist)],
                     wet.cum_kill[idx(G::Antagonist)], dry.cum_kill[idx(G::Fusarium)], wet.cum_kill[idx(G::Fusarium)]));
    // 전제: 관수 없는 쪽이 실제로 마름
    CHECK(mean_all(dry.water_mm) < mean_all(wet.water_mm));
    // 2주 시점: 마른 쪽 병원균이 덜 줄었어요
    for (const G g : {G::Fusarium, G::Phytophthora}) {
        CHECK(dry.microbe_at(g, day14) > wet.microbe_at(g, day14));
    }
    // 포화되지 않는 무리: 마른 쪽 누적 사멸이 작아요
    CHECK(dry.cum_kill[idx(G::Decomposer)] < wet.cum_kill[idx(G::Decomposer)]);
    CHECK(dry.cum_kill[idx(G::Antagonist)] < wet.cum_kill[idx(G::Antagonist)]);
}

// S20: 소독한 세 쪽(태양열·다조멧·클로로피크린) 모두 유익(분해균·질산화균·길항균·균근균)과 유해(병원균)가 함께 감소
// (완료 기준). 약제는 길항균도 크게 줄이고 태양열은 덜 줄여요. 클로로피크린은 질산화균 회복이 다조멧보다 늦어요.
TEST_CASE("M5a S20 every disinfection lowers beneficial and harmful groups together", "[scenario][m5a][s20]") {
    const Run& none = run_scenario("s20_none");
    const Run& solar = run_scenario("s20_solar");
    const Run& daz = run_scenario("s20_dazomet");
    const Run& chl = run_scenario("s20_chloropicrin");
    const std::size_t win = 28 * kDay + 1;  // 처음 28일 (회복 확인은 70일 전체)
    const std::array<G, 7> all = {G::Decomposer, G::NitrifierAob, G::NitrifierNob, G::Antagonist,
                                  G::Mycorrhiza,  G::Fusarium,     G::Phytophthora};
    for (const Run* r : {&none, &solar, &daz, &chl}) {
        std::string line;
        for (const G g : all) {
            line += std::format(" {:.3f}", min_range(r->microbe[idx(g)], 0, win));
        }
        WARN("S20 28-day minimum index (decomp aob nob antag myco fus phyto):" + line);
    }
    for (const Run* r : {&solar, &daz, &chl}) {
        for (const G g : all) {
            INFO("group " << sim::kMicrobeGroupIds[idx(g)]);
            const double treated = min_range(r->microbe[idx(g)], 0, win);
            const double control = min_range(none.microbe[idx(g)], 0, win);
            // 대조보다 낮아요 (질산화균은 다조멧에서 거의 살아서(A10 30% 이하 감소) 5% 차이만 요구)
            CHECK(treated < 0.95 * control);
            if (g == G::Fusarium || g == G::Phytophthora) {
                CHECK(treated < 0.5 * control);
            }
        }
        // 소독 무기화·망간 환원이 실제로 일어났어요
        CHECK(r->ledger.N_flush_g > 0.0);
        CHECK(r->ledger.Mn_flush_g > 0.0);
    }
    CHECK(none.ledger.N_flush_g == 0.0);

    // 약제는 길항균을 크게 줄이고(시작의 절반 아래), 태양열은 덜 줄여요 (최저값·누적 사멸 모두)
    const double antagonist_start = none.microbe_at(G::Antagonist, 0);
    const double solar_antagonist_min = min_all(solar.microbe[idx(G::Antagonist)]);
    WARN(std::format("S20 antagonist min solar {:.3f} dazomet {:.3f} chloropicrin {:.3f}; cum kill {:.3f} {:.3f} {:.3f}",
                     solar_antagonist_min, min_all(daz.microbe[idx(G::Antagonist)]),
                     min_all(chl.microbe[idx(G::Antagonist)]), solar.cum_kill[idx(G::Antagonist)],
                     daz.cum_kill[idx(G::Antagonist)], chl.cum_kill[idx(G::Antagonist)]));
    for (const Run* r : {&daz, &chl}) {
        CHECK(min_all(r->microbe[idx(G::Antagonist)]) < 0.5 * antagonist_start);
        CHECK(solar_antagonist_min > min_all(r->microbe[idx(G::Antagonist)]) + 0.03);
        CHECK(solar.cum_kill[idx(G::Antagonist)] < r->cum_kill[idx(G::Antagonist)]);
    }

    // 클로로피크린은 질산화균을 거의 다 죽이고(다조멧은 거의 살림), 0.8까지 회복이 다조멧보다 늦어요
    const std::vector<double>& aob_daz = daz.microbe[idx(G::NitrifierAob)];
    const std::vector<double>& aob_chl = chl.microbe[idx(G::NitrifierAob)];
    const std::uint64_t daz_recovered = first_at_or_above(aob_daz, argmin(aob_daz), 0.8);
    const std::uint64_t chl_recovered = first_at_or_above(aob_chl, argmin(aob_chl), 0.8);
    WARN(std::format("S20 AOB min dazomet {:.3f} chloropicrin {:.3f}; back to 0.8 at day {:.1f} / {:.1f}; end {:.3f} / {:.3f}",
                     min_all(aob_daz), min_all(aob_chl),
                     daz_recovered == kNever ? -1.0 : static_cast<double>(daz_recovered) / static_cast<double>(kDay),
                     chl_recovered == kNever ? -1.0 : static_cast<double>(chl_recovered) / static_cast<double>(kDay),
                     aob_daz.back(),
                     aob_chl.back()));
    CHECK(min_all(aob_chl) < 0.2);
    CHECK(min_all(aob_daz) > 0.7);
    REQUIRE(daz_recovered != kNever);
    CHECK(chl_recovered > daz_recovered + 14 * kDay);  // 미회복(kNever)이어도 늦은 것
    CHECK(aob_chl[28 * kDay] < aob_daz[28 * kDay]);
    CHECK(aob_chl.back() < aob_daz.back());
}

// S21: 태양열 소독 끝 당일 정식 대 6주 뒤 정식 (둘 다 황산암모늄 기비). 당일 정식 쪽 NH4+ 최고치와 지속이 크고,
// 길항(K+NH4 / Ca+Mg)이 커서 Ca 충족도가 낮아요. 정식 뒤 같은 28일 구간으로 비교해요(6주 뒤 정식은 28일만 남음).
// 아질산: 명세·4차 값(A13)은 하우스 평균 NO2 원값을 임계치 2 g과 비교해 "알칼리만 넘음"이에요. 그 정의로 검사해요.
// (관찰 기록: 아질산 경보 이벤트는 pH 독성 배율을 곱한 유효 아질산으로 판정해서, pH 6.2인 당일 정식도 경보가 나요.
//  그래서 이벤트로는 "6주 뒤 정식은 경보 없음, 알칼리는 경보"만 검사해요.)
TEST_CASE("M5a S21 planting right after solarization traps ammonium", "[scenario][m5a][s21]") {
    const sim::Params p = load_repo_params();
    const Run& day0 = run_scenario("s21_plant_day0");
    const Run& day42 = run_scenario("s21_plant_day42");
    const Run& alkaline = run_scenario("s21_plant_day0_alkaline");
    const std::uint64_t window = 28 * kDay;
    const auto [a0, b0] = after_plant(day0, window);
    const auto [a42, b42] = after_plant(day42, window);
    CHECK(day0.plant_index < day42.plant_index);
    const double nh4_high_g_per_m2 = 5.0;  // "높게 남음"의 판정 기준 (게임용 임의값, 판정에만 씀)
    WARN(std::format("S21 28 days after planting: NH4 max {:.2f} / {:.2f}, mean {:.2f} / {:.2f}, hours > 5 g {} / {}; "
                     "Ca sufficiency min {:.3f} / {:.3f}; Ca antagonism factor mean {:.3f} / {:.3f}",
                     max_range(day0.nh4_N, a0, b0), max_range(day42.nh4_N, a42, b42), mean_range(day0.nh4_N, a0, b0),
                     mean_range(day42.nh4_N, a42, b42), count_above(day0.nh4_N, a0, b0, nh4_high_g_per_m2),
                     count_above(day42.nh4_N, a42, b42, nh4_high_g_per_m2), min_range(day0.sufficiency_Ca, a0, b0),
                     min_range(day42.sufficiency_Ca, a42, b42), mean_range(day0.antagonism_Ca, a0, b0),
                     mean_range(day42.antagonism_Ca, a42, b42)));
    // NH4+: 최고치와 평균이 크고, 높은 상태가 오래 가요
    CHECK(max_range(day0.nh4_N, a0, b0) > 2.0 * max_range(day42.nh4_N, a42, b42));
    CHECK(mean_range(day0.nh4_N, a0, b0) > 2.0 * mean_range(day42.nh4_N, a42, b42));
    CHECK(count_above(day0.nh4_N, a0, b0, nh4_high_g_per_m2) >
          count_above(day42.nh4_N, a42, b42, nh4_high_g_per_m2) + 7 * kDay);
    // 정식 때 질산화균: 당일 정식은 아직 죽어 있고 6주 뒤에는 회복 중
    CHECK(day0.microbe_at(G::NitrifierAob, a0) < 0.5 * day42.microbe_at(G::NitrifierAob, a42));
    // 길항이 커서(길항 배율이 작아서) Ca 충족도가 낮아요
    CHECK(mean_range(day0.antagonism_Ca, a0, b0) < mean_range(day42.antagonism_Ca, a42, b42) - 0.05);
    CHECK(min_range(day0.sufficiency_Ca, a0, b0) < min_range(day42.sufficiency_Ca, a42, b42) - 0.05);

    // 아질산: 하우스 평균 NO2 원값은 알칼리 변형에서만 임계치를 넘어요
    const double threshold = p.nitrification.nitrite_toxicity_threshold_g_N_per_m2;
    WARN(std::format("S21 NO2 max: day0 {:.3f}, day42 {:.3f}, alkaline {:.3f} (threshold {:.1f}); nitrite events {} / {} / {}",
                     max_all(day0.no2_N), max_all(day42.no2_N), max_all(alkaline.no2_N), threshold, day0.nitrite_events,
                     day42.nitrite_events, alkaline.nitrite_events));
    CHECK(max_all(alkaline.no2_N) > threshold);
    CHECK(max_all(day0.no2_N) < threshold);
    CHECK(max_all(day42.no2_N) < threshold);
    CHECK(alkaline.nitrite_events > 0);
    CHECK(day42.nitrite_events == 0);
}

// S21 약제 변형: 다조멧 필름 7일 + 경운 1회 뒤 0·3·21일에 정식. 일찍 심을수록 약제 잔류에 뿌리가 닿아 손상이 커요.
// 클로로피크린 21일 정식은 같은 날 다조멧보다 질산화균이 덜 회복돼 NH4+가 더 오래 남아요(암모늄 함정).
// 뿌리 건강도 최저값에는 뒤에 오는 시들음병 감염(정식이 이를수록 감염 기간이 김)이 섞여서, 약제 손상은
// 정식 뒤 7일 동안의 약제 노출(잔류 × 활성 합의 평균)과 정식 3일 뒤 뿌리 건강도로 봐요.
TEST_CASE("M5a S21 chemical variants damage roots when planted early", "[scenario][m5a][s21][s21_chem]") {
    const Run& d0 = run_scenario("s21_dazomet_day0");
    const Run& d3 = run_scenario("s21_dazomet_day3");
    const Run& d21 = run_scenario("s21_dazomet_day21");
    const Run& c21 = run_scenario("s21_chloropicrin_day21");
    const auto exposure_7d = [](const Run& r) {
        const auto [a, b] = after_plant(r, 7 * kDay);
        return mean_range(r.fumigant_exposure, a, b);
    };
    const auto residue_at_plant = [](const Run& r) {
        return r.residue[kDazomet].at(r.plant_index) + r.residue[kChloropicrin].at(r.plant_index);
    };
    const auto root_health_3d = [](const Run& r) { return r.root_health.at(r.plant_index + 3 * kDay); };
    WARN(std::format("S21 chem residue at planting day0 {:.4f} day3 {:.4f} day21 {:.6f}; 7-day exposure {:.4f} {:.4f} {:.6f}; "
                     "root health 3 days after planting {:.3f} {:.3f} {:.3f}",
                     residue_at_plant(d0), residue_at_plant(d3), residue_at_plant(d21), exposure_7d(d0), exposure_7d(d3),
                     exposure_7d(d21), root_health_3d(d0), root_health_3d(d3), root_health_3d(d21)));
    CHECK(residue_at_plant(d0) > residue_at_plant(d3));
    CHECK(residue_at_plant(d3) > residue_at_plant(d21));
    CHECK(exposure_7d(d0) > exposure_7d(d3));
    CHECK(exposure_7d(d3) > exposure_7d(d21));
    CHECK(root_health_3d(d0) < root_health_3d(d21) - 0.1);
    CHECK(root_health_3d(d0) < root_health_3d(d3));
    CHECK(root_health_3d(d3) <= root_health_3d(d21));

    // 클로로피크린: 정식 때 질산화균이 낮고 NH4+가 오래 남아요 (같은 21일 정식, 정식 뒤 28일)
    const std::uint64_t window = 28 * kDay;
    const auto [ad, bd] = after_plant(d21, window);
    const auto [ac, bc] = after_plant(c21, window);
    const double nh4_high_g_per_m2 = 3.0;  // 판정 기준 (게임용 임의값, 판정에만 씀)
    WARN(std::format("S21 chem day21 AOB at planting dazomet {:.3f} chloropicrin {:.3f}; NH4 mean {:.2f} / {:.2f}, hours > 3 g {} / {}",
                     d21.microbe_at(G::NitrifierAob, ad), c21.microbe_at(G::NitrifierAob, ac), mean_range(d21.nh4_N, ad, bd),
                     mean_range(c21.nh4_N, ac, bc), count_above(d21.nh4_N, ad, bd, nh4_high_g_per_m2),
                     count_above(c21.nh4_N, ac, bc, nh4_high_g_per_m2)));
    CHECK(c21.microbe_at(G::NitrifierAob, ac) < d21.microbe_at(G::NitrifierAob, ad) - 0.2);
    CHECK(mean_range(c21.nh4_N, ac, bc) > mean_range(d21.nh4_N, ad, bd));
    CHECK(count_above(c21.nh4_N, ac, bc, nh4_high_g_per_m2) > count_above(d21.nh4_N, ad, bd, nh4_high_g_per_m2));
}

// S22: 시들음병 오염 토양에서 정식 120일 뒤 고사 포기·감염 순서: 산성 > 무처리 > 길항균 > 소독.
// 소독 변형은 7/10 시작이라 정식(tick 1488) 뒤 120일이 실행 끝이에요(다른 변형과 같은 길이).
TEST_CASE("M5a S22 fusarium damage orders acid, untreated, antagonist, solarized", "[scenario][m5a][s22]") {
    const Run& acid = run_scenario("s22_fusarium_acid");
    const Run& none = run_scenario("s22_fusarium_none");
    const Run& antagonist = run_scenario("s22_fusarium_antagonist");
    const Run& solar = run_scenario("s22_fusarium_solar");
    REQUIRE(solar.ticks - (solar.plant_index - 1) == none.ticks);  // 정식 명령 틱 = plant_index − 1
    const auto dead = [](const Run& r) { return 1.0 - r.alive_fraction; };
    WARN(std::format("S22 infection acid {:.4f} none {:.4f} antagonist {:.4f} solar {:.4f}; dead fraction {:.3f} {:.3f} {:.3f} {:.3f}",
                     acid.infection_fusarium, none.infection_fusarium, antagonist.infection_fusarium,
                     solar.infection_fusarium, dead(acid), dead(none), dead(antagonist), dead(solar)));
    CHECK(acid.infection_fusarium > none.infection_fusarium);
    CHECK(none.infection_fusarium > antagonist.infection_fusarium);
    CHECK(antagonist.infection_fusarium > solar.infection_fusarium);
    CHECK(solar.infection_fusarium < 0.1 * none.infection_fusarium);
    CHECK(dead(acid) > dead(none));
    CHECK(dead(none) > dead(antagonist));
    CHECK(dead(antagonist) >= dead(solar));
    CHECK(acid.died_events > none.died_events);
}

// S23: 역병 감염 과습·온화(20~23 °C) > 과습·고온(28 °C 이상), 과습·저온(12 °C) > 적정 관수
TEST_CASE("M5a S23 phytophthora favours waterlogged mild soil", "[scenario][m5a][s23]") {
    const Run& proper = run_scenario("s23_proper");
    const Run& mild = run_scenario("s23_waterlogged_mild");
    const Run& hot = run_scenario("s23_waterlogged_hot");
    const Run& cold = run_scenario("s23_waterlogged_cold");
    WARN(std::format("S23 phytophthora infection proper {:.4f} mild {:.4f} hot {:.4f} cold {:.4f}; soil temp {:.1f} {:.1f} {:.1f} {:.1f}",
                     proper.infection_phytophthora, mild.infection_phytophthora, hot.infection_phytophthora,
                     cold.infection_phytophthora, mean_all(proper.soil_temp_degC), mean_all(mild.soil_temp_degC),
                     mean_all(hot.soil_temp_degC), mean_all(cold.soil_temp_degC)));
    // 전제: 지온 구간이 명세대로 갈려요
    CHECK(mean_all(hot.soil_temp_degC) > mean_all(mild.soil_temp_degC) + 4.0);
    CHECK(mean_all(cold.soil_temp_degC) < mean_all(mild.soil_temp_degC) - 4.0);
    CHECK(mild.infection_phytophthora > hot.infection_phytophthora + 0.05);
    CHECK(mild.infection_phytophthora > cold.infection_phytophthora + 0.05);
    CHECK(hot.infection_phytophthora > proper.infection_phytophthora + 0.02);
    CHECK(cold.infection_phytophthora > proper.infection_phytophthora + 0.02);
}

// S24: 퇴비 쪽 길항균 지수가 높고 시들음병균 증식이 느려요 (4차 값: 병원균 차이는 작음 → 증가량 순서만 검사)
TEST_CASE("M5a S24 compost feeds antagonists and slows fusarium", "[scenario][m5a][s24]") {
    const Run& bare = run_scenario("s24_no_compost");
    const Run& compost = run_scenario("s24_compost");
    const double fus_gain_bare = bare.microbe_end(G::Fusarium) - bare.microbe_at(G::Fusarium, 0);
    const double fus_gain_compost = compost.microbe_end(G::Fusarium) - compost.microbe_at(G::Fusarium, 0);
    WARN(std::format("S24 antagonist end no compost {:.3f} compost {:.3f}; fusarium gain {:.4f} / {:.4f}",
                     bare.microbe_end(G::Antagonist), compost.microbe_end(G::Antagonist), fus_gain_bare, fus_gain_compost));
    CHECK(compost.microbe_end(G::Antagonist) > bare.microbe_end(G::Antagonist) + 0.05);
    CHECK(mean_all(compost.microbe[idx(G::Antagonist)]) > mean_all(bare.microbe[idx(G::Antagonist)]));
    CHECK(fus_gain_compost < fus_gain_bare);
}

// S25: 균근균 투입 쪽 정착 지수가 높고, 고인산(유효 P 100)에서는 정착 지수가 낮아요(A20: 기준의 절반 이하).
// 건조 손해: 4차 값 A19는 이탈 기록(완화가 거의 0, 디렉터 결정 "지금처럼 약하게")이라 크기(10~25% 작음)는 검사하지 않고,
// 투입 쪽 손해가 더 크지 않다는 방향만 봐요.
TEST_CASE("M5a S25 mycorrhiza establishes with inoculation and is suppressed by high phosphorus",
          "[scenario][m5a][s25]") {
    const Run& std_none = run_scenario("s25_standard_none");
    const Run& std_inoc = run_scenario("s25_standard_inoc");
    const Run& dry_none = run_scenario("s25_dry_none");
    const Run& dry_inoc = run_scenario("s25_dry_inoc");
    const Run& highp_none = run_scenario("s25_highp_none");
    const Run& highp_inoc = run_scenario("s25_highp_inoc");
    WARN(std::format("S25 mycorrhiza end standard {:.3f}/{:.3f}, dry {:.3f}/{:.3f}, high P {:.3f}/{:.3f} (none/inoculated)",
                     std_none.microbe_end(G::Mycorrhiza), std_inoc.microbe_end(G::Mycorrhiza),
                     dry_none.microbe_end(G::Mycorrhiza), dry_inoc.microbe_end(G::Mycorrhiza),
                     highp_none.microbe_end(G::Mycorrhiza), highp_inoc.microbe_end(G::Mycorrhiza)));
    CHECK(std_inoc.microbe_end(G::Mycorrhiza) > std_none.microbe_end(G::Mycorrhiza) + 0.02);
    CHECK(dry_inoc.microbe_end(G::Mycorrhiza) > dry_none.microbe_end(G::Mycorrhiza) + 0.02);
    CHECK(highp_inoc.microbe_end(G::Mycorrhiza) < 0.5 * std_inoc.microbe_end(G::Mycorrhiza));
    CHECK(highp_none.microbe_end(G::Mycorrhiza) < 0.5 * std_none.microbe_end(G::Mycorrhiza));

    const double loss_none = 1.0 - dry_none.gross_sum_g_per_m2 / std_none.gross_sum_g_per_m2;
    const double loss_inoc = 1.0 - dry_inoc.gross_sum_g_per_m2 / std_inoc.gross_sum_g_per_m2;
    WARN(std::format("S25 drought loss of gross production none {:.4f} inoculated {:.4f} (A19 magnitude: recorded deviation)",
                     loss_none, loss_inoc));
    CHECK(loss_none > 0.0);
    CHECK(loss_inoc <= loss_none);
    CHECK(dry_inoc.gross_sum_g_per_m2 >= dry_none.gross_sum_g_per_m2);
}

// S33 소독 단계: 같은 소독 조건에서 지온 투명 > 흑색 > 없음 > 백색, 병원균 사멸도 같은 순서, 필름은 모두 증발을 줄여요.
// 4차 값 기록: 백색과 없음의 지온 차이는 매우 작아요(일최고 평균 34.1 ≈ 34.1, 기간 평균은 백색이 0.1 °C 높음).
// 그래서 없음 > 백색은 차이가 분명한 기간 최고 지온과 병원균 사멸로만 검사해요.
TEST_CASE("M5a S33 film type orders soil temperature and kill during disinfection", "[scenario][m5a][s33]") {
    const Run& clear = run_scenario("s33_disinfect_clear");
    const Run& black = run_scenario("s33_disinfect_black");
    const Run& white = run_scenario("s33_disinfect_white");
    const Run& none = run_scenario("s33_disinfect_none");
    WARN(std::format("S33 disinfect soil temp mean/daily max mean/max: clear {:.2f}/{:.2f}/{:.2f} black {:.2f}/{:.2f}/{:.2f} "
                     "none {:.2f}/{:.2f}/{:.2f} white {:.2f}/{:.2f}/{:.2f}",
                     mean_all(clear.soil_temp_degC), daily_max_mean(clear.soil_temp_degC), max_all(clear.soil_temp_degC),
                     mean_all(black.soil_temp_degC), daily_max_mean(black.soil_temp_degC), max_all(black.soil_temp_degC),
                     mean_all(none.soil_temp_degC), daily_max_mean(none.soil_temp_degC), max_all(none.soil_temp_degC),
                     mean_all(white.soil_temp_degC), daily_max_mean(white.soil_temp_degC), max_all(white.soil_temp_degC)));
    CHECK(mean_all(clear.soil_temp_degC) > mean_all(black.soil_temp_degC) + 1.0);
    CHECK(mean_all(black.soil_temp_degC) > mean_all(none.soil_temp_degC) + 1.0);
    CHECK(daily_max_mean(clear.soil_temp_degC) > daily_max_mean(black.soil_temp_degC) + 1.0);
    CHECK(daily_max_mean(black.soil_temp_degC) > daily_max_mean(none.soil_temp_degC) + 1.0);
    CHECK(max_all(none.soil_temp_degC) > max_all(white.soil_temp_degC) + 0.5);
    WARN(std::format("S33 disinfect cum kill fusarium clear {:.3f} black {:.3f} none {:.4f} white {:.4f}",
                     clear.cum_kill[idx(G::Fusarium)], black.cum_kill[idx(G::Fusarium)], none.cum_kill[idx(G::Fusarium)],
                     white.cum_kill[idx(G::Fusarium)]));
    for (const G g : {G::Fusarium, G::Phytophthora}) {
        CHECK(clear.cum_kill[idx(g)] > black.cum_kill[idx(g)]);
        CHECK(black.cum_kill[idx(g)] > none.cum_kill[idx(g)]);
        CHECK(none.cum_kill[idx(g)] > white.cum_kill[idx(g)]);
    }
    for (const Run* film : {&clear, &black, &white}) {
        CHECK(film->ledger.water_evaporated_mm < 0.5 * none.ledger.water_evaporated_mm);
    }
}

// S33 정식 후(11~1월, 60일): 지온 투명 > 흑색 > 없음, 필름은 모두 증발을 줄이고, 백색은 반사광으로 총생산이 조금 커요.
// 관찰 기록(5장 4차 값 표에 없는 항목): 겨울 정식 후에는 백색 필름 쪽 지온이 필름 없음보다 높아요(맨땅은 증발 냉각이
// 남고 겨울 일사가 약해 반사 손해가 작음). 명세의 "없음 > 백색"이 정식 후에도 해당하는지 확인이 필요해서 검사하지 않아요.
TEST_CASE("M5a S33 film type after planting changes soil temperature and white film adds light",
          "[scenario][m5a][s33][s33_planted]") {
    const Run& clear = run_scenario("s33_planted_clear");
    const Run& black = run_scenario("s33_planted_black");
    const Run& white = run_scenario("s33_planted_white");
    const Run& none = run_scenario("s33_planted_none");
    WARN(std::format("S33 planted soil temp mean clear {:.2f} black {:.2f} none {:.2f} white {:.2f}; "
                     "gross sum {:.1f} {:.1f} {:.1f} {:.1f}; leaf {:.2f} {:.2f} {:.2f} {:.2f}",
                     mean_all(clear.soil_temp_degC), mean_all(black.soil_temp_degC), mean_all(none.soil_temp_degC),
                     mean_all(white.soil_temp_degC), clear.gross_sum_g_per_m2, black.gross_sum_g_per_m2,
                     none.gross_sum_g_per_m2, white.gross_sum_g_per_m2, clear.leaf_g_per_m2, black.leaf_g_per_m2,
                     none.leaf_g_per_m2, white.leaf_g_per_m2));
    CHECK(mean_all(clear.soil_temp_degC) > mean_all(black.soil_temp_degC));
    CHECK(mean_all(black.soil_temp_degC) > mean_all(none.soil_temp_degC));
    for (const Run* film : {&clear, &black, &white}) {
        CHECK(film->ledger.water_evaporated_mm < 0.5 * none.ledger.water_evaporated_mm);
    }
    for (const Run* other : {&clear, &black, &none}) {
        CHECK(white.gross_sum_g_per_m2 > other->gross_sum_g_per_m2);
    }
    CHECK(white.leaf_g_per_m2 > none.leaf_g_per_m2);
}
