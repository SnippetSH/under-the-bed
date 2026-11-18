#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sim {

// 토양 셀 1개 = 베드 면적 1 m², 근권 한 층 (docs/spec/m1-overview.md "토양 셀 상태")
// 질소 풀은 모두 질소 원소(N) 기준이에요.
struct SoilCell {
    // 정적 성질 (시나리오 [soil]에서 옴. 셀별로 다를 수 있게 셀에 둬요)
    double field_capacity_mm = 0.0;
    double saturation_mm = 0.0;                 // M2 aeration
    double base_buffer_eq_per_m2_per_pH = 0.0;  // M2 soil-chemistry
    double cec_mineral_eq_per_m2 = 0.0;         // M3 cation-exchange

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

    // pH: M2부터 변수 (soil-chemistry)
    double ph = 0.0;

    // 통기·다짐 (aeration, M2)
    double compaction_index = 0.0;
    double aeration_index = 0.0;  // 셀 단계 c에서 매 틱 계산

    // 양분 풀: 흙에 붙은 것 + 녹은 것, 원소 기준 (soil-chemistry, M2)
    double k_g_per_m2 = 0.0;
    double ca_g_per_m2 = 0.0;
    double mg_g_per_m2 = 0.0;
    double p_g_per_m2 = 0.0;
    double s_g_per_m2 = 0.0;

    // M3: 망간 총량, 고정 인산 (manganese, phosphorus)
    double mn_g_per_m2 = 0.0;        // 유효태 Mn2+ (교환성 + 용액)
    double mn_oxide_g_per_m2 = 0.0;  // 산화물 (ADR-0010)
    double p_fixed_g_per_m2 = 0.0;   // 고정 인산 (흡수·용탈 안 됨)

    // M3: 총량 중 녹은 부분 (cation-exchange). 0 ≤ 용액 ≤ 총량
    double nh4_solution_N_g_per_m2 = 0.0;
    double k_solution_g_per_m2 = 0.0;
    double ca_solution_g_per_m2 = 0.0;
    double mg_solution_g_per_m2 = 0.0;
    double mn_solution_g_per_m2 = 0.0;

    // M3 파생값 (셀 단계 f2, 매 틱)
    double cec_eq_per_m2 = 0.0;
    double exchange_free_site_index = 0.0;

    // 석회와 산 (soil-chemistry, M2)
    double lime_undissolved_eq_per_m2 = 0.0;
    double lime_ca_fraction = 0.0;     // 미용해 석회 당량 중 Ca 비율 (가중 평균)
    double lime_mg_fraction = 0.0;
    double acid_H_eq_this_tick = 0.0;  // 명령·질산화가 더하고, pH 단계가 소비한 뒤 0으로
    double pending_H_eq_per_m2 = 0.0;  // 작물 흡수가 더하고 다음 틱 pH 단계가 소비 (음수 = 염기)

    // 파생값 (셀 단계 j)
    double ec_dS_per_m = 0.0;

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
    double N_gas_g = 0.0;  // M4: 탈질
    double C_in_g = 0.0;
    double C_respired_g = 0.0;

    // M2 (m2-overview "장부 추가")
    double water_transpired_mm = 0.0;
    double N_uptake_g = 0.0;
    double P_in_g = 0.0;
    double P_leached_g = 0.0;
    double P_uptake_g = 0.0;
    double K_in_g = 0.0;
    double K_leached_g = 0.0;
    double K_uptake_g = 0.0;
    double Ca_in_g = 0.0;
    double Ca_leached_g = 0.0;
    double Ca_uptake_g = 0.0;
    double Mg_in_g = 0.0;
    double Mg_leached_g = 0.0;
    double Mg_uptake_g = 0.0;
    double S_in_g = 0.0;
    double S_leached_g = 0.0;
    double H_eq_net = 0.0;  // pH 단계에 들어간 순 산 누계 (검증용)
    double C_root_litter_g = 0.0;  // 뿌리 고사로 토양 이분해성 풀에 들어간 C

    // M3
    double Mn_in_g = 0.0;
    double Mn_leached_g = 0.0;
    double Mn_uptake_g = 0.0;

    // M4: 수확 반출, 낙엽(+저온 피해 잎·과실) C
    double N_harvested_g = 0.0;
    double P_harvested_g = 0.0;
    double K_harvested_g = 0.0;
    double Ca_harvested_g = 0.0;
    double Mg_harvested_g = 0.0;
    double Mn_harvested_g = 0.0;
    double C_leaf_litter_g = 0.0;

    bool operator==(const Ledger&) const = default;
};

// ---- 작물 (docs/spec/strawberry-minimal.md, M2) ----

// stress_hours 인덱스. 동률일 때는 이 순서상 앞선 요인을 기록해요.
// M3: NutrientMn, ToxicityMn은 Root 다음 맨 뒤에 추가 (manganese.md "스트레스")
enum class StressFactor : std::uint8_t {
    Water = 0, Salt, NutrientN, NutrientP, NutrientK, NutrientCa, NutrientMg, Root, NutrientMn, ToxicityMn
};
inline constexpr std::size_t kStressFactorCount = 10;

enum class RootDamageCause : std::uint8_t { Waterlog = 0, Heat, Nitrite, Acid };

