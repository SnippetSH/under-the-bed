// 패키지 3 단위 테스트: 유기물 분해 (docs/spec/organic-matter.md "테스트 (단위)")
#include <algorithm>
#include <cmath>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sim/processes.hpp"
#include "test_helpers.hpp"

namespace {

using Catch::Matchers::WithinRel;

constexpr double kRelTol = 1e-12;

const sim::CompostDef& compost(const sim::Params& params, std::string_view id) {
    const auto it = std::find_if(params.composts.begin(), params.composts.end(),
                                 [&](const sim::CompostDef& c) { return c.id == id; });
    REQUIRE(it != params.composts.end());
    return *it;
}

// test_cell()에서 유기물 풀을 비운 셀 (수분·온도는 그대로)
sim::SoilCell empty_om_cell(double nh4_N_g_per_m2, double no3_N_g_per_m2) {
    sim::SoilCell cell = test::test_cell();
    cell.labile_C_g_per_m2 = 0.0;
    cell.labile_N_g_per_m2 = 0.0;
    cell.stable_C_g_per_m2 = 0.0;
    cell.stable_N_g_per_m2 = 0.0;
    cell.nh4_N_g_per_m2 = nh4_N_g_per_m2;
    cell.no2_N_g_per_m2 = 0.0;
    cell.no3_N_g_per_m2 = no3_N_g_per_m2;
    return cell;
}

void decompose(sim::SoilCell& cell, sim::Ledger& ledger, const sim::Params& params) {
    sim::process::decompose_organic_matter(cell, params.organic_matter, params.soil_temperature, ledger);
}

bool all_pools_valid(const sim::SoilCell& c) {
    const double pools[] = {c.labile_C_g_per_m2, c.labile_N_g_per_m2, c.stable_C_g_per_m2, c.stable_N_g_per_m2,
                            c.nh4_N_g_per_m2,    c.no2_N_g_per_m2,    c.no3_N_g_per_m2};
    for (const double v : pools) {
        if (!std::isfinite(v) || v < 0.0) {
            return false;
        }
    }
    return std::isfinite(c.soil_temp_degC);
}

}  // namespace

TEST_CASE("Compost splits C and N into labile and stable pools and records the ledger", "[unit][organic_matter]") {
    const sim::Params params = test::test_params();
    const sim::CompostDef& immature = compost(params, "immature_manure");
    sim::SoilCell cell = empty_om_cell(0.0, 0.0);
    sim::Ledger ledger;

    sim::process::apply_compost(cell, immature, 3.0, ledger);

    CHECK_THAT(ledger.C_in_g, WithinRel(540.0, kRelTol));
    CHECK_THAT(ledger.N_in_g, WithinRel(36.0, kRelTol));
    CHECK_THAT(cell.labile_C_g_per_m2, WithinRel(540.0 * 0.6, kRelTol));
    CHECK_THAT(cell.stable_C_g_per_m2, WithinRel(540.0 * 0.4, kRelTol));
    CHECK_THAT(cell.labile_N_g_per_m2, WithinRel(36.0 * 0.7, kRelTol));
    CHECK_THAT(cell.stable_N_g_per_m2, WithinRel(36.0 * 0.3, kRelTol));
    CHECK_THAT(cell.organic_C_g_per_m2(), WithinRel(ledger.C_in_g, kRelTol));
    CHECK_THAT(cell.organic_N_g_per_m2(), WithinRel(ledger.N_in_g, kRelTol));
}

TEST_CASE("Higher labile fraction respires more C for the same total C", "[unit][organic_matter]") {
    const sim::Params params = test::test_params();
    sim::CompostDef high = compost(params, "immature_manure");
    sim::CompostDef low = high;
    low.labile_C_fraction = 0.1;
    low.labile_N_fraction = 0.1;

    sim::SoilCell cell_high = empty_om_cell(2.0, 10.0);
    sim::SoilCell cell_low = cell_high;
    sim::Ledger ledger_high;
    sim::Ledger ledger_low;
    sim::process::apply_compost(cell_high, high, 3.0, ledger_high);
    sim::process::apply_compost(cell_low, low, 3.0, ledger_low);
    REQUIRE(ledger_high.C_in_g == ledger_low.C_in_g);

    for (int hour = 0; hour < 24; ++hour) {
        decompose(cell_high, ledger_high, params);
        decompose(cell_low, ledger_low, params);
    }
    CHECK(ledger_low.C_respired_g > 0.0);
    CHECK(ledger_high.C_respired_g > ledger_low.C_respired_g);
}

