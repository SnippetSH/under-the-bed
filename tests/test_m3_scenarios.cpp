// M3 시나리오 테스트 S5, S6, S11(+석회 조건), S12 (docs/spec/m3-overview.md "시나리오 테스트 (M3)")
// 실제 data/params.toml과 data/scenarios/m3/*.toml(석회 조건은 m2/s9_*.toml)을 읽어 "기대 방향"만 검사해요.
// 측정 정의는 명세 그대로예요: "평균"은 0틱 포함 시간별 하우스 평균의 평균, "변동폭"은 하우스 평균의 최대 - 최소,
// "N일째"는 N x 24틱 시점, 장부 값은 하우스 전체 합계. 값은 WARN으로 출력해요.
// S5의 EC 보조 확인은 참고 정보(디렉터 결정 8번)라 검사하지 않아요.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numeric>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

constexpr std::uint64_t kTicksPerDay = 24;

// 하우스 평균 시계열 (인덱스 = 처리한 틱 수, 0은 초기 상태)과 마지막 시점의 작물·장부 값
struct Series {
    std::vector<double> ph;
    std::vector<double> k_solution;
    std::vector<double> ec;
    std::vector<double> mn_solution;
    std::vector<double> mn_available;  // Mn2+ 풀 (교환 + 용액) = "유효태 Mn"
    std::vector<double> p_labile;
    // 마지막 시점 (작물이 있는 타일 평균)
    double sufficiency_K = 0.0;
    double sufficiency_Ca = 0.0;
    double sufficiency_Mn = 0.0;
    double plant_Mn_ratio = 0.0;
    sim::Ledger ledger;
};

double mean_of(const std::vector<double>& v) {
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}
double range_of(const std::vector<double>& v) {
    return *std::max_element(v.begin(), v.end()) - *std::min_element(v.begin(), v.end());
}

void record(Series& s, const sim::Snapshot& snap, const sim::CropParams& crop_params) {
    const double n = static_cast<double>(snap.soil.size());
    double ph = 0.0, k = 0.0, ec = 0.0, mn_sol = 0.0, mn = 0.0, p = 0.0;
    for (const sim::SoilCell& c : snap.soil) {
        ph += c.ph;
        k += c.k_solution_g_per_m2;
        ec += c.ec_dS_per_m;
        mn_sol += c.mn_solution_g_per_m2;
        mn += c.mn_g_per_m2;
        p += c.p_g_per_m2;
    }
    s.ph.push_back(ph / n);
    s.k_solution.push_back(k / n);
    s.ec.push_back(ec / n);
    s.mn_solution.push_back(mn_sol / n);
    s.mn_available.push_back(mn / n);
    s.p_labile.push_back(p / n);

    double suff_k = 0.0, suff_ca = 0.0, suff_mn = 0.0, ratio = 0.0;
    std::size_t present = 0;
    for (const sim::Crop& crop : snap.crops) {
        if (!crop.present) {
            continue;
        }
        const sim::process::NutrientStatus st = sim::process::nutrient_status(crop, crop_params);
        suff_k += st.sufficiency_K;
        suff_ca += st.sufficiency_Ca;
        suff_mn += st.sufficiency_Mn;
        ratio += st.plant_Mn_ratio;
        ++present;
    }
    if (present > 0) {
        const double m = static_cast<double>(present);
        s.sufficiency_K = suff_k / m;
        s.sufficiency_Ca = suff_ca / m;
        s.sufficiency_Mn = suff_mn / m;
        s.plant_Mn_ratio = ratio / m;
    }
    s.ledger = snap.ledger;
}

Series run_scenario(const std::string& dir, const std::string& name, std::uint64_t ticks) {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    const sim::CropParams crop_params = params->crop;
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/" + dir + "/" + name + ".toml"));
    REQUIRE(bundle.has_value());
    auto world = sim::World::create(std::move(*params), std::move(bundle->scenario));
    REQUIRE(world.has_value());

    Series series;
    record(series, world->snapshot(), crop_params);
    std::size_t next = 0;
    const auto& schedule = bundle->schedule;
    for (std::uint64_t t = 0; t < ticks; ++t) {
        while (next < schedule.size() && schedule[next].tick == t) {
            auto seq = world->enqueue(schedule[next].command);
            REQUIRE(seq.has_value());
            ++next;
        }
        world->step(1);
        record(series, world->snapshot(), crop_params);
    }
    return series;
}

}  // namespace

