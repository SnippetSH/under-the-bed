#include "sim/world.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
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

SoilCell make_cell(const SoilInit& init, const Params& params) {
    SoilCell cell;
    cell.field_capacity_mm = init.field_capacity_mm;
    cell.saturation_mm = init.saturation_mm;
    cell.base_buffer_eq_per_m2_per_pH = init.base_buffer_eq_per_m2_per_pH;
    cell.water_mm = init.water_mm;
    cell.soil_temp_degC = init.soil_temp_degC;
    cell.deep_soil_temp_degC = init.soil_temp_degC;  // M5a 프로토타입 3차: 깊은 흙도 시나리오 지온에서 시작
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
    // M5a: 시나리오 [microbes] 시작 지수, 멀칭 없음, 약제별 잔류 0 (params.disinfectants 순서)
    cell.microbe_index = init.microbe_index;
    cell.mulch_def_index = kNoMulch;
    cell.fumigant_residue_index.assign(params.disinfectants.size(), 0.0);
    return cell;
}

// 포기 간 차이 (strawberry-phenology 2번): 0이면 난수를 뽑지 않아 기존 난수 순서 그대로.
// 월드 생성과 Plant 명령이 같은 규칙을 써요 (타일 순서대로 1회씩).
void apply_induction_spread(Crop& crop, const Crop& planted, const PhenologyParams& phenology, Pcg32& biology_rng) {
    if (phenology.induction_spread_index > 0.0) {
        crop.induction_index =
            std::max(0.0, planted.induction_index - phenology.induction_spread_index * biology_rng.next_unit_double());
    }
}

// 포기 감수성 차이 (M5a 프로토타입 3차, soil-microbes 3번): 0이면 난수를 뽑지 않고 1 그대로.
// 월드 생성과 Plant 명령이 같은 규칙을 써요 (타일마다 화아분화 차이 다음에 1회).
void apply_susceptibility_spread(Crop& crop, const MicrobesParams& microbes, Pcg32& biology_rng) {
    crop.disease_susceptibility_factor = 1.0;
    if (microbes.susceptibility_spread_index > 0.0) {
        const double u = biology_rng.next_unit_double();
        crop.disease_susceptibility_factor = 1.0 + microbes.susceptibility_spread_index * (2.0 * u - 1.0);
    }
}

// 병원균 시작 지수의 셀 간 차이 (M5a 프로토타입 3차, soil-microbes 1번): 월드 생성 때만.
// 병원균 무리(MicrobeGroup 순서: fusarium, phytophthora)마다 시작 지수 > 0이고 spread > 0일 때만 1회씩 뽑아요.
// 지수 × (1 + spread × (2U − 1))은 자르기 전 평균이 시작 지수와 같고, [0, 1]로 잘라요.
void apply_pathogen_spread(SoilCell& cell, const SoilInit& init, const MicrobesParams& microbes, Pcg32& biology_rng) {
    if (!(microbes.pathogen_spread_index > 0.0)) {
        return;
    }
    for (std::size_t d = 0; d < kSoilDiseaseCount; ++d) {
        const std::size_t g = microbe(pathogen_of(static_cast<SoilDisease>(d)));
        const double start_index = init.microbe_index[g];
        if (!(start_index > 0.0)) {
            continue;
        }
        const double u = biology_rng.next_unit_double();
        cell.microbe_index[g] =
            std::clamp(start_index * (1.0 + microbes.pathogen_spread_index * (2.0 * u - 1.0)), 0.0, 1.0);
    }
}

}  // namespace

