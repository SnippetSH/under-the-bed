// sim-cli — 헤드리스 실행기
//
//   sim-cli --params data/params.toml --scenario data/scenarios/empty.toml --ticks 8760
//           [--out runs/empty.csv]
//
// CSV는 틱 0(초기 상태)부터 틱 N까지 N+1행이에요. 행의 값은 해당 틱 시점의 상태예요.
// 표준 출력 마지막 줄에 최종 상태 해시를 찍어요.

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <print>
#include <string>
#include <string_view>

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
    "outside_rh_percent,pending_commands,state_hash";

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

void write_row(std::ofstream& csv, const sim::World& world) {
    const sim::Snapshot s = world.snapshot();
    // {}는 최단 왕복 표현이라 같은 값이면 항상 같은 문자열이 나와요.
    std::println(csv, "{},{},{},{},{},{},{},{},0x{:016x}", s.tick, s.time.elapsed_days,
                 s.time.day_of_year, s.time.hour, s.weather.outside_air_temp_degC,
                 s.weather.solar_radiation_W_per_m2, s.weather.outside_rh_percent,
                 s.pending_command_count, world.state_hash());
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
    auto scenario = simdata::load_scenario(opt->scenario);
    if (!scenario) {
        std::println(stderr, "error: {}", scenario.error().message);
        return kExitRuntimeError;
    }
    auto world = sim::World::create(std::move(*params), std::move(*scenario));
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
    for (std::uint32_t t = 0; t < opt->ticks; ++t) {
        world->step(1);
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
    std::println("final_state_hash=0x{:016x}", world->state_hash());
    return kExitOk;
}