// S5: 황산칼륨 과다(60 g/m2)는 Ca 흡수를 막고 Ca를 밀어내지만 K 흡수는 막지 않는다 (21일)
TEST_CASE("S5 excess potassium antagonizes calcium uptake", "[scenario][m3][s5]") {
    const std::uint64_t ticks = 21 * kTicksPerDay;
    const Series std_run = run_scenario("m3", "s5_standard", ticks);
    const Series exc = run_scenario("m3", "s5_k_excess", ticks);

    CAPTURE(std_run.ledger.Ca_uptake_g, exc.ledger.Ca_uptake_g, std_run.sufficiency_Ca, exc.sufficiency_Ca,
            std_run.sufficiency_K, exc.sufficiency_K, std_run.ledger.Ca_leached_g, exc.ledger.Ca_leached_g);
    WARN(std::format("S5 (standard vs excess): Ca uptake g {:.3f} vs {:.3f}; final Ca sufficiency {:.4f} vs {:.4f}; "
                     "final K sufficiency {:.4f} vs {:.4f}; Ca leached g {:.3f} vs {:.3f}",
                     std_run.ledger.Ca_uptake_g, exc.ledger.Ca_uptake_g, std_run.sufficiency_Ca, exc.sufficiency_Ca,
                     std_run.sufficiency_K, exc.sufficiency_K, std_run.ledger.Ca_leached_g, exc.ledger.Ca_leached_g));

    CHECK(exc.ledger.Ca_uptake_g < std_run.ledger.Ca_uptake_g);    // 1
    CHECK(exc.sufficiency_Ca < std_run.sufficiency_Ca);            // 2
    CHECK(exc.sufficiency_K >= std_run.sufficiency_K);             // 3 전체 흡수 실패가 아님
    CHECK(exc.ledger.Ca_leached_g > std_run.ledger.Ca_leached_g);  // 4 밀려남
}

// S6: 같은 처방에서 사토(광물 CEC 1)가 양토(25)보다 pH, 용액 K, EC의 변동폭이 모두 크고 K 용탈도 크다 (21일)
TEST_CASE("S6 sand swings pH potassium and EC more than loam", "[scenario][m3][s6]") {
    const std::uint64_t ticks = 21 * kTicksPerDay;
    const Series loam = run_scenario("m3", "s6_loam", ticks);
    const Series sand = run_scenario("m3", "s6_sand", ticks);

    const double ph_loam = range_of(loam.ph), ph_sand = range_of(sand.ph);
    const double k_loam = range_of(loam.k_solution), k_sand = range_of(sand.k_solution);
    const double ec_loam = range_of(loam.ec), ec_sand = range_of(sand.ec);
    CAPTURE(ph_loam, ph_sand, k_loam, k_sand, ec_loam, ec_sand, loam.ledger.K_leached_g, sand.ledger.K_leached_g);
    WARN(std::format("S6 range (loam vs sand): pH {:.4f} vs {:.4f}; solution K g/m2 {:.4f} vs {:.4f}; EC dS/m {:.4f} vs "
                     "{:.4f}; K leached g {:.3f} vs {:.3f}",
                     ph_loam, ph_sand, k_loam, k_sand, ec_loam, ec_sand, loam.ledger.K_leached_g,
                     sand.ledger.K_leached_g));

    CHECK(ph_sand > ph_loam);
    CHECK(k_sand > k_loam);
    CHECK(ec_sand > ec_loam);
    CHECK(sand.ledger.K_leached_g > loam.ledger.K_leached_g);
}

