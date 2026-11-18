// 세이브 형식 (리틀 엔디언)
//
//   magic "FGSV" | format_version u32
//   [정적 입력] params, scenario (토양 초기값, 기상 표 포함)
//   [동적 상태] tick, next_command_seq, pending_commands, tiles(토양 셀), house_air_temp, ledger
//
// state_hash()는 동적 상태 부분만 해시해요. 이벤트는 저장하지 않아요.
// 필드를 추가하면 kSaveFormatVersion을 올려요 (M0 = 1, M1 = 2).

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
constexpr std::uint32_t kSaveFormatVersion = 2;

constexpr std::size_t kWeatherHourBytes = 24;       // f64 × 3
constexpr std::size_t kCurvePointBytes = 16;        // f64 × 2
constexpr std::size_t kQueuedCommandMinBytes = 12;  // seq u64 + variant index u32
constexpr std::size_t kMinStringBytes = 8;          // 길이 u64

std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

// ---- 공통 조각 ----

void write_curve(detail::ByteWriter& w, const PiecewiseLinear& c) {
    w.u64(c.points.size());
    for (const auto& p : c.points) {
        w.f64(p[0]);
        w.f64(p[1]);
    }
}

void read_curve(detail::ByteReader& r, PiecewiseLinear& c) {
    const std::uint64_t n = r.u64();
    if (!r.ok() || n > r.remaining() / kCurvePointBytes) {
        r.fail();
        return;
    }
    c.points.resize(static_cast<std::size_t>(n));
    for (auto& p : c.points) {
        p[0] = r.f64();
        p[1] = r.f64();
    }
}

void write_area(detail::ByteWriter& w, const TileArea& a) {
    w.i32(a.row);
    w.i32(a.col);
    w.i32(a.rows);
    w.i32(a.cols);
}

TileArea read_area(detail::ByteReader& r) {
    TileArea a;
    a.row = r.i32();
    a.col = r.i32();
    a.rows = r.i32();
    a.cols = r.i32();
    return a;
}

void write_cell(detail::ByteWriter& w, const SoilCell& c) {
    w.f64(c.field_capacity_mm);
    w.f64(c.water_mm);
    w.f64(c.drained_mm_this_tick);
    w.f64(c.soil_temp_degC);
    w.f64(c.labile_C_g_per_m2);
    w.f64(c.labile_N_g_per_m2);
    w.f64(c.stable_C_g_per_m2);
    w.f64(c.stable_N_g_per_m2);
    w.f64(c.nh4_N_g_per_m2);
    w.f64(c.no2_N_g_per_m2);
    w.f64(c.no3_N_g_per_m2);
    w.f64(c.ph);
    w.u8(c.nitrite_alarm ? 1 : 0);
}

void read_cell(detail::ByteReader& r, SoilCell& c) {
    c.field_capacity_mm = r.f64();
    c.water_mm = r.f64();
    c.drained_mm_this_tick = r.f64();
    c.soil_temp_degC = r.f64();
    c.labile_C_g_per_m2 = r.f64();
    c.labile_N_g_per_m2 = r.f64();
    c.stable_C_g_per_m2 = r.f64();
    c.stable_N_g_per_m2 = r.f64();
    c.nh4_N_g_per_m2 = r.f64();
    c.no2_N_g_per_m2 = r.f64();
    c.no3_N_g_per_m2 = r.f64();
    c.ph = r.f64();
    const std::uint8_t alarm = r.u8();
    if (alarm > 1) {
        r.fail();
    }
    c.nitrite_alarm = alarm == 1;
}

// ---- 정적 입력 ----