World::World(Params params, Scenario scenario)
    : params_(std::move(params)),
      scenario_(std::move(scenario)),
      tiles_(static_cast<std::size_t>(scenario_.bed_rows) * static_cast<std::size_t>(scenario_.bed_cols),
             Tile{make_cell(scenario_.soil, params_), Crop{}}),
      biology_rng_(make_stream(scenario_.seed, static_cast<std::uint64_t>(RngStream::Biology))),
      disease_rng_(make_stream(scenario_.seed, static_cast<std::uint64_t>(RngStream::Disease))) {
    std::optional<Crop> planted;
    if (scenario_.crop) {
        // 등급 존재는 create()에서 검증됨 (validate_scenario_against_params)
        const SeedlingGradeDef* grade = process::find_seedling_grade(params_, scenario_.crop->seedling_grade);
        if (grade != nullptr) {
            planted = process::plant_crop(*grade, scenario_.crop->plants_per_m2, params_);
            planted->establishment_index = scenario_.crop->initial_establishment_index;
        }
    }
    // 난수 순서 (결정론): 타일 행 우선 순서대로, 타일마다
    //   biology 스트림: 1) 화아분화 차이 (작물이 있고 induction_spread_index > 0)
    //   disease 스트림: 2) 감수성 차이 (작물이 있고 susceptibility_spread_index > 0)
    //                   3) 병원균 시작 차이 (MicrobeGroup 순서, pathogen_spread_index > 0이고 시작 지수 > 0인 무리만)
    // 병해 차이는 별도 스트림이라 biology 스트림(화아분화·낙과 등)의 순서·결과를 바꾸지 않아요.
    for (Tile& tile : tiles_) {
        if (planted) {
            tile.crop = *planted;
            apply_induction_spread(tile.crop, *planted, params_.phenology, biology_rng_);
            apply_susceptibility_spread(tile.crop, params_.microbes, disease_rng_);
        }
        apply_pathogen_spread(tile.soil, scenario_.soil, params_.microbes, disease_rng_);
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

std::optional<std::size_t> World::find_mulch_index(std::string_view id) const {
    for (std::size_t i = 0; i < params_.mulches.size(); ++i) {
        if (params_.mulches[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> World::find_disinfectant_index(std::string_view id) const {
    for (std::size_t i = 0; i < params_.disinfectants.size(); ++i) {
        if (params_.disinfectants[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

const InoculantDef* World::find_inoculant(std::string_view id) const {
    for (const InoculantDef& d : params_.inoculants) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

bool World::any_live_crop() const {
    return std::any_of(tiles_.begin(), tiles_.end(),
                       [](const Tile& tile) { return tile.crop.present && tile.crop.alive; });
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
        // ---- M5a (disinfection, soil-microbes) ----
        std::expected<void, Error> operator()(const SetMulchCommand& c) const {
            SIM_CHECK(world.validate_area(c.area));
            if (c.mulch_id != kNoMulchId && !world.find_mulch_index(c.mulch_id)) {
                return fail(ErrorCode::InvalidArgument, std::format("unknown mulch id '{}'", c.mulch_id));
            }
            return {};
        }
        std::expected<void, Error> operator()(const ApplyDisinfectantCommand& c) const {
            SIM_CHECK(world.validate_area(c.area));
            if (!world.find_disinfectant_index(c.disinfectant_id)) {
                return fail(ErrorCode::InvalidArgument,
                            std::format("unknown disinfectant id '{}'", c.disinfectant_id));
            }
            return validation::non_negative(c.g_per_m2, "apply_disinfectant g_per_m2");
        }
        std::expected<void, Error> operator()(const AddInoculantCommand& c) const {
            SIM_CHECK(world.validate_area(c.area));
            if (world.find_inoculant(c.inoculant_id) == nullptr) {
                return fail(ErrorCode::InvalidArgument, std::format("unknown inoculant id '{}'", c.inoculant_id));
            }
            return validation::non_negative(c.g_per_m2, "add_inoculant g_per_m2");
        }
        std::expected<void, Error> operator()(const PlantCommand& c) const {
            if (process::find_seedling_grade(world.params_, c.seedling_grade) == nullptr) {
                return fail(ErrorCode::InvalidArgument,
                            std::format("unknown seedling grade '{}'", c.seedling_grade));
            }
            SIM_CHECK(validation::positive(c.plants_per_m2, "plant plants_per_m2"));
            // 살아 있는 작물이 하나라도 있으면 거부 (하우스 전체 정식, disinfection 6번)
            return validation::require(!world.any_live_crop(), "plant is rejected while a live crop exists");
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
    for_each_tile_in(command.area, [&](Tile& tile) {
        process::apply_till(tile.soil, tile.crop, params_.aeration);
        process::release_fumigant_on_till(tile.soil, params_.disinfection);  // M5a: 가스 빼기
    });
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

// ---- M5a (disinfection.md 1·3·6번, soil-microbes.md 4번) ----

void World::apply_command(const SetMulchCommand& command) {
    // "none"이면 걷음. 작물이 있어도 덮을 수 있어요 (enqueue에서 id 검증됨)
    std::int32_t mulch_index = kNoMulch;
    if (command.mulch_id != kNoMulchId) {
        const std::optional<std::size_t> found = find_mulch_index(command.mulch_id);
        if (!found) {
            return;
        }
        mulch_index = static_cast<std::int32_t>(*found);
    }
    for_each_tile_in(command.area, [&](Tile& tile) { tile.soil.mulch_def_index = mulch_index; });
}

void World::apply_command(const ApplyDisinfectantCommand& command) {
    const std::optional<std::size_t> index = find_disinfectant_index(command.disinfectant_id);  // enqueue에서 검증됨
    if (!index) {
        return;
    }
    const DisinfectantDef& disinfectant = params_.disinfectants[*index];
    // 장부의 약제별 누적은 처음 투입할 때 params.disinfectants 크기로 만들어요 (그 전에는 비어 있음 = 전부 0)
    if (ledger_.disinfectant_applied_g.size() != params_.disinfectants.size()) {
        ledger_.disinfectant_applied_g.resize(params_.disinfectants.size(), 0.0);
    }
    for_each_tile_in(command.area, [&](Tile& tile) {
        process::apply_disinfectant(tile.soil, *index, disinfectant, command.g_per_m2, ledger_);
    });
}

void World::apply_command(const AddInoculantCommand& command) {
    const InoculantDef& inoculant = *find_inoculant(command.inoculant_id);  // enqueue에서 검증됨
    for_each_tile_in(command.area, [&](Tile& tile) {
        process::apply_inoculant(tile.soil, inoculant, command.g_per_m2, ledger_);
    });
}

void World::apply_command(const PlantCommand& command) {
    // 같은 틱에 앞선 Plant가 이미 심었으면(살아 있는 작물) 무시해요
    if (any_live_crop()) {
        return;
    }
    const SeedlingGradeDef* grade = process::find_seedling_grade(params_, command.seedling_grade);
    if (grade == nullptr) {
        return;  // enqueue에서 검증됨
    }
    const Crop planted = process::plant_crop(*grade, command.plants_per_m2, params_);  // 활착 0 (막 심음)
    for (Tile& tile : tiles_) {
        // 죽은 작물의 체내 원소·건물은 낙엽 규칙으로 토양에 (장부 보존)
        process::return_dead_crop_to_soil(tile.crop, tile.soil, params_.crop, ledger_);
        tile.crop = planted;
        // 난수: 타일마다 화아분화 차이(biology) → 감수성 차이(disease) (월드 생성과 같음, 병원균 차이는 생성 때만)
        apply_induction_spread(tile.crop, planted, params_.phenology, biology_rng_);
        apply_susceptibility_spread(tile.crop, params_.microbes, disease_rng_);
        // 모종의 체내 원소는 계 밖에서 들어온 투입 (tick 0 정식은 초기값이라 장부에 넣지 않아요)
        ledger_.N_in_g += planted.plant_N_g_per_m2;
        ledger_.P_in_g += planted.plant_P_g_per_m2;
        ledger_.K_in_g += planted.plant_K_g_per_m2;
        ledger_.Ca_in_g += planted.plant_Ca_g_per_m2;
        ledger_.Mg_in_g += planted.plant_Mg_g_per_m2;
        ledger_.Mn_in_g += planted.plant_Mn_g_per_m2;
    }
}

// ---- 틱 ----

void World::step(std::uint32_t ticks) {
    events_.clear();
    for (std::uint32_t i = 0; i < ticks; ++i) {
        tick_once();
    }
}

// 틱 처리 순서: docs/spec/m5-overview.md 7장 "틱 처리 순서 (M5)" (M5a 부분)
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
        // M5a: 멀칭 증발 배율 (disinfection 1번)
        const double evaporated_mm =
            process::update_soil_water(cell, params_.soil_water, inside_solar, interception, transpiration_demand,
                                       ledger_, process::mulch_evaporation_factor(cell, params_));
        // b. 온도 (M5a: 완화 목표에 멀칭 지온 이득. 증발 냉각은 실제 증발량이라 필름이 줄여요)
        process::relax_soil_temperature(
            cell, params_.soil_temperature,
            process::soil_relax_target_degC(cell, params_, house_air_temp_degC, inside_solar, interception),
            evaporated_mm);
        // c. 통기
        cell.aeration_index = process::aeration_index(cell, params_.aeration);
        // d. 미생물 (M5a, soil-microbes·disinfection)
        //   d1. 약제 잔류 분해(필름 보존)
        process::decay_fumigant(cell, params_);
        //   d2. 개체군 갱신 (수용력: 살아 있는 작물의 뿌리·감염)
        process::MicrobeHost host;
        if (crop_alive) {
            host.root_g_per_m2 = crop.root_g_per_m2;
            host.infection_index = crop.infection_index;
        }
        process::update_microbes(cell, params_, host);
        //   d3. 소독 사멸분 → 무기화 증가(N flush), 망간 산화물 환원
        process::apply_disinfection_flush(cell, params_.disinfection, ledger_);
        // e. 유기물 분해 (무기화 NH4 → 용액, 부동화 NH4 → 비례 차감) × 분해균계수
        process::decompose_organic_matter(cell, params_.organic_matter, params_.soil_temperature, ledger_,
                                          process::decomposer_activity_factor(cell, params_.microbes));
        // f. 질산화 (NH4 용액 비례 차감) × 1·2단계균 활동
        process::nitrify(cell, params_.nitrification, params_.soil_chemistry, params_.ions,
                         process::aob_activity_factor(cell, params_.microbes),
                         process::nob_activity_factor(cell, params_.microbes));
        // f1. 탈질 (M4): NO3 → N_gas, H+ 소비는 pH 단계(g)로
        process::denitrify(cell, params_.denitrification, params_.ions, ledger_);
        // f2. 망간 산화·환원 → 양이온 교환: CEC → φ → 평형 → 완화 (M3)
        process::update_cation_exchange(cell, params_);
        // f3. 인산 고정 (M3) × 균근균 가용화 (M5a)
        process::fix_phosphorus(cell, params_.phosphorus, process::mycorrhiza_p_release_factor(cell, params_.microbes));
        // g. 석회 용해(Ca·Mg → 총량과 용액) → pH (완충력에 광물 CEC 항)
        process::update_lime_and_ph(cell, params_, ledger_);
        // h. 작물
        if (crop_alive) {
            const process::NutrientAvailability available = process::available_nutrients(cell, params_);
            process::CropEnvironment env = crop_env_base;
            env.tile_index = tile_index;
            env.canopy_reflect_factor = process::mulch_canopy_reflect_factor(cell, params_);  // M5a 백색 필름
            const process::CropTickResult result =
                process::update_crop(crop, cell, params_, available, env, biology_rng_, ledger_);
            crop_net_dry_matter_g += crop.last_gross_g_per_m2 - crop.last_respiration_g_per_m2;
            if (result.harvested) {
                events_.push_back(Event{tick_, *result.harvested});
            }
            for (const auto& onset : result.disease_onsets) {  // M5a (SoilDisease 순서)
                if (onset) {
                    events_.push_back(Event{tick_, *onset});
                }
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
