// M1 시나리오 테스트 S1~S3 (docs/spec/m1-overview.md "시나리오 테스트 (M1)")
// 실제 data/params.toml과 data/scenarios/m1/*.toml을 읽어 "기대 방향"만 검사해요.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

// 셀 평균 시계열 (인덱스 = 처리한 틱 수, 0은 초기 상태)
struct Series {
    std::vector<double> soil_temp_degC;
    std::vector<double> nh4_N;
    std::vector<double> no2_N;
    std::vector<double> no3_N;
    sim::Ledger ledger;  // 종료 시점
    int nitrite_events = 0;
};

double mean_of(const sim::Snapshot& snap, double sim::SoilCell::*member) {
    double sum = 0.0;
    for (const sim::SoilCell& cell : snap.soil) {
        sum += cell.*member;
    }
    return sum / static_cast<double>(snap.soil.size());
}

void record(Series& s, const sim::Snapshot& snap) {
    s.soil_temp_degC.push_back(mean_of(snap, &sim::SoilCell::soil_temp_degC));
    s.nh4_N.push_back(mean_of(snap, &sim::SoilCell::nh4_N_g_per_m2));
    s.no2_N.push_back(mean_of(snap, &sim::SoilCell::no2_N_g_per_m2));
    s.no3_N.push_back(mean_of(snap, &sim::SoilCell::no3_N_g_per_m2));
}

Series run_scenario(const std::string& name, std::uint64_t ticks) {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m1/" + name + ".toml"));
    REQUIRE(bundle.has_value());
    auto world = sim::World::create(std::move(*params), std::move(bundle->scenario));
    REQUIRE(world.has_value());

    Series series;
    record(series, world->snapshot());
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
            if (std::holds_alternative<sim::NitriteThresholdExceededEvent>(event.payload)) {
                ++series.nitrite_events;
            }
        }
        record(series, world->snapshot());
    }
    series.ledger = world->snapshot().ledger;
    return series;
}

double max_of(const std::vector<double>& v) { return *std::max_element(v.begin(), v.end()); }

constexpr std::uint64_t kTicksPerDay = 24;

}  // namespace

// S1: 미숙 퇴비는 숙성 퇴비보다 발열(토양 온도)과 NH4+ 최대치가 크고, 무처리보다 뚜렷이 따뜻하다 (7일)
TEST_CASE("S1 immature compost heats more and releases more ammonium than mature", "[scenario][m1][s1]") {
    const std::uint64_t ticks = 7 * kTicksPerDay;
    const Series control = run_scenario("s0_control", ticks);
    const Series immature = run_scenario("s1_immature", ticks);
    const Series mature = run_scenario("s1_mature", ticks);

    double max_warming = -1e9;
    for (std::size_t i = 0; i < immature.soil_temp_degC.size(); ++i) {
        max_warming = std::max(max_warming, immature.soil_temp_degC[i] - control.soil_temp_degC[i]);
    }

    const double t_imm = max_of(immature.soil_temp_degC);
    const double t_mat = max_of(mature.soil_temp_degC);
    const double t_ctl = max_of(control.soil_temp_degC);
    const double n_imm = max_of(immature.nh4_N);
    const double n_mat = max_of(mature.nh4_N);
    CAPTURE(t_imm, t_mat, t_ctl, n_imm, n_mat, max_warming);
    WARN(std::format("S1 peak soil temp degC: immature={:.4f} mature={:.4f} control={:.4f}; "
                     "peak NH4 g N/m2: immature={:.4f} mature={:.4f}; max(immature-control)={:.4f}",
                     t_imm, t_mat, t_ctl, n_imm, n_mat, max_warming));

    CHECK(t_imm > t_mat);
    CHECK(n_imm > n_mat);
    CHECK(max_warming > 1.0);
}

// S2: 알칼리 토양에서 황산암모늄을 주면 NO2-가 더 많이 쌓이고, 아질산 독성 이벤트는 알칼리 쪽에서만 난다 (21일)
TEST_CASE("S2 alkaline soil accumulates more nitrite than neutral", "[scenario][m1][s2]") {
    const std::uint64_t ticks = 21 * kTicksPerDay;
    const Series alkaline = run_scenario("s2_alkaline", ticks);
    const Series neutral = run_scenario("s2_neutral", ticks);

    const double no2_alk = max_of(alkaline.no2_N);
    const double no2_neu = max_of(neutral.no2_N);
    CAPTURE(no2_alk, no2_neu, alkaline.nitrite_events, neutral.nitrite_events);
    WARN(std::format("S2 peak NO2 g N/m2: alkaline={:.4f} neutral={:.4f}; "
                     "NitriteThresholdExceeded events: alkaline={} neutral={}",
                     no2_alk, no2_neu, alkaline.nitrite_events, neutral.nitrite_events));

    CHECK(no2_alk > no2_neu);
    CHECK(alkaline.nitrite_events >= 1);
    CHECK(neutral.nitrite_events == 0);
}

// S3: 과관수는 질산태 질소를 씻어 내서 14일 뒤 NO3- 잔량이 적고 누적 용탈량이 크다
TEST_CASE("S3 over-irrigation leaches nitrate", "[scenario][m1][s3]") {
    const std::uint64_t ticks = 14 * kTicksPerDay;
    const Series over = run_scenario("s3_over", ticks);
    const Series proper = run_scenario("s3_proper", ticks);

    const double no3_over = over.no3_N.back();
    const double no3_proper = proper.no3_N.back();
    const double leached_over = over.ledger.N_leached_g;
    const double leached_proper = proper.ledger.N_leached_g;
    CAPTURE(no3_over, no3_proper, leached_over, leached_proper);
    WARN(std::format("S3 final NO3 g N/m2 (cell mean): over={:.4f} proper={:.4f}; "
                     "cumulative N_leached g (sum of cells): over={:.4f} proper={:.4f}",
                     no3_over, no3_proper, leached_over, leached_proper));

    CHECK(no3_over < no3_proper);
    CHECK(leached_over > leached_proper);
}
