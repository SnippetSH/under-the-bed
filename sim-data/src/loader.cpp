#include "simdata/loader.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

#include <toml++/toml.hpp>

#include "sim/calendar.hpp"

namespace simdata {

namespace {

using sim::Error;
using sim::ErrorCode;

std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

std::string path_string(const std::filesystem::path& path) {
    const std::u8string u8 = path.generic_u8string();
    return {u8.begin(), u8.end()};
}

std::filesystem::path path_from_utf8(std::string_view utf8) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::expected<std::string, Error> read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail(ErrorCode::Io, std::format("cannot open '{}'", path_string(path)));
    }
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad()) {
        return fail(ErrorCode::Io, std::format("failed to read '{}'", path_string(path)));
    }
    constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
    if (text.starts_with(kUtf8Bom)) {
        text.erase(0, kUtf8Bom.size());
    }
    return text;
}

std::expected<toml::table, Error> parse_toml(std::string_view text, std::string_view source_name) {
    toml::parse_result result = toml::parse(text, source_name);
    if (!result) {
        const toml::parse_error& err = result.error();
        return fail(ErrorCode::InvalidData,
                    std::format("{}:{}:{}: {}", source_name, err.source().begin.line,
                                err.source().begin.column, err.description()));
    }
    return std::move(result).table();
}

// 테이블에서 키를 하나씩 꺼내면서 사용한 키를 기록하고, finish()에서 모르는 키를 거부해요.
// 첫 오류는 reader 안에 기록하고(이후 읽기는 기본값을 돌려줘요) finish()가 돌려줘요.
class TableReader {
public:
    TableReader(const toml::table* table, std::string_view source_name, std::string context)
        : table_(table), source_name_(source_name), context_(std::move(context)) {}

    std::int64_t integer(std::string_view key) {
        const toml::node* node = lookup(key);
        if (node == nullptr) {
            return 0;
        }
        if (const auto value = node->value_exact<std::int64_t>()) {
            return *value;
        }
        wrong_type(key, "integer");
        return 0;
    }

    int small_integer(std::string_view key) {
        const std::int64_t value = integer(key);
        if (!std::in_range<int>(value)) {
            set_error(std::format("{}: '{}' is out of range", source_name_, qualified(key)));
            return 0;
        }
        return static_cast<int>(value);
    }

    // 실수 키는 TOML 정수(예: 3)도 받아요.
    double number(std::string_view key) {
        const toml::node* node = lookup(key);
        if (node == nullptr) {
            return 0.0;
        }
        return number_from(*node, qualified(key));
    }

    std::string string(std::string_view key) {
        const toml::node* node = lookup(key);
        if (node == nullptr) {
            return {};
        }
        if (auto value = node->value_exact<std::string>()) {
            return std::move(*value);
        }
        wrong_type(key, "string");
        return {};
    }

    // [[x, y], ...] 배열. 점의 순서·범위 검사는 코어(validate_curve)가 해요.
    sim::PiecewiseLinear curve(std::string_view key) {
        sim::PiecewiseLinear result;
        const toml::node* node = lookup(key);
        if (node == nullptr) {
            return result;
        }
        const toml::array* array = node->as_array();
        if (array == nullptr) {
            wrong_type(key, "array of [x, y] pairs");
            return result;
        }
        for (std::size_t i = 0; i < array->size(); ++i) {
            const toml::array* pair = array->get(i)->as_array();
            const bool is_pair = pair != nullptr && pair->size() == 2 && pair->get(0)->is_number() &&
                                 pair->get(1)->is_number();
            if (!is_pair) {
                set_error(std::format("{}: '{}[{}]' must be an [x, y] pair of numbers", source_name_,
                                      qualified(key), i));
                return {};
            }
            result.points.push_back({number_from(*pair->get(0), qualified(key)),
                                     number_from(*pair->get(1), qualified(key))});
        }
        return result;
    }

    // 필수 하위 테이블
    TableReader child(std::string_view key) {
        const toml::node* node = lookup(key);
        if (node == nullptr) {
            return failed_child(key);
        }
        if (const toml::table* t = node->as_table()) {
            return TableReader(t, source_name_, qualified(key));
        }
        wrong_type(key, "table");
        return failed_child(key);
    }

