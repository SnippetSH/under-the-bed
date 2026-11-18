// M4 시나리오 테스트 (docs/spec/m4-overview.md "시나리오 테스트 (M4)", house-climate S18, denitrification S13)
// 실제 data/params.toml과 data/scenarios/m4/*.toml을 읽어 "기대 방향"만 검사해요. 수치는 WARN으로 출력해요.
// 측정 정의는 명세 그대로예요: 시간별 하우스 평균(작물이 있는 타일 평균), 장부는 하우스 합계,
// "평균 당도·경도"는 작기 전체 수확의 생체중 가중 평균, "첫 수확"은 처음 FruitHarvested가 나온 틱.
// 같은 시나리오는 한 테스트 바이너리 안에서 한 번만 돌리고 결과를 캐시해요 (작기 7개 x 6,312틱).
// 작기(263일) 케이스는 [season] 태그를 달아요: sim-tests "~[season]"으로 빼고 돌릴 수 있어요.

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

#include "sim/calendar.hpp"
#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

constexpr std::uint64_t kTicksPerDay = 24;
constexpr std::uint64_t kSeasonTicks = 263 * kTicksPerDay;  // 6,312틱 (9월 10일 ~ 5월 31일)
constexpr std::uint64_t kS15Ticks = 113 * kTicksPerDay;
constexpr std::uint64_t kS13Ticks = 14 * kTicksPerDay;
constexpr std::uint64_t kWinterTicks = 7 * kTicksPerDay;
constexpr std::uint64_t kAutumnTicks = 28 * kTicksPerDay;
constexpr std::uint64_t kCo2Ticks = 7 * kTicksPerDay;
constexpr std::uint64_t kNever = std::numeric_limits<std::uint64_t>::max();

// 월(0 = 1월)의 시작 day_of_year (윤년 없는 365일 달력)
constexpr std::array<int, 12> kMonthStartDoy = {1, 32, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335};

int month_of(int day_of_year) {
    int month = 0;
    for (int i = 0; i < 12; ++i) {
        if (day_of_year >= kMonthStartDoy[static_cast<std::size_t>(i)]) {
            month = i;
        }
    }
    return month;
}

// 한 시나리오 실행에서 모아 둔 지표 (작물 값은 작물이 있는 타일 평균, 장부는 하우스 합계)
struct Run {
    std::size_t cells = 0;
    std::uint64_t ticks = 0;

    // 수확 (끝 시점 누적, 타일 평균 g/m2)
    double fresh_g_per_m2 = 0.0;
    double count_per_m2 = 0.0;
    double brix_weighted_sum = 0.0;  // 타일 합
    double firmness_weighted_sum = 0.0;
    double fresh_sum = 0.0;
    double thinned_per_m2 = 0.0;
    std::array<double, 12> monthly_fresh_g_per_m2{};  // 월별 수확 생체중 (처리한 틱의 달력 기준)
    std::uint64_t first_harvest_tick = kNever;        // 첫 FruitHarvested 이벤트의 틱
    std::uint64_t first_truss_tick = kNever;          // 화방 수 평균이 처음 0.5를 넘은 스냅샷 틱

    double alive_fraction = 0.0;
    double spring_sufficiency_N = 0.0;  // 3~5월(행 평균)
    double mean_sufficiency_K = 0.0;    // 전 기간 행 평균 (보고용)
    double mean_sufficiency_Ca = 0.0;
    double leaf_g_per_m2 = 0.0;
    double induction_progress = 0.0;  // 끝 시점 induction_index + 화방 수
    double establishment_index = 0.0;

    // 하우스 시계열 (스냅샷 인덱스 = 처리한 틱 수, 0 = 초기 상태)
    std::vector<double> air_temp_degC;
    std::vector<double> co2_ppm;
    std::vector<int> hour;
    double gross_sum_g_per_m2 = 0.0;  // last_gross_g_per_m2의 모든 스냅샷 합 (총생산)
    double heating_energy_MJ_per_m2 = 0.0;
    sim::Ledger ledger;  // 끝 시점

    [[nodiscard]] double mean_brix() const { return fresh_sum > 0.0 ? brix_weighted_sum / fresh_sum : 0.0; }
    [[nodiscard]] double mean_firmness() const { return fresh_sum > 0.0 ? firmness_weighted_sum / fresh_sum : 0.0; }
    [[nodiscard]] double fruit_mean_g() const { return count_per_m2 > 0.0 ? fresh_g_per_m2 / count_per_m2 : 0.0; }
};

