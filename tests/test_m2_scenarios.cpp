// M2 시나리오 테스트 S4~S10 (docs/spec/m2-overview.md "시나리오 테스트 (M2)")
// 실제 data/params.toml과 data/scenarios/m2/*.toml을 읽어 "기대 방향"만 검사해요.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

// 셀 평균 시계열 (인덱스 = 처리한 틱 수, 0은 초기 상태)
struct Series {
    std::vector<double> sufficiency_P;
    std::vector<double> soil_p;
    std::vector<double> root_health;
    std::vector<double> biomass;
    std::vector<double> aeration;
    std::vector<double> ph;
    std::vector<double> ec;
    int deaths = 0;
};

void record(Series& s, const sim::Snapshot& snap, const sim::CropParams& crop_params) {
    const double n = static_cast<double>(snap.soil.size());
    double suff = 0.0, p = 0.0, rh = 0.0, bio = 0.0, aer = 0.0, ph = 0.0, ec = 0.0;
    for (std::size_t i = 0; i < snap.soil.size(); ++i) {
        const sim::SoilCell& c = snap.soil[i];
        const sim::Crop& crop = snap.crops[i];
        p += c.p_g_per_m2;
        aer += c.aeration_index;
        ph += c.ph;
        ec += c.ec_dS_per_m;
        if (crop.present) {
            suff += sim::process::nutrient_status(crop, crop_params).sufficiency_P;
            rh += crop.root_health;
            bio += crop.leaf_g_per_m2 + crop.root_g_per_m2;
        }
    }
    s.sufficiency_P.push_back(suff / n);
    s.soil_p.push_back(p / n);
    s.root_health.push_back(rh / n);
    s.biomass.push_back(bio / n);
    s.aeration.push_back(aer / n);
    s.ph.push_back(ph / n);
    s.ec.push_back(ec / n);
}

Series run_scenario(const std::string& name, std::uint64_t ticks) {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    const sim::CropParams crop_params = params->crop;
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/m2/" + name + ".toml"));
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
        for (const sim::Event& event : world->events()) {
            if (std::holds_alternative<sim::PlantDiedEvent>(event.payload)) {
                ++series.deaths;
            }
        }
        record(series, world->snapshot(), crop_params);
    }
    return series;
}

double max_of(const std::vector<double>& v) { return *std::max_element(v.begin(), v.end()); }
double min_of(const std::vector<double>& v) { return *std::min_element(v.begin(), v.end()); }

constexpr std::uint64_t kTicksPerDay = 24;

}  // namespace

// S4: 알칼리(pH 8.0)에서는 토양 P가 같거나 더 많은데도 P 충족도가 낮다 (21일)
TEST_CASE("S4 alkaline soil lowers phosphorus sufficiency despite equal soil P", "[scenario][m2][s4]") {
    const std::uint64_t ticks = 21 * kTicksPerDay;
    const Series optimal = run_scenario("s4_optimal", ticks);
    const Series alkaline = run_scenario("s4_alkaline", ticks);

    const double suff_opt = optimal.sufficiency_P.back();
    const double suff_alk = alkaline.sufficiency_P.back();
    const double p_opt = optimal.soil_p.back();
    const double p_alk = alkaline.soil_p.back();
    CAPTURE(suff_opt, suff_alk, p_opt, p_alk);
    WARN(std::format("S4 final P sufficiency: pH6.2={:.4f} pH8.0={:.4f}; final soil P g/m2: pH6.2={:.4f} pH8.0={:.4f}; "
                     "deaths: {} / {}",
                     suff_opt, suff_alk, p_opt, p_alk, optimal.deaths, alkaline.deaths));

    CHECK(suff_alk < suff_opt);
    CHECK(p_alk >= p_opt);
}

// S4b: 산성(pH 4.6)에서는 뿌리 건강도와 생장량(잎+뿌리)이 낮다 (21일)
TEST_CASE("S4b acid soil damages roots and lowers growth", "[scenario][m2][s4b]") {
    const std::uint64_t ticks = 21 * kTicksPerDay;
    const Series optimal = run_scenario("s4_optimal", ticks);
    const Series acid = run_scenario("s4_acid", ticks);

    const double rh_opt = optimal.root_health.back();
    const double rh_acid = acid.root_health.back();
    const double bio_opt = optimal.biomass.back();
    const double bio_acid = acid.biomass.back();
    CAPTURE(rh_opt, rh_acid, bio_opt, bio_acid);
    WARN(std::format("S4b final root health: pH6.2={:.4f} pH4.6={:.4f}; final biomass g/m2: pH6.2={:.4f} pH4.6={:.4f}; "
                     "deaths: {} / {}",
                     rh_opt, rh_acid, bio_opt, bio_acid, optimal.deaths, acid.deaths));

    CHECK(rh_acid < rh_opt);
    CHECK(bio_acid < bio_opt);
}

