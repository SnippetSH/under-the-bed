#pragma once

#include <array>
#include <vector>

namespace sim {

// docs/spec/house-climate.md "상태 변수 (하우스)". 하우스는 하나의 공기 덩어리예요.
struct HouseState {
    int cover_layers = 0;
    // [layer][side] 개방률 (house.window_open_levels 중 하나), layer 0 = 가장 바깥, 길이 = cover_layers
    std::vector<std::array<double, 2>> window_open_fraction;
    bool heater_on = false;
    double heater_setpoint_degC = 0.0;
    double air_temp_degC = 0.0;             // 열 관성 상태
    double air_temp_mean_degC = 0.0;        // 이동 평균 (strawberry-phenology)
    double co2_ppm = 0.0;
    double heating_energy_MJ_per_m2 = 0.0;  // 누적 난방 에너지 (장부, 줄지 않음)
    // 마지막으로 처리한 틱의 파생값 (스냅샷용, 저장도 해요). 뒤의 셋은 진단·리포트용 기록 (m4-overview)
    double outside_co2_ppm = 0.0;
    double inside_solar_W_per_m2 = 0.0;   // 하우스 안 일사 = 일사 × 투과율^겹수
    double heating_power_W_per_m2 = 0.0;  // 난방 출력 Q_heat
    double vent_path_index = 0.0;         // 환기 경로 path 0~1 (직렬 개구부)

    // 열린 창(개방률 > 0) 수. 스냅샷 보기용 파생값
    [[nodiscard]] int windows_open_count() const {
        int n = 0;
        for (const auto& layer : window_open_fraction) {
            n += (layer[0] > 0.0 ? 1 : 0) + (layer[1] > 0.0 ? 1 : 0);
        }
        return n;
    }
    // 개방률 합 (창 6개 전부 100%면 6)
    [[nodiscard]] double windows_open_fraction_sum() const {
        double s = 0.0;
        for (const auto& layer : window_open_fraction) {
            s += layer[0] + layer[1];
        }
        return s;
    }

    bool operator==(const HouseState&) const = default;
};

}  // namespace sim
