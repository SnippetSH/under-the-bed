// sim-cli — 헤드리스 실행기
//
//   sim-cli --params data/params.toml --scenario data/scenarios/empty.toml --ticks 8760
//           [--out runs/empty.csv] [--no-hash] [--remove-on-onset <disease>]
//
// CSV는 틱 0(초기 상태)부터 틱 N까지 N+1행이에요. 행의 값은 해당 틱 시점의 상태예요.
// 열: 기상·명령 수, house_air_temp_degC, 셀 평균 토양 상태(soil.hpp 필드 이름), 작물 평균(작물이 있는 타일만,
// 작물이 하나도 없으면 빈 칸), 장부 합계(ledger_ 접두사), state_hash 순서예요.
// M3 파생 열: 교환성 양(*_exchangeable_* = 총량 − 용액), 토양 P 총량(p_total = 유효 + 고정),
// 작물 Mn 충족도·과잉 지수·체내 비율, 길항 배율, stress_hours 10개(StressFactor 순서).
// M4 파생 열: 하우스(house_air_temp_mean_degC, house_co2_ppm, outside_co2_ppm, 난방 에너지·출력, 피복 겹수,
// 열린 창 수·개방률 합, 난방 켜짐, 하우스 안 일사, 환기 경로 지수), 작물(관부, 과실 건물, 과실·꽃·화방 수,
// 활착·화아분화 지수, 과실 공급 비율, 크기 충족도 이동 평균, 총생산·유지 호흡), 수확 누적(정상과·기형과 생체중·개수,
// 하우스 전체 가중 평균 당도·경도·온도 점수, 낙과·적과 수, 크기 구간별 개수, 첫 수확 틱), 장부 M4 항목.
// 수확 열은 작물이 있는 타일 평균(개수는 타일당 평균)이에요. 진단 열은 m4-overview "진단·리포트용 기록"이에요.
// 명령 일정표([[schedule]])는 해당 틱을 처리하기 직전에 enqueue해요.
// M5a 파생 열: 무리별 하우스 평균 지수 <id>_index 7개, 약제별 평균 잔류 fumigant_residue_<id>_index, 멀칭 덮인 셀 비율
// mulched_fraction과 멀칭 id(mulch_id, 섞여 있으면 mixed), 작물 평균 감염 지수 infection_<병>_index, 이번 틱 소독 사멸
// disinfection_kill_<id>_index와 지금까지의 누적 disinfection_kill_cum_<id>_index(셀 평균), 장부 ledger_N_flush_g,
// ledger_Mn_flush_g, ledger_inoculant_applied_g, ledger_disinfectant_applied_<id>_g. 전부 플레이어 비공개 진짜 값이에요.
// M5b 파생 열: 하우스 수증기·상대습도·습도 이동 평균·공기 흐름·피복 안쪽 면 온도, 수증기 장부(vapor_source_mm,
// water_condensed_mm, vapor_vented_mm, vapor_deposited_mm), 작물 평균 잎 젖음·연속 젖음 시간·잎 사이 습도.
// M5a 프로토타입 3차 열: 깊은 흙 온도 deep_soil_temp_degC(셀 평균), 포기 감수성 disease_susceptibility_factor(작물 평균).
// M5b 지상부 병해충 열 (foliar-disease 2장): 하우스 점적·팬(drip_installed, fan_on, fan_hours, drip_installations),
// 포자 압력 botrytis_spore_index·powdery_mildew_spore_index, 탄저병 포자 원천 anthracnose_sporulation_index, 하우스 평균 응애
// house_spider_mite_mean_index, 하우스 안 일사 이동 평균 inside_solar_mean_W_per_m2, 두상 관수 셀 비율 overhead_irrigated_fraction,
// 작물 평균 감염 3종·응애·응애 2주 평균·천적·토양 무기 N 이동 평균·이번 틱 증가분·포식량, 작물 있는/살아 있는 칸 수,
// 하우스 전체 수확 합계(제거한 칸 포함) harvest_total_*, 누적 발병 이벤트 수 onset_<병>_count(토양 2 + 지상부 4),
// 장부 *_removed_g·plants_removed·fruit_rot_*·harvested_mildew*·predators_released_per_m2.
// --remove-on-onset <병>(anthracnose, botrytis, powdery_mildew, spider_mite)을 주면 그 병의 지상부 DiseaseOnset이 난 칸에
// 다음 틱 시작 전에 RemovePlant를 넣어요 (S28 s28_overhead_remove용 CLI 동작, 코어 규칙 아님).
// --no-hash를 주면 state_hash 열을 비워 둬요 (긴 실행을 빠르게). 최종 해시는 그래도 찍어요.
// 표준 출력에는 이벤트 종류별 횟수, 수확 합계, 최종 상태 해시를 찍어요.

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "sim/processes.hpp"
#include "sim/world.hpp"
#include "simdata/loader.hpp"

