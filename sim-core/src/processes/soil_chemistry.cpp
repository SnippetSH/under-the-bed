// docs/spec/soil-chemistry.md — 비료·퇴비 무기 성분, 석회 용해, pH 수지, 유효량, EC (패키지 2)
// 용탈은 leaching.cpp, 질산화 H+는 nitrification.cpp, 흡수 H+/OH-는 crop.cpp가 맡아요.
#include <algorithm>

#include "sim/processes.hpp"

namespace sim::process {

namespace {

constexpr double kGramsPerKilogram = 1000.0;  // 단위 환산
constexpr double kMilliPerUnit = 1000.0;      // 단위 환산 (eq → meq)

// 1차 반응의 "이번 틱 이동 비율"을 [0, 1]로 제한해요. NaN·음수는 0(이동 없음).
[[nodiscard]] double transfer_fraction(double rate_times_factors) {
    if (!(rate_times_factors > 0.0)) {
        return 0.0;
    }
    if (rate_times_factors >= 1.0) {
        return 1.0;
    }
    return rate_times_factors;
}

// 풀 × 비율. 풀이 0 이하(또는 NaN)면 0. 비율 ≤ 1이라 빼고 나서 음수가 되지 않아요.
[[nodiscard]] double portion_of(double pool, double fraction) {
    if (!(pool > 0.0)) {
        return 0.0;
    }
    return pool * transfer_fraction(fraction);
}

// 비료·퇴비 공통: 원소별 무기 성분을 풀과 장부에 더하고, 직접 산·염기를 이번 틱 H+에 더해요.
struct MineralContents {
    double k_g_per_kg = 0.0;
    double ca_g_per_kg = 0.0;
    double mg_g_per_kg = 0.0;
    double p_g_per_kg = 0.0;
    double s_g_per_kg = 0.0;
    double direct_acid_eq_per_kg = 0.0;
};

void add_minerals(SoilCell& cell, const MineralContents& contents, double product_kg_per_m2, Ledger& ledger) {
    const double k_g = product_kg_per_m2 * contents.k_g_per_kg;
    const double ca_g = product_kg_per_m2 * contents.ca_g_per_kg;
    const double mg_g = product_kg_per_m2 * contents.mg_g_per_kg;
    const double p_g = product_kg_per_m2 * contents.p_g_per_kg;
    const double s_g = product_kg_per_m2 * contents.s_g_per_kg;

    cell.k_g_per_m2 += k_g;
    cell.ca_g_per_m2 += ca_g;
    cell.mg_g_per_m2 += mg_g;
    cell.p_g_per_m2 += p_g;
    cell.s_g_per_m2 += s_g;

    ledger.K_in_g += k_g;
    ledger.Ca_in_g += ca_g;
    ledger.Mg_in_g += mg_g;
    ledger.P_in_g += p_g;
    ledger.S_in_g += s_g;

    // 음수 = 염기. pH 단계(g)가 같은 틱에 소비해요.
    cell.acid_H_eq_this_tick += product_kg_per_m2 * contents.direct_acid_eq_per_kg;
}

// 용액 비율 [0, 1] 제한 (검증을 통과한 값이면 그대로)
[[nodiscard]] double solution_fraction(double fraction) { return std::clamp(fraction, 0.0, 1.0); }

// 원소별 용액 부분 (g/m²). 유효량과 EC가 같이 써요.
struct SolutionParts {
    double nh4_N = 0.0;
    double no3_N = 0.0;
    double no2_N = 0.0;
    double k = 0.0;
    double ca = 0.0;
    double mg = 0.0;
    double p = 0.0;
    double s = 0.0;
};

[[nodiscard]] double non_negative(double pool) { return pool > 0.0 ? pool : 0.0; }

[[nodiscard]] SolutionParts solution_parts(const SoilCell& cell, const Params& params) {
    const SoilChemistryParams& chem = params.soil_chemistry;
    return SolutionParts{
        .nh4_N = non_negative(cell.nh4_N_g_per_m2) * solution_fraction(params.leaching.nh4_solution_fraction),
        .no3_N = non_negative(cell.no3_N_g_per_m2),
        .no2_N = non_negative(cell.no2_N_g_per_m2),
        .k = non_negative(cell.k_g_per_m2) * solution_fraction(chem.k_solution_fraction),
        .ca = non_negative(cell.ca_g_per_m2) * solution_fraction(chem.ca_solution_fraction),
        .mg = non_negative(cell.mg_g_per_m2) * solution_fraction(chem.mg_solution_fraction),
        .p = non_negative(cell.p_g_per_m2) * solution_fraction(chem.p_solution_fraction),
        .s = non_negative(cell.s_g_per_m2) * solution_fraction(chem.s_solution_fraction),
    };
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

    add_minerals(cell,
                 MineralContents{.k_g_per_kg = fertilizer.k_g_per_kg,
                                 .ca_g_per_kg = fertilizer.ca_g_per_kg,
                                 .mg_g_per_kg = fertilizer.mg_g_per_kg,
                                 .p_g_per_kg = fertilizer.p_g_per_kg,
                                 .s_g_per_kg = fertilizer.s_g_per_kg,
                                 .direct_acid_eq_per_kg = fertilizer.direct_acid_eq_per_kg},
                 product_kg_per_m2, ledger);
}

void apply_compost_minerals(SoilCell& cell, const CompostDef& compost, double kg_per_m2, Ledger& ledger) {
    if (!(kg_per_m2 > 0.0)) {
        return;
    }
    // 퇴비의 K 등은 투입 즉시 무기 풀로 들어간다고 단순화 (soil-chemistry "투입")
    add_minerals(cell,
                 MineralContents{.k_g_per_kg = compost.k_g_per_kg,
                                 .ca_g_per_kg = compost.ca_g_per_kg,
                                 .mg_g_per_kg = compost.mg_g_per_kg,
                                 .p_g_per_kg = compost.p_g_per_kg,
                                 .s_g_per_kg = compost.s_g_per_kg,
                                 .direct_acid_eq_per_kg = compost.direct_acid_eq_per_kg},
                 kg_per_m2, ledger);
}

void apply_lime(SoilCell& cell, const LimeDef& lime, double g_per_m2) {
    if (!(g_per_m2 > 0.0)) {
        return;
    }
    const double added_eq_per_m2 = g_per_m2 / kGramsPerKilogram * lime.eq_per_kg;
    if (!(added_eq_per_m2 > 0.0)) {
        return;
    }
    const double old_eq_per_m2 = non_negative(cell.lime_undissolved_eq_per_m2);
    const double total_eq_per_m2 = old_eq_per_m2 + added_eq_per_m2;

    // Ca·Mg 비율은 기존 미용해 석회와 당량 가중 평균
    cell.lime_ca_fraction =
        (old_eq_per_m2 * cell.lime_ca_fraction + added_eq_per_m2 * lime.ca_fraction) / total_eq_per_m2;
    cell.lime_mg_fraction =
        (old_eq_per_m2 * cell.lime_mg_fraction + added_eq_per_m2 * lime.mg_fraction) / total_eq_per_m2;
    cell.lime_undissolved_eq_per_m2 = total_eq_per_m2;
}

void update_lime_and_ph(SoilCell& cell, const SoilChemistryParams& params, const IonParams& ions, Ledger& ledger) {
    // 석회 용해
    const double dissolution_fraction =
        transfer_fraction(params.lime_dissolution_rate_per_hour * params.lime_dissolution_ph_curve(cell.ph) *
                          params.lime_dissolution_moisture_curve(cell.relative_water()));
    const double dissolved_eq_per_m2 = portion_of(cell.lime_undissolved_eq_per_m2, dissolution_fraction);
    cell.lime_undissolved_eq_per_m2 -= dissolved_eq_per_m2;

    const double ca_g = dissolved_eq_per_m2 * cell.lime_ca_fraction * ions.ca_g_per_eq;
    const double mg_g = dissolved_eq_per_m2 * cell.lime_mg_fraction * ions.mg_g_per_eq;
    cell.ca_g_per_m2 += ca_g;
    cell.mg_g_per_m2 += mg_g;
    ledger.Ca_in_g += ca_g;
    ledger.Mg_in_g += mg_g;

    // pH 수지: 이번 틱 질산화·직접 산 + 지난 틱 흡수 − 석회 용해
    const double net_H_eq_per_m2 = cell.acid_H_eq_this_tick + cell.pending_H_eq_per_m2 - dissolved_eq_per_m2;
    const double buffer_eq_per_m2_per_pH =
        cell.base_buffer_eq_per_m2_per_pH +
        params.organic_C_buffer_eq_per_g_C_per_pH * non_negative(cell.organic_C_g_per_m2());

    // 완충력이 0 이하(검증 위반)이면 나눗셈을 건너뛰어 NaN·무한대를 막아요.
    if (buffer_eq_per_m2_per_pH > 0.0) {
        cell.ph -= net_H_eq_per_m2 / buffer_eq_per_m2_per_pH;
    }
    cell.ph = std::clamp(cell.ph, params.ph_min, params.ph_max);

    cell.acid_H_eq_this_tick = 0.0;
    cell.pending_H_eq_per_m2 = 0.0;
    ledger.H_eq_net += net_H_eq_per_m2;
}

NutrientAvailability available_nutrients(const SoilCell& cell, const Params& params) {
    const SolutionParts solution = solution_parts(cell, params);
    const AvailabilityParams& curves = params.availability;
    const double ph = cell.ph;
    const double n_factor = curves.n_ph_curve(ph);
    return NutrientAvailability{
        .nh4_N = solution.nh4_N * n_factor,
        .no3_N = solution.no3_N * n_factor,
        .p = solution.p * curves.p_ph_curve(ph),
        .k = solution.k * curves.k_ph_curve(ph),
        .ca = solution.ca * curves.ca_ph_curve(ph),
        .mg = solution.mg * curves.mg_ph_curve(ph),
    };
}

double soil_ec_dS_per_m(const SoilCell& cell, const Params& params) {
    const SolutionParts solution = solution_parts(cell, params);
    const IonParams& ions = params.ions;

    const double cation_meq_per_m2 = (solution.nh4_N / ions.nh4_N_g_per_eq + solution.k / ions.k_g_per_eq +
                                      solution.ca / ions.ca_g_per_eq + solution.mg / ions.mg_g_per_eq) *
                                     kMilliPerUnit;
    const double anion_meq_per_m2 = (solution.no3_N / ions.no3_N_g_per_eq + solution.no2_N / ions.no2_N_g_per_eq +
                                     solution.s / ions.s_g_per_eq + solution.p / ions.p_g_per_eq) *
                                    kMilliPerUnit;

    // water_mm는 L/m²라서 나누면 meq/L. 마른 흙에서 발산하지 않도록 ec_min_water_mm로 하한
    const double water_L_per_m2 = std::max(cell.water_mm, params.soil_chemistry.ec_min_water_mm);
    const double ion_meq_per_L = (cation_meq_per_m2 + anion_meq_per_m2) / 2.0 / water_L_per_m2;
    return ion_meq_per_L / params.soil_chemistry.ec_meq_per_L_per_dS_per_m;
}

}  // namespace sim::process
