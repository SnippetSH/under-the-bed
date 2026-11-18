#include "simdata/loader.hpp"

#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
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
class TableReader {
public:
    TableReader(const toml::table& table, std::string_view source_name, std::string context)
        : table_(table), source_name_(source_name), context_(std::move(context)) {}

    std::expected<std::int64_t, Error> integer(std::string_view key) {
        const toml::node* node = lookup(key);
        if (node == nullptr) {
            return missing(key);
        }
        if (const auto value = node->value_exact<std::int64_t>()) {
            return *value;
        }
        return wrong_type(key, "integer");
    }

    std::expected<int, Error> small_integer(std::string_view key) {
        auto value = integer(key);
        if (!value) {
            return std::unexpected(std::move(value.error()));
        }
        if (!std::in_range<int>(*value)) {
            return fail(ErrorCode::InvalidData,
                        std::format("{}: '{}' is out of range", source_name_, qualified(key)));
        }
        return static_cast<int>(*value);
    }

    std::expected<std::string, Error> string(std::string_view key) {
        const toml::node* node = lookup(key);
        if (node == nullptr) {
            return missing(key);
        }
        if (auto value = node->value_exact<std::string>()) {
            return std::move(*value);
        }
        return wrong_type(key, "string");
    }

    std::expected<const toml::table*, Error> table(std::string_view key) {
        const toml::node* node = lookup(key);
        if (node == nullptr) {
            return missing(key);
        }
        if (const toml::table* t = node->as_table()) {
            return t;
        }
        return wrong_type(key, "table");
    }

    [[nodiscard]] std::string qualified(std::string_view key) const {
        return context_.empty() ? std::string(key) : std::format("{}.{}", context_, key);
    }

    std::expected<void, Error> finish() const {
        for (auto&& [key, node] : table_) {
            if (!used_.contains(key.str())) {
                return fail(ErrorCode::InvalidData, std::format("{}: unknown key '{}'", source_name_,
                                                                qualified(key.str())));
            }
        }
        return {};
    }

private:
    const toml::node* lookup(std::string_view key) {
        used_.emplace(key);
        return table_.get(key);
    }

    std::unexpected<Error> missing(std::string_view key) const {
        return fail(ErrorCode::InvalidData,
                    std::format("{}: missing required key '{}'", source_name_, qualified(key)));
    }

    std::unexpected<Error> wrong_type(std::string_view key, std::string_view expected) const {
        return fail(ErrorCode::InvalidData, std::format("{}: key '{}' must be a {}", source_name_,
                                                        qualified(key), expected));
    }

    const toml::table& table_;
    std::string_view source_name_;
    std::string context_;
    std::set<std::string, std::less<>> used_;
};

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

}  // namespace

std::expected<sim::Params, Error> parse_params_toml(std::string_view text,
                                                    std::string_view source_name) {
    auto root = parse_toml(text, source_name);
    if (!root) {
        return std::unexpected(std::move(root.error()));
    }
    TableReader reader(*root, source_name, "");

    auto schema_version = reader.integer("schema_version");
    if (!schema_version) {
        return std::unexpected(std::move(schema_version.error()));
    }
    if (auto done = reader.finish(); !done) {
        return std::unexpected(std::move(done.error()));
    }
    return sim::Params{.schema_version = *schema_version};
}

std::expected<ScenarioDocument, Error> parse_scenario_toml(std::string_view text,
                                                           std::string_view source_name) {
    auto root = parse_toml(text, source_name);
    if (!root) {
        return std::unexpected(std::move(root.error()));
    }

    TableReader root_reader(*root, source_name, "");
    auto schema_version = root_reader.integer("schema_version");
    if (!schema_version) {
        return std::unexpected(std::move(schema_version.error()));
    }
    if (*schema_version != kScenarioFileSchemaVersion) {
        return fail(ErrorCode::UnsupportedVersion,
                    std::format("{}: scenario schema_version {} is not supported (expected {})",
                                source_name, *schema_version, kScenarioFileSchemaVersion));
    }
    auto scenario_table = root_reader.table("scenario");
    if (!scenario_table) {
        return std::unexpected(std::move(scenario_table.error()));
    }
    auto house_table = root_reader.table("house");
    if (!house_table) {
        return std::unexpected(std::move(house_table.error()));
    }
    if (auto done = root_reader.finish(); !done) {
        return std::unexpected(std::move(done.error()));
    }

    ScenarioDocument doc;

    TableReader scenario_reader(**scenario_table, source_name, "scenario");
    auto name = scenario_reader.string("name");
    auto seed = scenario_reader.integer("seed");
    auto start_day = scenario_reader.small_integer("start_day_of_year");
    auto weather_csv = scenario_reader.string("weather_csv");
    if (!name) return std::unexpected(std::move(name.error()));
    if (!seed) return std::unexpected(std::move(seed.error()));
    if (!start_day) return std::unexpected(std::move(start_day.error()));
    if (!weather_csv) return std::unexpected(std::move(weather_csv.error()));
    if (auto done = scenario_reader.finish(); !done) {
        return std::unexpected(std::move(done.error()));
    }
    if (*seed < 0) {
        return fail(ErrorCode::InvalidData,
                    std::format("{}: 'scenario.seed' must not be negative", source_name));
    }

    TableReader house_reader(**house_table, source_name, "house");
    auto bed_rows = house_reader.small_integer("bed_rows");
    auto bed_cols = house_reader.small_integer("bed_cols");
    if (!bed_rows) return std::unexpected(std::move(bed_rows.error()));
    if (!bed_cols) return std::unexpected(std::move(bed_cols.error()));
    if (auto done = house_reader.finish(); !done) {
        return std::unexpected(std::move(done.error()));
    }

    doc.scenario.name = std::move(*name);
    doc.scenario.seed = static_cast<std::uint64_t>(*seed);
    doc.scenario.start_day_of_year = *start_day;
    doc.scenario.bed_rows = *bed_rows;
    doc.scenario.bed_cols = *bed_cols;
    doc.weather_csv = std::move(*weather_csv);
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

std::expected<sim::Scenario, Error> load_scenario(const std::filesystem::path& path) {
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
    return std::move(doc->scenario);
}

}  // namespace simdata
