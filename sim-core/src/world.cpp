#include "sim/world.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>

#include "sim/calendar.hpp"
#include "sim/processes.hpp"
#include "validation.hpp"

namespace sim {

namespace {

std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

SoilCell make_cell(const SoilInit& init) {
    SoilCell cell;
    cell.field_capacity_mm = init.field_capacity_mm;
    cell.saturation_mm = init.saturation_mm;
    cell.base_buffer_eq_per_m2_per_pH = init.base_buffer_eq_per_m2_per_pH;
    cell.water_mm = init.water_mm;
    cell.soil_temp_degC = init.soil_temp_degC;
    cell.labile_C_g_per_m2 = init.labile_C_g_per_m2;
    cell.labile_N_g_per_m2 = init.labile_N_g_per_m2;
    cell.stable_C_g_per_m2 = init.stable_C_g_per_m2;
    cell.stable_N_g_per_m2 = init.stable_N_g_per_m2;
    cell.nh4_N_g_per_m2 = init.nh4_N_g_per_m2;
    cell.no2_N_g_per_m2 = init.no2_N_g_per_m2;
    cell.no3_N_g_per_m2 = init.no3_N_g_per_m2;
    cell.ph = init.ph;
    cell.compaction_index = init.compaction_index;
    cell.k_g_per_m2 = init.k_g_per_m2;
    cell.ca_g_per_m2 = init.ca_g_per_m2;
    cell.mg_g_per_m2 = init.mg_g_per_m2;
    cell.p_g_per_m2 = init.p_g_per_m2;
    cell.s_g_per_m2 = init.s_g_per_m2;
    cell.cec_mineral_eq_per_m2 = init.cec_mineral_eq_per_m2;
    cell.mn_g_per_m2 = init.mn_g_per_m2;
    cell.mn_oxide_g_per_m2 = init.mn_oxide_g_per_m2;
    cell.p_fixed_g_per_m2 = init.p_fixed_g_per_m2;
    return cell;
}

}  // namespace

World::World(Params params, Scenario scenario)
    : params_(std::move(params)),
      scenario_(std::move(scenario)),
      tiles_(static_cast<std::size_t>(scenario_.bed_rows) * static_cast<std::size_t>(scenario_.bed_cols),
             Tile{make_cell(scenario_.soil), Crop{}}),
      biology_rng_(make_stream(scenario_.seed, static_cast<std::uint64_t>(RngStream::Biology))) {
    if (scenario_.crop) {
        // 등급 존재는 create()에서 검증됨 (validate_scenario_against_params)
        const SeedlingGradeDef* grade = process::find_seedling_grade(params_, scenario_.crop->seedling_grade);
        if (grade != nullptr) {
            Crop planted = process::plant_crop(*grade, scenario_.crop->plants_per_m2, params_);
            planted.establishment_index = scenario_.crop->initial_establishment_index;
            for (Tile& tile : tiles_) {
                tile.crop = planted;
            }
        }
    }
    const CalendarTime t0 = calendar_at(scenario_.start_day_of_year, 0);
    house_ = process::initial_house(params_.house, scenario_.cover_layers,
                                    scenario_.weather_hourly[weather_index(t0.day_of_year, t0.hour)], t0.hour);
    // M3 (cation-exchange "초기화 순서"): 통기 지수를 먼저 계산한 뒤 평형을 구해 용액 상태를 채워요(완화 없이).
    // 세이브 로드(deserialize)는 이 뒤에 저장된 셀 상태로 덮어써서 평형을 다시 구하지 않아요.
    for (Tile& tile : tiles_) {
        tile.soil.aeration_index = process::aeration_index(tile.soil, params_.aeration);
        process::set_solution_to_equilibrium(tile.soil, params_);
    }
    refresh_derived_cell_values();
}

std::expected<World, Error> World::create(Params params, Scenario scenario) {
    SIM_CHECK(validation::validate_params(params));
    SIM_CHECK(validation::validate_scenario(scenario));
    SIM_CHECK(validation::validate_scenario_against_params(scenario, params));
    return World(std::move(params), std::move(scenario));
}

void World::refresh_derived_cell_values() {
    for (Tile& tile : tiles_) {
        tile.soil.aeration_index = process::aeration_index(tile.soil, params_.aeration);
        tile.soil.ec_dS_per_m = process::soil_ec_dS_per_m(tile.soil, params_);
    }
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

const LimeDef* World::find_lime(std::string_view id) const {
    for (const LimeDef& l : params_.limes) {
        if (l.id == id) {
            return &l;
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
            return validation::non_negative(c.amount_mm, "irrigate amount_mm");
        }
        std::expected<void, Error> operator()(const AddCompostCommand& c) const {
            SIM_CHECK(world.validate_area(c.area));
            if (world.find_compost(c.compost_id) == nullptr) {
                return fail(ErrorCode::InvalidArgument, std::format("unknown compost id '{}'", c.compost_id));
            }
            return validation::non_negative(c.kg_per_m2, "add_compost kg_per_m2");
        }
        std::expected<void, Error> operator()(const AddFertilizerCommand& c) const {
            SIM_CHECK(world.validate_area(c.area));
            if (world.find_fertilizer(c.fertilizer_id) == nullptr) {
                return fail(ErrorCode::InvalidArgument, std::format("unknown fertilizer id '{}'", c.fertilizer_id));
            }
            return validation::non_negative(c.g_per_m2, "add_fertilizer g_per_m2");
        }
        std::expected<void, Error> operator()(const AddLimeCommand& c) const {
            SIM_CHECK(world.validate_area(c.area));
            if (world.find_lime(c.lime_id) == nullptr) {
                return fail(ErrorCode::InvalidArgument, std::format("unknown lime id '{}'", c.lime_id));
            }
            return validation::non_negative(c.g_per_m2, "add_lime g_per_m2");
        }
        std::expected<void, Error> operator()(const TillCommand& c) const { return world.validate_area(c.area); }
        std::expected<void, Error> operator()(const SetWindowCommand& c) const {
            SIM_CHECK(validation::require(c.layer >= 0 && c.layer < world.house_.cover_layers,
                                          std::format("set_window layer {} is outside 0..{}", c.layer,
                                                      world.house_.cover_layers - 1)));
            SIM_CHECK(validation::require(c.side == WindowSide::Left || c.side == WindowSide::Right,
                                          "set_window side is not a known value"));
            const auto& levels = world.params_.house.window_open_levels;
            const bool allowed = std::find(levels.begin(), levels.end(), c.open_fraction) != levels.end();
            return validation::require(allowed, std::format("set_window open_fraction {} is not an allowed level",
                                                            c.open_fraction));
        }
        std::expected<void, Error> operator()(const SetHeaterCommand& c) const {
            const HouseParams& h = world.params_.house;
            return validation::require(std::isfinite(c.setpoint_degC) && c.setpoint_degC >= h.heater_setpoint_min_degC &&
                                           c.setpoint_degC <= h.heater_setpoint_max_degC,
                                       std::format("set_heater setpoint_degC must be within {}..{}",
                                                   h.heater_setpoint_min_degC, h.heater_setpoint_max_degC));
        }
        std::expected<void, Error> operator()(const ThinFlowersCommand& c) const {
            SIM_CHECK(world.validate_area(c.area));
            return validation::non_negative(c.per_plant, "thin_flowers per_plant");
        }
        std::expected<void, Error> operator()(const SetCoverLayersCommand& c) const {
            const HouseParams& h = world.params_.house;
            SIM_CHECK(validation::require(c.layers >= h.min_cover_layers && c.layers <= h.max_cover_layers,
                                          std::format("set_cover_layers layers must be within {}..{}",
                                                      h.min_cover_layers, h.max_cover_layers)));
            // 명령이 적용될 틱(다음 틱 = tick_)의 날짜가 휴작기 안이어야 해요
            const CalendarTime when = calendar_at(world.scenario_.start_day_of_year, world.tick_);
            return validation::require(process::in_cover_change_window(h, when.day_of_year),
                                       std::format("set_cover_layers is only accepted on day_of_year {}..{} (got {})",
                                                   h.cover_change_window_doy[0], h.cover_change_window_doy[1],
                                                   when.day_of_year));
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
    for_each_tile_in(command.area, [&](Tile& tile) {
        process::apply_irrigation(tile.soil, command.amount_mm, params_.aeration, ledger_);
    });
}

void World::apply_command(const AddCompostCommand& command) {
    const CompostDef& compost = *find_compost(command.compost_id);  // enqueue에서 검증됨
    for_each_tile_in(command.area, [&](Tile& tile) {
        process::apply_compost(tile.soil, compost, command.kg_per_m2, ledger_);
        process::apply_compost_minerals(tile.soil, compost, command.kg_per_m2, ledger_);
    });
}

void World::apply_command(const AddFertilizerCommand& command) {
    const FertilizerDef& fertilizer = *find_fertilizer(command.fertilizer_id);  // enqueue에서 검증됨
    for_each_tile_in(command.area, [&](Tile& tile) {
        process::apply_fertilizer(tile.soil, fertilizer, command.g_per_m2, ledger_);
    });
}

void World::apply_command(const AddLimeCommand& command) {
    const LimeDef& lime = *find_lime(command.lime_id);  // enqueue에서 검증됨
    for_each_tile_in(command.area, [&](Tile& tile) { process::apply_lime(tile.soil, lime, command.g_per_m2); });
}

void World::apply_command(const TillCommand& command) {
    for_each_tile_in(command.area, [&](Tile& tile) { process::apply_till(tile.soil, tile.crop, params_.aeration); });
}

void World::apply_command(const SetWindowCommand& command) {
    // 같은 틱에 앞선 SetCoverLayers로 겹이 줄었으면 무시해요
    if (command.layer >= 0 && command.layer < house_.cover_layers) {
        house_.window_open_fraction[static_cast<std::size_t>(command.layer)][static_cast<std::size_t>(command.side)] =
            command.open_fraction;
    }
}

void World::apply_command(const SetHeaterCommand& command) {
    house_.heater_on = command.on;
    house_.heater_setpoint_degC = command.setpoint_degC;
}

void World::apply_command(const SetCoverLayersCommand& command) { process::set_cover_layers(house_, command.layers); }

void World::apply_command(const ThinFlowersCommand& command) {
    // 생물 변이 스트림을 써요. 칸 순서는 영역 안 행 우선이라 결정론적이에요.
    for_each_tile_in(command.area, [&](Tile& tile) {
        (void)process::thin_flowers(tile.crop, tile.soil, params_, command.per_plant, biology_rng_, ledger_);
    });
}

// ---- 틱 ----

void World::step(std::uint32_t ticks) {
    events_.clear();
    for (std::uint32_t i = 0; i < ticks; ++i) {
        tick_once();
    }
}

// 틱 처리 순서: docs/spec/m4-overview.md "틱 처리 순서 (M4)"
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
    process::update_house_air(house_, params_.house, weather, now.hour);
    const double inside_solar = house_.inside_solar_W_per_m2;
    const double house_air_temp_degC = house_.air_temp_degC;
    const process::CropEnvironment crop_env_base{
        .inside_solar_W_per_m2 = inside_solar,
        .house_air_temp_degC = house_air_temp_degC,
        .house_air_temp_mean_degC = house_.air_temp_mean_degC,
        .house_co2_ppm = house_.co2_ppm,  // 틱 시작 때 값 (CO2는 셀 계산 뒤에 갱신)
        .day_of_year = now.day_of_year,
        .tick = tick_,
        .mode = scenario_.stress_combination,
        .tile_index = 0,
    };
    const double C_respired_before_g = ledger_.C_respired_g;
    double crop_net_dry_matter_g = 0.0;

    // 3. 토양 셀 (행 우선)
    for (std::size_t i = 0; i < tiles_.size(); ++i) {
        SoilCell& cell = tiles_[i].soil;
        Crop& crop = tiles_[i].crop;
        const auto tile_index = static_cast<std::uint32_t>(i);
        const bool crop_alive = crop.present && crop.alive;

        // a. 수분 (증산 요구량은 이전 틱의 EC와 현재 수분으로 계산)
        double interception = 0.0;
        double transpiration_demand = 0.0;
        if (crop_alive) {
            interception = process::canopy_interception(crop, params_.crop);
            transpiration_demand = process::transpiration_demand_mm(crop, params_.crop, inside_solar,
                                                                    process::water_salt_factors(cell, params_.crop));
        }
        process::update_soil_water(cell, params_.soil_water, inside_solar, interception, transpiration_demand,
                                   ledger_);
        // b. 온도
        process::relax_soil_temperature(cell, params_.soil_temperature, house_air_temp_degC);
        // c. 통기
        cell.aeration_index = process::aeration_index(cell, params_.aeration);
        // d. 미생물: M5
        // e. 유기물 분해 (무기화 NH4 → 용액, 부동화 NH4 → 비례 차감)
        process::decompose_organic_matter(cell, params_.organic_matter, params_.soil_temperature, ledger_);
        // f. 질산화 (NH4 용액 비례 차감)
        process::nitrify(cell, params_.nitrification, params_.soil_chemistry, params_.ions);
        // f1. 탈질 (M4): NO3 → N_gas, H+ 소비는 pH 단계(g)로
        process::denitrify(cell, params_.denitrification, params_.ions, ledger_);
        // f2. 망간 산화·환원 → 양이온 교환: CEC → φ → 평형 → 완화 (M3)
        process::update_cation_exchange(cell, params_);
        // f3. 인산 고정 (M3)
        process::fix_phosphorus(cell, params_.phosphorus);
        // g. 석회 용해(Ca·Mg → 총량과 용액) → pH (완충력에 광물 CEC 항)
        process::update_lime_and_ph(cell, params_, ledger_);
        // h. 작물
        if (crop_alive) {
            const process::NutrientAvailability available = process::available_nutrients(cell, params_);
            process::CropEnvironment env = crop_env_base;
            env.tile_index = tile_index;
            const process::CropTickResult result =
                process::update_crop(crop, cell, params_, available, env, biology_rng_, ledger_);
            crop_net_dry_matter_g += crop.last_gross_g_per_m2 - crop.last_respiration_g_per_m2;
            if (result.harvested) {
                events_.push_back(Event{tick_, *result.harvested});
            }
            if (result.died) {
                events_.push_back(Event{tick_, *result.died});
            }
        }
        // i. 용탈
        process::leach(cell, params_, ledger_);
        // j. 파생값
        cell.ec_dS_per_m = process::soil_ec_dS_per_m(cell, params_);
        // k. 이벤트 판정 (작물 고사는 h에서)
        if (auto event = process::check_nitrite_alarm(cell, params_.nitrification, tile_index)) {
            events_.push_back(Event{tick_, *event});
        }
    }

    // 4. 하우스 CO2 갱신 (이번 틱 작물 흡수·토양 호흡·환기, 셀 평균)
    if (!tiles_.empty()) {
        const double cells = static_cast<double>(tiles_.size());
        process::update_house_co2(house_, params_.house, params_.crop, crop_net_dry_matter_g / cells,
                                  (ledger_.C_respired_g - C_respired_before_g) / cells);
    }

    ++tick_;
}

Snapshot World::snapshot() const {
    const CalendarTime time = calendar_at(scenario_.start_day_of_year, tick_);
    std::vector<SoilCell> soil;
    std::vector<Crop> crops;
    soil.reserve(tiles_.size());
    crops.reserve(tiles_.size());
    for (const Tile& tile : tiles_) {
        soil.push_back(tile.soil);
        crops.push_back(tile.crop);
    }
    return Snapshot{
        .tick = tick_,
        .time = time,
        .weather = scenario_.weather_hourly[weather_index(time.day_of_year, time.hour)],
        .bed_rows = scenario_.bed_rows,
        .bed_cols = scenario_.bed_cols,
        .pending_command_count = pending_commands_.size(),
        .house_air_temp_degC = house_.air_temp_degC,
        .house = house_,
        .soil = std::move(soil),
        .crops = std::move(crops),
        .ledger = ledger_,
    };
}

}  // namespace sim
