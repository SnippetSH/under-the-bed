#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sim {

// 한 시간 동안의 외부 기상 입력
struct WeatherHour {
    double outside_air_temp_degC = 0.0;
    double solar_radiation_W_per_m2 = 0.0;  // 수평면 전천일사 (하우스 밖)
    double outside_rh_percent = 0.0;        // 상대습도 0..100

    bool operator==(const WeatherHour&) const = default;
};

// 토양 초기값. M1은 모든 셀이 같은 균일 토양이에요 (docs/spec/m1-overview.md "시나리오 TOML")
struct SoilInit {
    double field_capacity_mm = 0.0;
    double water_mm = 0.0;
    double soil_temp_degC = 0.0;
    double ph = 0.0;
    double nh4_N_g_per_m2 = 0.0;
    double no2_N_g_per_m2 = 0.0;
    double no3_N_g_per_m2 = 0.0;
    double labile_C_g_per_m2 = 0.0;
    double labile_N_g_per_m2 = 0.0;
    double stable_C_g_per_m2 = 0.0;
    double stable_N_g_per_m2 = 0.0;

    bool operator==(const SoilInit&) const = default;
};

// 월드 초기 조건. 계획서 3장 "초기화" 항목의 시나리오예요.
// 명령 일정표([[schedule]])는 코어가 아니라 로더·CLI·테스트가 다뤄요.
struct Scenario {
    std::string name;
    std::uint64_t seed = 0;     // 마스터 시드. 서브시스템 스트림은 여기서 파생해요.
    int start_day_of_year = 0;  // 1..365, 틱 0의 날짜
    int bed_rows = 0;           // 하우스 베드 타일 그리드 행 수
    int bed_cols = 0;           // 열 수
    SoilInit soil;

    // (day_of_year, hour) 오름차순 365 × 24 = 8,760개. 인덱스는 weather_index()로 구해요.
    std::vector<WeatherHour> weather_hourly;

    bool operator==(const Scenario&) const = default;
};

}  // namespace sim