    // 테이블 배열 ([[name]]). required가 false면 없어도 되고, 그때는 빈 목록이에요.
    std::vector<TableReader> table_array(std::string_view key, bool required) {
        std::vector<TableReader> result;
        const toml::node* node = lookup(key, required);
        if (node == nullptr) {
            return result;
        }
        const toml::array* array = node->as_array();
        if (array == nullptr || !(array->empty() || array->is_array_of_tables())) {
            wrong_type(key, "array of tables");
            return result;
        }
        for (std::size_t i = 0; i < array->size(); ++i) {
            result.emplace_back(array->get(i)->as_table(), source_name_,
                                std::format("{}[{}]", qualified(key), i));
        }
        return result;
    }

    // 선택 키가 있는지 확인해요 (사용한 키로 기록해요).
    bool has(std::string_view key) {
        used_.emplace(key);
        return table_ != nullptr && table_->contains(key);
    }

    // 정수 4개짜리 배열 (area)
    std::array<int, 4> int_array4(std::string_view key) {
        std::array<int, 4> result{};
        const toml::node* node = lookup(key);
        if (node == nullptr) {
            return result;
        }
        const toml::array* array = node->as_array();
        bool ok = array != nullptr && array->size() == 4;
        for (std::size_t i = 0; ok && i < 4; ++i) {
            const auto value = array->get(i)->value_exact<std::int64_t>();
            ok = value.has_value() && std::in_range<int>(*value);
            if (ok) {
                result[i] = static_cast<int>(*value);
            }
        }
        if (!ok) {
            set_error(std::format("{}: key '{}' must be an array of 4 integers", source_name_,
                                  qualified(key)));
        }
        return result;
    }

    [[nodiscard]] std::string qualified(std::string_view key) const {
        return context_.empty() ? std::string(key) : std::format("{}.{}", context_, key);
    }

    void set_error(std::string message) {
        if (!error_) {
            error_ = Error{ErrorCode::InvalidData, std::move(message)};
        }
    }

    [[nodiscard]] bool ok() const { return !error_.has_value(); }

    [[nodiscard]] std::expected<void, Error> finish() const {
        if (error_) {
            return std::unexpected(*error_);
        }
        if (table_ != nullptr) {
            for (auto&& [key, node] : *table_) {
                if (!used_.contains(key.str())) {
                    return fail(ErrorCode::InvalidData, std::format("{}: unknown key '{}'", source_name_,
                                                                    qualified(key.str())));
                }
            }
        }
        return {};
    }

private:
    const toml::node* lookup(std::string_view key, bool required = true) {
        used_.emplace(key);
        if (table_ == nullptr || error_) {
            return nullptr;
        }
        const toml::node* node = table_->get(key);
        if (node == nullptr && required) {
            set_error(std::format("{}: missing required key '{}'", source_name_, qualified(key)));
        }
        return node;
    }

    // 하위 테이블을 얻지 못했을 때: 이 reader의 오류를 물려받은 빈 reader
    TableReader failed_child(std::string_view key) const {
        TableReader child(nullptr, source_name_, qualified(key));
        child.error_ = error_;
        return child;
    }

    void wrong_type(std::string_view key, std::string_view expected) {
        set_error(std::format("{}: key '{}' must be a {}", source_name_, qualified(key), expected));
    }

    double number_from(const toml::node& node, const std::string& name) {
        if (node.is_integer() || node.is_floating_point()) {
            if (const auto value = node.value<double>(); value && std::isfinite(*value)) {
                return *value;
            }
            set_error(std::format("{}: key '{}' must be finite", source_name_, name));
            return 0.0;
        }
        set_error(std::format("{}: key '{}' must be a number", source_name_, name));
        return 0.0;
    }

    const toml::table* table_;
    std::string_view source_name_;
    std::string context_;
    std::set<std::string, std::less<>> used_;
    std::optional<Error> error_;
};

std::expected<void, Error> check_schema(TableReader& root, std::int64_t expected, std::string_view what,
                                        std::string_view source_name) {
    const std::int64_t version = root.integer("schema_version");
    if (root.ok() && version != expected) {
        return fail(ErrorCode::UnsupportedVersion,
                    std::format("{}: {} schema_version {} is not supported (expected {})", source_name,
                                what, version, expected));
    }
    return {};
}

std::string_view trim(std::string_view s) {
    constexpr std::string_view kSpace = " \t\r";
    const auto first = s.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(kSpace);
    return s.substr(first, last - first + 1);
}

std::vector<std::string_view> split_fields(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t comma = line.find(',', start);
        fields.push_back(trim(line.substr(start, comma - start)));
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return fields;
}

