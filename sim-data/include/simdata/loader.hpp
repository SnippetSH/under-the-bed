#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "sim/command.hpp"
#include "sim/error.hpp"
#include "sim/params.hpp"
#include "sim/scenario.hpp"

namespace simdata {

// 시나리오 TOML 파일의 스키마 버전 (M5b: 7 — [house] drip_installed, [microbes] anthracnose, [foliar] 필수)
inline constexpr std::int64_t kScenarioFileSchemaVersion = 7;

// params TOML 파일의 스키마 버전 (M5b: 7). 코어의 sim::kParamsSchemaVersion과 같아야 해요.
inline constexpr std::int64_t kParamsFileSchemaVersion = 7;

// 기상 CSV 머리글 (순서 고정)
inline constexpr std::string_view kWeatherCsvHeader =
    "day_of_year,hour,outside_air_temp_degC,solar_radiation_W_per_m2,outside_rh_percent";

// [[schedule]]을 펼친 항목: 이 틱을 처리하기 직전에 enqueue할 명령
struct ScheduledCommand {
    std::uint64_t tick;
    sim::Command command;
};

// 시나리오 TOML을 파싱한 결과. 기상 표는 아직 비어 있고, 경로만 들어 있어요.
struct ScenarioDocument {
    sim::Scenario scenario;
    std::string weather_csv;  // 시나리오 파일 기준 상대 경로 (UTF-8)
    // 반복을 펼치고 tick 오름차순으로 안정 정렬한 명령 목록 (같은 틱은 파일 순서)
    std::vector<ScheduledCommand> schedule;
};

// 월드 초기 조건과 명령 일정표 (기상 표까지 채워진 상태)
struct ScenarioBundle {
    sim::Scenario scenario;
    std::vector<ScheduledCommand> schedule;
};

// 문자열 파서. source_name은 오류 메시지에만 써요.
// 모르는 키, 빠진 키, 타입 불일치는 모두 오류예요 (ADR-0002 엄격한 로딩).
[[nodiscard]] std::expected<sim::Params, sim::Error> parse_params_toml(std::string_view text,
                                                                       std::string_view source_name);
[[nodiscard]] std::expected<ScenarioDocument, sim::Error> parse_scenario_toml(
    std::string_view text, std::string_view source_name);
[[nodiscard]] std::expected<std::vector<sim::WeatherHour>, sim::Error> parse_weather_csv(
    std::string_view text, std::string_view source_name);
// 명령 일정표만 담은 TOML 글자(최상위 키는 [[schedule]] 배열 하나뿐)를 파싱해요. 항목 규칙(키, 영역 기본값,
// 반복 펼치기, 안정 정렬)은 parse_scenario_toml의 [[schedule]]과 같은 코드예요. 엔진 래퍼(sim-godot)가
// 명령 사전 변환과 세이브 묶음의 일정표 복원에 써요 (M6 프로토타입, m6-overview 4.3).
[[nodiscard]] std::expected<std::vector<ScheduledCommand>, sim::Error> parse_schedule_toml(
    std::string_view text, int bed_rows, int bed_cols, std::string_view source_name);

// 파일 로더
[[nodiscard]] std::expected<sim::Params, sim::Error> load_params(const std::filesystem::path& path);
// 일정표까지 함께 돌려줘요.
[[nodiscard]] std::expected<ScenarioBundle, sim::Error> load_scenario_bundle(
    const std::filesystem::path& path);
// 시나리오 TOML을 읽고, 그 안의 weather_csv 경로(시나리오 파일 기준)로 기상 표까지 채워요.
[[nodiscard]] std::expected<sim::Scenario, sim::Error> load_scenario(
    const std::filesystem::path& path);

}  // namespace simdata
