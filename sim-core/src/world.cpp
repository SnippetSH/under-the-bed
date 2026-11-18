#include "sim/world.hpp"

#include <cmath>
#include <format>
#include <utility>

#include "sim/calendar.hpp"

namespace sim {

namespace {

std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

std::expected<void, Error> validate(const Params& params, const Scenario& scenario) {
    if (params.schema_version != kParamsSchemaVersion) {
        return fail(ErrorCode::UnsupportedVersion,
                    std::format("params schema_version {} is not supported (expected {})",
                                params.schema_version, kParamsSchemaVersion));
    }
    if (scenario.start_day_of_year < 1 || scenario.start_day_of_year > kDaysPerYear) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("start_day_of_year must be 1..{}, got {}", kDaysPerYear,
                                scenario.start_day_of_year));
    }
    if (scenario.bed_rows <= 0 || scenario.bed_cols <= 0) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("bed grid must be at least 1x1, got {}x{}", scenario.bed_rows,
                                scenario.bed_cols));
    }
    if (scenario.weather_hourly.size() != kHoursPerYear) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("weather_hourly must have {} entries, got {}", kHoursPerYear,
                                scenario.weather_hourly.size()));
    }
    for (std::size_t i = 0; i < scenario.weather_hourly.size(); ++i) {
        const WeatherHour& w = scenario.weather_hourly[i];
        if (!std::isfinite(w.outside_air_temp_degC) || !std::isfinite(w.solar_radiation_W_per_m2) ||
            !std::isfinite(w.outside_rh_percent)) {
            return fail(ErrorCode::InvalidArgument,
                        std::format("weather_hourly[{}] has a non-finite value", i));
        }
        if (w.solar_radiation_W_per_m2 < 0.0) {
            return fail(ErrorCode::InvalidArgument,
                        std::format("weather_hourly[{}] solar radiation is negative", i));
        }
        if (w.outside_rh_percent < 0.0 || w.outside_rh_percent > 100.0) {
            return fail(ErrorCode::InvalidArgument,
                        std::format("weather_hourly[{}] relative humidity is outside 0..100", i));
        }
    }
    return {};
}

}  // namespace

World::World(Params params, Scenario scenario)
    : params_(std::move(params)),
      scenario_(std::move(scenario)),
      tiles_(static_cast<std::size_t>(scenario_.bed_rows) *
             static_cast<std::size_t>(scenario_.bed_cols)) {}

std::expected<World, Error> World::create(Params params, Scenario scenario) {
    if (auto valid = validate(params, scenario); !valid) {
        return std::unexpected(std::move(valid.error()));
    }
    return World(std::move(params), std::move(scenario));
}

std::uint64_t World::enqueue(Command command) {
    const std::uint64_t seq = next_command_seq_++;
    pending_commands_.push_back(QueuedCommand{seq, std::move(command)});
    return seq;
}

void World::step(std::uint32_t ticks) {
    events_.clear();
    for (std::uint32_t i = 0; i < ticks; ++i) {
        tick_once();
    }
}

// 틱 처리 순서는 계획서 4장 "틱 처리 순서"를 따라요. M0에는 명령 적용만 있어요.
void World::tick_once() {
    for (const QueuedCommand& queued : pending_commands_) {
        std::visit([this](const auto& command) { apply_command(command); }, queued.command);
        events_.push_back(Event{tick_, CommandAppliedEvent{queued.seq}});
    }
    pending_commands_.clear();

    ++tick_;
}

void World::apply_command(const NoopCommand& /*command*/) {}

Snapshot World::snapshot() const {
    const CalendarTime time = calendar_at(scenario_.start_day_of_year, tick_);
    return Snapshot{
        .tick = tick_,
        .time = time,
        .weather = scenario_.weather_hourly[weather_index(time.day_of_year, time.hour)],
        .bed_rows = scenario_.bed_rows,
        .bed_cols = scenario_.bed_cols,
        .pending_command_count = pending_commands_.size(),
    };
}

}  // namespace sim
