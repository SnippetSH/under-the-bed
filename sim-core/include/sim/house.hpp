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

    // M5b 하우스 습도 (docs/spec/house-humidity.md 2장). 저장 대상
    double vapor_g_per_m3 = 0.0;   // 공기 중 수증기 농도 (절대 습도)
    double rh_mean_index = 0.0;    // 상대습도 이동 평균 (약 3일)
    bool fan_on = false;           // 유동팬 (SetFan, 다음 틱부터)
    bool drip_installed = true;    // 점적 호스 (시나리오 [house] drip_installed, InstallDrip). 없으면 관수 = 두상 관수
    double fan_hours = 0.0;        // 유동팬 가동 시간 장부 (전기료 M7)
    double drip_installations = 0.0;  // 점적 설치 횟수 장부 (비용 M7)
    // 장부 (지면 1 m²당 mm, 1 mm = 1000 g/m²)
    double vapor_source_mm = 0.0;     // 공기로 들어온 수증기 누적 (증산 + 증발)
    double water_condensed_mm = 0.0;  // 피복 결로 누적
    double vapor_vented_mm = 0.0;     // 환기·틈새 순유출 누적 (음수 틱 가능)
    double vapor_deposited_mm = 0.0;  // 포화 초과 안개·이슬 누적
    double vapor_initial_mm = 0.0;    // 월드 생성 시 공기 중 수증기 (보존식 시작값)
    // 마지막 틱 파생값 (스냅샷용, 저장도 해요)
    double rh_index = 0.0;               // vapor ÷ sat(house_air)
    double airflow_index = 0.0;          // 잎 주변 공기 흐름 0~1
    double cover_inner_temp_degC = 0.0;  // 피복 안쪽 면 온도

    // M5b 지상부 병해충 하우스 값 (foliar-disease 2장·4장). 셀 계산은 틱 시작 값을 읽고, 셀 계산 뒤 한 번에 갱신해요
    double botrytis_spore_index = 0.0;          // 공기 중 잿빛곰팡이 포자 압력 0~1
    double powdery_mildew_spore_index = 0.0;    // 공기 중 흰가루병 포자 압력 0~1
    double anthracnose_sporulation_index = 0.0; // 탄저병 포자 원천 (물 튐 포기 → 포기), 0~1
    double spider_mite_mean_index = 0.0;        // 살아 있는 포기의 응애 밀도 평균 (응애 이동)
    double inside_solar_mean_W_per_m2 = 0.0;    // 하우스 안 일사 하루 이동 평균 (흰가루병 약광)

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