void write_params(detail::ByteWriter& w, const Params& p) {
    w.i64(p.schema_version);

    w.f64(p.house.base_offset_degC);
    w.f64(p.house.solar_gain_degC_per_W_per_m2);
    w.f64(p.house.solar_transmittance_factor);

    w.f64(p.soil_water.evap_mm_per_hour_per_W_per_m2);
    w.f64(p.soil_water.evap_base_mm_per_hour);
    write_curve(w, p.soil_water.evap_moisture_curve);
    w.f64(p.soil_water.drainage_rate_per_hour);

    w.f64(p.soil_temperature.relax_rate_per_hour);
    w.f64(p.soil_temperature.heat_degC_per_g_C);

    w.f64(p.organic_matter.labile_decay_rate_per_hour);
    w.f64(p.organic_matter.stable_decay_rate_per_hour);
    w.f64(p.organic_matter.humification_fraction);
    w.f64(p.organic_matter.humus_C_to_N_ratio);
    write_curve(w, p.organic_matter.temp_curve);
    write_curve(w, p.organic_matter.moisture_curve);

    const NitrificationParams& n = p.nitrification;
    w.f64(n.step1_rate_per_hour);
    w.f64(n.step2_rate_per_hour);
    write_curve(w, n.temp_curve);
    write_curve(w, n.moisture_curve);
    write_curve(w, n.step1_ph_curve);
    write_curve(w, n.step2_ph_curve);
    write_curve(w, n.ammonia_fraction_ph_curve);
    write_curve(w, n.free_ammonia_inhibition_curve);
    w.f64(n.nitrite_toxicity_threshold_g_N_per_m2);
    w.f64(n.nitrite_alarm_reset_fraction);

    w.f64(p.leaching.nh4_solution_fraction);

    w.u64(p.composts.size());
    for (const CompostDef& c : p.composts) {
        w.str(c.id);
        w.str(c.name);
        w.f64(c.C_g_per_kg);
        w.f64(c.N_g_per_kg);
        w.f64(c.labile_C_fraction);
        w.f64(c.labile_N_fraction);
    }
    w.u64(p.fertilizers.size());
    for (const FertilizerDef& f : p.fertilizers) {
        w.str(f.id);
        w.str(f.name);
        w.f64(f.nh4_N_g_per_kg);
        w.f64(f.no3_N_g_per_kg);
    }
}

void read_params(detail::ByteReader& r, Params& p) {
    p.schema_version = r.i64();

    p.house.base_offset_degC = r.f64();
    p.house.solar_gain_degC_per_W_per_m2 = r.f64();
    p.house.solar_transmittance_factor = r.f64();

    p.soil_water.evap_mm_per_hour_per_W_per_m2 = r.f64();
    p.soil_water.evap_base_mm_per_hour = r.f64();
    read_curve(r, p.soil_water.evap_moisture_curve);
    p.soil_water.drainage_rate_per_hour = r.f64();

    p.soil_temperature.relax_rate_per_hour = r.f64();
    p.soil_temperature.heat_degC_per_g_C = r.f64();

    p.organic_matter.labile_decay_rate_per_hour = r.f64();
    p.organic_matter.stable_decay_rate_per_hour = r.f64();
    p.organic_matter.humification_fraction = r.f64();
    p.organic_matter.humus_C_to_N_ratio = r.f64();
    read_curve(r, p.organic_matter.temp_curve);
    read_curve(r, p.organic_matter.moisture_curve);

    NitrificationParams& n = p.nitrification;
    n.step1_rate_per_hour = r.f64();
    n.step2_rate_per_hour = r.f64();
    read_curve(r, n.temp_curve);
    read_curve(r, n.moisture_curve);
    read_curve(r, n.step1_ph_curve);
    read_curve(r, n.step2_ph_curve);
    read_curve(r, n.ammonia_fraction_ph_curve);
    read_curve(r, n.free_ammonia_inhibition_curve);
    n.nitrite_toxicity_threshold_g_N_per_m2 = r.f64();
    n.nitrite_alarm_reset_fraction = r.f64();

    p.leaching.nh4_solution_fraction = r.f64();

    const std::uint64_t compost_count = r.u64();
    if (!r.ok() || compost_count > r.remaining() / (2 * kMinStringBytes)) {
        r.fail();
        return;
    }
    p.composts.resize(static_cast<std::size_t>(compost_count));
    for (CompostDef& c : p.composts) {
        c.id = r.str();
        c.name = r.str();
        c.C_g_per_kg = r.f64();
        c.N_g_per_kg = r.f64();
        c.labile_C_fraction = r.f64();
        c.labile_N_fraction = r.f64();
    }
    const std::uint64_t fertilizer_count = r.u64();
    if (!r.ok() || fertilizer_count > r.remaining() / (2 * kMinStringBytes)) {
        r.fail();
        return;
    }
    p.fertilizers.resize(static_cast<std::size_t>(fertilizer_count));
    for (FertilizerDef& f : p.fertilizers) {
        f.id = r.str();
        f.name = r.str();
        f.nh4_N_g_per_kg = r.f64();
        f.no3_N_g_per_kg = r.f64();
    }
}