template <typename T>
bool parse_number(std::string_view text, T& out) {
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

// [[schedule]] 한 항목을 (tick, 명령) 목록으로 펼쳐서 out 뒤에 붙여요. 오류는 r에 기록해요.
void read_schedule_entry(TableReader& r, int bed_rows, int bed_cols, std::vector<ScheduledCommand>& out) {
    const std::int64_t tick = r.integer("tick");
    const std::string action = r.string("action");
    if (!r.ok()) {
        return;
    }
    if (tick < 0) {
        r.set_error(std::format("'{}' must not be negative", r.qualified("tick")));
        return;
    }

    // area는 action과 무관하게 같은 키라서 먼저 읽어요. 없으면 하우스 전체예요.
    sim::TileArea area = sim::whole_grid(bed_rows, bed_cols);
    if (r.has("area")) {
        const std::array<int, 4> a = r.int_array4("area");
        area = sim::TileArea{.row = a[0], .col = a[1], .rows = a[2], .cols = a[3]};
    }

    sim::Command command;
    if (action == "irrigate") {
        command = sim::IrrigateCommand{.area = area, .amount_mm = r.number("amount_mm")};
    } else if (action == "add_compost") {
        std::string id = r.string("compost");
        const double kg_per_m2 = r.number("kg_per_m2");
        command = sim::AddCompostCommand{.area = area, .compost_id = std::move(id), .kg_per_m2 = kg_per_m2};
    } else if (action == "add_fertilizer") {
        std::string id = r.string("fertilizer");
        const double g_per_m2 = r.number("g_per_m2");
        command = sim::AddFertilizerCommand{.area = area, .fertilizer_id = std::move(id), .g_per_m2 = g_per_m2};
    } else if (action == "add_lime") {
        std::string id = r.string("lime");
        const double g_per_m2 = r.number("g_per_m2");
        command = sim::AddLimeCommand{.area = area, .lime_id = std::move(id), .g_per_m2 = g_per_m2};
    } else if (action == "till") {
        command = sim::TillCommand{.area = area};
    } else {
        r.set_error(std::format("'{}': unknown action '{}' (expected irrigate, add_compost, add_fertilizer, "
                                "add_lime or till)",
                                r.qualified("action"), action));
        return;
    }

    const bool has_every = r.has("repeat_every_ticks");
    const bool has_count = r.has("repeat_count");
    if (has_every != has_count) {
        r.set_error(std::format("'{}' needs both repeat_every_ticks and repeat_count, or neither",
                                r.qualified("repeat_every_ticks")));
        return;
    }
    std::int64_t every = 1;
    std::int64_t count = 1;
    if (has_every) {
        every = r.integer("repeat_every_ticks");
        count = r.integer("repeat_count");
        if (!r.ok()) {
            return;
        }
        if (every < 1 || count < 1) {
            r.set_error(std::format("'{}': repeat_every_ticks and repeat_count must be at least 1",
                                    r.qualified("repeat_every_ticks")));
            return;
        }
        constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
        if ((count - 1) > (kMax - tick) / every) {
            r.set_error(std::format("'{}': repeated ticks overflow", r.qualified("repeat_count")));
            return;
        }
    }
    if (!r.ok()) {
        return;
    }
    for (std::int64_t i = 0; i < count; ++i) {
        out.push_back(
            ScheduledCommand{.tick = static_cast<std::uint64_t>(tick + i * every), .command = command});
    }
}

}  // namespace