// S11: 산성·과습은 용액 Mn과 체내 Mn이 중성보다 높고, 알칼리는 낮으며 Mn 충족도도 낮다 (21일)
TEST_CASE("S11 manganese availability follows pH and aeration", "[scenario][m3][s11]") {
    const std::uint64_t ticks = 21 * kTicksPerDay;
    const Series neutral = run_scenario("m3", "s11_neutral", ticks);
    const Series acid = run_scenario("m3", "s11_acid", ticks);
    const Series wet = run_scenario("m3", "s11_wet", ticks);
    const Series alkaline = run_scenario("m3", "s11_alkaline", ticks);

    const double sol_n = mean_of(neutral.mn_solution), sol_a = mean_of(acid.mn_solution);
    const double sol_w = mean_of(wet.mn_solution), sol_k = mean_of(alkaline.mn_solution);
    CAPTURE(sol_n, sol_a, sol_w, sol_k, neutral.plant_Mn_ratio, acid.plant_Mn_ratio, wet.plant_Mn_ratio,
            alkaline.plant_Mn_ratio, neutral.sufficiency_Mn, alkaline.sufficiency_Mn);
    WARN(std::format("S11 mean solution Mn g/m2: neutral={:.5f} acid={:.5f} wet={:.5f} alkaline={:.5f}; "
                     "final plant Mn ratio: {:.3f} {:.3f} {:.3f} {:.3f}; final Mn sufficiency neutral={:.4f} "
                     "alkaline={:.4f}",
                     sol_n, sol_a, sol_w, sol_k, neutral.plant_Mn_ratio, acid.plant_Mn_ratio, wet.plant_Mn_ratio,
                     alkaline.plant_Mn_ratio, neutral.sufficiency_Mn, alkaline.sufficiency_Mn));

    CHECK(sol_a > sol_n);
    CHECK(sol_w > sol_n);
    CHECK(sol_n > sol_k);
    CHECK(acid.plant_Mn_ratio > neutral.plant_Mn_ratio);
    CHECK(wet.plant_Mn_ratio > neutral.plant_Mn_ratio);
    CHECK(neutral.plant_Mn_ratio > alkaline.plant_Mn_ratio);
    CHECK(alkaline.sufficiency_Mn < neutral.sufficiency_Mn);
}

// S11 석회 조건: 산성 토양에 석회를 주면 14일 평균 용액 Mn, 최종 유효태 Mn, 최종 체내 Mn 비율이 모두 작다
TEST_CASE("S11 lime lowers solution manganese available manganese and plant manganese", "[scenario][m3][s11]") {
    const std::uint64_t ticks = 14 * kTicksPerDay;
    const Series lime = run_scenario("m2", "s9_lime", ticks);
    const Series none = run_scenario("m2", "s9_none", ticks);

    const double sol_lime = mean_of(lime.mn_solution), sol_none = mean_of(none.mn_solution);
    const double avail_lime = lime.mn_available.back(), avail_none = none.mn_available.back();
    CAPTURE(sol_lime, sol_none, avail_lime, avail_none, lime.plant_Mn_ratio, none.plant_Mn_ratio);
    WARN(std::format("S11 lime (lime vs none): 14d mean solution Mn g/m2 {:.5f} vs {:.5f}; final available Mn g/m2 "
                     "{:.4f} vs {:.4f}; final plant Mn ratio {:.3f} vs {:.3f}",
                     sol_lime, sol_none, avail_lime, avail_none, lime.plant_Mn_ratio, none.plant_Mn_ratio));

    CHECK(sol_lime < sol_none);                        // 1
    CHECK(avail_lime < avail_none);                    // 2
    CHECK(lime.plant_Mn_ratio < none.plant_Mn_ratio);  // 3
}

// S12: 과인산석회 같은 양을 주면 28일째 유효 P는 중성이 산성·알칼리보다 높고, 세 토양 모두 1일째 > 28일째 (28일)
TEST_CASE("S12 phosphorus fixation is strongest outside the neutral pH window", "[scenario][m3][s12]") {
    const std::uint64_t ticks = 28 * kTicksPerDay;
    const Series neutral = run_scenario("m3", "s12_neutral", ticks);
    const Series acid = run_scenario("m3", "s12_acid", ticks);
    const Series alkaline = run_scenario("m3", "s12_alkaline", ticks);

    const double n1 = neutral.p_labile[1 * kTicksPerDay], n28 = neutral.p_labile[28 * kTicksPerDay];
    const double a1 = acid.p_labile[1 * kTicksPerDay], a28 = acid.p_labile[28 * kTicksPerDay];
    const double k1 = alkaline.p_labile[1 * kTicksPerDay], k28 = alkaline.p_labile[28 * kTicksPerDay];
    CAPTURE(n1, n28, a1, a28, k1, k28);
    WARN(std::format("S12 labile P g/m2 day1 -> day28: neutral {:.3f} -> {:.3f}; acid {:.3f} -> {:.3f}; "
                     "alkaline {:.3f} -> {:.3f}",
                     n1, n28, a1, a28, k1, k28));

    CHECK(n28 > a28);
    CHECK(n28 > k28);
    CHECK(n1 > n28);
    CHECK(a1 > a28);
    CHECK(k1 > k28);
}