void write_scenario(detail::ByteWriter& w, const Scenario& s) {
    w.str(s.name);
    w.u64(s.seed);
    w.i32(s.start_day_of_year);
    w.i32(s.bed_rows);
    w.i32(s.bed_cols);

    const SoilInit& soil = s.soil;
    w.f64(soil.field_capacity_mm);
    w.f64(soil.water_mm);
    w.f64(soil.soil_temp_degC);
    w.f64(soil.ph);
    w.f64(soil.nh4_N_g_per_m2);
    w.f64(soil.no2_N_g_per_m2);
    w.f64(soil.no3_N_g_per_m2);
    w.f64(soil.labile_C_g_per_m2);
    w.f64(soil.labile_N_g_per_m2);
    w.f64(soil.stable_C_g_per_m2);
    w.f64(soil.stable_N_g_per_m2);

    w.u64(s.weather_hourly.size());
    for (const WeatherHour& h : s.weather_hourly) {
        w.f64(h.outside_air_temp_degC);
        w.f64(h.solar_radiation_W_per_m2);
        w.f64(h.outside_rh_percent);
    }
}

void read_scenario(detail::ByteReader& r, Scenario& s) {
    s.name = r.str();
    s.seed = r.u64();
    s.start_day_of_year = r.i32();
    s.bed_rows = r.i32();
    s.bed_cols = r.i32();

    SoilInit& soil = s.soil;
    soil.field_capacity_mm = r.f64();
    soil.water_mm = r.f64();
    soil.soil_temp_degC = r.f64();
    soil.ph = r.f64();
    soil.nh4_N_g_per_m2 = r.f64();
    soil.no2_N_g_per_m2 = r.f64();
    soil.no3_N_g_per_m2 = r.f64();
    soil.labile_C_g_per_m2 = r.f64();
    soil.labile_N_g_per_m2 = r.f64();
    soil.stable_C_g_per_m2 = r.f64();
    soil.stable_N_g_per_m2 = r.f64();

    const std::uint64_t weather_count = r.u64();
    if (!r.ok() || weather_count > r.remaining() / kWeatherHourBytes) {
        r.fail();
        return;
    }
    s.weather_hourly.resize(static_cast<std::size_t>(weather_count));
    for (WeatherHour& h : s.weather_hourly) {
        h.outside_air_temp_degC = r.f64();
        h.solar_radiation_W_per_m2 = r.f64();
        h.outside_rh_percent = r.f64();
    }
}

// ---- 명령 ----

void write_command(detail::ByteWriter& w, const Command& command) {
    w.u32(static_cast<std::uint32_t>(command.index()));
    struct Visitor {
        detail::ByteWriter& w;
        void operator()(const NoopCommand&) const {}
        void operator()(const IrrigateCommand& c) const {
            write_area(w, c.area);
            w.f64(c.amount_mm);
        }
        void operator()(const AddCompostCommand& c) const {
            write_area(w, c.area);
            w.str(c.compost_id);
            w.f64(c.kg_per_m2);
        }
        void operator()(const AddFertilizerCommand& c) const {
            write_area(w, c.area);
            w.str(c.fertilizer_id);
            w.f64(c.g_per_m2);
        }
    };
    std::visit(Visitor{w}, command);
}