struct CropMeans {
    double fresh = 0.0, count = 0.0, brix_w = 0.0, firm_w = 0.0, thinned = 0.0;
    double alive = 0.0, suff_N = 0.0, suff_K = 0.0, suff_Ca = 0.0, trusses = 0.0, induction = 0.0, leaf = 0.0;
    double gross = 0.0, establishment = 0.0;
    std::size_t present = 0;
};

CropMeans crop_means(const sim::Snapshot& snap, const sim::CropParams& crop_params) {
    CropMeans m;
    for (const sim::Crop& crop : snap.crops) {
        if (!crop.present) {
            continue;
        }
        ++m.present;
        m.fresh += crop.harvest.harvested_fresh_g;
        m.count += static_cast<double>(crop.harvest.harvested_fruits);
        m.brix_w += crop.harvest.harvested_brix_weighted;
        m.firm_w += crop.harvest.harvested_firmness_weighted;
        m.thinned += static_cast<double>(crop.harvest.thinned_fruits);
        m.alive += crop.alive ? 1.0 : 0.0;
        const sim::process::NutrientStatus st = sim::process::nutrient_status(crop, crop_params);
        m.suff_N += st.sufficiency_N;
        m.suff_K += st.sufficiency_K;
        m.suff_Ca += st.sufficiency_Ca;
        m.trusses += static_cast<double>(crop.trusses.size());
        m.induction += crop.induction_index;
        m.leaf += crop.leaf_g_per_m2;
        m.gross += crop.last_gross_g_per_m2;
        m.establishment += crop.establishment_index;
    }
    return m;
}

Run run_scenario_uncached(const std::string& name, std::uint64_t ticks) {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    const sim::CropParams crop_params = params->crop;
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m4/" + name + ".toml"));
    REQUIRE(bundle.has_value());
    const int start_doy = bundle->scenario.start_day_of_year;
    auto world = sim::World::create(std::move(*params), std::move(bundle->scenario));
    REQUIRE(world.has_value());

    Run run;
    run.ticks = ticks;
    run.air_temp_degC.reserve(ticks + 1);
    run.co2_ppm.reserve(ticks + 1);
    run.hour.reserve(ticks + 1);

    double spring_N_sum = 0.0, K_sum = 0.0, Ca_sum = 0.0;
    std::size_t spring_rows = 0, rows = 0;
    double prev_fresh = 0.0;

    auto record = [&](const sim::Snapshot& snap, std::uint64_t processed_tick, bool initial) {
        const CropMeans m = crop_means(snap, crop_params);
        run.cells = snap.soil.size();
        run.air_temp_degC.push_back(snap.house_air_temp_degC);
        run.co2_ppm.push_back(snap.house.co2_ppm);
        run.hour.push_back(snap.time.hour);
        if (m.present == 0) {
            return;
        }
        const double n = static_cast<double>(m.present);
        run.gross_sum_g_per_m2 += m.gross / n;
        if (run.first_truss_tick == kNever && m.trusses / n > 0.5) {
            run.first_truss_tick = snap.tick;
        }
        if (!initial) {
            // 이번 틱(처리한 틱)의 달력으로 월별 수확을 모아요
            const int month = month_of(sim::calendar_at(start_doy, processed_tick).day_of_year);
            run.monthly_fresh_g_per_m2[static_cast<std::size_t>(month)] += m.fresh / n - prev_fresh;
        }
        prev_fresh = m.fresh / n;
        const int month_now = month_of(snap.time.day_of_year);
        ++rows;
        K_sum += m.suff_K / n;
        Ca_sum += m.suff_Ca / n;
        if (month_now == 2 || month_now == 3 || month_now == 4) {
            spring_N_sum += m.suff_N / n;
            ++spring_rows;
        }
        run.fresh_g_per_m2 = m.fresh / n;
        run.count_per_m2 = m.count / n;
        run.brix_weighted_sum = m.brix_w;
        run.firmness_weighted_sum = m.firm_w;
        run.fresh_sum = m.fresh;
        run.thinned_per_m2 = m.thinned / n;
        run.alive_fraction = m.alive / n;
        run.leaf_g_per_m2 = m.leaf / n;
        run.induction_progress = m.induction / n + m.trusses / n;
        run.establishment_index = m.establishment / n;
        run.heating_energy_MJ_per_m2 = snap.house.heating_energy_MJ_per_m2;
        run.ledger = snap.ledger;
    };

    record(world->snapshot(), 0, true);
    std::size_t next = 0;
    const auto& schedule = bundle->schedule;
    for (std::uint64_t t = 0; t < ticks; ++t) {
        while (next < schedule.size() && schedule[next].tick == t) {
            auto seq = world->enqueue(schedule[next].command);
            REQUIRE(seq.has_value());
            ++next;
        }
        world->step(1);
        for (const sim::Event& event : world->events()) {
            if (std::holds_alternative<sim::FruitHarvestedEvent>(event.payload)) {
                run.first_harvest_tick = std::min(run.first_harvest_tick, event.tick);
            }
        }
        record(world->snapshot(), t, false);
    }
    run.mean_sufficiency_K = rows > 0 ? K_sum / static_cast<double>(rows) : 0.0;
    run.mean_sufficiency_Ca = rows > 0 ? Ca_sum / static_cast<double>(rows) : 0.0;
    run.spring_sufficiency_N = spring_rows > 0 ? spring_N_sum / static_cast<double>(spring_rows) : 0.0;
    return run;
}

