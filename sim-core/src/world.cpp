#include "sim/world.hpp"

#include <cmath>
#include <format>
#include <set>
#include <string>
#include <utility>

#include "sim/calendar.hpp"
#include "sim/processes.hpp"

namespace sim {

namespace {

std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

// ---- 값 검증 도우미 ----

std::expected<void, Error> require(bool ok, std::string_view what) {
    if (!ok) {
        return fail(ErrorCode::InvalidArgument, std::string(what));
    }
    return {};
}

std::expected<void, Error> non_negative(double v, std::string_view name) {
    return require(std::isfinite(v) && v >= 0.0, std::format("{} must be finite and >= 0", name));
}

std::expected<void, Error> fraction(double v, std::string_view name) {
    return require(std::isfinite(v) && v >= 0.0 && v <= 1.0, std::format("{} must be within 0..1", name));
}

std::expected<void, Error> positive(double v, std::string_view name) {
    return require(std::isfinite(v) && v > 0.0, std::format("{} must be finite and > 0", name));
}

std::expected<void, Error> finite(double v, std::string_view name) {
    return require(std::isfinite(v), std::format("{} must be finite", name));
}

#define SIM_CHECK(expr)                                  \
    do {                                                 \
        if (auto sim_check_result = (expr); !sim_check_result) { \
            return std::unexpected(std::move(sim_check_result.error())); \
        }                                                \
    } while (false)

std::expected<void, Error> validate_params(const Params& p) {
    if (p.schema_version != kParamsSchemaVersion) {
        return fail(ErrorCode::UnsupportedVersion,
                    std::format("params schema_version {} is not supported (expected {})", p.schema_version,
                                kParamsSchemaVersion));
    }

    SIM_CHECK(finite(p.house.base_offset_degC, "house.base_offset_degC"));
    SIM_CHECK(non_negative(p.house.solar_gain_degC_per_W_per_m2, "house.solar_gain_degC_per_W_per_m2"));
    SIM_CHECK(fraction(p.house.solar_transmittance_factor, "house.solar_transmittance_factor"));

    SIM_CHECK(non_negative(p.soil_water.evap_mm_per_hour_per_W_per_m2, "soil_water.evap_mm_per_hour_per_W_per_m2"));
    SIM_CHECK(non_negative(p.soil_water.evap_base_mm_per_hour, "soil_water.evap_base_mm_per_hour"));
    SIM_CHECK(validate_curve(p.soil_water.evap_moisture_curve, "soil_water.evap_moisture_curve"));
    SIM_CHECK(non_negative(p.soil_water.drainage_rate_per_hour, "soil_water.drainage_rate_per_hour"));

    SIM_CHECK(non_negative(p.soil_temperature.relax_rate_per_hour, "soil_temperature.relax_rate_per_hour"));
    SIM_CHECK(non_negative(p.soil_temperature.heat_degC_per_g_C, "soil_temperature.heat_degC_per_g_C"));

    const OrganicMatterParams& om = p.organic_matter;
    SIM_CHECK(non_negative(om.labile_decay_rate_per_hour, "organic_matter.labile_decay_rate_per_hour"));
    SIM_CHECK(non_negative(om.stable_decay_rate_per_hour, "organic_matter.stable_decay_rate_per_hour"));
    SIM_CHECK(fraction(om.humification_fraction, "organic_matter.humification_fraction"));
    SIM_CHECK(positive(om.humus_C_to_N_ratio, "organic_matter.humus_C_to_N_ratio"));
    SIM_CHECK(validate_curve(om.temp_curve, "organic_matter.temp_curve"));
    SIM_CHECK(validate_curve(om.moisture_curve, "organic_matter.moisture_curve"));

    const NitrificationParams& nit = p.nitrification;
    SIM_CHECK(non_negative(nit.step1_rate_per_hour, "nitrification.step1_rate_per_hour"));
    SIM_CHECK(non_negative(nit.step2_rate_per_hour, "nitrification.step2_rate_per_hour"));
    SIM_CHECK(validate_curve(nit.temp_curve, "nitrification.temp_curve"));
    SIM_CHECK(validate_curve(nit.moisture_curve, "nitrification.moisture_curve"));
    SIM_CHECK(validate_curve(nit.step1_ph_curve, "nitrification.step1_ph_curve"));
    SIM_CHECK(validate_curve(nit.step2_ph_curve, "nitrification.step2_ph_curve"));
    SIM_CHECK(validate_curve(nit.ammonia_fraction_ph_curve, "nitrification.ammonia_fraction_ph_curve"));
    SIM_CHECK(validate_curve(nit.free_ammonia_inhibition_curve, "nitrification.free_ammonia_inhibition_curve"));
    SIM_CHECK(positive(nit.nitrite_toxicity_threshold_g_N_per_m2, "nitrification.nitrite_toxicity_threshold_g_N_per_m2"));
    SIM_CHECK(fraction(nit.nitrite_alarm_reset_fraction, "nitrification.nitrite_alarm_reset_fraction"));

    SIM_CHECK(fraction(p.leaching.nh4_solution_fraction, "leaching.nh4_solution_fraction"));

    std::set<std::string, std::less<>> compost_ids;
    for (const CompostDef& c : p.composts) {
        SIM_CHECK(require(!c.id.empty() && compost_ids.insert(c.id).second,
                          std::format("compost id '{}' must be non-empty and unique", c.id)));
        SIM_CHECK(non_negative(c.C_g_per_kg, std::format("compost '{}' C_g_per_kg", c.id)));
        SIM_CHECK(non_negative(c.N_g_per_kg, std::format("compost '{}' N_g_per_kg", c.id)));
        SIM_CHECK(fraction(c.labile_C_fraction, std::format("compost '{}' labile_C_fraction", c.id)));
        SIM_CHECK(fraction(c.labile_N_fraction, std::format("compost '{}' labile_N_fraction", c.id)));
    }
    std::set<std::string, std::less<>> fertilizer_ids;
    for (const FertilizerDef& f : p.fertilizers) {
        SIM_CHECK(require(!f.id.empty() && fertilizer_ids.insert(f.id).second,
                          std::format("fertilizer id '{}' must be non-empty and unique", f.id)));
        SIM_CHECK(non_negative(f.nh4_N_g_per_kg, std::format("fertilizer '{}' nh4_N_g_per_kg", f.id)));
        SIM_CHECK(non_negative(f.no3_N_g_per_kg, std::format("fertilizer '{}' no3_N_g_per_kg", f.id)));
    }
    return {};
}

std::expected<void, Error> validate_scenario(const Scenario& s) {
    if (s.start_day_of_year < 1 || s.start_day_of_year > kDaysPerYear) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("start_day_of_year must be 1..{}, got {}", kDaysPerYear, s.start_day_of_year));
    }
    if (s.bed_rows <= 0 || s.bed_cols <= 0) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("bed grid must be at least 1x1, got {}x{}", s.bed_rows, s.bed_cols));
    }

    const SoilInit& soil = s.soil;
    SIM_CHECK(positive(soil.field_capacity_mm, "soil.field_capacity_mm"));
    SIM_CHECK(non_negative(soil.water_mm, "soil.water_mm"));
    SIM_CHECK(finite(soil.soil_temp_degC, "soil.soil_temp_degC"));
    SIM_CHECK(require(std::isfinite(soil.ph) && soil.ph >= 0.0 && soil.ph <= 14.0, "soil.ph must be within 0..14"));
    SIM_CHECK(non_negative(soil.nh4_N_g_per_m2, "soil.nh4_N_g_per_m2"));
    SIM_CHECK(non_negative(soil.no2_N_g_per_m2, "soil.no2_N_g_per_m2"));
    SIM_CHECK(non_negative(soil.no3_N_g_per_m2, "soil.no3_N_g_per_m2"));
    SIM_CHECK(non_negative(soil.labile_C_g_per_m2, "soil.labile_C_g_per_m2"));
    SIM_CHECK(non_negative(soil.labile_N_g_per_m2, "soil.labile_N_g_per_m2"));
    SIM_CHECK(non_negative(soil.stable_C_g_per_m2, "soil.stable_C_g_per_m2"));
    SIM_CHECK(non_negative(soil.stable_N_g_per_m2, "soil.stable_N_g_per_m2"));

    if (s.weather_hourly.size() != kHoursPerYear) {
        return fail(ErrorCode::InvalidArgument, std::format("weather_hourly must have {} entries, got {}",
                                                            kHoursPerYear, s.weather_hourly.size()));
    }
    for (std::size_t i = 0; i < s.weather_hourly.size(); ++i) {
        const WeatherHour& w = s.weather_hourly[i];
        if (!std::isfinite(w.outside_air_temp_degC) || !std::isfinite(w.solar_radiation_W_per_m2) ||
            !std::isfinite(w.outside_rh_percent)) {
            return fail(ErrorCode::InvalidArgument, std::format("weather_hourly[{}] has a non-finite value", i));
        }
        if (w.solar_radiation_W_per_m2 < 0.0) {
            return fail(ErrorCode::InvalidArgument, std::format("weather_hourly[{}] solar radiation is negative", i));
        }
        if (w.outside_rh_percent < 0.0 || w.outside_rh_percent > 100.0) {
            return fail(ErrorCode::InvalidArgument,
                        std::format("weather_hourly[{}] relative humidity is outside 0..100", i));
        }
    }
    return {};
}

