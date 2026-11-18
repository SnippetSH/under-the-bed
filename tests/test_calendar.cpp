#include <catch2/catch_test_macros.hpp>

#include "sim/calendar.hpp"

TEST_CASE("Calendar starts at hour 0 of the start day", "[unit][calendar]") {
    const sim::CalendarTime t = sim::calendar_at(244, 0);
    CHECK(t == sim::CalendarTime{.elapsed_days = 0, .day_of_year = 244, .hour = 0});
}

TEST_CASE("Calendar advances hours, then days", "[unit][calendar]") {
    CHECK(sim::calendar_at(10, 23) == sim::CalendarTime{0, 10, 23});
    CHECK(sim::calendar_at(10, 24) == sim::CalendarTime{1, 11, 0});
    CHECK(sim::calendar_at(10, 24 * 5 + 7) == sim::CalendarTime{5, 15, 7});
}

TEST_CASE("Calendar wraps from day 365 to day 1 (no leap years)", "[unit][calendar]") {
    CHECK(sim::calendar_at(365, 23) == sim::CalendarTime{0, 365, 23});
    CHECK(sim::calendar_at(365, 24) == sim::CalendarTime{1, 1, 0});
    // 정확히 1년 뒤에는 같은 날짜로 돌아와요.
    CHECK(sim::calendar_at(244, sim::kHoursPerYear).day_of_year == 244);
    CHECK(sim::calendar_at(244, sim::kHoursPerYear).elapsed_days == 365);
}

TEST_CASE("Weather index covers the table exactly", "[unit][calendar]") {
    CHECK(sim::weather_index(1, 0) == 0);
    CHECK(sim::weather_index(1, 23) == 23);
    CHECK(sim::weather_index(2, 0) == 24);
    CHECK(sim::weather_index(365, 23) == sim::kHoursPerYear - 1);
}
