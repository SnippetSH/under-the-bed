// docs/spec/organic-matter.md — 퇴비 투입, 유기물 분해(부식화, 무기화/부동화), 분해 발열
#include <algorithm>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

// 풀 하나(이분해성 또는 난분해성)의 한 틱 분해 결과를 셀에 반영하고, 호흡으로 날아간 C를 돌려줘요.
// pool_C/pool_N이 stable 풀 자신일 수도 있어요(난분해성 풀의 부식화 C는 다시 자기 풀로 돌아와요).
// 그래서 변화량은 모두 이 풀 단계 시작 시점의 값(C0, N0)으로 먼저 계산하고, 갱신은 맨 끝에 해요.
double decompose_pool(SoilCell& cell, double& pool_C_g_per_m2, double& pool_N_g_per_m2, double decay_rate_per_hour,
                      double environment_factor, const OrganicMatterParams& params) {
    const double C0_g_per_m2 = pool_C_g_per_m2;
    const double N0_g_per_m2 = pool_N_g_per_m2;
    // C_p = 0이면 건너뛰어요. (NaN도 여기서 걸러요)
    if (!(C0_g_per_m2 > 0.0)) {
        return 0.0;
    }

    // 분해 비율 = min(1, 속도 × f). 음수·NaN이면 분해 없음.
    const double decay_fraction = std::min(1.0, decay_rate_per_hour * environment_factor);
    if (!(decay_fraction > 0.0)) {
        return 0.0;
    }

    // dC = C_p × 비율. released_N = dC × N_p / C_p 는 수학적으로 N_p × 비율과 같아요.
    // N_p × 비율로 계산하면 반올림이 단조라서 released_N ≤ N_p가 항상 보장돼요(풀이 음수가 되지 않음).
    double dC_g_per_m2 = C0_g_per_m2 * decay_fraction;
    double released_N_g_per_m2 = std::max(0.0, N0_g_per_m2) * decay_fraction;
    double humified_C_g_per_m2 = params.humification_fraction * dC_g_per_m2;
    double demand_N_g_per_m2 = humified_C_g_per_m2 / params.humus_C_to_N_ratio;

    // 새 부식이 실제로 붙잡는 N. 무기화면 demand 그대로, 부동화면 released + 실제로 가져온 무기 N이에요.
    // 이렇게 두면 셀 N 합이 반올림 오차 안에서 정확히 보존돼요.
    double humus_N_g_per_m2 = demand_N_g_per_m2;

    const double net_N_g_per_m2 = released_N_g_per_m2 - demand_N_g_per_m2;
    if (net_N_g_per_m2 >= 0.0) {
        // 무기화
        cell.nh4_N_g_per_m2 += net_N_g_per_m2;
    } else {
        // 부동화: NH4+ 먼저, 모자라면 NO3-
        const double need_N_g_per_m2 = -net_N_g_per_m2;
        const double nh4_N_g_per_m2 = std::max(0.0, cell.nh4_N_g_per_m2);
        const double no3_N_g_per_m2 = std::max(0.0, cell.no3_N_g_per_m2);
        const double available_N_g_per_m2 = nh4_N_g_per_m2 + no3_N_g_per_m2;

        if (!(available_N_g_per_m2 > 0.0)) {
            // 무기 질소가 없으면 고탄질비 유기물은 분해가 멈춰요. 아무것도 바꾸지 않아요.
            return 0.0;
        }

        if (available_N_g_per_m2 < need_N_g_per_m2) {
            // 분해 자체를 available / need 비율로 줄이고, 무기 질소를 전부 가져가요.
            const double scale_factor = available_N_g_per_m2 / need_N_g_per_m2;
            dC_g_per_m2 *= scale_factor;
            released_N_g_per_m2 *= scale_factor;
            humified_C_g_per_m2 *= scale_factor;
            demand_N_g_per_m2 *= scale_factor;
            cell.nh4_N_g_per_m2 = 0.0;
            cell.no3_N_g_per_m2 = 0.0;
            humus_N_g_per_m2 = released_N_g_per_m2 + available_N_g_per_m2;
        } else {
            const double take_nh4_N_g_per_m2 = std::min(nh4_N_g_per_m2, need_N_g_per_m2);
            // available ≥ need 이지만 반올림 때문에 NO3- 몫이 NO3-를 살짝 넘을 수 있어서 한 번 더 제한해요.
            const double take_no3_N_g_per_m2 = std::min(no3_N_g_per_m2, need_N_g_per_m2 - take_nh4_N_g_per_m2);
            cell.nh4_N_g_per_m2 = nh4_N_g_per_m2 - take_nh4_N_g_per_m2;
            cell.no3_N_g_per_m2 = no3_N_g_per_m2 - take_no3_N_g_per_m2;
            humus_N_g_per_m2 = released_N_g_per_m2 + take_nh4_N_g_per_m2 + take_no3_N_g_per_m2;
        }
    }

    // 풀 갱신 (C0, N0 기준 변화량을 순서대로 적용. stable 풀 자신이어도 결과가 같아요)
    pool_C_g_per_m2 = C0_g_per_m2 - dC_g_per_m2;
    pool_N_g_per_m2 = N0_g_per_m2 - released_N_g_per_m2;
    cell.stable_C_g_per_m2 += humified_C_g_per_m2;
    cell.stable_N_g_per_m2 += humus_N_g_per_m2;

    return dC_g_per_m2 - humified_C_g_per_m2;
}

}  // namespace