SoilCell make_cell(const SoilInit& init) {
    return SoilCell{
        .field_capacity_mm = init.field_capacity_mm,
        .water_mm = init.water_mm,
        .drained_mm_this_tick = 0.0,
        .soil_temp_degC = init.soil_temp_degC,
        .labile_C_g_per_m2 = init.labile_C_g_per_m2,
        .labile_N_g_per_m2 = init.labile_N_g_per_m2,
        .stable_C_g_per_m2 = init.stable_C_g_per_m2,
        .stable_N_g_per_m2 = init.stable_N_g_per_m2,
        .nh4_N_g_per_m2 = init.nh4_N_g_per_m2,
        .no2_N_g_per_m2 = init.no2_N_g_per_m2,
        .no3_N_g_per_m2 = init.no3_N_g_per_m2,
        .ph = init.ph,
        .nitrite_alarm = false,
    };
}

}  // namespace

World::World(Params params, Scenario scenario)
    : params_(std::move(params)),
      scenario_(std::move(scenario)),
      tiles_(static_cast<std::size_t>(scenario_.bed_rows) * static_cast<std::size_t>(scenario_.bed_cols),
             Tile{make_cell(scenario_.soil)}) {
    const CalendarTime t0 = calendar_at(scenario_.start_day_of_year, 0);
    house_air_temp_degC_ =
        process::house_air_temp_degC(params_.house, scenario_.weather_hourly[weather_index(t0.day_of_year, t0.hour)]);
}