std::expected<sim::Params, Error> parse_params_toml(std::string_view text,
                                                    std::string_view source_name) {
    auto root_table = parse_toml(text, source_name);
    if (!root_table) {
        return std::unexpected(std::move(root_table.error()));
    }
    TableReader root(&*root_table, source_name, "");
    if (auto v = check_schema(root, sim::kParamsSchemaVersion, "params", source_name); !v) {
        return std::unexpected(std::move(v.error()));
    }

    sim::Params p;
    p.schema_version = sim::kParamsSchemaVersion;

    // 하위 테이블마다 읽고 finish()로 오류와 모르는 키를 잡아요. 순서는 params.hpp와 같아요.
    std::expected<void, Error> status;
    auto run = [&](std::string_view name, auto&& read) {
        if (status) {
            TableReader r = root.child(name);
            read(r);
            status = r.finish();
        }
    };

    run("house", [&](TableReader& r) {
        p.house.base_offset_degC = r.number("base_offset_degC");
        p.house.solar_gain_degC_per_W_per_m2 = r.number("solar_gain_degC_per_W_per_m2");
        p.house.solar_transmittance_factor = r.number("solar_transmittance_factor");
    });
    run("soil_water", [&](TableReader& r) {
        p.soil_water.evap_mm_per_hour_per_W_per_m2 = r.number("evap_mm_per_hour_per_W_per_m2");
        p.soil_water.evap_base_mm_per_hour = r.number("evap_base_mm_per_hour");
        p.soil_water.evap_moisture_curve = r.curve("evap_moisture_curve");
        p.soil_water.drainage_rate_per_hour = r.number("drainage_rate_per_hour");
        p.soil_water.drainage_compaction_curve = r.curve("drainage_compaction_curve");
    });
    run("soil_temperature", [&](TableReader& r) {
        p.soil_temperature.relax_rate_per_hour = r.number("relax_rate_per_hour");
        p.soil_temperature.heat_degC_per_g_C = r.number("heat_degC_per_g_C");
    });
    run("organic_matter", [&](TableReader& r) {
        auto& om = p.organic_matter;
        om.labile_decay_rate_per_hour = r.number("labile_decay_rate_per_hour");
        om.stable_decay_rate_per_hour = r.number("stable_decay_rate_per_hour");
        om.humification_fraction = r.number("humification_fraction");
        om.humus_C_to_N_ratio = r.number("humus_C_to_N_ratio");
        om.temp_curve = r.curve("temp_curve");
        om.moisture_curve = r.curve("moisture_curve");
        om.o2_curve = r.curve("o2_curve");
    });
    run("nitrification", [&](TableReader& r) {
        auto& n = p.nitrification;
        n.step1_rate_per_hour = r.number("step1_rate_per_hour");
        n.step2_rate_per_hour = r.number("step2_rate_per_hour");
        n.temp_curve = r.curve("temp_curve");
        n.moisture_curve = r.curve("moisture_curve");
        n.step1_ph_curve = r.curve("step1_ph_curve");
        n.step2_ph_curve = r.curve("step2_ph_curve");
        n.ammonia_fraction_ph_curve = r.curve("ammonia_fraction_ph_curve");
        n.free_ammonia_inhibition_curve = r.curve("free_ammonia_inhibition_curve");
        n.nitrite_toxicity_threshold_g_N_per_m2 = r.number("nitrite_toxicity_threshold_g_N_per_m2");
        n.nitrite_alarm_reset_fraction = r.number("nitrite_alarm_reset_fraction");
        n.step1_o2_curve = r.curve("step1_o2_curve");
        n.step2_o2_curve = r.curve("step2_o2_curve");
        n.nitrite_toxicity_ph_curve = r.curve("nitrite_toxicity_ph_curve");
    });
    run("cation_exchange", [&](TableReader& r) {
        auto& c = p.cation_exchange;
        c.cec_organic_eq_per_g_C = r.number("cec_organic_eq_per_g_C");
        c.exchange_rate_per_hour = r.number("exchange_rate_per_hour");
        c.nh4_retention_factor = r.number("nh4_retention_factor");
        c.k_retention_factor = r.number("k_retention_factor");
        c.ca_retention_factor = r.number("ca_retention_factor");
        c.mg_retention_factor = r.number("mg_retention_factor");
        c.solver_iterations = r.integer("solver_iterations");
        c.buffer_eq_per_pH_per_eq_cec = r.number("buffer_eq_per_pH_per_eq_cec");
    });
    run("antagonism", [&](TableReader& r) {
        p.antagonism.ca_curve = r.curve("ca_curve");
        p.antagonism.mg_curve = r.curve("mg_curve");
        p.antagonism.min_demand_factor = r.number("min_demand_factor");
    });
    run("phosphorus", [&](TableReader& r) {
        p.phosphorus.fixation_rate_per_hour = r.number("fixation_rate_per_hour");
        p.phosphorus.fixation_ph_curve = r.curve("fixation_ph_curve");
        p.phosphorus.release_rate_per_hour = r.number("release_rate_per_hour");
    });
    run("manganese", [&](TableReader& r) {
        auto& m = p.manganese;
        m.retention_ph_curve = r.curve("retention_ph_curve");
        m.oxidation_rate_per_hour = r.number("oxidation_rate_per_hour");
        m.oxidation_ph_curve = r.curve("oxidation_ph_curve");
        m.oxidation_o2_curve = r.curve("oxidation_o2_curve");
        m.reduction_rate_per_hour = r.number("reduction_rate_per_hour");
        m.reduction_ph_curve = r.curve("reduction_ph_curve");
        m.reduction_o2_curve = r.curve("reduction_o2_curve");
    });
    run("aeration", [&](TableReader& r) {
        auto& a = p.aeration;
        a.compaction_cap_curve = r.curve("compaction_cap_curve");
        a.organic_C_cap_bonus_per_g_C = r.number("organic_C_cap_bonus_per_g_C");
        a.compaction_per_mm_irrigation = r.number("compaction_per_mm_irrigation");
        a.tilled_compaction_index = r.number("tilled_compaction_index");
        a.till_root_damage_fraction = r.number("till_root_damage_fraction");
    });
    run("ions", [&](TableReader& r) {
        auto& i = p.ions;
        i.nh4_N_g_per_eq = r.number("nh4_N_g_per_eq");
        i.no3_N_g_per_eq = r.number("no3_N_g_per_eq");
        i.no2_N_g_per_eq = r.number("no2_N_g_per_eq");
        i.k_g_per_eq = r.number("k_g_per_eq");
        i.ca_g_per_eq = r.number("ca_g_per_eq");
        i.mg_g_per_eq = r.number("mg_g_per_eq");
        i.p_g_per_eq = r.number("p_g_per_eq");
        i.s_g_per_eq = r.number("s_g_per_eq");
        i.mn_g_per_eq = r.number("mn_g_per_eq");
    });
    run("soil_chemistry", [&](TableReader& r) {
        auto& c = p.soil_chemistry;
        c.p_solution_fraction = r.number("p_solution_fraction");
        c.s_solution_fraction = r.number("s_solution_fraction");
        c.nitrification_H_eq_per_eq_N = r.number("nitrification_H_eq_per_eq_N");
        c.uptake_H_eq_per_eq_NH4 = r.number("uptake_H_eq_per_eq_NH4");
        c.uptake_H_eq_per_eq_NO3 = r.number("uptake_H_eq_per_eq_NO3");
        c.organic_C_buffer_eq_per_g_C_per_pH = r.number("organic_C_buffer_eq_per_g_C_per_pH");
        c.ph_min = r.number("ph_min");
        c.ph_max = r.number("ph_max");
        c.lime_dissolution_rate_per_hour = r.number("lime_dissolution_rate_per_hour");
        c.lime_dissolution_ph_curve = r.curve("lime_dissolution_ph_curve");
        c.lime_dissolution_moisture_curve = r.curve("lime_dissolution_moisture_curve");
        c.ec_meq_per_L_per_dS_per_m = r.number("ec_meq_per_L_per_dS_per_m");
        c.ec_min_water_mm = r.number("ec_min_water_mm");
    });
    run("availability", [&](TableReader& r) {
        auto& a = p.availability;
        a.n_ph_curve = r.curve("n_ph_curve");
        a.p_ph_curve = r.curve("p_ph_curve");
        a.k_ph_curve = r.curve("k_ph_curve");
        a.ca_ph_curve = r.curve("ca_ph_curve");
        a.mg_ph_curve = r.curve("mg_ph_curve");
    });
    run("root_damage", [&](TableReader& r) {
        auto& d = p.root_damage;
        d.waterlog_curve = r.curve("waterlog_curve");
        d.heat_curve = r.curve("heat_curve");
        d.nitrite_curve = r.curve("nitrite_curve");
        d.acid_curve = r.curve("acid_curve");
    });
    run("crop", [&](TableReader& r) {
        auto& c = p.crop;
        c.specific_leaf_area_m2_per_g = r.number("specific_leaf_area_m2_per_g");
        c.light_interception_curve = r.curve("light_interception_curve");
        c.par_fraction = r.number("par_fraction");
        c.light_use_efficiency_g_per_MJ = r.number("light_use_efficiency_g_per_MJ");
        c.temp_curve = r.curve("temp_curve");
        c.leaf_allocation_fraction = r.number("leaf_allocation_fraction");
        c.water_stress_curve = r.curve("water_stress_curve");
        c.salt_stress_curve = r.curve("salt_stress_curve");
        c.nutrient_stress_curve = r.curve("nutrient_stress_curve");
        c.target_N_g_per_g = r.number("target_N_g_per_g");
        c.target_P_g_per_g = r.number("target_P_g_per_g");
        c.target_K_g_per_g = r.number("target_K_g_per_g");
        c.target_Ca_g_per_g = r.number("target_Ca_g_per_g");
        c.target_Mg_g_per_g = r.number("target_Mg_g_per_g");
        c.luxury_uptake_ratio = r.number("luxury_uptake_ratio");
        c.uptake_rate_per_g_root_per_hour = r.number("uptake_rate_per_g_root_per_hour");
        c.ca_uptake_rate_per_g_root_per_hour = r.number("ca_uptake_rate_per_g_root_per_hour");
        c.mg_uptake_rate_per_g_root_per_hour = r.number("mg_uptake_rate_per_g_root_per_hour");
        c.transpiration_mm_per_hour_per_W_per_m2 = r.number("transpiration_mm_per_hour_per_W_per_m2");
        c.root_recovery_rate_per_hour = r.number("root_recovery_rate_per_hour");
        c.death_root_health = r.number("death_root_health");
        c.root_dieback_per_damage = r.number("root_dieback_per_damage");
        c.dry_matter_C_fraction = r.number("dry_matter_C_fraction");
        c.target_Mn_g_per_g = r.number("target_Mn_g_per_g");
        c.mn_uptake_rate_per_g_root_per_hour = r.number("mn_uptake_rate_per_g_root_per_hour");
        c.mn_max_uptake_ratio = r.number("mn_max_uptake_ratio");
        c.mn_toxicity_curve = r.curve("mn_toxicity_curve");
    });
    if (!status) {
        return std::unexpected(std::move(status.error()));
    }

    for (TableReader& r : root.table_array("compost", true)) {
        sim::CompostDef c;
        c.id = r.string("id");
        c.name = r.string("name");
        c.C_g_per_kg = r.number("C_g_per_kg");
        c.N_g_per_kg = r.number("N_g_per_kg");
        c.labile_C_fraction = r.number("labile_C_fraction");
        c.labile_N_fraction = r.number("labile_N_fraction");
        c.k_g_per_kg = r.number("k_g_per_kg");
        c.ca_g_per_kg = r.number("ca_g_per_kg");
        c.mg_g_per_kg = r.number("mg_g_per_kg");
        c.p_g_per_kg = r.number("p_g_per_kg");
        c.s_g_per_kg = r.number("s_g_per_kg");
        c.mn_g_per_kg = r.number("mn_g_per_kg");
        c.direct_acid_eq_per_kg = r.number("direct_acid_eq_per_kg");
        if (auto done = r.finish(); !done) {
            return std::unexpected(std::move(done.error()));
        }
        p.composts.push_back(std::move(c));
    }
    for (TableReader& r : root.table_array("fertilizer", true)) {
        sim::FertilizerDef f;
        f.id = r.string("id");
        f.name = r.string("name");
        f.nh4_N_g_per_kg = r.number("nh4_N_g_per_kg");
        f.no3_N_g_per_kg = r.number("no3_N_g_per_kg");
        f.k_g_per_kg = r.number("k_g_per_kg");
        f.ca_g_per_kg = r.number("ca_g_per_kg");
        f.mg_g_per_kg = r.number("mg_g_per_kg");
        f.p_g_per_kg = r.number("p_g_per_kg");
        f.s_g_per_kg = r.number("s_g_per_kg");
        f.mn_g_per_kg = r.number("mn_g_per_kg");
        f.direct_acid_eq_per_kg = r.number("direct_acid_eq_per_kg");
        if (auto done = r.finish(); !done) {
            return std::unexpected(std::move(done.error()));
        }
        p.fertilizers.push_back(std::move(f));
    }
    for (TableReader& r : root.table_array("lime", true)) {
        sim::LimeDef l;
        l.id = r.string("id");
        l.name = r.string("name");
        l.eq_per_kg = r.number("eq_per_kg");
        l.ca_fraction = r.number("ca_fraction");
        l.mg_fraction = r.number("mg_fraction");
        if (auto done = r.finish(); !done) {
            return std::unexpected(std::move(done.error()));
        }
        p.limes.push_back(std::move(l));
    }

    if (auto done = root.finish(); !done) {
        return std::unexpected(std::move(done.error()));
    }
    return p;
}