std::expected<Command, Error> read_command(detail::ByteReader& r) {
    const std::uint32_t index = r.u32();
    switch (index) {
        case 0:
            return NoopCommand{};
        case 1: {
            IrrigateCommand c;
            c.area = read_area(r);
            c.amount_mm = r.f64();
            return c;
        }
        case 2: {
            AddCompostCommand c;
            c.area = read_area(r);
            c.compost_id = r.str();
            c.kg_per_m2 = r.f64();
            return c;
        }
        case 3: {
            AddFertilizerCommand c;
            c.area = read_area(r);
            c.fertilizer_id = r.str();
            c.g_per_m2 = r.f64();
            return c;
        }
        default:
            if (!r.ok()) {
                return fail(ErrorCode::Truncated, "save data ends inside the command queue");
            }
            return fail(ErrorCode::InvalidData, std::format("unknown command type index {}", index));
    }
}

}  // namespace

void World::write_dynamic(detail::ByteWriter& w) const {
    w.u64(tick_);
    w.u64(next_command_seq_);

    w.u64(pending_commands_.size());
    for (const QueuedCommand& queued : pending_commands_) {
        w.u64(queued.seq);
        write_command(w, queued.command);
    }

    w.u64(tiles_.size());
    for (const Tile& tile : tiles_) {
        write_cell(w, tile.soil);
    }

    w.f64(house_air_temp_degC_);

    w.f64(ledger_.water_in_mm);
    w.f64(ledger_.water_evaporated_mm);
    w.f64(ledger_.water_drained_mm);
    w.f64(ledger_.N_in_g);
    w.f64(ledger_.N_leached_g);
    w.f64(ledger_.N_gas_g);
    w.f64(ledger_.C_in_g);
    w.f64(ledger_.C_respired_g);
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
        auto command = read_command(r);
        if (!command) {
            return std::unexpected(std::move(command.error()));
        }
        if (!r.ok()) {
            return fail(ErrorCode::Truncated, "save data ends inside the command queue");
        }
        const bool ordered = pending_commands_.empty() || seq > pending_commands_.back().seq;
        if (!ordered || seq >= next_command_seq_) {
            return fail(ErrorCode::InvalidData, "command sequence numbers are inconsistent");
        }
        if (auto valid = validate_command(*command); !valid) {
            return fail(ErrorCode::InvalidData, std::format("saved command is invalid: {}", valid.error().message));
        }
        pending_commands_.push_back(QueuedCommand{seq, std::move(*command)});
    }

    const std::uint64_t tile_count = r.u64();
    if (r.ok() && tile_count != tiles_.size()) {
        return fail(ErrorCode::InvalidData,
                    std::format("tile count {} does not match grid size {}", tile_count, tiles_.size()));
    }
    for (Tile& tile : tiles_) {
        read_cell(r, tile.soil);
    }

    house_air_temp_degC_ = r.f64();

    ledger_.water_in_mm = r.f64();
    ledger_.water_evaporated_mm = r.f64();
    ledger_.water_drained_mm = r.f64();
    ledger_.N_in_g = r.f64();
    ledger_.N_leached_g = r.f64();
    ledger_.N_gas_g = r.f64();
    ledger_.C_in_g = r.f64();
    ledger_.C_respired_g = r.f64();

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
    write_params(w, params_);
    write_scenario(w, scenario_);
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
                    std::format("save format version {} is not supported (expected {})", version, kSaveFormatVersion));
    }

    Params params;
    Scenario scenario;
    read_params(r, params);
    if (r.ok()) {
        read_scenario(r, scenario);
    }
    if (!r.ok()) {
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
        return fail(ErrorCode::InvalidData, std::format("{} unexpected trailing bytes in save data", r.remaining()));
    }
    return world;
}

std::uint64_t World::state_hash() const {
    detail::ByteWriter w;
    write_dynamic(w);
    return fnv1a64(w.bytes());
}

}  // namespace sim
