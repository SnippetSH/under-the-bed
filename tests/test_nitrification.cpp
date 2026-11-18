// 패키지 4 단위 테스트: 비료 투입, 2단계 질산화, 아질산 경보 (docs/spec/nitrogen.md "테스트 → 단위")
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sim/processes.hpp"
#include "test_helpers.hpp"

namespace {

using Catch::Matchers::WithinRel;

const sim::FertilizerDef& fertilizer(const sim::Params& params, std::string_view id) {
    const auto it = std::find_if(params.fertilizers.begin(), params.fertilizers.end(),
                                 [&](const sim::FertilizerDef& f) { return f.id == id; });
    REQUIRE(it != params.fertilizers.end());
    return *it;
}

// nitrify() 한 번의 결과에서 2단계 실효 이동 비율(= min(1, step2_rate × 계수들))을 역산해요.
// 2단계 직전 NO2 = 끝난 뒤 NO2 + 2단계 이동량, 2단계 이동량 = NO3 증가량.
double observed_step2_fraction(sim::SoilCell cell, const sim::NitrificationParams& params) {
    const double no3_before = cell.no3_N_g_per_m2;
    sim::process::nitrify(cell, params, test::test_params().soil_chemistry, test::test_params().ions);
    const double n2 = cell.no3_N_g_per_m2 - no3_before;
    const double no2_before_step2 = cell.no2_N_g_per_m2 + n2;
    REQUIRE(no2_before_step2 > 0.0);
    return n2 / no2_before_step2;
}

bool pools_finite_and_non_negative(const sim::SoilCell& cell) {
    for (const double v : {cell.nh4_N_g_per_m2, cell.no2_N_g_per_m2, cell.no3_N_g_per_m2}) {
        if (!std::isfinite(v) || v < 0.0) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("Fertilizer adds exactly the expected N to pools and ledger", "[unit][nitrification]") {
    const sim::Params params = test::test_params();

    for (const auto& fert : params.fertilizers) {
        CAPTURE(fert.id);
        sim::SoilCell cell = test::test_cell();
        sim::Ledger ledger;
        const double nh4_before = cell.nh4_N_g_per_m2;
        const double no3_before = cell.no3_N_g_per_m2;
        const double no2_before = cell.no2_N_g_per_m2;
        const double g_per_m2 = 50.0;

        sim::process::apply_fertilizer(cell, fert, g_per_m2, ledger);

        const double expected_nh4 = g_per_m2 / 1000.0 * fert.nh4_N_g_per_kg;
        const double expected_no3 = g_per_m2 / 1000.0 * fert.no3_N_g_per_kg;
        CHECK(cell.nh4_N_g_per_m2 == nh4_before + expected_nh4);
        CHECK(cell.no3_N_g_per_m2 == no3_before + expected_no3);
        CHECK(cell.no2_N_g_per_m2 == no2_before);
        CHECK(ledger.N_in_g == expected_nh4 + expected_no3);
        CHECK(ledger.N_leached_g == 0.0);
        CHECK(ledger.N_gas_g == 0.0);
    }

    SECTION("50 g/m2 ammonium sulfate is 10.5 g N/m2 (S2 input)") {
        sim::SoilCell cell = test::test_cell();
        sim::Ledger ledger;
        sim::process::apply_fertilizer(cell, fertilizer(params, "ammonium_sulfate"), 50.0, ledger);
        CHECK_THAT(ledger.N_in_g, WithinRel(10.5, 1e-12));
    }

    SECTION("Ledger accumulates across applications") {
        sim::SoilCell cell = test::test_cell();
        sim::Ledger ledger;
        const auto& urea = fertilizer(params, "urea");
        sim::process::apply_fertilizer(cell, urea, 10.0, ledger);
        sim::process::apply_fertilizer(cell, urea, 10.0, ledger);
        CHECK_THAT(ledger.N_in_g, WithinRel(2 * 10.0 / 1000.0 * urea.nh4_N_g_per_kg, 1e-12));
    }
}

TEST_CASE("Step-2 factor is smaller at pH 8.0 than 7.0 for the same NH4", "[unit][nitrification]") {
    const sim::Params params = test::test_params();
    sim::SoilCell base = test::test_cell();
    base.nh4_N_g_per_m2 = 10.0;
    base.no2_N_g_per_m2 = 1.0;

    sim::SoilCell neutral = base;
    neutral.ph = 7.0;
    sim::SoilCell alkaline = base;
    alkaline.ph = 8.0;

    const double f_neutral = observed_step2_fraction(neutral, params.nitrification);
    const double f_alkaline = observed_step2_fraction(alkaline, params.nitrification);
    CAPTURE(f_neutral, f_alkaline);
    CHECK(f_alkaline > 0.0);
    CHECK(f_alkaline < f_neutral);
}

TEST_CASE("More NH4 gives a smaller step-2 factor", "[unit][nitrification]") {
    const sim::Params params = test::test_params();
    for (const double ph : {7.0, 8.0}) {
        CAPTURE(ph);
        sim::SoilCell low = test::test_cell();
        low.ph = ph;
        low.nh4_N_g_per_m2 = 2.0;
        low.no2_N_g_per_m2 = 1.0;
        sim::SoilCell high = low;
        high.nh4_N_g_per_m2 = 20.0;

        const double f_low = observed_step2_fraction(low, params.nitrification);
        const double f_high = observed_step2_fraction(high, params.nitrification);
        CAPTURE(f_low, f_high);
        CHECK(f_high < f_low);
    }
}

TEST_CASE("Nitrification only changes N form: mineral N is conserved", "[unit][nitrification]") {
    const sim::Params params = test::test_params();
    for (const double ph : {5.0, 6.5, 7.0, 8.0, 9.5}) {
        for (const double temp_degC : {5.0, 20.0, 32.0}) {
            CAPTURE(ph, temp_degC);
            sim::SoilCell cell = test::test_cell();
            cell.ph = ph;
            cell.soil_temp_degC = temp_degC;
            cell.nh4_N_g_per_m2 = 12.5;
            cell.no2_N_g_per_m2 = 0.7;
            const double organic_before = cell.organic_N_g_per_m2();
            const double mineral_before = cell.mineral_N_g_per_m2();

            for (int hour = 0; hour < 24; ++hour) {
                sim::process::nitrify(cell, params.nitrification, test::test_params().soil_chemistry, test::test_params().ions);
                CHECK_THAT(cell.mineral_N_g_per_m2(), WithinRel(mineral_before, 1e-12));
            }
            CHECK(cell.organic_N_g_per_m2() == organic_before);
            CHECK(cell.nh4_N_g_per_m2 < 12.5);  // 실제로 질산화가 일어났는지
        }
    }
}

TEST_CASE("Nitrite alarm hysteresis emits one event per crossing", "[unit][nitrification]") {
    const sim::Params params = test::test_params();
    const auto& np = params.nitrification;
    const double threshold = np.nitrite_toxicity_threshold_g_N_per_m2;
    const double reset = threshold * np.nitrite_alarm_reset_fraction;
    const std::uint32_t tile = 7;

    sim::SoilCell cell = test::test_cell();
    cell.ph = 7.0;  // 독성 배율 1: 유효 아질산 = NO2
    cell.no2_N_g_per_m2 = 0.0;
    REQUIRE_FALSE(sim::process::check_nitrite_alarm(cell, np, tile).has_value());
    REQUIRE_FALSE(cell.nitrite_alarm);

    // 정확히 임계치: "넘음"이 아니므로 이벤트 없음
    cell.no2_N_g_per_m2 = threshold;
    CHECK_FALSE(sim::process::check_nitrite_alarm(cell, np, tile).has_value());
    CHECK_FALSE(cell.nitrite_alarm);

    // 처음 넘음 → 이벤트 1개
    cell.no2_N_g_per_m2 = threshold * 1.5;
    const auto first = sim::process::check_nitrite_alarm(cell, np, tile);
    REQUIRE(first.has_value());
    CHECK(first->tile_index == tile);
    CHECK(first->no2_N_g_per_m2 == threshold * 1.5);
    CHECK(cell.nitrite_alarm);

    // 계속 위 → 이벤트 없음
    cell.no2_N_g_per_m2 = threshold * 2.0;
    CHECK_FALSE(sim::process::check_nitrite_alarm(cell, np, tile).has_value());
    CHECK(cell.nitrite_alarm);

    // 해제 기준과 임계치 사이로 내려감 → 이벤트 없음, 경보 유지
    cell.no2_N_g_per_m2 = (reset + threshold) / 2.0;
    CHECK_FALSE(sim::process::check_nitrite_alarm(cell, np, tile).has_value());
    CHECK(cell.nitrite_alarm);

    // 다시 임계치 위로 → 아직 해제 전이라 이벤트 없음
    cell.no2_N_g_per_m2 = threshold * 1.2;
    CHECK_FALSE(sim::process::check_nitrite_alarm(cell, np, tile).has_value());
    CHECK(cell.nitrite_alarm);

    // 해제 기준 아래로 → 해제, 이벤트 없음
    cell.no2_N_g_per_m2 = reset * 0.5;
    CHECK_FALSE(sim::process::check_nitrite_alarm(cell, np, tile).has_value());
    CHECK_FALSE(cell.nitrite_alarm);

    // 다시 넘음 → 이벤트 다시 1개
    cell.no2_N_g_per_m2 = threshold * 1.1;
    const auto second = sim::process::check_nitrite_alarm(cell, np, tile);
    REQUIRE(second.has_value());
    CHECK(second->no2_N_g_per_m2 == threshold * 1.1);
    CHECK(cell.nitrite_alarm);
    CHECK_FALSE(sim::process::check_nitrite_alarm(cell, np, tile).has_value());
}

TEST_CASE("Nitrification stays finite and non-negative for extreme inputs", "[unit][nitrification][extreme]") {
    const sim::Params params = test::test_params();
    const auto& ammonium = fertilizer(params, "ammonium_sulfate");
    const auto& nitrate = fertilizer(params, "calcium_nitrate");

    for (const double ph : {0.0, 4.0, 7.0, 9.0, 14.0}) {
        for (const double temp_degC : {-40.0, 0.0, 25.0, 32.0, 50.0, 80.0}) {
            for (const double relative_water : {0.0, 0.8, 1.0, 5.0}) {
                CAPTURE(ph, temp_degC, relative_water);
                sim::SoilCell cell = test::test_cell();
                cell.ph = ph;
                cell.soil_temp_degC = temp_degC;
                cell.water_mm = cell.field_capacity_mm * relative_water;
                sim::Ledger ledger;
                sim::process::apply_fertilizer(cell, ammonium, 1e6, ledger);
                sim::process::apply_fertilizer(cell, nitrate, 1e6, ledger);
                REQUIRE(pools_finite_and_non_negative(cell));
                REQUIRE(std::isfinite(ledger.N_in_g));

                const double mineral_before = cell.mineral_N_g_per_m2();
                for (int hour = 0; hour < 24 * 30; ++hour) {
                    sim::process::nitrify(cell, params.nitrification, test::test_params().soil_chemistry, test::test_params().ions);
                    (void)sim::process::check_nitrite_alarm(cell, params.nitrification, 0);
                }
                CHECK(pools_finite_and_non_negative(cell));
                CHECK_THAT(cell.mineral_N_g_per_m2(), WithinRel(mineral_before, 1e-12));
            }
        }
    }

    SECTION("Rate products above 1 move at most the whole pool") {
        sim::Params fast = params;
        fast.nitrification.step1_rate_per_hour = 1e9;
        fast.nitrification.step2_rate_per_hour = 1e9;
        sim::SoilCell cell = test::test_cell();
        cell.soil_temp_degC = 32.0;
        cell.water_mm = cell.field_capacity_mm;
        cell.ph = 6.5;
        const double mineral_before = cell.mineral_N_g_per_m2();
        sim::process::nitrify(cell, fast.nitrification, test::test_params().soil_chemistry, test::test_params().ions);
        CHECK(cell.nh4_N_g_per_m2 == 0.0);
        CHECK(cell.no2_N_g_per_m2 >= 0.0);
        CHECK(pools_finite_and_non_negative(cell));
        CHECK_THAT(cell.mineral_N_g_per_m2(), WithinRel(mineral_before, 1e-12));
    }

    SECTION("Empty pools stay exactly zero") {
        sim::SoilCell cell = test::test_cell();
        cell.nh4_N_g_per_m2 = 0.0;
        cell.no2_N_g_per_m2 = 0.0;
        cell.no3_N_g_per_m2 = 0.0;
        sim::process::nitrify(cell, params.nitrification, test::test_params().soil_chemistry, test::test_params().ions);
        CHECK(cell.nh4_N_g_per_m2 == 0.0);
        CHECK(cell.no2_N_g_per_m2 == 0.0);
        CHECK(cell.no3_N_g_per_m2 == 0.0);
    }
}

// S2 방향을 질산화 프로세스만으로 확인해요 (다른 프로세스 없음, 시나리오 전체는 패키지 6).
TEST_CASE("S2 direction: alkaline soil accumulates more nitrite after ammonium fertilizer",
          "[unit][nitrification][s2]") {
    const sim::Params params = test::test_params();
    const auto& np = params.nitrification;
    const auto& ammonium = fertilizer(params, "ammonium_sulfate");
    const int hours = 21 * 24;

    struct Run {
        double max_no2_g_per_m2 = 0.0;
        int events = 0;
        bool ever_above_threshold = false;
    };
    const auto run = [&](double ph) {
        sim::SoilCell cell = test::test_cell();
        cell.ph = ph;
        cell.soil_temp_degC = 20.0;
        cell.water_mm = cell.field_capacity_mm * 0.8;
        sim::Ledger ledger;
        sim::process::apply_fertilizer(cell, ammonium, 50.0, ledger);
        const double mineral_before = cell.mineral_N_g_per_m2();

        Run r;
        for (int hour = 0; hour < hours; ++hour) {
            sim::process::nitrify(cell, np, test::test_params().soil_chemistry, test::test_params().ions);
            if (sim::process::check_nitrite_alarm(cell, np, 0).has_value()) {
                ++r.events;
            }
            r.max_no2_g_per_m2 = std::max(r.max_no2_g_per_m2, cell.no2_N_g_per_m2);
            r.ever_above_threshold =
                r.ever_above_threshold || cell.no2_N_g_per_m2 > np.nitrite_toxicity_threshold_g_N_per_m2;
        }
        CHECK_THAT(cell.mineral_N_g_per_m2(), WithinRel(mineral_before, 1e-12));
        return r;
    };

    const Run alkaline = run(8.0);
    const Run neutral = run(7.0);
    UNSCOPED_INFO("max NO2 pH 8.0 = " << alkaline.max_no2_g_per_m2 << " g N/m2, events = " << alkaline.events);
    UNSCOPED_INFO("max NO2 pH 7.0 = " << neutral.max_no2_g_per_m2 << " g N/m2, events = " << neutral.events);

    CHECK(alkaline.max_no2_g_per_m2 > neutral.max_no2_g_per_m2);
    CHECK(alkaline.ever_above_threshold);
    CHECK(alkaline.events >= 1);
    CHECK_FALSE(neutral.ever_above_threshold);
    CHECK(neutral.events == 0);
}

// M2: 저산소는 2단계를 1단계보다 더 억제해 아질산이 쌓인다
TEST_CASE("Low aeration slows step 2 more than step 1 so nitrite accumulates", "[unit][nitrification][m2]") {
    const sim::Params params = test::test_params();
    const auto run = [&](double aeration) {
        sim::SoilCell cell = test::test_cell();
        cell.aeration_index = aeration;
        cell.nh4_N_g_per_m2 = 10.0;
        cell.no2_N_g_per_m2 = 0.0;
        cell.no3_N_g_per_m2 = 0.0;
        cell.soil_temp_degC = 25.0;
        for (int hour = 0; hour < 48; ++hour) {
            sim::process::nitrify(cell, params.nitrification, params.soil_chemistry, params.ions);
        }
        return cell;
    };
    const sim::SoilCell good = run(1.0);
    const sim::SoilCell poor = run(0.3);
    CHECK(poor.nh4_N_g_per_m2 > good.nh4_N_g_per_m2);  // 1단계도 느려짐
    CHECK(poor.no2_N_g_per_m2 > good.no2_N_g_per_m2);  // 2단계가 더 느려서 NO2 축적
    CHECK(poor.no3_N_g_per_m2 < good.no3_N_g_per_m2);
}

// M2: 질산화된 N 1 eq당 H+ 2 eq
TEST_CASE("Nitrification adds 2 eq H+ per eq N nitrified", "[unit][nitrification][m2]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = test::test_cell();
    cell.nh4_N_g_per_m2 = 10.0;
    cell.acid_H_eq_this_tick = 0.0;
    const double nh4_before = cell.nh4_N_g_per_m2;
    sim::process::nitrify(cell, params.nitrification, params.soil_chemistry, params.ions);
    const double nitrified_g = nh4_before - cell.nh4_N_g_per_m2;
    REQUIRE(nitrified_g > 0.0);
    CHECK_THAT(cell.acid_H_eq_this_tick, WithinRel(2.0 * nitrified_g / params.ions.nh4_N_g_per_eq, 1e-12));
}

// M2: 같은 NO2라도 산성에서 유효 아질산이 크고 경보도 산성에서만 울린다
TEST_CASE("Nitrite toxicity rises at low pH and drives the alarm", "[unit][nitrification][m2]") {
    const sim::Params params = test::test_params();
    const auto& np = params.nitrification;
    sim::SoilCell acid = test::test_cell();
    sim::SoilCell neutral = test::test_cell();
    acid.ph = 5.0;
    neutral.ph = 7.0;
    acid.no2_N_g_per_m2 = 0.6 * np.nitrite_toxicity_threshold_g_N_per_m2;
    neutral.no2_N_g_per_m2 = acid.no2_N_g_per_m2;

    CHECK(sim::process::effective_nitrite_g_N_per_m2(acid, np) > sim::process::effective_nitrite_g_N_per_m2(neutral, np));
    CHECK(sim::process::effective_nitrite_g_N_per_m2(neutral, np) == neutral.no2_N_g_per_m2);

    const auto acid_event = sim::process::check_nitrite_alarm(acid, np, 3);
    const auto neutral_event = sim::process::check_nitrite_alarm(neutral, np, 3);
    REQUIRE(acid_event.has_value());
    CHECK(acid_event->no2_N_g_per_m2 == acid.no2_N_g_per_m2);  // 이벤트에는 원래 NO2
    CHECK_FALSE(neutral_event.has_value());
}