namespace {

namespace fs = std::filesystem;

constexpr int kExitOk = 0;
constexpr int kExitRuntimeError = 1;
constexpr int kExitUsage = 2;

constexpr std::string_view kUsage =
    "usage: sim-cli --params <params.toml> --scenario <scenario.toml> --ticks <N> "
    "[--out <file.csv>] [--no-hash] [--remove-on-onset <anthracnose|botrytis|powdery_mildew|spider_mite>]";

struct Options {
    fs::path params;
    fs::path scenario;
    std::optional<fs::path> out;
    std::uint32_t ticks = 0;
    bool no_hash = false;  // CSV의 state_hash 열을 비움
    std::optional<sim::FoliarDisease> remove_on_onset;  // M5b: 이 병의 발병 칸을 뽑아요
};

std::optional<Options> parse_args(int argc, char** argv) {
    Options opt;
    bool has_ticks = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--no-hash") {
            opt.no_hash = true;
            continue;
        }
        if (i + 1 >= argc) {
            std::println(stderr, "missing value for {}", arg);
            return std::nullopt;
        }
        const std::string_view value = argv[++i];
        if (arg == "--params") {
            opt.params = value;
        } else if (arg == "--scenario") {
            opt.scenario = value;
        } else if (arg == "--out") {
            opt.out = fs::path(value);
        } else if (arg == "--remove-on-onset") {
            for (std::size_t d = 0; d < sim::kFoliarDiseaseCount; ++d) {
                if (value == sim::kFoliarDiseaseIds[d]) {
                    opt.remove_on_onset = static_cast<sim::FoliarDisease>(d);
                }
            }
            if (!opt.remove_on_onset) {
                std::println(stderr, "--remove-on-onset must be one of anthracnose, botrytis, powdery_mildew, "
                                     "spider_mite, got '{}'", value);
                return std::nullopt;
            }
        } else if (arg == "--ticks") {
            const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), opt.ticks);
            if (ec != std::errc{} || ptr != value.data() + value.size()) {
                std::println(stderr, "--ticks must be a non-negative integer, got '{}'", value);
                return std::nullopt;
            }
            has_ticks = true;
        } else {
            std::println(stderr, "unknown option {}", arg);
            return std::nullopt;
        }
    }
    if (opt.params.empty() || opt.scenario.empty() || !has_ticks) {
        return std::nullopt;
    }
    return opt;
}

// CSV 열: 이름과 값을 같은 함수에서 만들어 순서가 어긋나지 않게 해요.
struct Row {
    std::vector<std::string> names;
    std::vector<std::string> values;
    void add(std::string_view name, double v) {
        names.emplace_back(name);
        values.push_back(std::format("{}", v));
    }
    void add_blank(std::string_view name) {
        names.emplace_back(name);
        values.emplace_back();
    }
    void add_text(std::string_view name, std::string v) {
        names.emplace_back(name);
        values.push_back(std::move(v));
    }
};

// 고사 원인 이름 (RootDamageCause 순서)
constexpr std::array<std::string_view, sim::kRootDamageCauseCount> kDeathCauseNames = {
    "waterlog", "heat", "nitrite", "acid", "fusarium", "phytophthora", "fumigant", "anthracnose"};

// 누적 발병 이벤트 수 (토양 병해 SoilDisease 순서, 지상부 FoliarDisease 순서)
struct OnsetCounts {
    std::array<std::uint64_t, sim::kSoilDiseaseCount> soil{};
    std::array<std::uint64_t, sim::kFoliarDiseaseCount> foliar{};
};

template <typename Fn>
double mean_of(const std::vector<sim::SoilCell>& cells, Fn&& fn) {
    if (cells.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const sim::SoilCell& c : cells) {
        sum += fn(c);
    }
    return sum / static_cast<double>(cells.size());
}

