#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace sim {

// ---- M5a 토양 미생물 (docs/spec/soil-microbes.md "무리"). 순서는 세이브·CSV 열 순서, 뒤에만 추가 ----
// M5b: Anthracnose(탄저병균 흙·잔재 접종원, foliar-disease 3.2, 무리 순서 7)를 뒤에 추가
enum class MicrobeGroup : std::uint8_t {
    Decomposer = 0, NitrifierAob, NitrifierNob, Antagonist, Mycorrhiza, Fusarium, Phytophthora, Anthracnose
};
inline constexpr std::size_t kMicrobeGroupCount = 8;
// 데이터 키·CSV 열 이름 (`[microbes.<id>]`, 시나리오 `[microbes]`, `<id>_index`)
inline constexpr std::array<std::string_view, kMicrobeGroupCount> kMicrobeGroupIds = {
    "decomposer", "nitrifier_aob", "nitrifier_nob", "antagonist", "mycorrhiza", "fusarium", "phytophthora",
    "anthracnose"};
using MicrobeIndices = std::array<double, kMicrobeGroupCount>;  // MicrobeGroup 순서
[[nodiscard]] constexpr std::size_t microbe(MicrobeGroup g) { return static_cast<std::size_t>(g); }

// 토양 병해 (soil-microbes "작물 감염"). 병원균 무리와 짝: Fusarium <-> MicrobeGroup::Fusarium
enum class SoilDisease : std::uint8_t { Fusarium = 0, Phytophthora };
inline constexpr std::size_t kSoilDiseaseCount = 2;
inline constexpr std::array<std::string_view, kSoilDiseaseCount> kSoilDiseaseIds = {"fusarium", "phytophthora"};
[[nodiscard]] constexpr MicrobeGroup pathogen_of(SoilDisease d) {
    return d == SoilDisease::Fusarium ? MicrobeGroup::Fusarium : MicrobeGroup::Phytophthora;
}

// M5b 지상부 병해충 (foliar-disease 2장). DiseaseOnset(지상부)의 병 종류이고 Crop::foliar_onset_reported 순서예요.
enum class FoliarDisease : std::uint8_t { Anthracnose = 0, Botrytis, PowderyMildew, SpiderMite };
inline constexpr std::size_t kFoliarDiseaseCount = 4;
inline constexpr std::array<std::string_view, kFoliarDiseaseCount> kFoliarDiseaseIds = {
    "anthracnose", "botrytis", "powdery_mildew", "spider_mite"};