std::expected<ScenarioDocument, Error> parse_scenario_toml(std::string_view text,
                                                           std::string_view source_name) {
    auto root_table = parse_toml(text, source_name);
    if (!root_table) {
        return std::unexpected(std::move(root_table.error()));
    }

    TableReader root(&*root_table, source_name, "");
    if (auto v = check_schema(root, kScenarioFileSchemaVersion, "scenario", source_name); !v) {
        return std::unexpected(std::move(v.error()));
    }

    ScenarioDocument doc;

    TableReader scenario = root.child("scenario");
    doc.scenario.name = scenario.string("name");
    const std::int64_t seed = scenario.integer("seed");
    doc.scenario.start_day_of_year = scenario.small_integer("start_day_of_year");
    doc.weather_csv = scenario.string("weather_csv");
    if (auto done = scenario.finish(); !done) {
        return std::unexpected(std::move(done.error()));
    }
    if (seed < 0) {
        return fail(ErrorCode::InvalidData,
                    std::format("{}: 'scenario.seed' must not be negative", source_name));
    }
    doc.scenario.seed = static_cast<std::uint64_t>(seed);

    TableReader house = root.child("house");
    doc.scenario.bed_rows = house.small_integer("bed_rows");
    doc.scenario.bed_cols = house.small_integer("bed_cols");
    if (auto done = house.finish(); !done) {
        return std::unexpected(std::move(done.error()));
    }

    TableReader soil = root.child("soil");
    sim::SoilInit& s = doc.scenario.soil;
    s.field_capacity_mm = soil.number("field_capacity_mm");
    s.water_mm = soil.number("water_mm");
    s.soil_temp_degC = soil.number("soil_temp_degC");
    s.ph = soil.number("ph");
    s.nh4_N_g_per_m2 = soil.number("nh4_N_g_per_m2");
    s.no2_N_g_per_m2 = soil.number("no2_N_g_per_m2");
    s.no3_N_g_per_m2 = soil.number("no3_N_g_per_m2");
    s.labile_C_g_per_m2 = soil.number("labile_C_g_per_m2");
    s.labile_N_g_per_m2 = soil.number("labile_N_g_per_m2");
    s.stable_C_g_per_m2 = soil.number("stable_C_g_per_m2");
    s.stable_N_g_per_m2 = soil.number("stable_N_g_per_m2");
    s.saturation_mm = soil.number("saturation_mm");
    s.compaction_index = soil.number("compaction_index");
    s.base_buffer_eq_per_m2_per_pH = soil.number("base_buffer_eq_per_m2_per_pH");
    s.k_g_per_m2 = soil.number("k_g_per_m2");
    s.ca_g_per_m2 = soil.number("ca_g_per_m2");
    s.mg_g_per_m2 = soil.number("mg_g_per_m2");
    s.p_g_per_m2 = soil.number("p_g_per_m2");
    s.s_g_per_m2 = soil.number("s_g_per_m2");
    s.cec_mineral_eq_per_m2 = soil.number("cec_mineral_eq_per_m2");
    s.mn_g_per_m2 = soil.number("mn_g_per_m2");
    s.mn_oxide_g_per_m2 = soil.number("mn_oxide_g_per_m2");
    s.p_fixed_g_per_m2 = soil.number("p_fixed_g_per_m2");
    if (auto done = soil.finish(); !done) {
        return std::unexpected(std::move(done.error()));
    }

    // [crop]은 선택이에요. 있으면 두 키가 모두 필수예요.
    if (root.has("crop")) {
        TableReader crop = root.child("crop");
        sim::CropInit init;
        init.leaf_g_per_m2 = crop.number("leaf_g_per_m2");
        init.root_g_per_m2 = crop.number("root_g_per_m2");
        if (auto done = crop.finish(); !done) {
            return std::unexpected(std::move(done.error()));
        }
        doc.scenario.crop = init;
    }

    TableReader difficulty = root.child("difficulty");
    const std::string combination = difficulty.string("stress_combination");
    if (auto done = difficulty.finish(); !done) {
        return std::unexpected(std::move(done.error()));
    }
    if (combination == "minimum") {
        doc.scenario.stress_combination = sim::StressCombination::Minimum;
    } else if (combination == "product") {
        doc.scenario.stress_combination = sim::StressCombination::Product;
    } else {
        return fail(ErrorCode::InvalidData,
                    std::format("{}: key 'difficulty.stress_combination' must be \"minimum\" or \"product\", got '{}'",
                                source_name, combination));
    }

    for (TableReader& entry : root.table_array("schedule", false)) {
        read_schedule_entry(entry, doc.scenario.bed_rows, doc.scenario.bed_cols, doc.schedule);
        if (auto done = entry.finish(); !done) {
            return std::unexpected(std::move(done.error()));
        }
    }
    if (auto done = root.finish(); !done) {
        return std::unexpected(std::move(done.error()));
    }

    // 같은 틱은 파일 순서(펼친 순서)를 유지해요.
    std::stable_sort(doc.schedule.begin(), doc.schedule.end(),
                     [](const ScheduledCommand& a, const ScheduledCommand& b) { return a.tick < b.tick; });
    return doc;
}