// cum_kill: 이 행까지 누적한 셀 평균 소독 사멸 (무리 순서). 행 값은 틱 시점 상태라 호출자가 이어서 쌓아요.
Row build_row(const sim::World& world, bool include_hash, const sim::MicrobeIndices& cum_kill,
              const OnsetCounts& onsets) {
    const sim::Snapshot s = world.snapshot();
    const sim::CropParams& cp = world.params().crop;
    Row row;
    row.add("tick", static_cast<double>(s.tick));
    row.add("elapsed_days", static_cast<double>(s.time.elapsed_days));
    row.add("day_of_year", s.time.day_of_year);
    row.add("hour", s.time.hour);
    row.add("outside_air_temp_degC", s.weather.outside_air_temp_degC);
    row.add("solar_radiation_W_per_m2", s.weather.solar_radiation_W_per_m2);
    row.add("outside_rh_percent", s.weather.outside_rh_percent);
    row.add("pending_commands", static_cast<double>(s.pending_command_count));
    row.add("house_air_temp_degC", s.house_air_temp_degC);
    // M4 하우스
    row.add("house_air_temp_mean_degC", s.house.air_temp_mean_degC);
    row.add("house_co2_ppm", s.house.co2_ppm);
    row.add("outside_co2_ppm", s.house.outside_co2_ppm);
    row.add("heating_energy_MJ_per_m2", s.house.heating_energy_MJ_per_m2);
    row.add("heating_power_W_per_m2", s.house.heating_power_W_per_m2);
    row.add("cover_layers", s.house.cover_layers);
    row.add("windows_open_count", s.house.windows_open_count());
    row.add("windows_open_fraction_sum", s.house.windows_open_fraction_sum());
    row.add("heater_on", s.house.heater_on ? 1.0 : 0.0);
    row.add("inside_solar_W_per_m2", s.house.inside_solar_W_per_m2);
    row.add("vent_path_index", s.house.vent_path_index);
    // M5b 하우스 습도 (house-humidity 2장): 상태, 마지막 틱 파생값, 수증기 장부(지면 1 m²당 mm)
    row.add("house_vapor_g_per_m3", s.house.vapor_g_per_m3);
    row.add("house_rh_index", s.house.rh_index);
    row.add("house_rh_mean_index", s.house.rh_mean_index);
    row.add("airflow_index", s.house.airflow_index);
    row.add("cover_inner_temp_degC", s.house.cover_inner_temp_degC);
    row.add("vapor_source_mm", s.house.vapor_source_mm);
    row.add("water_condensed_mm", s.house.water_condensed_mm);
    row.add("vapor_vented_mm", s.house.vapor_vented_mm);
    row.add("vapor_deposited_mm", s.house.vapor_deposited_mm);
    // M5b 점적·팬, 지상부 병해충 하우스 값 (foliar-disease 2장)
    row.add("drip_installed", s.house.drip_installed ? 1.0 : 0.0);
    row.add("fan_on", s.house.fan_on ? 1.0 : 0.0);
    row.add("fan_hours", s.house.fan_hours);
    row.add("drip_installations", s.house.drip_installations);
    row.add("botrytis_spore_index", s.house.botrytis_spore_index);
    row.add("powdery_mildew_spore_index", s.house.powdery_mildew_spore_index);
    row.add("anthracnose_sporulation_index", s.house.anthracnose_sporulation_index);
    row.add("house_spider_mite_mean_index", s.house.spider_mite_mean_index);
    row.add("inside_solar_mean_W_per_m2", s.house.inside_solar_mean_W_per_m2);
    row.add("overhead_irrigated_fraction", mean_of(s.soil, [](const sim::SoilCell& c) {
                return c.overhead_irrigated_this_tick ? 1.0 : 0.0;
            }));

#define SOIL_COL(field) row.add(#field, mean_of(s.soil, [](const sim::SoilCell& c) { return c.field; }))
    SOIL_COL(water_mm);
    SOIL_COL(drained_mm_this_tick);
    SOIL_COL(soil_temp_degC);
    SOIL_COL(deep_soil_temp_degC);  // M5a 프로토타입 3차: 깊은 흙 온도 (하우스 평균)
    SOIL_COL(labile_C_g_per_m2);
    SOIL_COL(labile_N_g_per_m2);
    SOIL_COL(stable_C_g_per_m2);
    SOIL_COL(stable_N_g_per_m2);
    SOIL_COL(nh4_N_g_per_m2);
    SOIL_COL(no2_N_g_per_m2);
    SOIL_COL(no3_N_g_per_m2);
    SOIL_COL(ph);
    SOIL_COL(compaction_index);
    SOIL_COL(aeration_index);
    SOIL_COL(k_g_per_m2);
    SOIL_COL(ca_g_per_m2);
    SOIL_COL(mg_g_per_m2);
    SOIL_COL(p_g_per_m2);
    SOIL_COL(s_g_per_m2);
    SOIL_COL(lime_undissolved_eq_per_m2);
    SOIL_COL(ec_dS_per_m);
    // M3
    SOIL_COL(cec_mineral_eq_per_m2);
    SOIL_COL(cec_eq_per_m2);
    SOIL_COL(exchange_free_site_index);
    SOIL_COL(nh4_solution_N_g_per_m2);
    SOIL_COL(k_solution_g_per_m2);
    SOIL_COL(ca_solution_g_per_m2);
    SOIL_COL(mg_solution_g_per_m2);
    SOIL_COL(mn_solution_g_per_m2);
    SOIL_COL(mn_g_per_m2);
    SOIL_COL(mn_oxide_g_per_m2);
    SOIL_COL(p_fixed_g_per_m2);
#undef SOIL_COL
    row.add("p_total_g_per_m2",
            mean_of(s.soil, [](const sim::SoilCell& c) { return c.p_g_per_m2 + c.p_fixed_g_per_m2; }));
    row.add("nh4_exchangeable_N_g_per_m2",
            mean_of(s.soil, [](const sim::SoilCell& c) { return c.nh4_N_g_per_m2 - c.nh4_solution_N_g_per_m2; }));
    row.add("k_exchangeable_g_per_m2",
            mean_of(s.soil, [](const sim::SoilCell& c) { return c.k_g_per_m2 - c.k_solution_g_per_m2; }));
    row.add("ca_exchangeable_g_per_m2",
            mean_of(s.soil, [](const sim::SoilCell& c) { return c.ca_g_per_m2 - c.ca_solution_g_per_m2; }));
    row.add("mg_exchangeable_g_per_m2",
            mean_of(s.soil, [](const sim::SoilCell& c) { return c.mg_g_per_m2 - c.mg_solution_g_per_m2; }));
    row.add("mn_exchangeable_g_per_m2",
            mean_of(s.soil, [](const sim::SoilCell& c) { return c.mn_g_per_m2 - c.mn_solution_g_per_m2; }));

    // M5a 토양 미생물·소독 (하우스 평균)
    for (std::size_t g = 0; g < sim::kMicrobeGroupCount; ++g) {
        row.add(std::format("{}_index", sim::kMicrobeGroupIds[g]),
                mean_of(s.soil, [g](const sim::SoilCell& c) { return c.microbe_index[g]; }));
    }
    const auto& disinfectants = world.params().disinfectants;
    for (std::size_t d = 0; d < disinfectants.size(); ++d) {
        row.add(std::format("fumigant_residue_{}_index", disinfectants[d].id),
                mean_of(s.soil, [d](const sim::SoilCell& c) {
                    return d < c.fumigant_residue_index.size() ? c.fumigant_residue_index[d] : 0.0;
                }));
    }
    row.add("mulched_fraction",
            mean_of(s.soil, [](const sim::SoilCell& c) { return c.mulch_def_index != sim::kNoMulch ? 1.0 : 0.0; }));
    {
        // 모든 셀이 같은 멀칭이면 그 id, 섞여 있으면 mixed
        std::int32_t first = s.soil.empty() ? sim::kNoMulch : s.soil.front().mulch_def_index;
        bool uniform = true;
        for (const sim::SoilCell& c : s.soil) {
            uniform = uniform && c.mulch_def_index == first;
        }
        const auto& mulches = world.params().mulches;
        std::string id = "mixed";
        if (uniform) {
            id = (first >= 0 && static_cast<std::size_t>(first) < mulches.size()) ? mulches[first].id : "none";
        }
        row.add_text("mulch_id", std::move(id));
    }
    for (std::size_t g = 0; g < sim::kMicrobeGroupCount; ++g) {
        row.add(std::format("disinfection_kill_{}_index", sim::kMicrobeGroupIds[g]),
                mean_of(s.soil, [g](const sim::SoilCell& c) { return c.disinfection_kill_index[g]; }));
    }
    for (std::size_t g = 0; g < sim::kMicrobeGroupCount; ++g) {
        row.add(std::format("disinfection_kill_cum_{}_index", sim::kMicrobeGroupIds[g]), cum_kill[g]);
    }

    // 작물 (작물이 있는 타일 평균; 없으면 빈 칸)
    std::vector<const sim::Crop*> present;
    std::size_t alive = 0;
    for (const sim::Crop& c : s.crops) {
        if (c.present) {
            present.push_back(&c);
            alive += c.alive ? 1 : 0;
        }
    }
    const bool has_crop = !present.empty();
    const double n_crop = static_cast<double>(present.size());
    auto crop_col = [&](std::string_view name, auto&& fn) {
        if (!has_crop) {
            row.add_blank(name);
            return;
        }
        double sum = 0.0;
        for (const sim::Crop* c : present) {
            sum += fn(*c);
        }
        row.add(name, sum / n_crop);
    };
    if (has_crop) {
        row.add("crop_alive_fraction", static_cast<double>(alive) / n_crop);
    } else {
        row.add_blank("crop_alive_fraction");
    }
    // M5b: 작물이 있는 칸 수(RemovePlant로 줄어요), 살아 있는 칸 수
    row.add("crop_present_count", static_cast<double>(present.size()));
    row.add("crop_alive_count", static_cast<double>(alive));
#define CROP_COL(field) crop_col(#field, [](const sim::Crop& c) { return c.field; })
#define STATUS_COL(name, field) \
    crop_col(name, [&](const sim::Crop& c) { return sim::process::nutrient_status(c, cp).field; })
    CROP_COL(leaf_g_per_m2);
    CROP_COL(root_g_per_m2);
    CROP_COL(root_health);
    CROP_COL(last_growth_g_per_m2);
    STATUS_COL("sufficiency_N", sufficiency_N);
    STATUS_COL("sufficiency_P", sufficiency_P);
    STATUS_COL("sufficiency_K", sufficiency_K);
    STATUS_COL("sufficiency_Ca", sufficiency_Ca);
    STATUS_COL("sufficiency_Mg", sufficiency_Mg);
    crop_col("leaf_area_index", [&](const sim::Crop& c) { return c.leaf_g_per_m2 * cp.specific_leaf_area_m2_per_g; });
    // M3
    STATUS_COL("sufficiency_Mn", sufficiency_Mn);
    STATUS_COL("excess_index_Mn", excess_Mn);
    STATUS_COL("plant_Mn_ratio", plant_Mn_ratio);
    STATUS_COL("excess_index_K", excess_K);
    STATUS_COL("excess_index_Ca", excess_Ca);
    CROP_COL(plant_N_g_per_m2);
    CROP_COL(plant_P_g_per_m2);
    CROP_COL(plant_K_g_per_m2);
    CROP_COL(plant_Ca_g_per_m2);
    CROP_COL(plant_Mg_g_per_m2);
    CROP_COL(plant_Mn_g_per_m2);
    CROP_COL(antagonism_Ca_factor);
    CROP_COL(antagonism_Mg_factor);
    // M4
    CROP_COL(crown_g_per_m2);
    crop_col("fruit_g_per_m2", [](const sim::Crop& c) { return c.fruit_g_per_m2(); });
    crop_col("fruit_count", [](const sim::Crop& c) {
        double n = 0.0;
        for (const sim::Fruit& f : c.fruits) {
            n += f.set ? 1.0 : 0.0;
        }
        return n;
    });
    crop_col("flowers_pending", [](const sim::Crop& c) {
        double n = 0.0;
        for (const sim::Fruit& f : c.fruits) {
            n += f.set ? 0.0 : 1.0;
        }
        return n;
    });
    crop_col("flowers_in_unopened_trusses", [](const sim::Crop& c) {
        double n = 0.0;
        for (const sim::Truss& t : c.trusses) {
            n += t.opened ? 0.0 : t.flowers_per_m2;
        }
        return n;
    });
    crop_col("trusses_initiated", [](const sim::Crop& c) { return static_cast<double>(c.trusses.size()); });
    crop_col("trusses_opened", [](const sim::Crop& c) {
        double n = 0.0;
        for (const sim::Truss& t : c.trusses) {
            n += t.opened ? 1.0 : 0.0;
        }
        return n;
    });
    CROP_COL(establishment_index);
    CROP_COL(induction_index);
    CROP_COL(last_supply_ratio);
    CROP_COL(size_satisfaction_mean);
    CROP_COL(reserve_g_per_m2);
    CROP_COL(rgr_mean_per_day);
    // M5a 토양 병해 감염 지수 (SoilDisease 순서)
    for (std::size_t d = 0; d < sim::kSoilDiseaseCount; ++d) {
        crop_col(std::format("infection_{}_index", sim::kSoilDiseaseIds[d]),
                 [d](const sim::Crop& c) { return c.infection_index[d]; });
    }
    CROP_COL(disease_susceptibility_factor);  // M5a 프로토타입 3차: 포기 감수성 (작물 평균)
    // M5b 잎 젖음 (house-humidity 4.3, 작물 평균)
    CROP_COL(leaf_wetness_index);
    CROP_COL(wet_run_hours);
    CROP_COL(canopy_rh_index);
    // M5b 지상부 병해충 (작물 평균, 플레이어 비공개 진짜 값)
    CROP_COL(infection_anthracnose_index);
    CROP_COL(infection_botrytis_index);
    CROP_COL(infection_powdery_mildew_index);
    CROP_COL(spider_mite_index);
    CROP_COL(spider_mite_mean_index);
    CROP_COL(predator_mite_index);
    CROP_COL(soil_mineral_N_mean_g_per_m2);
    CROP_COL(last_anthracnose_increase_index);
    CROP_COL(last_botrytis_increase_index);
    CROP_COL(last_powdery_mildew_increase_index);
    CROP_COL(last_mite_predation_index);
    CROP_COL(last_fruit_rot_count);
    crop_col("last_gross_g_per_m2", [](const sim::Crop& c) { return c.last_gross_g_per_m2; });
    crop_col("last_respiration_g_per_m2", [](const sim::Crop& c) { return c.last_respiration_g_per_m2; });
    STATUS_COL("excess_index_N", excess_N);
    // 체내 N / 요구량 (하우스 평균) — 충족도(<=1) + 과잉 지수
    crop_col("plant_N_ratio", [&](const sim::Crop& c) {
        const sim::process::NutrientStatus st = sim::process::nutrient_status(c, cp);
        return st.sufficiency_N + st.excess_N;
    });
    row.add("uptake_temp_factor", mean_of(s.soil, [&](const sim::SoilCell& c) { return cp.uptake_temp_curve(c.soil_temp_degC); }));
    // 수확 누적 (작물이 있는 타일 평균)
    crop_col("harvest_fresh_g_per_m2", [](const sim::Crop& c) { return c.harvest.harvested_fresh_g; });
    crop_col("harvest_count", [](const sim::Crop& c) { return static_cast<double>(c.harvest.harvested_fruits); });
    crop_col("harvest_deformed_count",
             [](const sim::Crop& c) { return static_cast<double>(c.harvest.harvested_deformed); });
    crop_col("harvest_deformed_fresh_g_per_m2",
             [](const sim::Crop& c) { return c.harvest.harvested_deformed_fresh_g; });
    {
        // 하우스 전체 정상과 가중 평균 (생체중 가중)
        double fresh_sum = 0.0;
        double brix_sum = 0.0;
        double firmness_sum = 0.0;
        for (const sim::Crop* c : present) {
            fresh_sum += c->harvest.harvested_fresh_g;
            brix_sum += c->harvest.harvested_brix_weighted;
            firmness_sum += c->harvest.harvested_firmness_weighted;
        }
        double day_sum = 0.0, night_sum = 0.0, ktemp_sum = 0.0;
        for (const sim::Crop* c : present) {
            day_sum += c->harvest.harvested_day_score_weighted;
            night_sum += c->harvest.harvested_night_score_weighted;
            ktemp_sum += c->harvest.harvested_temp_score_weighted;
        }
        if (fresh_sum > 0.0) {
            row.add("harvest_mean_brix", brix_sum / fresh_sum);
            row.add("harvest_mean_firmness", firmness_sum / fresh_sum);
            row.add("harvest_mean_day_score", day_sum / fresh_sum);
            row.add("harvest_mean_night_score", night_sum / fresh_sum);
            row.add("harvest_mean_temp_score", ktemp_sum / fresh_sum);
        } else {
            for (const char* n : {"harvest_mean_brix", "harvest_mean_firmness", "harvest_mean_day_score",
                                  "harvest_mean_night_score", "harvest_mean_temp_score"}) {
                row.add_blank(n);
            }
        }
    }
    {
        // M5b: 하우스 전체 수확 합계 (모든 칸, RemovePlant로 비운 칸의 기록 포함)
        double total_fresh = 0.0;
        double total_count = 0.0;
        double total_deformed = 0.0;
        for (const sim::Crop& c : s.crops) {
            total_fresh += c.harvest.harvested_fresh_g;
            total_count += static_cast<double>(c.harvest.harvested_fruits);
            total_deformed += static_cast<double>(c.harvest.harvested_deformed);
        }
        row.add("harvest_total_fresh_g", total_fresh);
        row.add("harvest_total_count", total_count);
        row.add("harvest_total_deformed_count", total_deformed);
    }
    for (std::size_t d = 0; d < sim::kSoilDiseaseCount; ++d) {
        row.add(std::format("onset_{}_count", sim::kSoilDiseaseIds[d]), static_cast<double>(onsets.soil[d]));
    }
    for (std::size_t d = 0; d < sim::kFoliarDiseaseCount; ++d) {
        row.add(std::format("onset_{}_count", sim::kFoliarDiseaseIds[d]), static_cast<double>(onsets.foliar[d]));
    }
    crop_col("aborted_count", [](const sim::Crop& c) { return static_cast<double>(c.harvest.aborted_fruits); });
    crop_col("thinned_count", [](const sim::Crop& c) { return static_cast<double>(c.harvest.thinned_fruits); });
    const std::size_t size_class_count = world.params().fruit.size_class_bounds_g.size() + 1;
    for (std::size_t i = 0; i < size_class_count; ++i) {
        crop_col(std::format("harvest_size_class_{}", i), [i](const sim::Crop& c) {
            const auto& counts = c.harvest.harvested_size_class_counts;
            return i < counts.size() ? static_cast<double>(counts[i]) : 0.0;
        });
    }
    {
        bool any = false;
        std::uint64_t first = 0;
        for (const sim::Crop* c : present) {
            if (c->harvest.has_first_harvest && (!any || c->harvest.first_harvest_tick < first)) {
                any = true;
                first = c->harvest.first_harvest_tick;
            }
        }
        if (any) {
            row.add("first_harvest_tick", static_cast<double>(first));
        } else {
            row.add_blank("first_harvest_tick");
        }
    }
#undef CROP_COL
#undef STATUS_COL
    constexpr std::array<std::string_view, sim::kStressFactorCount> kStressNames = {
        "stress_hours_water", "stress_hours_salt", "stress_hours_N", "stress_hours_P", "stress_hours_K",
        "stress_hours_Ca", "stress_hours_Mg", "stress_hours_root", "stress_hours_Mn_deficiency",
        "stress_hours_Mn_toxicity"};
    for (std::size_t i = 0; i < sim::kStressFactorCount; ++i) {
        crop_col(kStressNames[i], [i](const sim::Crop& c) { return c.stress_hours[i]; });
    }

    const sim::Ledger& l = s.ledger;
#define LEDGER_COL(field) row.add("ledger_" #field, l.field)
    LEDGER_COL(water_in_mm);
    LEDGER_COL(water_evaporated_mm);
    LEDGER_COL(water_drained_mm);
    LEDGER_COL(N_in_g);
    LEDGER_COL(N_leached_g);
    LEDGER_COL(N_gas_g);
    LEDGER_COL(C_in_g);
    LEDGER_COL(C_respired_g);
    LEDGER_COL(water_transpired_mm);
    LEDGER_COL(N_uptake_g);
    LEDGER_COL(P_in_g);
    LEDGER_COL(P_leached_g);
    LEDGER_COL(P_uptake_g);
    LEDGER_COL(K_in_g);
    LEDGER_COL(K_leached_g);
    LEDGER_COL(K_uptake_g);
    LEDGER_COL(Ca_in_g);
    LEDGER_COL(Ca_leached_g);
    LEDGER_COL(Ca_uptake_g);
    LEDGER_COL(Mg_in_g);
    LEDGER_COL(Mg_leached_g);
    LEDGER_COL(Mg_uptake_g);
    LEDGER_COL(S_in_g);
    LEDGER_COL(S_leached_g);
    LEDGER_COL(H_eq_net);
    LEDGER_COL(C_root_litter_g);
    LEDGER_COL(Mn_in_g);
    LEDGER_COL(Mn_leached_g);
    LEDGER_COL(Mn_uptake_g);
    // M4
    LEDGER_COL(N_harvested_g);
    LEDGER_COL(P_harvested_g);
    LEDGER_COL(K_harvested_g);
    LEDGER_COL(Ca_harvested_g);
    LEDGER_COL(Mg_harvested_g);
    LEDGER_COL(Mn_harvested_g);
    LEDGER_COL(C_leaf_litter_g);
    // M5a
    LEDGER_COL(N_flush_g);
    LEDGER_COL(Mn_flush_g);
    LEDGER_COL(inoculant_applied_g);
    // M5b
    LEDGER_COL(N_removed_g);
    LEDGER_COL(P_removed_g);
    LEDGER_COL(K_removed_g);
    LEDGER_COL(Ca_removed_g);
    LEDGER_COL(Mg_removed_g);
    LEDGER_COL(Mn_removed_g);
    LEDGER_COL(dry_matter_removed_g);
    row.add("ledger_plants_removed", static_cast<double>(l.plants_removed));
    row.add("ledger_fruit_rot_count", static_cast<double>(l.fruit_rot_count));
    LEDGER_COL(fruit_rot_fresh_g);
    row.add("ledger_harvested_mildew", static_cast<double>(l.harvested_mildew));
    LEDGER_COL(harvested_mildew_fresh_g);
    LEDGER_COL(predators_released_per_m2);
#undef LEDGER_COL
    for (std::size_t d = 0; d < disinfectants.size(); ++d) {
        row.add(std::format("ledger_disinfectant_applied_{}_g", disinfectants[d].id),
                d < l.disinfectant_applied_g.size() ? l.disinfectant_applied_g[d] : 0.0);
    }
    if (include_hash) {
        row.add_text("state_hash", std::format("0x{:016x}", world.state_hash()));
    } else {
        row.add_blank("state_hash");
    }
    return row;
}

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        out += parts[i];
    }
    return out;
}

