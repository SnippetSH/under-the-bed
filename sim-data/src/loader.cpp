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
    } else {
        r.set_error(std::format("'{}': unknown action '{}' (expected irrigate, add_compost or add_fertilizer)",
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
    });
    run("leaching", [&](TableReader& r) {
        p.leaching.nh4_solution_fraction = r.number("nh4_solution_fraction");
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
        if (auto done = r.finish(); !done) {
            return std::unexpected(std::move(done.error()));
        }
        p.fertilizers.push_back(std::move(f));
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
    if (auto done = soil.finish(); !done) {
        return std::unexpected(std::move(done.error()));
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
