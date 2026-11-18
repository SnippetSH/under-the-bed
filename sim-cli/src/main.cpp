// sim-cli — 헤드리스 실행기
//
//   sim-cli --params data/params.toml --scenario data/scenarios/empty.toml --ticks 8760
//           [--out runs/empty.csv]
//
// CSV는 틱 0(초기 상태)부터 틱 N까지 N+1행이에요. 행의 값은 해당 틱 시점의 상태예요.
// 열: 기상·명령 수, house_air_temp_degC, 셀 평균 토양 상태(soil.hpp 필드 이름), 작물 평균(작물이 있는 타일만,
// 작물이 하나도 없으면 빈 칸), 장부 합계(ledger_ 접두사), state_hash 순서예요.
// 명령 일정표([[schedule]])는 해당 틱을 처리하기 직전에 enqueue해요.
// 표준 출력에는 이벤트 종류별 횟수와 최종 상태 해시를 찍어요.

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
    "[--out <file.csv>]";

constexpr std::string_view kCsvHeader =
    "tick,elapsed_days,day_of_year,hour,outside_air_temp_degC,solar_radiation_W_per_m2,"
    "outside_rh_percent,pending_commands,house_air_temp_degC,"
    "water_mm,drained_mm_this_tick,soil_temp_degC,labile_C_g_per_m2,labile_N_g_per_m2,"
    "stable_C_g_per_m2,stable_N_g_per_m2,nh4_N_g_per_m2,no2_N_g_per_m2,no3_N_g_per_m2,ph,"
    "compaction_index,aeration_index,k_g_per_m2,ca_g_per_m2,mg_g_per_m2,p_g_per_m2,s_g_per_m2,"
    "lime_undissolved_eq_per_m2,ec_dS_per_m,"
    "crop_alive_fraction,leaf_g_per_m2,root_g_per_m2,root_health,last_growth_g_per_m2,"
    "sufficiency_N,sufficiency_P,sufficiency_K,sufficiency_Ca,sufficiency_Mg,leaf_area_index,"
    "ledger_water_in_mm,ledger_water_evaporated_mm,ledger_water_drained_mm,"
    "ledger_N_in_g,ledger_N_leached_g,ledger_N_gas_g,ledger_C_in_g,ledger_C_respired_g,"
    "ledger_water_transpired_mm,ledger_N_uptake_g,"
    "ledger_P_in_g,ledger_P_leached_g,ledger_P_uptake_g,"
    "ledger_K_in_g,ledger_K_leached_g,ledger_K_uptake_g,"
    "ledger_Ca_in_g,ledger_Ca_leached_g,ledger_Ca_uptake_g,"
    "ledger_Mg_in_g,ledger_Mg_leached_g,ledger_Mg_uptake_g,"
    "ledger_S_in_g,ledger_S_leached_g,ledger_H_eq_net,ledger_C_root_litter_g,"
    "state_hash";

constexpr std::size_t kCropColumnCount = 11;

struct Options {
    fs::path params;
    fs::path scenario;
    std::optional<fs::path> out;
    std::uint32_t ticks = 0;
};