// 이번 틱(마지막으로 처리한 틱)의 셀 평균 소독 사멸을 누적에 더해요. 틱 0 행은 처리한 틱이 없어서 0이에요.
void accumulate_kill(const sim::World& world, sim::MicrobeIndices& cum_kill) {
    const sim::Snapshot s = world.snapshot();
    for (std::size_t g = 0; g < sim::kMicrobeGroupCount; ++g) {
        cum_kill[g] += mean_of(s.soil, [g](const sim::SoilCell& c) { return c.disinfection_kill_index[g]; });
    }
}

void write_header(std::ofstream& csv, const sim::World& world) {
    std::println(csv, "{}", join(build_row(world, true, sim::MicrobeIndices{}, OnsetCounts{}).names));
}

void write_row(std::ofstream& csv, const sim::World& world, bool include_hash, const sim::MicrobeIndices& cum_kill,
               const OnsetCounts& onsets) {
    std::println(csv, "{}", join(build_row(world, include_hash, cum_kill, onsets).values));
}

}  // namespace

int main(int argc, char** argv) {
    const std::optional<Options> opt = parse_args(argc, argv);
    if (!opt) {
        std::println(stderr, "{}", kUsage);
        return kExitUsage;
    }

    auto params = simdata::load_params(opt->params);
    if (!params) {
        std::println(stderr, "error: {}", params.error().message);
        return kExitRuntimeError;
    }
    auto bundle = simdata::load_scenario_bundle(opt->scenario);
    if (!bundle) {
        std::println(stderr, "error: {}", bundle.error().message);
        return kExitRuntimeError;
    }
    const std::vector<simdata::ScheduledCommand> schedule = std::move(bundle->schedule);
    auto world = sim::World::create(std::move(*params), std::move(bundle->scenario));
    if (!world) {
        std::println(stderr, "error: {}", world.error().message);
        return kExitRuntimeError;
    }

    std::ofstream csv;
    if (opt->out) {
        if (opt->out->has_parent_path()) {
            std::error_code ec;
            fs::create_directories(opt->out->parent_path(), ec);
            if (ec) {
                std::println(stderr, "error: cannot create output directory: {}", ec.message());
                return kExitRuntimeError;
            }
        }
        csv.open(*opt->out, std::ios::binary | std::ios::trunc);
        if (!csv) {
            std::println(stderr, "error: cannot open output file");
            return kExitRuntimeError;
        }
        write_header(csv, *world);
    }

    const auto started = std::chrono::steady_clock::now();
    sim::MicrobeIndices cum_kill{};
    OnsetCounts onsets;
    if (csv.is_open()) {
        write_row(csv, *world, !opt->no_hash, cum_kill, onsets);
    }
    const int bed_cols = world->scenario().bed_cols;
    std::size_t next_command = 0;
    std::uint64_t command_applied_events = 0;
    std::uint64_t nitrite_events = 0;
    std::uint64_t plant_died_events = 0;
    std::uint64_t fruit_harvested_events = 0;
    std::uint64_t disease_onset_events = 0;
    std::array<std::uint64_t, kDeathCauseNames.size()> died_by_cause{};
    std::vector<sim::Command> removals;  // M5b --remove-on-onset: 다음 틱 시작 전에 넣을 RemovePlant
    for (std::uint32_t t = 0; t < opt->ticks; ++t) {
        for (sim::Command& removal : removals) {
            // 같은 칸이 이미 비었으면(앞선 명령) enqueue가 거부할 수 있어요. 그때는 건너뛰어요
            (void)world->enqueue(std::move(removal));
        }
        removals.clear();
        // 일정표는 tick 오름차순이라 현재 틱의 명령만 앞에서부터 꺼내면 돼요.
        while (next_command < schedule.size() && schedule[next_command].tick == t) {
            const auto seq = world->enqueue(schedule[next_command].command);
            if (!seq) {
                std::println(stderr, "error: schedule entry for tick {} rejected: {}", t, seq.error().message);
                return kExitRuntimeError;
            }
            ++next_command;
        }
        world->step(1);
        for (const sim::Event& event : world->events()) {
            if (std::holds_alternative<sim::CommandAppliedEvent>(event.payload)) {
                ++command_applied_events;
            } else if (std::holds_alternative<sim::NitriteThresholdExceededEvent>(event.payload)) {
                ++nitrite_events;
            } else if (const auto* died = std::get_if<sim::PlantDiedEvent>(&event.payload)) {
                ++plant_died_events;
                const auto cause = static_cast<std::size_t>(died->cause);
                if (cause < died_by_cause.size()) {
                    ++died_by_cause[cause];
                }
            } else if (const auto* onset = std::get_if<sim::DiseaseOnsetEvent>(&event.payload)) {
                ++disease_onset_events;
                ++onsets.soil[static_cast<std::size_t>(onset->disease)];
            } else if (const auto* foliar = std::get_if<sim::FoliarDiseaseOnsetEvent>(&event.payload)) {
                ++disease_onset_events;
                ++onsets.foliar[static_cast<std::size_t>(foliar->disease)];
                if (opt->remove_on_onset && foliar->disease == *opt->remove_on_onset) {
                    const int row = static_cast<int>(foliar->tile_index) / bed_cols;
                    const int col = static_cast<int>(foliar->tile_index) % bed_cols;
                    removals.emplace_back(sim::RemovePlantCommand{
                        .area = sim::TileArea{.row = row, .col = col, .rows = 1, .cols = 1}});
                }
            } else if (std::holds_alternative<sim::FruitHarvestedEvent>(event.payload)) {
                ++fruit_harvested_events;
            }
        }
        if (csv.is_open()) {
            accumulate_kill(*world, cum_kill);
            write_row(csv, *world, !opt->no_hash, cum_kill, onsets);
        }
    }
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count();

    if (csv.is_open()) {
        csv.flush();
        if (!csv) {
            std::println(stderr, "error: failed while writing output file");
            return kExitRuntimeError;
        }
    }

    std::println("scenario={} ticks={} elapsed_ms={}", world->scenario().name, opt->ticks, elapsed_ms);
    std::println(
        "events command_applied={} nitrite_threshold_exceeded={} plant_died={} fruit_harvested={} disease_onset={}",
        command_applied_events, nitrite_events, plant_died_events, fruit_harvested_events, disease_onset_events);
    {
        std::string causes;
        for (std::size_t i = 0; i < died_by_cause.size(); ++i) {
            causes += std::format("{}{}={}", i == 0 ? "" : " ", kDeathCauseNames[i], died_by_cause[i]);
        }
        std::println("plant_died_by_cause {}", causes);
        std::string onset_text;
        for (std::size_t i = 0; i < sim::kSoilDiseaseCount; ++i) {
            onset_text += std::format("{}{}={}", i == 0 ? "" : " ", sim::kSoilDiseaseIds[i], onsets.soil[i]);
        }
        for (std::size_t i = 0; i < sim::kFoliarDiseaseCount; ++i) {
            onset_text += std::format(" {}={}", sim::kFoliarDiseaseIds[i], onsets.foliar[i]);
        }
        std::println("disease_onset_by_disease {}", onset_text);
    }

    // 하우스 전체 수확 합계 (모든 타일)
    double harvest_fresh_g = 0.0;
    double harvest_deformed_fresh_g = 0.0;
    std::uint64_t harvest_count = 0;
    std::uint64_t harvest_deformed_count = 0;
    for (const sim::Crop& crop : world->snapshot().crops) {
        harvest_fresh_g += crop.harvest.harvested_fresh_g;
        harvest_deformed_fresh_g += crop.harvest.harvested_deformed_fresh_g;
        harvest_count += crop.harvest.harvested_fruits;
        harvest_deformed_count += crop.harvest.harvested_deformed;
    }
    std::println("harvest_total fresh_g={} count={} deformed_fresh_g={} deformed_count={}", harvest_fresh_g,
                 harvest_count, harvest_deformed_fresh_g, harvest_deformed_count);
    {
        // M5b 지상부 병해충 장부 요약
        const sim::Ledger& l = world->snapshot().ledger;
        std::println("foliar fruit_rot_count={} fruit_rot_fresh_g={} harvested_mildew={} harvested_mildew_fresh_g={} "
                     "plants_removed={} predators_released_per_m2={}",
                     l.fruit_rot_count, l.fruit_rot_fresh_g, l.harvested_mildew, l.harvested_mildew_fresh_g,
                     l.plants_removed, l.predators_released_per_m2);
    }
    {
        // 최종 하우스 평균 미생물 지수 (빠른 확인용)
        const sim::Snapshot fin = world->snapshot();
        std::string line;
        for (std::size_t g = 0; g < sim::kMicrobeGroupCount; ++g) {
            const double m = mean_of(fin.soil, [g](const sim::SoilCell& c) { return c.microbe_index[g]; });
            line += std::format("{}{}={:.4f}", g == 0 ? "" : " ", sim::kMicrobeGroupIds[g], m);
        }
        std::println("final_microbe_index {}", line);
    }
    std::println("final_state_hash=0x{:016x}", world->state_hash());
    return kExitOk;
}