// 같은 (이름, 틱 수)는 한 번만 돌려요.
const Run& run_scenario(const std::string& name, std::uint64_t ticks) {
    static std::map<std::pair<std::string, std::uint64_t>, Run> cache;
    const auto key = std::make_pair(name, ticks);
    auto it = cache.find(key);
    if (it == cache.end()) {
        it = cache.emplace(key, run_scenario_uncached(name, ticks)).first;
    }
    return it->second;
}

const Run& season(const std::string& name) { return run_scenario(name, kSeasonTicks); }

double day_of(std::uint64_t tick) {
    return tick == kNever ? -1.0 : static_cast<double>(tick) / static_cast<double>(kTicksPerDay);
}

double min_of(const std::vector<double>& v, std::size_t from) {
    return *std::min_element(v.begin() + static_cast<std::ptrdiff_t>(from), v.end());
}
double max_of(const std::vector<double>& v) { return *std::max_element(v.begin(), v.end()); }
double mean_of(const std::vector<double>& v) {
    double s = 0.0;
    for (double x : v) {
        s += x;
    }
    return s / static_cast<double>(v.size());
}

// 8~16시 스냅샷의 최저 CO2 (낮 최저)
double day_min_co2(const Run& run) {
    double lowest = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < run.co2_ppm.size(); ++i) {
        if (run.hour[i] >= 8 && run.hour[i] <= 16) {
            lowest = std::min(lowest, run.co2_ppm[i]);
        }
    }
    return lowest;
}

}  // namespace

// ---- 1작기 ----

// 1작기: season_standard가 끝까지 돌고, 12월 15일 이전에 첫 수확, 12~5월 매달 수확, 작물이 살아 있다 (디렉터 결정 11번)
TEST_CASE("M4 first season finishes with first harvest by Dec 15 monthly harvest Dec to May and live crop",
          "[scenario][m4][season][first_season]") {
    const Run& run = season("season_standard");
    constexpr std::uint64_t kDec15End = (349 - 253 + 1) * kTicksPerDay;  // 정식 9월 10일(day 253), 12월 15일 = day 349
    const std::array<int, 6> months = {11, 0, 1, 2, 3, 4};               // 12월 ~ 5월
    const char* names[] = {"Dec", "Jan", "Feb", "Mar", "Apr", "May"};
    std::string monthly;
    for (std::size_t i = 0; i < months.size(); ++i) {
        monthly += std::format(" {}={:.0f}", names[i], run.monthly_fresh_g_per_m2[static_cast<std::size_t>(months[i])]);
    }
    CAPTURE(run.first_harvest_tick, run.alive_fraction, run.fresh_g_per_m2);
    WARN(std::format("1 season: first harvest day {:.1f} (Dec 15 ends at day 97); alive {:.2f}; monthly fresh g/m2:{}; "
                     "total {:.0f} g/m2, {:.1f} fruits/plant, {:.1f} g/fruit, brix {:.2f}",
                     day_of(run.first_harvest_tick), run.alive_fraction, monthly, run.fresh_g_per_m2,
                     run.count_per_m2 / 8.0, run.fruit_mean_g(), run.mean_brix()));

    CHECK(run.ticks == kSeasonTicks);
    CHECK(run.first_harvest_tick != kNever);
    CHECK(run.first_harvest_tick < kDec15End);
    for (int m : months) {
        INFO("month index " << m);
        CHECK(run.monthly_fresh_g_per_m2[static_cast<std::size_t>(m)] > 0.0);
    }
    CHECK(run.alive_fraction > 0.0);
}

