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
    "[--out <file.csv>]";

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

// CSV 열: 이름과 값을 같은 함수에서 만들어 순서가 어긋나지 않게 해요 (M3 프로토타입).
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

Row build_row(const sim::World& world) {
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

#define SOIL_COL(field) row.add(#field, mean_of(s.soil, [](const sim::SoilCell& c) { return c.field; }))
    SOIL_COL(water_mm);
    SOIL_COL(drained_mm_this_tick);
    SOIL_COL(soil_temp_degC);
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
    SOIL_COL(mn_oxidized_this_tick_g_per_m2);
    SOIL_COL(mn_reduced_this_tick_g_per_m2);
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
    row.add("mn_retention_factor", mean_of(s.soil, [&](const sim::SoilCell& c) {
                return sim::process::manganese_retention(c, world.params().manganese);
            }));

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
    STATUS_COL("plant_Mn_ratio", ratio_Mn);
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
    CROP_COL(diag_demand_K_g_per_m2);
    CROP_COL(diag_capacity_K_g_per_m2);
    CROP_COL(diag_demand_Ca_g_per_m2);
    CROP_COL(diag_capacity_Ca_g_per_m2);
    CROP_COL(diag_demand_Mg_g_per_m2);
    CROP_COL(diag_capacity_Mg_g_per_m2);
    CROP_COL(diag_demand_Mn_g_per_m2);
    CROP_COL(diag_capacity_Mn_g_per_m2);
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
#undef LEDGER_COL
    row.add_text("state_hash", std::format("0x{:016x}", world.state_hash()));
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

void write_header(std::ofstream& csv, const sim::World& world) { std::println(csv, "{}", join(build_row(world).names)); }

void write_row(std::ofstream& csv, const sim::World& world) { std::println(csv, "{}", join(build_row(world).values)); }

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
