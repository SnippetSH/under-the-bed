#include <format>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "sim/calendar.hpp"
#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {

std::string weather_csv(std::size_t rows, std::string_view newline = "\n") {
    std::string text = "# comment line\n";
    text += simdata::kWeatherCsvHeader;
    text += newline;
    for (std::size_t i = 0; i < rows; ++i) {
        text += std::format("{},{},{},{},{}{}", i / sim::kHoursPerDay + 1, i % sim::kHoursPerDay,
                            -2.5, 100.0, 60.0, newline);
    }
    return text;
}

constexpr std::string_view kValidScenario = R"(
schema_version = 1
[scenario]
name = "t"
seed = 7
start_day_of_year = 100
weather_csv = "w.csv"
[house]
bed_rows = 2
bed_cols = 5
)";

}  // namespace

TEST_CASE("Params TOML loads strictly", "[unit][loader]") {
    SECTION("valid") {
        auto p = simdata::parse_params_toml("schema_version = 1\n", "p.toml");
        REQUIRE(p.has_value());
        CHECK(p->schema_version == 1);
    }
    SECTION("unknown key is rejected") {
        auto p = simdata::parse_params_toml("schema_version = 1\nfield_capacty_mm = 3\n", "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("unknown key 'field_capacty_mm'"));
    }
    SECTION("missing key is rejected") {
        auto p = simdata::parse_params_toml("", "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("missing required key 'schema_version'"));
    }
    SECTION("wrong type is rejected") {
        auto p = simdata::parse_params_toml("schema_version = \"1\"\n", "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("must be a integer"));
    }
    SECTION("syntax error reports the location") {
        auto p = simdata::parse_params_toml("schema_version = \n", "p.toml");
        REQUIRE_FALSE(p.has_value());
        CHECK_THAT(p.error().message, ContainsSubstring("p.toml:1:"));
    }
}

TEST_CASE("Scenario TOML loads strictly", "[unit][loader]") {
    SECTION("valid") {
        auto doc = simdata::parse_scenario_toml(kValidScenario, "s.toml");
        REQUIRE(doc.has_value());
        CHECK(doc->scenario.name == "t");
        CHECK(doc->scenario.seed == 7);
        CHECK(doc->scenario.start_day_of_year == 100);
        CHECK(doc->scenario.bed_rows == 2);
        CHECK(doc->scenario.bed_cols == 5);
        CHECK(doc->weather_csv == "w.csv");
    }
    SECTION("unknown nested key names its table") {
        std::string text(kValidScenario);
        text += "bed_depth_cm = 30\n";  // [house] 테이블 안에 들어가요
        auto doc = simdata::parse_scenario_toml(text, "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK_THAT(doc.error().message, ContainsSubstring("unknown key 'house.bed_depth_cm'"));
    }
    SECTION("missing table") {
        auto doc = simdata::parse_scenario_toml("schema_version = 1\n[scenario]\n", "s.toml");
        CHECK_FALSE(doc.has_value());
    }
    SECTION("negative seed") {
        std::string text(kValidScenario);
        text.replace(text.find("seed = 7"), 8, "seed = -1");
        CHECK_FALSE(simdata::parse_scenario_toml(text, "s.toml").has_value());
    }
    SECTION("unsupported schema version") {
        std::string text(kValidScenario);
        text.replace(text.find("schema_version = 1"), 18, "schema_version = 2");
        auto doc = simdata::parse_scenario_toml(text, "s.toml");
        REQUIRE_FALSE(doc.has_value());
        CHECK(doc.error().code == sim::ErrorCode::UnsupportedVersion);
    }
}

TEST_CASE("Weather CSV loads strictly", "[unit][loader]") {
    SECTION("valid, with comments and CRLF line endings") {
        auto w = simdata::parse_weather_csv(weather_csv(sim::kHoursPerYear, "\r\n"), "w.csv");
        REQUIRE(w.has_value());
        CHECK(w->size() == sim::kHoursPerYear);
        CHECK(w->front().outside_air_temp_degC == -2.5);
    }
    SECTION("missing a row") {
        auto w = simdata::parse_weather_csv(weather_csv(sim::kHoursPerYear - 1), "w.csv");
        REQUIRE_FALSE(w.has_value());
        CHECK_THAT(w.error().message, ContainsSubstring("expected 8760 data rows"));
    }
    SECTION("extra row") {
        std::string text = weather_csv(sim::kHoursPerYear);
        text += "1,0,0,0,0\n";
        CHECK_FALSE(simdata::parse_weather_csv(text, "w.csv").has_value());
    }
    SECTION("wrong header") {
        std::string text = weather_csv(sim::kHoursPerYear);
        text.replace(text.find("hour,"), 5, "hr,");
        CHECK_FALSE(simdata::parse_weather_csv(text, "w.csv").has_value());
    }
    SECTION("rows out of order") {
        std::string text = weather_csv(sim::kHoursPerYear);
        const auto pos = text.find("\n1,1,");
        text.replace(pos, 5, "\n1,2,");
        auto w = simdata::parse_weather_csv(text, "w.csv");
        REQUIRE_FALSE(w.has_value());
        CHECK_THAT(w.error().message, ContainsSubstring("expected day_of_year=1 hour=1"));
    }
    SECTION("malformed number") {
        std::string text = weather_csv(sim::kHoursPerYear);
        text.replace(text.find("-2.5"), 4, "abc");
        CHECK_FALSE(simdata::parse_weather_csv(text, "w.csv").has_value());
    }
}

TEST_CASE("Repository data files load and build a world", "[unit][loader][data]") {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    auto scenario = simdata::load_scenario(test::repo_path("data/scenarios/empty.toml"));
    REQUIRE(scenario.has_value());
    CHECK(scenario->weather_hourly.size() == sim::kHoursPerYear);
    CHECK(sim::World::create(*params, std::move(*scenario)).has_value());
}

TEST_CASE("Missing files report an I/O error", "[unit][loader]") {
    auto p = simdata::load_params(test::repo_path("data/does-not-exist.toml"));
    REQUIRE_FALSE(p.has_value());
    CHECK(p.error().code == sim::ErrorCode::Io);
}