void apply_compost(SoilCell& cell, const CompostDef& compost, double kg_per_m2, Ledger& ledger) {
    const double C_g_per_m2 = kg_per_m2 * compost.C_g_per_kg;
    const double N_g_per_m2 = kg_per_m2 * compost.N_g_per_kg;

    // 나머지 몫은 C × (1 − 비율) 대신 C − 이분해성 몫으로 계산해요(수학적으로 같고, 합이 C와 더 정확히 맞아요).
    const double labile_C_g_per_m2 = C_g_per_m2 * compost.labile_C_fraction;
    const double labile_N_g_per_m2 = N_g_per_m2 * compost.labile_N_fraction;
    cell.labile_C_g_per_m2 += labile_C_g_per_m2;
    cell.stable_C_g_per_m2 += C_g_per_m2 - labile_C_g_per_m2;
    cell.labile_N_g_per_m2 += labile_N_g_per_m2;
    cell.stable_N_g_per_m2 += N_g_per_m2 - labile_N_g_per_m2;

    ledger.C_in_g += C_g_per_m2;
    ledger.N_in_g += N_g_per_m2;
}

void decompose_organic_matter(SoilCell& cell, const OrganicMatterParams& params,
                              const SoilTemperatureParams& temperature_params, Ledger& ledger) {
    // 환경 계수는 이 단계 시작 시점의 토양 온도·수분으로 한 번 계산해 두 풀에 똑같이 써요.
    // (이번 틱의 발열은 다음 틱 분해 속도에 반영돼요.)
    // O2 계수(M2 통기성)를 곱해요. 나중에 분해균계수(M5)도 곱해져요.
    const double environment_factor = params.temp_curve(cell.soil_temp_degC) *
                                      params.moisture_curve(cell.relative_water()) *
                                      params.o2_curve(cell.aeration_index);

    double respired_C_g_per_m2 = 0.0;
    // 이분해성 → 난분해성 순서. 난분해성 단계는 이분해성 단계에서 부식화된 C·N까지 포함한 값에서 시작해요.
    // M3: 풀마다 NH4 총량 변화를 용액에 반영 (무기화 → 용액, 부동화 → 비례 차감)
    double nh4_before = cell.nh4_N_g_per_m2;
    respired_C_g_per_m2 += decompose_pool(cell, cell.labile_C_g_per_m2, cell.labile_N_g_per_m2,
                                          params.labile_decay_rate_per_hour, environment_factor, params);
    sync_nh4_solution(cell, nh4_before);
    nh4_before = cell.nh4_N_g_per_m2;
    respired_C_g_per_m2 += decompose_pool(cell, cell.stable_C_g_per_m2, cell.stable_N_g_per_m2,
                                          params.stable_decay_rate_per_hour, environment_factor, params);
    sync_nh4_solution(cell, nh4_before);

    ledger.C_respired_g += respired_C_g_per_m2;
    // 분해 발열 (docs/spec/soil-temperature.md "분해 발열")
    cell.soil_temp_degC += respired_C_g_per_m2 * temperature_params.heat_degC_per_g_C;
}

}  // namespace sim::process
