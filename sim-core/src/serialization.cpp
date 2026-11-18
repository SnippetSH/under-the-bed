// 세이브 형식 (리틀 엔디언)
//
//   magic "FGSV" | format_version u32
//   [정적 입력] params, scenario (기상 표 포함)
//   [동적 상태] tick, next_command_seq, pending_commands, tiles
//
// state_hash()는 동적 상태 부분만 해시해요. 이벤트는 저장하지 않아요.

#include <array>
#include <format>
#include <utility>

#include "byte_io.hpp"
#include "sim/calendar.hpp"
#include "sim/hash.hpp"
#include "sim/world.hpp"

namespace sim {

namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'F', 'G', 'S', 'V'};
constexpr std::uint32_t kSaveFormatVersion = 1;

// WeatherHour 하나의 바이트 수 (f64 × 3)
constexpr std::size_t kWeatherHourBytes = 24;
// QueuedCommand 하나의 최소 바이트 수 (seq u64 + variant index u32)
constexpr std::size_t kQueuedCommandMinBytes = 12;

std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

void write_static(detail::ByteWriter& w, const Params& params, const Scenario& scenario) {
    w.i64(params.schema_version);

    w.str(scenario.name);
    w.u64(scenario.seed);
    w.i32(scenario.start_day_of_year);
    w.i32(scenario.bed_rows);
    w.i32(scenario.bed_cols);
    w.u64(scenario.weather_hourly.size());
    for (const WeatherHour& h : scenario.weather_hourly) {
        w.f64(h.outside_air_temp_degC);
        w.f64(h.solar_radiation_W_per_m2);
        w.f64(h.outside_rh_percent);
    }
}

bool read_static(detail::ByteReader& r, Params& params, Scenario& scenario) {
    params.schema_version = r.i64();

    scenario.name = r.str();
    scenario.seed = r.u64();
    scenario.start_day_of_year = r.i32();
    scenario.bed_rows = r.i32();
    scenario.bed_cols = r.i32();
    const std::uint64_t weather_count = r.u64();
    if (!r.ok() || weather_count > r.remaining() / kWeatherHourBytes) {
        return false;
    }
    scenario.weather_hourly.resize(static_cast<std::size_t>(weather_count));
    for (WeatherHour& h : scenario.weather_hourly) {
        h.outside_air_temp_degC = r.f64();
        h.solar_radiation_W_per_m2 = r.f64();
        h.outside_rh_percent = r.f64();
    }
    return r.ok();
}

}  // namespace

void World::write_dynamic(detail::ByteWriter& w) const {
    w.u64(tick_);
    w.u64(next_command_seq_);

    w.u64(pending_commands_.size());
    for (const QueuedCommand& queued : pending_commands_) {
        w.u64(queued.seq);
        w.u32(static_cast<std::uint32_t>(queued.command.index()));
        // 명령별 필드. NoopCommand는 필드가 없어요.
        std::visit([](const NoopCommand&) {}, queued.command);
    }

    w.u64(tiles_.size());
    // Tile/SoilCell 필드는 M1부터 여기에 행 우선 순서로 기록해요.
}

std::expected<void, Error> World::read_dynamic(detail::ByteReader& r) {
    tick_ = r.u64();
    next_command_seq_ = r.u64();

    const std::uint64_t pending_count = r.u64();
    if (!r.ok() || pending_count > r.remaining() / kQueuedCommandMinBytes) {
        return fail(ErrorCode::Truncated, "save data ends inside the command queue");
    }
    pending_commands_.clear();
    pending_commands_.reserve(static_cast<std::size_t>(pending_count));
    for (std::uint64_t i = 0; i < pending_count; ++i) {
        const std::uint64_t seq = r.u64();
        const std::uint32_t index = r.u32();
        Command command;
        switch (index) {
            case 0:
                command = NoopCommand{};
                break;
            default:
                return fail(ErrorCode::InvalidData,
                            std::format("unknown command type index {}", index));
        }
        const bool ordered = pending_commands_.empty() || seq > pending_commands_.back().seq;
        if (!ordered || seq >= next_command_seq_) {
            return fail(ErrorCode::InvalidData, "command sequence numbers are inconsistent");
        }
        pending_commands_.push_back(QueuedCommand{seq, command});
    }

    const std::uint64_t tile_count = r.u64();
    if (r.ok() && tile_count != tiles_.size()) {
        return fail(ErrorCode::InvalidData,
                    std::format("tile count {} does not match grid size {}", tile_count,
                                tiles_.size()));
    }

    if (!r.ok()) {
        return fail(ErrorCode::Truncated, "save data ends inside the dynamic state");
    }
    return {};
}

std::vector<std::byte> World::serialize() const {
    detail::ByteWriter w;
    for (const std::uint8_t c : kMagic) {
        w.u8(c);
    }
    w.u32(kSaveFormatVersion);
    write_static(w, params_, scenario_);
    write_dynamic(w);
    return w.take();
}

std::expected<World, Error> World::deserialize(std::span<const std::byte> bytes) {
    detail::ByteReader r(bytes);

    for (const std::uint8_t expected : kMagic) {
        if (r.u8() != expected) {
            return fail(ErrorCode::InvalidData, "not a save file (magic mismatch)");
        }
    }
    const std::uint32_t version = r.u32();
    if (!r.ok()) {
        return fail(ErrorCode::Truncated, "save data ends inside the header");
    }
    if (version != kSaveFormatVersion) {
        return fail(ErrorCode::UnsupportedVersion,
                    std::format("save format version {} is not supported (expected {})", version,
                                kSaveFormatVersion));
    }

    Params params;
    Scenario scenario;
    if (!read_static(r, params, scenario)) {
        return fail(ErrorCode::Truncated, "save data ends inside the static inputs");
    }

    auto world = World::create(std::move(params), std::move(scenario));
    if (!world) {
        return std::unexpected(std::move(world.error()));
    }
    if (auto loaded = world->read_dynamic(r); !loaded) {
        return std::unexpected(std::move(loaded.error()));
    }
    if (!r.at_end()) {
        return fail(ErrorCode::InvalidData,
                    std::format("{} unexpected trailing bytes in save data", r.remaining()));
    }
    return world;
}

std::uint64_t World::state_hash() const {
    detail::ByteWriter w;
    write_dynamic(w);
    return fnv1a64(w.bytes());
}

}  // namespace sim
