#pragma once

#include <cstddef>
#include <cstdint>

namespace sim {

// 1틱 = 게임 내 1시간 (계획서 3장, 고정값)
// 게임 달력은 윤년 없는 365일 (docs/spec/core-api.md "시간과 달력")
inline constexpr int kHoursPerDay = 24;
inline constexpr int kDaysPerYear = 365;
inline constexpr std::size_t kHoursPerYear =
    static_cast<std::size_t>(kHoursPerDay) * static_cast<std::size_t>(kDaysPerYear);

struct CalendarTime {
    std::uint64_t elapsed_days;  // 시작 이후 경과 일수
    int day_of_year;             // 1..365
    int hour;                    // 0..23

    bool operator==(const CalendarTime&) const = default;
};

// start_day_of_year(1..365) 0시가 틱 0이에요.
[[nodiscard]] constexpr CalendarTime calendar_at(int start_day_of_year, std::uint64_t tick) {
    constexpr auto hours_per_day = static_cast<std::uint64_t>(kHoursPerDay);
    constexpr auto days_per_year = static_cast<std::uint64_t>(kDaysPerYear);
    const std::uint64_t days = tick / hours_per_day;
    const auto start_offset = static_cast<std::uint64_t>(start_day_of_year - 1);
    return CalendarTime{
        .elapsed_days = days,
        .day_of_year = static_cast<int>((start_offset + days) % days_per_year) + 1,
        .hour = static_cast<int>(tick % hours_per_day),
    };
}

// 시간별 기상 표의 인덱스. 표는 (day_of_year, hour) 오름차순이에요.
[[nodiscard]] constexpr std::size_t weather_index(int day_of_year, int hour) {
    return static_cast<std::size_t>(day_of_year - 1) * static_cast<std::size_t>(kHoursPerDay) +
           static_cast<std::size_t>(hour);
}

}  // namespace sim