// 멀칭 없음 (SoilCell::mulch_def_index)
inline constexpr std::int32_t kNoMulch = -1;
// SetMulch의 "걷기" id. params [[mulch]] id로는 쓸 수 없어요 (검증)
inline constexpr std::string_view kNoMulchId = "none";

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
    // 깊은 흙 온도 (M5a 프로토타입 3차): 근권 완화 목표의 일부를 차지하고 근권 온도를 아주 느리게 따라가요.
    // 월드 생성 때 시나리오 soil_temp_degC로 시작해요
    double deep_soil_temp_degC = 0.0;

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

    // M5a 토양 미생물·소독 (soil-microbes, disinfection). 모두 플레이어 비공개 진짜 값
    MicrobeIndices microbe_index{};              // 무리별 상대 밀도 0~1
    std::int32_t mulch_def_index = kNoMulch;     // params.mulches 인덱스, kNoMulch = 덮지 않음
    std::vector<double> fumigant_residue_index;  // params.disinfectants 순서, 약제별 잔류 (표준량 1회 = 1)
    // 파생값 (셀 단계 d2, 매 틱): 무리별 이번 틱 소독(열 + 약제) 사멸분. d3 무기화·망간이 읽어요
    MicrobeIndices disinfection_kill_index{};

    // M5b (house-humidity 4.4): 이번 틱 두상 관수를 받았는지 (점적 없이 Irrigate). 틱 시작 때 지우고 명령 단계가 세워요.
    // 잎 젖음(4.3)과 탄저병 물 튐(foliar-disease 4.2)이 읽어요. 파생값이지만 세이브 왕복이 같도록 저장해요
    bool overhead_irrigated_this_tick = false;

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

    // M5a (disinfection, soil-microbes)
    double N_flush_g = 0.0;                      // 소독 무기화: 유기 N -> NH4+ (계 안 이동, 진단용)
    double Mn_flush_g = 0.0;                     // 소독 환원: 산화물 -> 유효태 (계 안 이동, 진단용)
    // params.disinfectants 순서 (비용은 M7). 첫 ApplyDisinfectant 전에는 비어 있어요(= 전부 0)
    std::vector<double> disinfectant_applied_g;
    double inoculant_applied_g = 0.0;            // 유익균 제품 투입 합계 (담체 C·N은 C_in_g·N_in_g에)

    // M5b (foliar-disease 2장 "장부 추가")
    // 병든 포기 제거(RemovePlant)로 계 밖으로 나간 체내 원소·건물 (원소 보존식의 − removed 항)
    double N_removed_g = 0.0;
    double P_removed_g = 0.0;
    double K_removed_g = 0.0;
    double Ca_removed_g = 0.0;
    double Mg_removed_g = 0.0;
    double Mn_removed_g = 0.0;
    double dry_matter_removed_g = 0.0;  // 잎·관부·뿌리·과실·저장 건물
    std::uint64_t plants_removed = 0;   // 제거한 칸 수
    // 잿빛곰팡이로 썩어 버린 과실 (건물·양분은 낙엽 규칙으로 토양에, 계 안 이동)
    std::uint64_t fruit_rot_count = 0;
    double fruit_rot_fresh_g = 0.0;
    // 흰가루 과실 (상품 아님, 원소는 수확 반출 장부에 같이 들어가요). 정상과·기형과 집계에서 빠져요
    std::uint64_t harvested_mildew = 0;
    double harvested_mildew_fresh_g = 0.0;
    // 누적 천적 방사 (하우스 평균 마리/m², 영역 칸 수 ÷ 전체 칸 수로 환산)
    double predators_released_per_m2 = 0.0;

    bool operator==(const Ledger&) const = default;
};

// ---- 작물 (docs/spec/strawberry-minimal.md, M2) ----

// stress_hours 인덱스. 동률일 때는 이 순서상 앞선 요인을 기록해요.
// M3: NutrientMn, ToxicityMn은 Root 다음 맨 뒤에 추가 (manganese.md "스트레스")
enum class StressFactor : std::uint8_t {
    Water = 0, Salt, NutrientN, NutrientP, NutrientK, NutrientCa, NutrientMg, Root, NutrientMn, ToxicityMn
};
inline constexpr std::size_t kStressFactorCount = 10;

// M5a: Fusarium, Phytophthora(토양 병해), Fumigant(약제 잔류)를 뒤에 추가 (disinfection, soil-microbes)
// M5b: Anthracnose(관부탄저, foliar-disease 4.2)를 뒤에 추가
enum class RootDamageCause : std::uint8_t {
    Waterlog = 0, Heat, Nitrite, Acid, Fusarium, Phytophthora, Fumigant, Anthracnose
};
inline constexpr std::size_t kRootDamageCauseCount = 8;

// ---- M4 (docs/spec/strawberry-phenology.md, strawberry-fruit.md) ----