// ---- 처방 비교 ----

// 처방 비교: 기준·무처리·질소 과다·칼륨 부족·칼륨 과다·산성·적과의 누적 수확량과 평균 당도가 모두 서로 다르다
TEST_CASE("M4 prescriptions differ in cumulative yield and mean brix", "[scenario][m4][season][prescriptions]") {
    const std::array<const char*, 7> names = {"season_standard", "season_no_topdress", "season_n_excess",
                                              "season_k_low",    "season_k_excess",    "season_acid",
                                              "season_thinned"};
    std::vector<double> yields;
    std::vector<double> brixes;
    std::string line;
    for (const char* name : names) {
        const Run& run = season(name);
        yields.push_back(run.fresh_g_per_m2);
        brixes.push_back(run.mean_brix());
        line += std::format(" {}={:.0f}g/{:.2f}Bx/firm {:.3f};", name, run.fresh_g_per_m2, run.mean_brix(),
                            run.mean_firmness());
        CHECK(run.fresh_g_per_m2 > 0.0);
    }
    WARN("prescriptions (yield g/m2 / mean brix / mean firmness):" + line);
    for (std::size_t i = 0; i < names.size(); ++i) {
        for (std::size_t j = i + 1; j < names.size(); ++j) {
            INFO(names[i] << " vs " << names[j]);
            CHECK(std::abs(yields[i] - yields[j]) > 0.1);
            CHECK(std::abs(brixes[i] - brixes[j]) > 0.001);
        }
    }
}

// ---- 적과 (strawberry-fruit 6c) ----

// 적과: season_thinned의 과실 평균 무게와 평균 당도가 season_standard보다 크다
TEST_CASE("M4 thinning gives larger mean fruit and higher brix", "[scenario][m4][season][thinning]") {
    const Run& standard = season("season_standard");
    const Run& thinned = season("season_thinned");
    WARN(std::format("thinning: g/fruit {:.2f} vs {:.2f}; brix {:.3f} vs {:.3f}; yield {:.0f} vs {:.0f} g/m2; "
                     "fruits/plant {:.1f} vs {:.1f}; thinned {:.1f}/m2",
                     standard.fruit_mean_g(), thinned.fruit_mean_g(), standard.mean_brix(), thinned.mean_brix(),
                     standard.fresh_g_per_m2, thinned.fresh_g_per_m2, standard.count_per_m2 / 8.0,
                     thinned.count_per_m2 / 8.0, thinned.thinned_per_m2));
    CHECK(thinned.thinned_per_m2 > 0.0);
    CHECK(thinned.fruit_mean_g() > standard.fruit_mean_g());
    CHECK(thinned.mean_brix() > standard.mean_brix());
}

// ---- S7 (N 부분과 수확량만, K는 S16) ----

// S7: 웃거름이 없으면 3~5월 평균 N 충족도가 낮고 누적 수확량이 적다
TEST_CASE("S7 no topdressing lowers spring nitrogen sufficiency and yield", "[scenario][m4][season][s7]") {
    const Run& standard = season("season_standard");
    const Run& none = season("season_no_topdress");
    WARN(std::format("S7: Mar-May mean N sufficiency {:.3f} vs {:.3f}; yield {:.0f} vs {:.0f} g/m2",
                     standard.spring_sufficiency_N, none.spring_sufficiency_N, standard.fresh_g_per_m2,
                     none.fresh_g_per_m2));
    CHECK(none.spring_sufficiency_N < standard.spring_sufficiency_N);
    CHECK(none.fresh_g_per_m2 < standard.fresh_g_per_m2);
}

// ---- S13 (14일) ----

