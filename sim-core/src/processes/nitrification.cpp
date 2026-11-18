// docs/spec/nitrogen.md — 비료 투입, 2단계 질산화, 아질산 경보 (패키지 4)
// 용탈은 leaching.cpp가 맡아요.
#include "sim/processes.hpp"

namespace sim::process {

namespace {

// 단위 환산 상수 (물리 계수가 아니에요)
constexpr double kGramsPerKilogram = 1000.0;

// 1차 반응의 "이번 틱 이동 비율"을 [0, 1]로 제한해요.
// NaN이나 음수 계수 곱은 0(이동 없음)으로 처리해서 풀이 음수가 되거나 NaN이 퍼지지 않게 해요.
[[nodiscard]] double transfer_fraction(double rate_times_factors) {
    if (!(rate_times_factors > 0.0)) {
        return 0.0;
    }
    if (rate_times_factors >= 1.0) {
        return 1.0;
    }
    return rate_times_factors;
}

// 원천 풀에서 비율만큼 옮길 양. 풀이 0 이하(또는 NaN)면 아무것도 옮기지 않아요.
// fraction ≤ 1이므로 pool × fraction ≤ pool이 부동소수점에서도 성립해서, 빼고 나서 음수가 되지 않아요.
[[nodiscard]] double transfer_amount(double pool, double fraction) {
    if (!(pool > 0.0)) {
        return 0.0;
    }
    return pool * fraction;
}

}  // namespace

void apply_fertilizer(SoilCell& cell, const FertilizerDef& fertilizer, double g_per_m2, Ledger& ledger) {
    // 음수·NaN 투입량은 enqueue 단계에서 거부되지만, 코어가 풀을 음수로 만들지 않도록 한 번 더 막아요.
    if (!(g_per_m2 > 0.0)) {
        return;
    }
    const double product_kg_per_m2 = g_per_m2 / kGramsPerKilogram;
    const double nh4_added_g_per_m2 = product_kg_per_m2 * fertilizer.nh4_N_g_per_kg;
    const double no3_added_g_per_m2 = product_kg_per_m2 * fertilizer.no3_N_g_per_kg;

    cell.nh4_N_g_per_m2 += nh4_added_g_per_m2;
    cell.no3_N_g_per_m2 += no3_added_g_per_m2;
    ledger.N_in_g += nh4_added_g_per_m2 + no3_added_g_per_m2;
}

void nitrify(SoilCell& cell, const NitrificationParams& params) {
    // 공통 환경 계수. O2 계수는 M1에서 1이에요.
    // TODO(M2): 통기성 명세가 승인되면 여기에 O2 계수(저산소일수록 2단계가 더 억제됨)를 곱해요.
    const double env_factor =
        params.temp_curve(cell.soil_temp_degC) * params.moisture_curve(cell.relative_water());

    // pH는 M1에서 고정이에요(질산화에 따른 산성화는 M2).
    const double ph = cell.ph;

    // 1단계: NH4+ → NO2-
    const double step1_fraction =
        transfer_fraction(params.step1_rate_per_hour * env_factor * params.step1_ph_curve(ph));
    const double n1_g_per_m2 = transfer_amount(cell.nh4_N_g_per_m2, step1_fraction);
    cell.nh4_N_g_per_m2 -= n1_g_per_m2;
    cell.no2_N_g_per_m2 += n1_g_per_m2;

    // 2단계: NO2- → NO3-. 유리 암모니아는 1단계 이후의 NH4+로 계산해요.
    const double free_ammonia_g_N_per_m2 = cell.nh4_N_g_per_m2 * params.ammonia_fraction_ph_curve(ph);
    const double step2_fraction =
        transfer_fraction(params.step2_rate_per_hour * env_factor * params.step2_ph_curve(ph) *
                          params.free_ammonia_inhibition_curve(free_ammonia_g_N_per_m2));
    const double n2_g_per_m2 = transfer_amount(cell.no2_N_g_per_m2, step2_fraction);
    cell.no2_N_g_per_m2 -= n2_g_per_m2;
    cell.no3_N_g_per_m2 += n2_g_per_m2;
}

std::optional<NitriteThresholdExceededEvent> check_nitrite_alarm(SoilCell& cell, const NitrificationParams& params,
                                                                 std::uint32_t tile_index) {
    const double threshold_g_N_per_m2 = params.nitrite_toxicity_threshold_g_N_per_m2;
    const double reset_g_N_per_m2 = threshold_g_N_per_m2 * params.nitrite_alarm_reset_fraction;

    if (!cell.nitrite_alarm) {
        if (cell.no2_N_g_per_m2 > threshold_g_N_per_m2) {
            cell.nitrite_alarm = true;
            return NitriteThresholdExceededEvent{.tile_index = tile_index, .no2_N_g_per_m2 = cell.no2_N_g_per_m2};
        }
    } else if (cell.no2_N_g_per_m2 < reset_g_N_per_m2) {
        cell.nitrite_alarm = false;
    }
    return std::nullopt;
}

}  // namespace sim::process