// ---- M4 (docs/spec/strawberry-phenology.md, strawberry-fruit.md) ----

// 화방 (분화 순서대로)
struct Truss {
    std::uint64_t initiated_tick = 0;
    double development_index = 0.0;  // 분화 → 개화 0~1
    double flowers_per_m2 = 0.0;     // 분화 시 정해짐 (저온 피해로 줄 수 있음)
    bool opened = false;
    // 프로토타입 진단용 (명세 밖, 5차): 개화 틱, 화방별 수확·낙과
    std::uint64_t opened_tick = 0;
    std::uint64_t harvested_fruits = 0;
    double harvested_fresh_g = 0.0;
    std::uint64_t aborted_fruits = 0;
    std::uint64_t first_harvest_tick = 0;
    std::uint64_t last_harvest_tick = 0;

    bool operator==(const Truss&) const = default;
};

// 개별 과실 (타일 1칸 = 1 m²). set = false면 아직 꽃
struct Fruit {
    std::uint32_t truss_index = 0;
    std::uint32_t rank = 0;  // 과위 0~(과위 수 − 1)
    std::uint64_t anthesis_tick = 0;
    bool set = false;
    double pollination_index = 0.0;
    bool deformed = false;
    double potential_fresh_g = 0.0;
    double dry_g = 0.0;
    double development_index = 0.0;  // 개화 → 수확 0~1
    double brix_factor_sum = 0.0;
    double firmness_factor_sum = 0.0;
    double weight_sum = 0.0;
    // 당도 온도 점수의 주·야간 가중 합 (낮 가중치 = 하우스 안 일사, 밤 가중치 = 시간)
    double day_score_sum = 0.0;
    double day_weight_sum = 0.0;
    double night_score_sum = 0.0;
    double night_weight_sum = 0.0;

    bool operator==(const Fruit&) const = default;
};

// 누적 수확 기록 (작물마다)
struct HarvestRecord {
    std::uint64_t harvested_fruits = 0;  // 정상과
    double harvested_fresh_g = 0.0;
    std::uint64_t harvested_deformed = 0;
    double harvested_deformed_fresh_g = 0.0;
    double harvested_brix_weighted = 0.0;      // 당도 × 생체중 합 (정상과)
    double harvested_firmness_weighted = 0.0;  // 경도 × 생체중 합 (정상과)
    std::vector<std::uint64_t> harvested_size_class_counts;  // 길이 = 경계 수 + 1
    bool has_first_harvest = false;
    std::uint64_t first_harvest_tick = 0;
    // 프로토타입 진단용 (명세 밖, 4차 점검): 낙과·적과 개수, 정상과의 온도 점수 × 생체중 합
    std::uint64_t aborted_fruits = 0;
    std::uint64_t thinned_fruits = 0;
    double harvested_day_score_weighted = 0.0;
    double harvested_night_score_weighted = 0.0;
    double harvested_temp_score_weighted = 0.0;

    bool operator==(const HarvestRecord&) const = default;
};

// 타일당 작물 0~1개. present = false면 나머지 필드는 의미 없음.
struct Crop {
    bool present = false;
    bool alive = false;
    double leaf_g_per_m2 = 0.0;  // 건물
    double root_g_per_m2 = 0.0;
    double root_health = 0.0;  // 0~1
    double plant_N_g_per_m2 = 0.0;
    double plant_P_g_per_m2 = 0.0;
    double plant_K_g_per_m2 = 0.0;
    double plant_Ca_g_per_m2 = 0.0;
    double plant_Mg_g_per_m2 = 0.0;
    double plant_Mn_g_per_m2 = 0.0;  // M3
    std::array<double, kStressFactorCount> stress_hours{};  // 요인별로 생장을 가장 크게 제한한 누적 시간
    double last_growth_g_per_m2 = 0.0;                       // 이번 틱 생장량 (스냅샷용)
    // M3 이번 틱 길항 배율 (스냅샷용, cation-exchange "흡수 길항")
    double antagonism_Ca_factor = 1.0;
    double antagonism_Mg_factor = 1.0;

    // M4
    double crown_g_per_m2 = 0.0;
    double plants_per_m2 = 0.0;  // 정식 시 고정
    double establishment_index = 0.0;
    double induction_index = 0.0;
    std::vector<Truss> trusses;
    std::vector<Fruit> fruits;
    HarvestRecord harvest;
    // 이번 틱 값 (스냅샷·CO2 수지용)
    double last_gross_g_per_m2 = 0.0;        // 총생산
    double last_respiration_g_per_m2 = 0.0;  // 유지 호흡
    double last_supply_ratio = 1.0;          // 과실 공급 비율
    // 프로토타입 점검 3차 (P2): 크기 충족도(Σ과실 건물 ÷ Σ목표 건물)의 이동 평균. 순생산 > 0인 틱에만 갱신 (당도 계수의 x)
    double size_satisfaction_mean = 1.0;

    // 달려 있는 과실 건물 합 g/m² (꽃은 0)
    [[nodiscard]] double fruit_g_per_m2() const {
        double sum = 0.0;
        for (const Fruit& f : fruits) {
            sum += f.dry_g;
        }
        return sum;
    }

    bool operator==(const Crop&) const = default;
};

}  // namespace sim