TEST_CASE("Low C:N pool mineralizes nitrogen into NH4", "[unit][organic_matter]") {
    const sim::Params params = test::test_params();
    // 미숙 우분: 이분해성 C:N ≈ 13
    sim::SoilCell cell = empty_om_cell(2.0, 10.0);
    sim::Ledger ledger;
    sim::process::apply_compost(cell, compost(params, "immature_manure"), 3.0, ledger);
    const sim::SoilCell before = cell;

    decompose(cell, ledger, params);

    CHECK(cell.nh4_N_g_per_m2 > before.nh4_N_g_per_m2);
    CHECK(cell.no3_N_g_per_m2 == before.no3_N_g_per_m2);
    CHECK(cell.organic_N_g_per_m2() < before.organic_N_g_per_m2());
    CHECK(cell.labile_C_g_per_m2 < before.labile_C_g_per_m2);
}

TEST_CASE("High C:N pool immobilizes mineral nitrogen, NH4 first", "[unit][organic_matter]") {
    const sim::Params params = test::test_params();
    const sim::CompostDef& bark = compost(params, "bark_compost");  // 이분해성 C:N ≈ 67

    SECTION("ample NH4: only NH4 is taken") {
        sim::SoilCell cell = empty_om_cell(2.0, 10.0);
        sim::Ledger ledger;
        sim::process::apply_compost(cell, bark, 3.0, ledger);
        const sim::SoilCell before = cell;

        decompose(cell, ledger, params);

        CHECK(cell.mineral_N_g_per_m2() < before.mineral_N_g_per_m2());
        CHECK(cell.nh4_N_g_per_m2 < before.nh4_N_g_per_m2);
        CHECK(cell.no3_N_g_per_m2 == before.no3_N_g_per_m2);
        CHECK(cell.organic_N_g_per_m2() > before.organic_N_g_per_m2());
    }

    SECTION("scarce NH4: NH4 is emptied, the rest comes from NO3") {
        sim::SoilCell cell = empty_om_cell(1e-4, 10.0);
        sim::Ledger ledger;
        sim::process::apply_compost(cell, bark, 3.0, ledger);
        const sim::SoilCell before = cell;

        decompose(cell, ledger, params);

        CHECK(cell.nh4_N_g_per_m2 == 0.0);
        CHECK(cell.no3_N_g_per_m2 < before.no3_N_g_per_m2);
        CHECK(cell.no3_N_g_per_m2 > 0.0);
    }

    SECTION("mineral N below need: decomposition is scaled down and takes all mineral N") {
        sim::SoilCell unconstrained = empty_om_cell(2.0, 10.0);
        sim::Ledger ledger_unconstrained;
        sim::process::apply_compost(unconstrained, bark, 3.0, ledger_unconstrained);
        sim::SoilCell scarce = unconstrained;
        scarce.nh4_N_g_per_m2 = 1e-4;
        scarce.no3_N_g_per_m2 = 1e-4;
        sim::Ledger ledger_scarce = ledger_unconstrained;

        decompose(unconstrained, ledger_unconstrained, params);
        decompose(scarce, ledger_scarce, params);

        CHECK(scarce.nh4_N_g_per_m2 == 0.0);
        CHECK(scarce.no3_N_g_per_m2 == 0.0);
        CHECK(ledger_scarce.C_respired_g > 0.0);
        CHECK(ledger_scarce.C_respired_g < ledger_unconstrained.C_respired_g);
    }
}

TEST_CASE("High C:N pool with no mineral N does not decompose at all", "[unit][organic_matter]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = empty_om_cell(0.0, 0.0);
    sim::Ledger ledger;
    sim::process::apply_compost(cell, compost(params, "bark_compost"), 3.0, ledger);
    const sim::SoilCell before = cell;
    const sim::Ledger ledger_before = ledger;

    for (int hour = 0; hour < 24; ++hour) {
        decompose(cell, ledger, params);
    }

    CHECK(cell == before);
    CHECK(ledger == ledger_before);
}

