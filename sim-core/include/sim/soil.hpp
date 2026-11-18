#pragma once

namespace sim {

// 토양 셀 1개 = 베드 면적 1 m², 근권 한 층 (docs/spec/m1-overview.md "토양 셀 상태")
// 질소 풀은 모두 질소 원소(N) 기준이에요.
struct SoilCell {
    // 정적 성질 (시나리오 [soil]에서 옴. 셀별로 다를 수 있게 셀에 둬요)
    double field_capacity_mm = 0.0;

    // 수분 (soil-water)
    double water_mm = 0.0;
    double drained_mm_this_tick = 0.0;  // 수분 단계가 쓰고 용탈 단계가 읽어요

    // 온도 (soil-temperature)
    double soil_temp_degC = 0.0;

    // 유기물 (organic-matter)
    double labile_C_g_per_m2 = 0.0;
    double labile_N_g_per_m2 = 0.0;
    double stable_C_g_per_m2 = 0.0;
    double stable_N_g_per_m2 = 0.0;

    // 무기 질소 (nitrogen)
    double nh4_N_g_per_m2 = 0.0;
    double no2_N_g_per_m2 = 0.0;
    double no3_N_g_per_m2 = 0.0;

    // pH: M1에서는 시나리오 초기값으로 고정 (nitrogen)
    double ph = 0.0;

    // 아질산 경보 상태 (이벤트 중복 방지)
    bool nitrite_alarm = false;

    [[nodiscard]] double relative_water() const { return water_mm / field_capacity_mm; }

    [[nodiscard]] double organic_N_g_per_m2() const { return labile_N_g_per_m2 + stable_N_g_per_m2; }
    [[nodiscard]] double organic_C_g_per_m2() const { return labile_C_g_per_m2 + stable_C_g_per_m2; }
    [[nodiscard]] double mineral_N_g_per_m2() const {
        return nh4_N_g_per_m2 + no2_N_g_per_m2 + no3_N_g_per_m2;
    }
    [[nodiscard]] double total_N_g_per_m2() const { return organic_N_g_per_m2() + mineral_N_g_per_m2(); }

    bool operator==(const SoilCell&) const = default;
};

// 보존 법칙 검증용 누적 장부 (docs/spec/m1-overview.md "장부")
// 셀은 면적 1 m²이므로 셀 값의 합이 곧 g, mm 합계예요.
struct Ledger {
    double water_in_mm = 0.0;
    double water_evaporated_mm = 0.0;
    double water_drained_mm = 0.0;
    double N_in_g = 0.0;
    double N_leached_g = 0.0;
    double N_gas_g = 0.0;  // M1에서는 항상 0
    double C_in_g = 0.0;
    double C_respired_g = 0.0;

    bool operator==(const Ledger&) const = default;
};

}  // namespace sim