std::optional<Options> parse_args(int argc, char** argv) {
    Options opt;
    bool has_ticks = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
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

// 셀 평균 토양 상태 (열 순서는 kCsvHeader와 같아요)
struct SoilMeans {
    double water_mm = 0.0;
    double drained_mm_this_tick = 0.0;
    double soil_temp_degC = 0.0;
    double labile_C_g_per_m2 = 0.0;
    double labile_N_g_per_m2 = 0.0;
    double stable_C_g_per_m2 = 0.0;
    double stable_N_g_per_m2 = 0.0;
    double nh4_N_g_per_m2 = 0.0;
    double no2_N_g_per_m2 = 0.0;
    double no3_N_g_per_m2 = 0.0;
    double ph = 0.0;
    double compaction_index = 0.0;
    double aeration_index = 0.0;
    double k_g_per_m2 = 0.0;
    double ca_g_per_m2 = 0.0;
    double mg_g_per_m2 = 0.0;
    double p_g_per_m2 = 0.0;
    double s_g_per_m2 = 0.0;
    double lime_undissolved_eq_per_m2 = 0.0;
    double ec_dS_per_m = 0.0;
};

SoilMeans mean_soil(const std::vector<sim::SoilCell>& cells) {
    SoilMeans m;
    for (const sim::SoilCell& c : cells) {
        m.water_mm += c.water_mm;
        m.drained_mm_this_tick += c.drained_mm_this_tick;
        m.soil_temp_degC += c.soil_temp_degC;
        m.labile_C_g_per_m2 += c.labile_C_g_per_m2;
        m.labile_N_g_per_m2 += c.labile_N_g_per_m2;
        m.stable_C_g_per_m2 += c.stable_C_g_per_m2;
        m.stable_N_g_per_m2 += c.stable_N_g_per_m2;
        m.nh4_N_g_per_m2 += c.nh4_N_g_per_m2;
        m.no2_N_g_per_m2 += c.no2_N_g_per_m2;
        m.no3_N_g_per_m2 += c.no3_N_g_per_m2;
        m.ph += c.ph;
        m.compaction_index += c.compaction_index;
        m.aeration_index += c.aeration_index;
        m.k_g_per_m2 += c.k_g_per_m2;
        m.ca_g_per_m2 += c.ca_g_per_m2;
        m.mg_g_per_m2 += c.mg_g_per_m2;
        m.p_g_per_m2 += c.p_g_per_m2;
        m.s_g_per_m2 += c.s_g_per_m2;
        m.lime_undissolved_eq_per_m2 += c.lime_undissolved_eq_per_m2;
        m.ec_dS_per_m += c.ec_dS_per_m;
    }
    if (!cells.empty()) {
        const double n = static_cast<double>(cells.size());
        m.water_mm /= n;
        m.drained_mm_this_tick /= n;
        m.soil_temp_degC /= n;
        m.labile_C_g_per_m2 /= n;
        m.labile_N_g_per_m2 /= n;
        m.stable_C_g_per_m2 /= n;
        m.stable_N_g_per_m2 /= n;
        m.nh4_N_g_per_m2 /= n;
        m.no2_N_g_per_m2 /= n;
        m.no3_N_g_per_m2 /= n;
        m.ph /= n;
        m.compaction_index /= n;
        m.aeration_index /= n;
        m.k_g_per_m2 /= n;
        m.ca_g_per_m2 /= n;
        m.mg_g_per_m2 /= n;
        m.p_g_per_m2 /= n;
        m.s_g_per_m2 /= n;
        m.lime_undissolved_eq_per_m2 /= n;
        m.ec_dS_per_m /= n;
    }
    return m;
}

// 작물이 있는 타일의 평균 (열 순서는 kCsvHeader와 같아요). 작물이 하나도 없으면 present = false.
// crop_alive_fraction은 작물이 있는 타일 중 살아 있는 비율이고, 나머지는 present 타일 평균이에요.
struct CropMeans {
    bool present = false;
    double alive_fraction = 0.0;
    double leaf_g_per_m2 = 0.0;
    double root_g_per_m2 = 0.0;
    double root_health = 0.0;
    double last_growth_g_per_m2 = 0.0;
    double sufficiency_N = 0.0;
    double sufficiency_P = 0.0;
    double sufficiency_K = 0.0;
    double sufficiency_Ca = 0.0;
    double sufficiency_Mg = 0.0;
    double leaf_area_index = 0.0;
};

CropMeans mean_crops(const std::vector<sim::Crop>& crops, const sim::CropParams& params) {
    CropMeans m;
    std::size_t count = 0;
    std::size_t alive = 0;
    for (const sim::Crop& c : crops) {
        if (!c.present) {
            continue;
        }
        ++count;
        alive += c.alive ? 1 : 0;
        m.leaf_g_per_m2 += c.leaf_g_per_m2;
        m.root_g_per_m2 += c.root_g_per_m2;
        m.root_health += c.root_health;
        m.last_growth_g_per_m2 += c.last_growth_g_per_m2;
        const sim::process::NutrientStatus st = sim::process::nutrient_status(c, params);
        m.sufficiency_N += st.sufficiency_N;
        m.sufficiency_P += st.sufficiency_P;
        m.sufficiency_K += st.sufficiency_K;
        m.sufficiency_Ca += st.sufficiency_Ca;
        m.sufficiency_Mg += st.sufficiency_Mg;
        m.leaf_area_index += c.leaf_g_per_m2 * params.specific_leaf_area_m2_per_g;
    }
    if (count > 0) {
        const double n = static_cast<double>(count);
        m.present = true;
        m.alive_fraction = static_cast<double>(alive) / n;
        m.leaf_g_per_m2 /= n;
        m.root_g_per_m2 /= n;
        m.root_health /= n;
        m.last_growth_g_per_m2 /= n;
        m.sufficiency_N /= n;
        m.sufficiency_P /= n;
        m.sufficiency_K /= n;
        m.sufficiency_Ca /= n;
        m.sufficiency_Mg /= n;
        m.leaf_area_index /= n;
    }
    return m;
}

// 작물 열 11개를 쉼표로 이어 붙인 문자열. 작물이 없으면 빈 칸이에요 (쉼표 개수는 같아요).
std::string crop_columns(const CropMeans& c) {
    if (!c.present) {
        return std::string(kCropColumnCount - 1, ',');
    }
    // {}는 최단 왕복 표현이라 같은 값이면 항상 같은 문자열이 나와요.
    return std::format("{},{},{},{},{},{},{},{},{},{},{}", c.alive_fraction, c.leaf_g_per_m2, c.root_g_per_m2,
                       c.root_health, c.last_growth_g_per_m2, c.sufficiency_N, c.sufficiency_P, c.sufficiency_K,
                       c.sufficiency_Ca, c.sufficiency_Mg, c.leaf_area_index);
}

void write_row(std::ofstream& csv, const sim::World& world) {
    const sim::Snapshot s = world.snapshot();
    const SoilMeans m = mean_soil(s.soil);
    const CropMeans crop = mean_crops(s.crops, world.params().crop);
    const sim::Ledger& l = s.ledger;
    std::println(csv,
                 "{},{},{},{},{},{},{},{},{},"
                 "{},{},{},{},{},{},{},{},{},{},{},"
                 "{},{},{},{},{},{},{},{},{},"
                 "{},"
                 "{},{},{},{},{},{},{},{},"
                 "{},{},"
                 "{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},"
                 "0x{:016x}",
                 s.tick, s.time.elapsed_days, s.time.day_of_year, s.time.hour, s.weather.outside_air_temp_degC,
                 s.weather.solar_radiation_W_per_m2, s.weather.outside_rh_percent, s.pending_command_count,
                 s.house_air_temp_degC, m.water_mm, m.drained_mm_this_tick, m.soil_temp_degC, m.labile_C_g_per_m2,
                 m.labile_N_g_per_m2, m.stable_C_g_per_m2, m.stable_N_g_per_m2, m.nh4_N_g_per_m2, m.no2_N_g_per_m2,
                 m.no3_N_g_per_m2, m.ph, m.compaction_index, m.aeration_index, m.k_g_per_m2, m.ca_g_per_m2,
                 m.mg_g_per_m2, m.p_g_per_m2, m.s_g_per_m2, m.lime_undissolved_eq_per_m2, m.ec_dS_per_m,
                 crop_columns(crop), l.water_in_mm, l.water_evaporated_mm, l.water_drained_mm, l.N_in_g,
                 l.N_leached_g, l.N_gas_g, l.C_in_g, l.C_respired_g, l.water_transpired_mm, l.N_uptake_g, l.P_in_g,
                 l.P_leached_g, l.P_uptake_g, l.K_in_g, l.K_leached_g, l.K_uptake_g, l.Ca_in_g, l.Ca_leached_g,
                 l.Ca_uptake_g, l.Mg_in_g, l.Mg_leached_g, l.Mg_uptake_g, l.S_in_g, l.S_leached_g, l.H_eq_net,
                 l.C_root_litter_g,
                 world.state_hash());
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
        std::println(csv, "{}", kCsvHeader);
    }

    const auto started = std::chrono::steady_clock::now();
    if (csv.is_open()) {
        write_row(csv, *world);
    }
    std::size_t next_command = 0;
    std::uint64_t command_applied_events = 0;
    std::uint64_t nitrite_events = 0;
    std::uint64_t plant_died_events = 0;
    for (std::uint32_t t = 0; t < opt->ticks; ++t) {
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
            } else if (std::holds_alternative<sim::PlantDiedEvent>(event.payload)) {
                ++plant_died_events;
            }
        }
        if (csv.is_open()) {
            write_row(csv, *world);
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
    std::println("events command_applied={} nitrite_threshold_exceeded={} plant_died={}", command_applied_events,
                 nitrite_events, plant_died_events);
    std::println("final_state_hash=0x{:016x}", world->state_hash());
    return kExitOk;
}
