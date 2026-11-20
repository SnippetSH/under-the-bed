#pragma once
// M5b 월드 단위 보존·불변 검사 (foliar-disease 9장 "보존", house-humidity 7장). test_m5b_foliar_world.cpp와
// test_m5b_scenarios.cpp가 같이 써요.
//   원소: 토양 + 체내 = 초기 + in − leached − gas − harvested − removed (RemovePlant만 계 밖 항).
//   유기 C: 초기 + C_in + C_root_litter + C_leaf_litter − C_respired.
//   토양 물: 초기 + in − 증발 − 증산 − 배수.
//   수증기: 공기 중 = 시작값 + vapor_source − 결로 − 환기 − 안개·이슬, 원천 = 셀 평균 (증산 + 증발).
//   불변: 지상부 지수·습도·잎 젖음 0~1, 탄저병 감염은 같은 작물이 이어지는 동안 줄지 않음, 시간 상태 ≥ 0,
//         수증기 ≥ 0, 토양 무기 N 평균 ≥ 0, 미생물 지수 0~1, NaN 없음.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>

#include "sim/world.hpp"

namespace test::m5b {

inline constexpr double kRelTol = 1e-9;
inline constexpr std::size_t kBudgetCount = 11;
inline constexpr std::array<const char*, kBudgetCount> kBudgetNames = {
    "water", "N", "P", "K", "Ca", "Mg", "Mn", "S", "C", "vapor", "vapor_source"};

struct Totals {
    double water = 0.0, N = 0.0, P = 0.0, K = 0.0, Ca = 0.0, Mg = 0.0, Mn = 0.0, S = 0.0, C = 0.0;
};

inline Totals totals_of(const sim::Snapshot& snap) {
    Totals t;
    for (std::size_t i = 0; i < snap.soil.size(); ++i) {
        const sim::SoilCell& c = snap.soil[i];
        const sim::Crop& crop = snap.crops[i];
        t.water += c.water_mm;
        t.N += c.total_N_g_per_m2() + crop.plant_N_g_per_m2;
        t.P += c.p_g_per_m2 + c.p_fixed_g_per_m2 + crop.plant_P_g_per_m2;
        t.K += c.k_g_per_m2 + crop.plant_K_g_per_m2;
        t.Ca += c.ca_g_per_m2 + crop.plant_Ca_g_per_m2;
        t.Mg += c.mg_g_per_m2 + crop.plant_Mg_g_per_m2;
        t.Mn += c.mn_g_per_m2 + c.mn_oxide_g_per_m2 + crop.plant_Mn_g_per_m2;
        t.S += c.s_g_per_m2;
        t.C += c.organic_C_g_per_m2();
    }
    return t;
}

inline double rel_err(double actual, double expected) {
    return std::abs(actual - expected) / std::max(1.0, std::abs(expected));
}
inline bool unit(double v) { return std::isfinite(v) && v >= 0.0 && v <= 1.0; }
inline bool non_negative(double v) { return std::isfinite(v) && v >= 0.0; }

// 공기 중 수증기 (mm = 지면 1 m²당 kg)
inline double air_vapor_mm(const sim::Params& p, const sim::HouseState& h) {
    return h.vapor_g_per_m3 * p.house.air_volume_m3_per_m2 / 1000.0;
}

// 수증기 보존식 상대 오차 (스케일 = 항들의 최대 크기, test_house_humidity.cpp와 같은 정의)
inline double vapor_balance_error(const sim::Params& p, const sim::HouseState& h) {
    const double air = air_vapor_mm(p, h);
    const double expected =
        h.vapor_initial_mm + h.vapor_source_mm - h.water_condensed_mm - h.vapor_vented_mm - h.vapor_deposited_mm;
    const double scale = std::max({1e-12, std::abs(h.vapor_initial_mm), h.vapor_source_mm, h.water_condensed_mm,
                                   std::abs(h.vapor_vented_mm), h.vapor_deposited_mm, air});
    return std::abs(air - expected) / scale;
}

// 초기 스냅샷 대비 모든 수지의 상대 오차
inline std::array<double, kBudgetCount> budget_errors(const sim::Params& p, const sim::Snapshot& init_snap,
                                                      const Totals& init, const sim::Snapshot& snap) {
    const Totals now = totals_of(snap);
    const sim::Ledger& l = snap.ledger;
    const sim::Ledger& l0 = init_snap.ledger;
    const double cells = static_cast<double>(snap.soil.size());
    const double vapor_from_soil = (l.water_transpired_mm - l0.water_transpired_mm + l.water_evaporated_mm -
                                    l0.water_evaporated_mm) / cells;
    return {
        rel_err(now.water, init.water + l.water_in_mm - l.water_evaporated_mm - l.water_transpired_mm -
                               l.water_drained_mm),
        rel_err(now.N, init.N + l.N_in_g - l.N_leached_g - l.N_gas_g - l.N_harvested_g - l.N_removed_g),
        rel_err(now.P, init.P + l.P_in_g - l.P_leached_g - l.P_harvested_g - l.P_removed_g),
        rel_err(now.K, init.K + l.K_in_g - l.K_leached_g - l.K_harvested_g - l.K_removed_g),
        rel_err(now.Ca, init.Ca + l.Ca_in_g - l.Ca_leached_g - l.Ca_harvested_g - l.Ca_removed_g),
        rel_err(now.Mg, init.Mg + l.Mg_in_g - l.Mg_leached_g - l.Mg_harvested_g - l.Mg_removed_g),
        rel_err(now.Mn, init.Mn + l.Mn_in_g - l.Mn_leached_g - l.Mn_harvested_g - l.Mn_removed_g),
        rel_err(now.S, init.S + l.S_in_g - l.S_leached_g),
        rel_err(now.C, init.C + l.C_in_g + l.C_root_litter_g + l.C_leaf_litter_g - l.C_respired_g),
        vapor_balance_error(p, snap.house),
        rel_err(snap.house.vapor_source_mm - init_snap.house.vapor_source_mm, vapor_from_soil),
    };
}

// 불변 조건 위반 사유 (없으면 빈 문자열)
inline std::string invalid_reason(const sim::Snapshot& prev, const sim::Snapshot& snap) {
    const sim::HouseState& h = snap.house;
    for (const double v : {h.botrytis_spore_index, h.powdery_mildew_spore_index, h.anthracnose_sporulation_index,
                           h.spider_mite_mean_index}) {
        if (!unit(v)) {
            return "house foliar index out of 0..1";
        }
    }
    for (const double v : {h.rh_index, h.rh_mean_index, h.airflow_index}) {
        if (!unit(v)) {
            return "house humidity index out of 0..1";
        }
    }
    if (!non_negative(h.vapor_g_per_m3)) {
        return "house vapor negative or NaN";
    }
    for (const double v : {h.vapor_source_mm, h.water_condensed_mm, h.vapor_deposited_mm, h.fan_hours,
                           h.drip_installations}) {
        if (!non_negative(v)) {
            return "house humidity ledger negative or NaN";
        }
    }
    if (!std::isfinite(h.vapor_vented_mm) || !std::isfinite(h.cover_inner_temp_degC)) {
        return "house vapor vented or cover temperature NaN";
    }
    if (!non_negative(h.inside_solar_mean_W_per_m2)) {
        return "house solar mean invalid";
    }
    for (std::size_t i = 0; i < snap.crops.size(); ++i) {
        const sim::Crop& c = snap.crops[i];
        for (const double v : {c.infection_anthracnose_index, c.infection_botrytis_index, c.infection_powdery_mildew_index,
                               c.spider_mite_index, c.spider_mite_mean_index, c.predator_mite_index, c.leaf_wetness_index,
                               c.canopy_rh_index}) {
            if (!unit(v)) {
                return std::format("crop {} foliar or wetness index out of 0..1", i);
            }
        }
        if (!non_negative(c.wet_run_hours) || !non_negative(c.dry_run_hours)) {
            return std::format("crop {} wet or dry run hours invalid", i);
        }
        if (!non_negative(c.soil_mineral_N_mean_g_per_m2)) {
            return std::format("crop {} soil mineral N mean invalid", i);
        }
        const sim::Crop& p = prev.crops[i];
        const bool same_crop = p.present && c.present && !(!p.alive && c.alive);
        if (same_crop && c.infection_anthracnose_index < p.infection_anthracnose_index) {
            return std::format("crop {} anthracnose infection decreased", i);
        }
    }
    for (std::size_t i = 0; i < snap.soil.size(); ++i) {
        for (std::size_t g = 0; g < sim::kMicrobeGroupCount; ++g) {
            if (!unit(snap.soil[i].microbe_index[g])) {
                return std::format("cell {} microbe {} out of 0..1", i, g);
            }
        }
    }
    return {};
}

}  // namespace test::m5b