std::expected<std::vector<sim::WeatherHour>, Error> parse_weather_csv(
    std::string_view text, std::string_view source_name) {
    std::vector<sim::WeatherHour> rows;
    rows.reserve(sim::kHoursPerYear);

    bool header_seen = false;
    std::size_t line_no = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t newline = text.find('\n', pos);
        const std::string_view raw =
            text.substr(pos, newline == std::string_view::npos ? std::string_view::npos
                                                               : newline - pos);
        pos = newline == std::string_view::npos ? text.size() : newline + 1;
        ++line_no;

        const std::string_view line = trim(raw);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const std::vector<std::string_view> fields = split_fields(line);
        if (!header_seen) {
            if (fields != split_fields(kWeatherCsvHeader)) {
                return fail(ErrorCode::InvalidData,
                            std::format("{}:{}: header must be '{}'", source_name, line_no,
                                        kWeatherCsvHeader));
            }
            header_seen = true;
            continue;
        }

        if (fields.size() != 5) {
            return fail(ErrorCode::InvalidData,
                        std::format("{}:{}: expected 5 fields, got {}", source_name, line_no,
                                    fields.size()));
        }
        if (rows.size() == sim::kHoursPerYear) {
            return fail(ErrorCode::InvalidData,
                        std::format("{}:{}: more than {} data rows", source_name, line_no,
                                    sim::kHoursPerYear));
        }

        int day_of_year = 0;
        int hour = 0;
        sim::WeatherHour w;
        if (!parse_number(fields[0], day_of_year) || !parse_number(fields[1], hour) ||
            !parse_number(fields[2], w.outside_air_temp_degC) ||
            !parse_number(fields[3], w.solar_radiation_W_per_m2) ||
            !parse_number(fields[4], w.outside_rh_percent)) {
            return fail(ErrorCode::InvalidData,
                        std::format("{}:{}: malformed number", source_name, line_no));
        }
        if (!std::isfinite(w.outside_air_temp_degC) || !std::isfinite(w.solar_radiation_W_per_m2) ||
            !std::isfinite(w.outside_rh_percent)) {
            return fail(ErrorCode::InvalidData,
                        std::format("{}:{}: non-finite value", source_name, line_no));
        }

        const auto expected_day = static_cast<int>(rows.size() / sim::kHoursPerDay) + 1;
        const auto expected_hour = static_cast<int>(rows.size() % sim::kHoursPerDay);
        if (day_of_year != expected_day || hour != expected_hour) {
            return fail(ErrorCode::InvalidData,
                        std::format("{}:{}: expected day_of_year={} hour={}, got {} {} "
                                    "(rows must be complete and in order)",
                                    source_name, line_no, expected_day, expected_hour, day_of_year,
                                    hour));
        }
        rows.push_back(w);
    }

    if (!header_seen) {
        return fail(ErrorCode::InvalidData, std::format("{}: missing header", source_name));
    }
    if (rows.size() != sim::kHoursPerYear) {
        return fail(ErrorCode::InvalidData,
                    std::format("{}: expected {} data rows, got {}", source_name,
                                sim::kHoursPerYear, rows.size()));
    }
    return rows;
}

