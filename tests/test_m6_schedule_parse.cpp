// M6a: sim-data parse_schedule_toml (일정표만 담은 TOML 글자). 엔진 래퍼가 명령 사전 변환과
// 세이브 묶음의 일정표 복원에 써요 (m6-overview 4.3, 17.3). 시나리오 [[schedule]]과 같은 규칙인지 봐요.

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

std::string read_text(const std::string& relative) {
    std::ifstream in(test::repo_path(relative), std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

// 시나리오 파일의 [[schedule]] 부분만 잘라 읽으면 parse_scenario_toml의 일정표와 같아요 (반복 펼치기·정렬 포함).
TEST_CASE("Schedule-only TOML parses like the schedule of a scenario file", "[unit][loader][m6]") {
    const std::string text = read_text("data/scenarios/m4/season_standard.toml");
    const auto doc = simdata::parse_scenario_toml(text, "season_standard.toml");
    REQUIRE(doc.has_value());
    const std::size_t first = text.find("[[schedule]]");
    REQUIRE(first != std::string::npos);
    const auto schedule = simdata::parse_schedule_toml(text.substr(first), doc->scenario.bed_rows,
                                                       doc->scenario.bed_cols, "schedule part");
    REQUIRE(schedule.has_value());
    REQUIRE(schedule->size() == doc->schedule.size());
    CHECK(schedule->size() > 1000);  // 반복이 펼쳐졌어요
    for (std::size_t i = 0; i < schedule->size(); ++i) {
        INFO("entry " << i);
        CHECK((*schedule)[i].tick == doc->schedule[i].tick);
        CHECK((*schedule)[i].command == doc->schedule[i].command);
    }
}

// 영역을 빼면 하우스 전체, 빈 글자는 빈 일정표, 모르는 키·다른 최상위 키는 InvalidData예요.
TEST_CASE("Schedule-only TOML applies area defaults and rejects unknown keys", "[unit][loader][m6]") {
    SECTION("whole grid default and order") {
        const auto s = simdata::parse_schedule_toml(
            "[[schedule]]\ntick = 5\naction = \"till\"\n\n[[schedule]]\ntick = 2\naction = \"irrigate\"\n"
            "area = [1, 2, 1, 3]\namount_mm = 4\n",
            4, 20, "inline");
        REQUIRE(s.has_value());
        REQUIRE(s->size() == 2);
        CHECK((*s)[0].tick == 2);
        const auto* irrigate = std::get_if<sim::IrrigateCommand>(&(*s)[0].command);
        REQUIRE(irrigate != nullptr);
        CHECK(irrigate->area == sim::TileArea{1, 2, 1, 3});
        CHECK(irrigate->amount_mm == 4.0);
        const auto* till = std::get_if<sim::TillCommand>(&(*s)[1].command);
        REQUIRE(till != nullptr);
        CHECK(till->area == sim::whole_grid(4, 20));
    }
    SECTION("empty text") {
        const auto s = simdata::parse_schedule_toml("", 4, 20, "inline");
        REQUIRE(s.has_value());
        CHECK(s->empty());
    }
    SECTION("unknown entry key") {
        const auto s = simdata::parse_schedule_toml("[[schedule]]\ntick = 0\naction = \"till\"\nfoo = 1\n", 4, 20,
                                                    "inline");
        REQUIRE_FALSE(s.has_value());
        CHECK(s.error().code == sim::ErrorCode::InvalidData);
    }
    SECTION("other top-level key") {
        const auto s = simdata::parse_schedule_toml("schema_version = 7\n", 4, 20, "inline");
        REQUIRE_FALSE(s.has_value());
        CHECK(s.error().code == sim::ErrorCode::InvalidData);
    }
}