// S13: 과습 쪽 N_gas가 크고, 적정 쪽은 0에 가깝다 (하우스 합계를 m2당으로 환산해 비교)
TEST_CASE("S13 waterlogging loses nitrogen as gas and proper watering loses almost none", "[scenario][m4][s13]") {
    const Run& proper = run_scenario("s13_proper", kS13Ticks);
    const Run& wet = run_scenario("s13_waterlogged", kS13Ticks);
    const double gas_proper = proper.ledger.N_gas_g / static_cast<double>(proper.cells);
    const double gas_wet = wet.ledger.N_gas_g / static_cast<double>(wet.cells);
    WARN(std::format("S13: N_gas g/m2 proper={:.5f} waterlogged={:.5f} (house totals {:.3f} vs {:.3f} g)", gas_proper,
                     gas_wet, proper.ledger.N_gas_g, wet.ledger.N_gas_g));
    CHECK(gas_wet > gas_proper);
    CHECK(gas_proper <= 0.05 * gas_wet);
}

// ---- S14 ----

// S14: 질소 과다 쪽 첫 화방 분화와 첫 수확이 늦고 평균 당도가 낮다
TEST_CASE("S14 nitrogen excess delays first truss and first harvest and lowers brix",
          "[scenario][m4][season][s14]") {
    const Run& standard = season("season_standard");
    const Run& excess = season("season_n_excess");
    WARN(std::format("S14: first truss day {:.1f} vs {:.1f}; first harvest day {:.1f} vs {:.1f}; brix {:.3f} vs {:.3f}; "
                     "yield {:.0f} vs {:.0f} g/m2",
                     day_of(standard.first_truss_tick), day_of(excess.first_truss_tick),
                     day_of(standard.first_harvest_tick), day_of(excess.first_harvest_tick), standard.mean_brix(),
                     excess.mean_brix(), standard.fresh_g_per_m2, excess.fresh_g_per_m2));
    CHECK(standard.first_truss_tick != kNever);
    CHECK(excess.first_truss_tick > standard.first_truss_tick);
    CHECK(excess.first_harvest_tick > standard.first_harvest_tick);
    CHECK(excess.mean_brix() < standard.mean_brix());
}

// ---- S15 (113일) ----

// S15: 모종 A는 첫 수확이 C보다 빠르고, 12월 말까지(113일) 누적 수확량이 많다
TEST_CASE("S15 grade A seedlings harvest earlier and more by year end than grade C", "[scenario][m4][s15]") {
    const Run& a = run_scenario("s15_grade_a", kS15Ticks);
    const Run& c = run_scenario("s15_grade_c", kS15Ticks);
    WARN(std::format("S15: first harvest day A={:.1f} C={:.1f}; yield by day 113 A={:.1f} C={:.1f} g/m2; first truss "
                     "day A={:.1f} C={:.1f}",
                     day_of(a.first_harvest_tick), day_of(c.first_harvest_tick), a.fresh_g_per_m2, c.fresh_g_per_m2,
                     day_of(a.first_truss_tick), day_of(c.first_truss_tick)));
    CHECK(a.first_harvest_tick != kNever);
    CHECK(a.first_harvest_tick < c.first_harvest_tick);  // C가 아직 수확 전이면 kNever라서 더 커요
    CHECK(a.fresh_g_per_m2 > c.fresh_g_per_m2);
}

// ---- S16 / S17 ----

// S16: 칼륨 부족 쪽 평균 당도가 낮다
TEST_CASE("S16 low potassium lowers mean brix", "[scenario][m4][season][s16]") {
    const Run& standard = season("season_standard");
    const Run& low = season("season_k_low");
    WARN(std::format("S16: mean brix {:.3f} vs {:.3f}; mean K sufficiency {:.3f} vs {:.3f}; yield {:.0f} vs {:.0f}",
                     standard.mean_brix(), low.mean_brix(), standard.mean_sufficiency_K, low.mean_sufficiency_K,
                     standard.fresh_g_per_m2, low.fresh_g_per_m2));
    CHECK(low.mean_brix() < standard.mean_brix());
}

// S17: 칼륨 과다 쪽 평균 경도가 낮다 (Ca 길항)
TEST_CASE("S17 excess potassium lowers mean firmness", "[scenario][m4][season][s17]") {
    const Run& standard = season("season_standard");
    const Run& excess = season("season_k_excess");
    WARN(std::format("S17: mean firmness {:.4f} vs {:.4f}; mean Ca sufficiency {:.3f} vs {:.3f}; yield {:.0f} vs {:.0f}",
                     standard.mean_firmness(), excess.mean_firmness(), standard.mean_sufficiency_Ca,
                     excess.mean_sufficiency_Ca, standard.fresh_g_per_m2, excess.fresh_g_per_m2));
    CHECK(excess.mean_firmness() < standard.mean_firmness());
}