std::expected<World, Error> World::create(Params params, Scenario scenario) {
    SIM_CHECK(validate_params(params));
    SIM_CHECK(validate_scenario(scenario));
    return World(std::move(params), std::move(scenario));
}

// ---- 명령 ----

const CompostDef* World::find_compost(std::string_view id) const {
    for (const CompostDef& c : params_.composts) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

const FertilizerDef* World::find_fertilizer(std::string_view id) const {
    for (const FertilizerDef& f : params_.fertilizers) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

std::expected<void, Error> World::validate_area(const TileArea& area) const {
    const bool ok = area.rows > 0 && area.cols > 0 && area.row >= 0 && area.col >= 0 &&
                    area.row <= scenario_.bed_rows - area.rows && area.col <= scenario_.bed_cols - area.cols;
    if (!ok) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("area (row {}, col {}, {}x{}) is outside the {}x{} grid", area.row, area.col,
                                area.rows, area.cols, scenario_.bed_rows, scenario_.bed_cols));
    }
    return {};
}

std::expected<void, Error> World::validate_command(const Command& command) const {
    struct Visitor {
        const World& world;
        std::expected<void, Error> operator()(const NoopCommand&) const { return {}; }
        std::expected<void, Error> operator()(const IrrigateCommand& c) const {
            SIM_CHECK(world.validate_area(c.area));
            return non_negative(c.amount_mm, "irrigate amount_mm");
        }
        std::expected<void, Error> operator()(const AddCompostCommand& c) const {
            SIM_CHECK(world.validate_area(c.area));
            if (world.find_compost(c.compost_id) == nullptr) {
                return fail(ErrorCode::InvalidArgument, std::format("unknown compost id '{}'", c.compost_id));
            }
            return non_negative(c.kg_per_m2, "add_compost kg_per_m2");
        }
        std::expected<void, Error> operator()(const AddFertilizerCommand& c) const {
            SIM_CHECK(world.validate_area(c.area));
            if (world.find_fertilizer(c.fertilizer_id) == nullptr) {
                return fail(ErrorCode::InvalidArgument, std::format("unknown fertilizer id '{}'", c.fertilizer_id));
            }
            return non_negative(c.g_per_m2, "add_fertilizer g_per_m2");
        }
    };
    return std::visit(Visitor{*this}, command);
}