// 화방 (분화 순서대로)
struct Truss {
    std::uint64_t initiated_tick = 0;
    double development_index = 0.0;  // 분화 → 개화 0~1
    double flowers_per_m2 = 0.0;     // 분화 시 정해짐 (저온 피해로 줄 수 있음)
    bool opened = false;
    // 진단·리포트용 기록 (m4-overview "진단·리포트용 기록"). 결과를 바꾸지 않아요.
    // 개화 틱, 이 화방에서 딴 정상과·기형과 개수·생체중, 낙과 수, 첫·마지막 수확 틱
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
    // 진단·리포트용 기록 (m4-overview "진단·리포트용 기록"). 결과를 바꾸지 않아요.
    // 낙과·적화·적과 개수, 정상과의 낮·밤·합산 온도 점수 × 생체중 합
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
    // 이번 틱 값 (진단·리포트용 기록, CO2 수지가 총생산 − 유지 호흡을 읽어요)
    double last_gross_g_per_m2 = 0.0;        // 총생산
    double last_respiration_g_per_m2 = 0.0;  // 유지 호흡
    double last_supply_ratio = 1.0;          // 과실 공급 비율
    // 크기 충족도(Σ 과실 건물 ÷ Σ 목표 건물)의 이동 평균 (strawberry-fruit 5번). 순생장 > 0인 틱에만 갱신.
    // 당도의 공급 입력과 낙과 확률이 읽어요. 시작값 1, 저장 상태
    double size_satisfaction_mean = 1.0;
    // 저장 탄수화물 (건물 환산 g/m², 양분 없음, 관부·뿌리에 있다고 봐요). strawberry-fruit 4b
    double reserve_g_per_m2 = 0.0;
    // 식물 상대 생장률(순생장 ÷ 전체 건물, /일)의 이동 평균. 유지 호흡 활성 보정이 읽어요. 저장 상태
    double rgr_mean_per_day = 0.0;

    // M5a 토양 병해 (soil-microbes 3번): SoilDisease 순서. 감염은 줄지 않아요(0~1). 비공개 진짜 값
    std::array<double, kSoilDiseaseCount> infection_index{};
    std::array<bool, kSoilDiseaseCount> disease_onset_reported{};  // DiseaseOnset 이벤트를 냈는지
    // 포기 간 감수성 차이 (M5a 프로토타입 3차): 두 토양 병해의 감염 증가분에 곱해요. 정식 때 정해지고 기본 1
    double disease_susceptibility_factor = 1.0;

    // M5b 잎 젖음 (house-humidity 2장·4.3). 비공개 진짜 값. 저장 상태
    double leaf_wetness_index = 0.0;  // 0~1
    double wet_run_hours = 0.0;       // 연속 젖음 시간
    double dry_run_hours = 0.0;       // 마른 상태 지속 시간 (끊김 판정)
    double canopy_rh_index = 0.0;     // 이번 틱 잎 사이 습도 (파생값, 스냅샷용)

    // M5b 지상부 병해충 (foliar-disease 2장). 모두 플레이어 비공개 진짜 값, 0~1
    double infection_anthracnose_index = 0.0;     // 관부 감염. 줄지 않아요
    double infection_botrytis_index = 0.0;        // 꽃·어린 과실 감염 압력 (서서히 줄어요)
    double infection_powdery_mildew_index = 0.0;  // 병든 잎 면적 비율
    double spider_mite_index = 0.0;               // 점박이응애 밀도 (1 = 소엽당 약 100마리)
    double spider_mite_mean_index = 0.0;          // 응애 밀도 2주 이동 평균 (광합성 피해가 읽음)
    double predator_mite_index = 0.0;             // 칠레이리응애 밀도 (응애와 같은 눈금)
    double soil_mineral_N_mean_g_per_m2 = 0.0;    // 근권 무기 N(NH4 + NO3) 2주 이동 평균 (질소 과다 감수성)
    std::array<bool, kFoliarDiseaseCount> foliar_onset_reported{};  // FoliarDisease 순서
    // 이번 틱 파생값 (스냅샷·CSV. 세이브 왕복이 같도록 저장해요)
    double last_anthracnose_increase_index = 0.0;
    double last_botrytis_increase_index = 0.0;
    double last_powdery_mildew_increase_index = 0.0;
    double last_mite_predation_index = 0.0;
    double last_fruit_rot_count = 0.0;  // 이번 틱 썩은 과실 수 (타일 1 m²라 곧 개/m²). 포자 원천이 읽어요

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