TEST_CASE("Decomposition conserves cell N and organic C + respired C", "[unit][organic_matter][conservation]") {
    const sim::Params params = test::test_params();

    struct Case {
        const char* name;
        const char* compost_id;  // nullptr = 퇴비 없음
        double nh4_N_g_per_m2;
        double no3_N_g_per_m2;
    };
    const Case cases[] = {
        {"default soil", nullptr, 2.0, 10.0},
        {"immature manure", "immature_manure", 2.0, 10.0},
        {"mature manure", "mature_manure", 2.0, 10.0},
        {"bark, ample mineral N", "bark_compost", 2.0, 10.0},
        {"bark, scarce NH4", "bark_compost", 1e-4, 10.0},
        {"bark, scarce mineral N", "bark_compost", 1e-4, 1e-4},
        {"bark, no mineral N", "bark_compost", 0.0, 0.0},
    };

    for (const Case& c : cases) {
        INFO(c.name);
        sim::SoilCell cell = test::test_cell();
        cell.nh4_N_g_per_m2 = c.nh4_N_g_per_m2;
        cell.no3_N_g_per_m2 = c.no3_N_g_per_m2;
        sim::Ledger ledger;
        if (c.compost_id != nullptr) {
            sim::process::apply_compost(cell, compost(params, c.compost_id), 3.0, ledger);
        }

        for (int hour = 0; hour < 48; ++hour) {
            const double N_before = cell.total_N_g_per_m2();
            const double C_before = cell.organic_C_g_per_m2();
            const double respired_before = ledger.C_respired_g;

            decompose(cell, ledger, params);

            const double respired = ledger.C_respired_g - respired_before;
            CHECK_THAT(cell.total_N_g_per_m2(), WithinRel(N_before, kRelTol));
            CHECK_THAT(cell.organic_C_g_per_m2() + respired, WithinRel(C_before, kRelTol));
            CHECK(respired >= 0.0);
            CHECK(all_pools_valid(cell));
        }
    }
}

TEST_CASE("Decomposition heat equals respired C times the heat coefficient", "[unit][organic_matter]") {
    const sim::Params params = test::test_params();
    sim::SoilCell cell = empty_om_cell(2.0, 10.0);
    sim::Ledger compost_ledger;
    sim::process::apply_compost(cell, compost(params, "immature_manure"), 3.0, compost_ledger);
    const double temp_before_degC = cell.soil_temp_degC;

    sim::Ledger ledger;  // 호흡 C가 0에서 시작해야 이번 틱 호흡량과 정확히 같아요
    decompose(cell, ledger, params);

    REQUIRE(ledger.C_respired_g > 0.0);
    CHECK(cell.soil_temp_degC == temp_before_degC + ledger.C_respired_g * params.soil_temperature.heat_degC_per_g_C);
    CHECK(cell.soil_temp_degC > temp_before_degC);
}