std::expected<std::uint64_t, Error> World::enqueue(Command command) {
    SIM_CHECK(validate_command(command));
    const std::uint64_t seq = next_command_seq_++;
    pending_commands_.push_back(QueuedCommand{seq, std::move(command)});
    return seq;
}

template <typename Fn>
void World::for_each_tile_in(const TileArea& area, Fn&& fn) {
    for (int r = area.row; r < area.row + area.rows; ++r) {
        for (int c = area.col; c < area.col + area.cols; ++c) {
            const auto index = static_cast<std::size_t>(r) * static_cast<std::size_t>(scenario_.bed_cols) +
                               static_cast<std::size_t>(c);
            fn(tiles_[index]);
        }
    }
}

void World::apply_command(const NoopCommand& /*command*/) {}

void World::apply_command(const IrrigateCommand& command) {
    for_each_tile_in(command.area,
                     [&](Tile& tile) { process::apply_irrigation(tile.soil, command.amount_mm, ledger_); });
}

void World::apply_command(const AddCompostCommand& command) {
    const CompostDef& compost = *find_compost(command.compost_id);  // enqueue에서 검증됨
    for_each_tile_in(command.area, [&](Tile& tile) {
        process::apply_compost(tile.soil, compost, command.kg_per_m2, ledger_);
    });
}

void World::apply_command(const AddFertilizerCommand& command) {
    const FertilizerDef& fertilizer = *find_fertilizer(command.fertilizer_id);  // enqueue에서 검증됨
    for_each_tile_in(command.area, [&](Tile& tile) {
        process::apply_fertilizer(tile.soil, fertilizer, command.g_per_m2, ledger_);
    });
}

// ---- 틱 ----

void World::step(std::uint32_t ticks) {
    events_.clear();
    for (std::uint32_t i = 0; i < ticks; ++i) {
        tick_once();
    }
}

// 틱 처리 순서: docs/spec/m1-overview.md "틱 처리 순서 (M1)"
void World::tick_once() {
    // 1. 명령 적용
    for (const QueuedCommand& queued : pending_commands_) {
        std::visit([this](const auto& command) { apply_command(command); }, queued.command);
        events_.push_back(Event{tick_, CommandAppliedEvent{queued.seq}});
    }
    pending_commands_.clear();

    // 2. 하우스 환경
    const CalendarTime now = calendar_at(scenario_.start_day_of_year, tick_);
    const WeatherHour& weather = scenario_.weather_hourly[weather_index(now.day_of_year, now.hour)];
    house_air_temp_degC_ = process::house_air_temp_degC(params_.house, weather);
    const double inside_solar = process::inside_solar_W_per_m2(params_.house, weather);

    // 3. 토양 셀 (행 우선)
    for (std::size_t i = 0; i < tiles_.size(); ++i) {
        SoilCell& cell = tiles_[i].soil;
        process::update_soil_water(cell, params_.soil_water, inside_solar, ledger_);                       // a
        process::relax_soil_temperature(cell, params_.soil_temperature, house_air_temp_degC_);            // b
        // c. 통기성: M2, d. 미생물: M5
        process::decompose_organic_matter(cell, params_.organic_matter, params_.soil_temperature, ledger_);  // e
        process::nitrify(cell, params_.nitrification);                                                     // f
        // g. 양이온 교환·pH: M2/M3, 흡수: M2
        process::leach(cell, params_.leaching, ledger_);                                                  // h
        if (auto event = process::check_nitrite_alarm(cell, params_.nitrification,
                                                      static_cast<std::uint32_t>(i))) {                    // i
            events_.push_back(Event{tick_, *event});
        }
    }

    ++tick_;
}

Snapshot World::snapshot() const {
    const CalendarTime time = calendar_at(scenario_.start_day_of_year, tick_);
    std::vector<SoilCell> soil;
    soil.reserve(tiles_.size());
    for (const Tile& tile : tiles_) {
        soil.push_back(tile.soil);
    }
    return Snapshot{
        .tick = tick_,
        .time = time,
        .weather = scenario_.weather_hourly[weather_index(time.day_of_year, time.hour)],
        .bed_rows = scenario_.bed_rows,
        .bed_cols = scenario_.bed_cols,
        .pending_command_count = pending_commands_.size(),
        .house_air_temp_degC = house_air_temp_degC_,
        .soil = std::move(soil),
        .ledger = ledger_,
    };
}

#undef SIM_CHECK

}  // namespace sim