// ---- S18 하우스 기후 ----

// S18 겨울(7일): 무관리는 최저 기온이 0 도 아래이고 잎을 잃고, 난방(6 도)은 최저가 설정 근처(첫날 열 관성 전이 제외)이고 에너지가 든다
TEST_CASE("S18 winter without management freezes and heating holds the setpoint", "[scenario][m4][s18]") {
    const Run& none = run_scenario("s18_winter_none", kWinterTicks);
    const Run& heated = run_scenario("s18_winter_heated", kWinterTicks);
    const double min_none = min_of(none.air_temp_degC, 1);
    const double min_heated = min_of(heated.air_temp_degC, kTicksPerDay);  // 첫날은 난방이 꺼진 채 만들어진 전이
    const double litter_extra = (none.ledger.C_leaf_litter_g - heated.ledger.C_leaf_litter_g) /
                                static_cast<double>(none.cells);
    WARN(std::format("S18 winter: min air none={:.2f} heated(after day 1)={:.2f} C; leaf none={:.2f} heated={:.2f} "
                     "g/m2; extra leaf-litter C {:.3f} g/m2; heating energy {:.1f} MJ/m2",
                     min_none, min_heated, none.leaf_g_per_m2, heated.leaf_g_per_m2, litter_extra,
                     heated.heating_energy_MJ_per_m2));
    CHECK(min_none < 0.0);
    CHECK(none.leaf_g_per_m2 < heated.leaf_g_per_m2);
    CHECK(litter_extra > 0.0);
    CHECK(min_heated >= 5.5);
    CHECK(heated.heating_energy_MJ_per_m2 > 0.0);
    CHECK(none.heating_energy_MJ_per_m2 == 0.0);
}

// S18 가을(28일): 창을 닫으면 최고 기온이 30 도를 넘고 화아분화가 느리다 (환기 쪽보다 진행이 작다)
TEST_CASE("S18 closed house in autumn overheats and slows flower induction", "[scenario][m4][s18]") {
    const Run& closed = run_scenario("s18_autumn_closed", kAutumnTicks);
    const Run& vented = run_scenario("s18_autumn_vented", kAutumnTicks);
    const double max_closed = max_of(closed.air_temp_degC);
    const double max_vented = max_of(vented.air_temp_degC);
    WARN(std::format("S18 autumn: max air closed={:.1f} vented={:.1f} C; induction progress closed={:.3f} "
                     "vented={:.3f}; establishment {:.2f} vs {:.2f}",
                     max_closed, max_vented, closed.induction_progress, vented.induction_progress,
                     closed.establishment_index, vented.establishment_index));
    CHECK(max_closed > 30.0);
    CHECK(closed.induction_progress < vented.induction_progress);
}

// S18 CO2(7일): 창을 닫으면 낮 최저 CO2가 낮고 총생산이 작다 (에너지 조건은 뺌, 7차 점검)
TEST_CASE("S18 closed house drops daytime CO2 and gross production", "[scenario][m4][s18][co2]") {
    const Run& closed = run_scenario("s18_co2_closed", kCo2Ticks);
    const Run& vented = run_scenario("s18_co2_vented", kCo2Ticks);
    const double min_closed = day_min_co2(closed);
    const double min_vented = day_min_co2(vented);
    WARN(std::format("S18 CO2: day min CO2 closed={:.0f} vented={:.0f} ppm; gross sum {:.2f} vs {:.2f} g/m2; mean air "
                     "{:.2f} vs {:.2f} C; energy {:.1f} vs {:.1f} MJ/m2",
                     min_closed, min_vented, closed.gross_sum_g_per_m2, vented.gross_sum_g_per_m2,
                     mean_of(closed.air_temp_degC), mean_of(vented.air_temp_degC), closed.heating_energy_MJ_per_m2,
                     vented.heating_energy_MJ_per_m2));
    CHECK(min_closed < min_vented);
    CHECK(closed.gross_sum_g_per_m2 < vented.gross_sum_g_per_m2);
}