std::expected<sim::Params, Error> load_params(const std::filesystem::path& path) {
    auto text = read_text_file(path);
    if (!text) {
        return std::unexpected(std::move(text.error()));
    }
    return parse_params_toml(*text, path_string(path));
}

std::expected<ScenarioBundle, Error> load_scenario_bundle(const std::filesystem::path& path) {
    auto text = read_text_file(path);
    if (!text) {
        return std::unexpected(std::move(text.error()));
    }
    auto doc = parse_scenario_toml(*text, path_string(path));
    if (!doc) {
        return std::unexpected(std::move(doc.error()));
    }

    const std::filesystem::path weather_path = path.parent_path() / path_from_utf8(doc->weather_csv);
    auto weather_text = read_text_file(weather_path);
    if (!weather_text) {
        return std::unexpected(std::move(weather_text.error()));
    }
    auto weather = parse_weather_csv(*weather_text, path_string(weather_path));
    if (!weather) {
        return std::unexpected(std::move(weather.error()));
    }

    doc->scenario.weather_hourly = std::move(*weather);
    return ScenarioBundle{.scenario = std::move(doc->scenario), .schedule = std::move(doc->schedule)};
}

std::expected<sim::Scenario, Error> load_scenario(const std::filesystem::path& path) {
    auto bundle = load_scenario_bundle(path);
    if (!bundle) {
        return std::unexpected(std::move(bundle.error()));
    }
    return std::move(bundle->scenario);
}

}  // namespace simdata