TEST_CASE("Pools stay non-negative and finite under extreme inputs", "[unit][organic_matter][extreme]") {
    const sim::Params params = test::test_params();
    constexpr int kSteps = 10000;

    struct Case {
        const char* name;
        const char* compost_id;
        double kg_per_m2;
        double nh4_N_g_per_m2;
        double no3_N_g_per_m2;
        double held_temp_degC;  // NaN = 고정하지 않음(발열로 자유롭게 오름)
    };
    const double kFree = std::nan("");
    const Case cases[] = {
        {"bark 100 kg, no mineral N", "bark_compost", 100.0, 0.0, 0.0, kFree},
        {"bark 100 kg, little mineral N, 40 degC", "bark_compost", 100.0, 0.5, 0.5, 40.0},
        {"immature 100 kg, free heating", "immature_manure", 100.0, 2.0, 10.0, kFree},
        {"immature 100 kg, 40 degC", "immature_manure", 100.0, 0.0, 0.0, 40.0},
        {"mature 100 kg, 200 degC", "mature_manure", 100.0, 2.0, 10.0, 200.0},
        {"no compost, -30 degC", nullptr, 0.0, 2.0, 10.0, -30.0},
    };

    for (const Case& c : cases) {
        INFO(c.name);
        sim::SoilCell cell = test::test_cell();
        cell.nh4_N_g_per_m2 = c.nh4_N_g_per_m2;
        cell.no3_N_g_per_m2 = c.no3_N_g_per_m2;
        sim::Ledger ledger;
        const double C_initial = cell.organic_C_g_per_m2();
        if (c.compost_id != nullptr) {
            sim::process::apply_compost(cell, compost(params, c.compost_id), c.kg_per_m2, ledger);
        }

        bool valid = true;
        bool N_conserved = true;
        for (int step = 0; step < kSteps && valid && N_conserved; ++step) {
            if (!std::isnan(c.held_temp_degC)) {
                cell.soil_temp_degC = c.held_temp_degC;
            }
            const double N_before = cell.total_N_g_per_m2();
            decompose(cell, ledger, params);
            valid = all_pools_valid(cell);
            N_conserved = std::abs(cell.total_N_g_per_m2() - N_before) <= kRelTol * N_before;
        }
        CHECK(valid);
        CHECK(N_conserved);
        CHECK_THAT(cell.organic_C_g_per_m2(), WithinRel(C_initial + ledger.C_in_g - ledger.C_respired_g, 1e-9));
    }

    SECTION("degenerate pools: N > C, tiny pools, zero field capacity") {
        sim::SoilCell cell = empty_om_cell(0.0, 0.0);
        cell.labile_C_g_per_m2 = 1.0;
        cell.labile_N_g_per_m2 = 100.0;
        cell.stable_C_g_per_m2 = 1e-300;
        cell.stable_N_g_per_m2 = 1e-300;
        cell.soil_temp_degC = 40.0;

        sim::SoilCell dry = empty_om_cell(1e-300, 0.0);
        dry.labile_C_g_per_m2 = 1e-300;
        dry.labile_N_g_per_m2 = 0.0;
        dry.field_capacity_mm = 0.0;  // relative_water = NaN (0/0)
        dry.water_mm = 0.0;

        sim::Ledger ledger;
        for (int step = 0; step < kSteps; ++step) {
            decompose(cell, ledger, params);
            decompose(dry, ledger, params);
        }
        CHECK(all_pools_valid(cell));
        CHECK(all_pools_valid(dry));
        CHECK(cell.nh4_N_g_per_m2 > 0.0);
    }
}

TEST_CASE("Immature manure respires more and peaks NH4 higher than mature manure", "[unit][organic_matter][m1]") {
    // S1 방향의 프로세스 수준 확인. 토양 온도 완화는 다른 패키지라서 토양 온도를 20 °C로 고정해요.
    const sim::Params params = test::test_params();
    constexpr int kHours = 7 * 24;
    constexpr double kHeldTempDegC = 20.0;

    struct Run {
        double respired_C_g = 0.0;
        double peak_nh4_N_g_per_m2 = 0.0;
    };
    const auto run = [&](std::string_view compost_id) {
        sim::SoilCell cell = test::test_cell();
        sim::Ledger ledger;
        sim::process::apply_compost(cell, compost(params, compost_id), 3.0, ledger);
        Run r;
        r.peak_nh4_N_g_per_m2 = cell.nh4_N_g_per_m2;
        for (int hour = 0; hour < kHours; ++hour) {
            cell.soil_temp_degC = kHeldTempDegC;
            decompose(cell, ledger, params);
            r.peak_nh4_N_g_per_m2 = std::max(r.peak_nh4_N_g_per_m2, cell.nh4_N_g_per_m2);
        }
        r.respired_C_g = ledger.C_respired_g;
        return r;
    };

    const Run immature = run("immature_manure");
    const Run mature = run("mature_manure");
    CAPTURE(immature.respired_C_g, mature.respired_C_g, immature.peak_nh4_N_g_per_m2, mature.peak_nh4_N_g_per_m2);
    CHECK(immature.respired_C_g > mature.respired_C_g);
    CHECK(immature.peak_nh4_N_g_per_m2 > mature.peak_nh4_N_g_per_m2);
}