// S8: 과습(다짐 0.8, 하루 25 mm)은 통기 지수 최저값, 뿌리 건강도, 생장량이 모두 낮다 (14일)
TEST_CASE("S8 waterlogging lowers aeration root health and growth", "[scenario][m2][s8]") {
    const std::uint64_t ticks = 14 * kTicksPerDay;
    const Series proper = run_scenario("s8_proper", ticks);
    const Series wet = run_scenario("s8_waterlogged", ticks);

    const double aer_proper = min_of(proper.aeration);
    const double aer_wet = min_of(wet.aeration);
    const double rh_proper = proper.root_health.back();
    const double rh_wet = wet.root_health.back();
    const double bio_proper = proper.biomass.back();
    const double bio_wet = wet.biomass.back();
    CAPTURE(aer_proper, aer_wet, rh_proper, rh_wet, bio_proper, bio_wet);
    WARN(std::format("S8 min aeration: proper={:.4f} waterlogged={:.4f}; final root health: proper={:.4f} "
                     "waterlogged={:.4f}; final biomass g/m2: proper={:.4f} waterlogged={:.4f}; deaths: {} / {}",
                     aer_proper, aer_wet, rh_proper, rh_wet, bio_proper, bio_wet, proper.deaths, wet.deaths));

    CHECK(aer_wet < aer_proper);
    CHECK(rh_wet < rh_proper);
    CHECK(bio_wet < bio_proper);
}

// S9: 석회는 서서히 pH를 올리고(1일 < 14일), 무처리는 오르지 않는다 (14일)
TEST_CASE("S9 lime raises pH gradually while untreated soil does not", "[scenario][m2][s9]") {
    const std::uint64_t ticks = 14 * kTicksPerDay;
    const Series lime = run_scenario("s9_lime", ticks);
    const Series none = run_scenario("s9_none", ticks);

    const double initial = lime.ph.front();
    const double rise_1d = lime.ph[1 * kTicksPerDay] - initial;
    const double rise_7d = lime.ph[7 * kTicksPerDay] - initial;
    const double rise_14d = lime.ph[14 * kTicksPerDay] - initial;
    const double none_initial = none.ph.front();
    const double none_final = none.ph.back();
    CAPTURE(rise_1d, rise_7d, rise_14d, none_initial, none_final);
    WARN(std::format("S9 lime pH rise: 1d={:.4f} 7d={:.4f} 14d={:.4f}; untreated pH {:.4f} -> {:.4f}", rise_1d, rise_7d,
                     rise_14d, none_initial, none_final));

    CHECK(rise_1d < rise_14d);
    CHECK(rise_14d > 0.0);
    CHECK(none_final <= none_initial + 1e-9);
}

// S10: 과다 시비는 EC 최대치가 높고 생장량이 낮다. 같은 시비에서 물을 끊으면 최종 EC가 더 높다 (14일)
TEST_CASE("S10 excess fertilizer raises EC and dry spell concentrates salts", "[scenario][m2][s10]") {
    const std::uint64_t ticks = 14 * kTicksPerDay;
    const Series standard = run_scenario("s10_standard", ticks);
    const Series excess = run_scenario("s10_excess", ticks);
    const Series dry = run_scenario("s10_dry", ticks);

    const double ec_max_std = max_of(standard.ec);
    const double ec_max_exc = max_of(excess.ec);
    const double bio_std = standard.biomass.back();
    const double bio_exc = excess.biomass.back();
    const double ec_final_std = standard.ec.back();
    const double ec_final_dry = dry.ec.back();
    CAPTURE(ec_max_std, ec_max_exc, bio_std, bio_exc, ec_final_std, ec_final_dry);
    WARN(std::format("S10 max EC dS/m: standard={:.4f} excess={:.4f}; final biomass g/m2: standard={:.4f} excess={:.4f}; "
                     "final EC dS/m: standard={:.4f} dry={:.4f}; final biomass dry={:.4f}; deaths: {} / {} / {}",
                     ec_max_std, ec_max_exc, bio_std, bio_exc, ec_final_std, ec_final_dry, dry.biomass.back(),
                     standard.deaths, excess.deaths, dry.deaths));

    CHECK(ec_max_exc > ec_max_std);
    CHECK(bio_exc < bio_std);
    CHECK(ec_final_dry > ec_final_std);
}
