#include "sim_world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>

#include "command_codec.hpp"
#include "sim/calendar.hpp"
#include "sim/save_format.hpp"
#include "snapshot_codec.hpp"
#include "text.hpp"

namespace simgodot {

namespace {

// get_engine_state() 사전의 형식 번호. 키를 바꾸면 올려요 (세이브 묶음 안의 엔진 상태, m6-overview 7.2)
constexpr int64_t kEngineStateFormat = 1;

// 윤년 없는 365일 달력에서 달(0 = 1월)이 시작하는 day_of_year (core-api "시간과 달력").
// tools/run_scenarios.py MONTH_START_DOY, tests/test_m4_scenarios.cpp kMonthStartDoy와 같은 표예요.
constexpr std::array<int, 12> kMonthStartDoy = {1, 32, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335};

const char* error_code_name(sim::ErrorCode code) {
    switch (code) {
        case sim::ErrorCode::InvalidArgument: return "InvalidArgument";
        case sim::ErrorCode::InvalidData: return "InvalidData";
        case sim::ErrorCode::UnsupportedVersion: return "UnsupportedVersion";
        case sim::ErrorCode::Truncated: return "Truncated";
        case sim::ErrorCode::Io: return "Io";
    }
    return "Unknown";
}

godot::Dictionary ok_result() {
    godot::Dictionary d;
    d["ok"] = true;
    d["code"] = "";
    d["message"] = "";
    return d;
}

godot::Dictionary error_result(const char* code, std::string_view message) {
    godot::Dictionary d;
    d["ok"] = false;
    d["code"] = code;
    d["message"] = gstr(message);
    return d;
}

godot::Dictionary error_result(const sim::Error& e) {
    return error_result(error_code_name(e.code), e.message);
}

godot::Dictionary not_loaded() {
    return error_result("InvalidArgument", "world not loaded");
}

std::span<const std::byte> byte_span(const godot::PackedByteArray& bytes) {
    return {reinterpret_cast<const std::byte*>(bytes.ptr()), static_cast<std::size_t>(bytes.size())};
}

// FileAccess로 파일 바이트를 그대로 읽어요 (줄바꿈 변환 없음). 실패하면 nullopt.
std::optional<std::string> read_file_bytes(const godot::String& path) {
    if (!godot::FileAccess::file_exists(path)) {
        return std::nullopt;
    }
    const godot::PackedByteArray bytes = godot::FileAccess::get_file_as_bytes(path);
    if (godot::FileAccess::get_open_error() != godot::OK) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(bytes.ptr()), static_cast<std::size_t>(bytes.size()));
}

// JSON을 거친 사전은 정수가 실수가 돼요. 음이 아닌 정수 값이면 받아요.
bool as_count(const godot::Variant& v, std::uint64_t& out) {
    if (v.get_type() == godot::Variant::INT) {
        const int64_t i = v;
        if (i < 0) {
            return false;
        }
        out = static_cast<std::uint64_t>(i);
        return true;
    }
    if (v.get_type() == godot::Variant::FLOAT) {
        const double d = v;
        if (!(d >= 0.0) || d > 9007199254740992.0 || std::floor(d) != d) {
            return false;
        }
        out = static_cast<std::uint64_t>(d);
        return true;
    }
    return false;
}

godot::Dictionary event_to_dict(const sim::Event& e) {
    godot::Dictionary d;
    d["tick"] = static_cast<int64_t>(e.tick);
    std::visit(
        [&](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, sim::CommandAppliedEvent>) {
                d["type"] = "command_applied";
                d["command_seq"] = static_cast<int64_t>(p.command_seq);
            } else if constexpr (std::is_same_v<T, sim::FruitHarvestedEvent>) {
                d["type"] = "fruit_harvested";
                d["tile_index"] = static_cast<int64_t>(p.tile_index);
                d["fruits"] = static_cast<int64_t>(p.fruits);
                d["deformed"] = static_cast<int64_t>(p.deformed);
                d["fresh_g"] = p.fresh_g;
                d["mean_brix"] = p.mean_brix;
                d["mean_firmness"] = p.mean_firmness;
            } else if constexpr (std::is_same_v<T, sim::PlantDiedEvent>) {
                d["type"] = "plant_died";
                d["tile_index"] = static_cast<int64_t>(p.tile_index);
                d["cause"] = id_at(sim::kRootDamageCauseIds, static_cast<std::size_t>(p.cause));
            } else if constexpr (std::is_same_v<T, sim::NitriteThresholdExceededEvent>) {
                d["type"] = "nitrite_threshold_exceeded";
                d["tile_index"] = static_cast<int64_t>(p.tile_index);
                d["no2_N_g_per_m2"] = p.no2_N_g_per_m2;
            } else if constexpr (std::is_same_v<T, sim::DiseaseOnsetEvent>) {
                d["type"] = "disease_onset";
                d["tile_index"] = static_cast<int64_t>(p.tile_index);
                d["disease"] = id_at(sim::kSoilDiseaseIds, static_cast<std::size_t>(p.disease));
            } else if constexpr (std::is_same_v<T, sim::FoliarDiseaseOnsetEvent>) {
                d["type"] = "foliar_disease_onset";
                d["tile_index"] = static_cast<int64_t>(p.tile_index);
                d["disease"] = id_at(sim::kFoliarDiseaseIds, static_cast<std::size_t>(p.disease));
            } else {
                static_assert(sizeof(T) == 0, "unhandled sim::EventPayload alternative");
            }
        },
        e.payload);
    return d;
}

std::string_view trim(std::string_view s) {
    constexpr std::string_view kSpace = " \t\r";
    const auto first = s.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(kSpace) - first + 1);
}

godot::Dictionary log_entry_dict(std::uint64_t tick, std::uint64_t seq, bool from_schedule, const sim::Command& c) {
    godot::Dictionary d;
    d["tick"] = static_cast<int64_t>(tick);
    d["seq"] = static_cast<int64_t>(seq);
    d["source"] = from_schedule ? "schedule" : "player";
    d["command"] = command_to_dict(c);
    return d;
}

}  // namespace

void SimWorld::reset_wrapper_state() {
    schedule_.clear();
    next_command_ = 0;
    ticks_done_ = 0;
    schedule_enabled_ = true;
    events_.clear();
    log_.clear();
    log_taken_ = 0;
    snapshot_.reset();
}

const sim::Snapshot* SimWorld::snap() const {
    if (!world_) {
        return nullptr;
    }
    if (!snapshot_) {
        snapshot_.emplace(world_->snapshot());
    }
    return &*snapshot_;
}

godot::Dictionary SimWorld::load_files(const godot::String& params_path, const godot::String& scenario_path) {
    const auto params_text = read_file_bytes(params_path);
    if (!params_text) {
        return error_result("Io", "cannot read " + to_utf8(params_path));
    }
    auto params = simdata::parse_params_toml(*params_text, to_utf8(params_path));
    if (!params) {
        return error_result(params.error());
    }
    const auto scenario_text = read_file_bytes(scenario_path);
    if (!scenario_text) {
        return error_result("Io", "cannot read " + to_utf8(scenario_path));
    }
    auto doc = simdata::parse_scenario_toml(*scenario_text, to_utf8(scenario_path));
    if (!doc) {
        return error_result(doc.error());
    }
    // 기상 CSV 경로는 시나리오 파일 기준 상대 경로예요 (load_scenario_bundle과 같은 규칙).
    const godot::String weather_path =
        scenario_path.get_base_dir().path_join(gstr(doc->weather_csv)).simplify_path();
    const auto weather_text = read_file_bytes(weather_path);
    if (!weather_text) {
        return error_result("Io", "cannot read " + to_utf8(weather_path));
    }
    auto weather = simdata::parse_weather_csv(*weather_text, to_utf8(weather_path));
    if (!weather) {
        return error_result(weather.error());
    }
    doc->scenario.weather_hourly = std::move(*weather);
    auto world = sim::World::create(std::move(*params), std::move(doc->scenario));
    if (!world) {
        return error_result(world.error());
    }
    world_.emplace(std::move(*world));
    reset_wrapper_state();
    schedule_ = std::move(doc->schedule);
    godot::Dictionary d = ok_result();
    d["weather_path"] = weather_path;
    d["schedule_size"] = static_cast<int64_t>(schedule_.size());
    return d;
}

void SimWorld::set_schedule_enabled(bool enabled) {
    schedule_enabled_ = enabled;
}

bool SimWorld::is_schedule_enabled() const {
    return schedule_enabled_;
}

bool SimWorld::is_loaded() const {
    return world_.has_value();
}

godot::Dictionary SimWorld::advance(int64_t ticks) {
    if (!world_) {
        return not_loaded();
    }
    if (ticks < 0) {
        return error_result("InvalidArgument", "ticks must be non-negative");
    }
    for (int64_t i = 0; i < ticks; ++i) {
        // sim-cli와 같은 순서: 틱 t 직전에 일정표의 tick == t 항목을 넣고 step(1).
        // 플레이어 명령은 enqueue 때 이미 큐에 있어요 (같은 틱 안에서 일정표 항목보다 앞).
        while (next_command_ < schedule_.size() && schedule_[next_command_].tick <= ticks_done_) {
            const simdata::ScheduledCommand& entry = schedule_[next_command_];
            if (schedule_enabled_ && entry.tick == ticks_done_) {
                const auto seq = world_->enqueue(entry.command);
                if (!seq) {
                    snapshot_.reset();
                    return error_result(
                        error_code_name(seq.error().code),
                        std::format("schedule entry for tick {} rejected: {}", ticks_done_, seq.error().message));
                }
                log_.push_back(LogEntry{.tick = ticks_done_, .seq = *seq, .from_schedule = true, .command = entry.command});
            }
            ++next_command_;  // 꺼져 있으면 건너뛰어요 (나중에 다시 켜도 지난 항목은 넣지 않아요)
        }
        world_->step(1);
        ++ticks_done_;
        const auto ev = world_->events();
        events_.insert(events_.end(), ev.begin(), ev.end());
    }
    snapshot_.reset();
    godot::Dictionary d = ok_result();
    d["tick"] = static_cast<int64_t>(ticks_done_);
    return d;
}

godot::String SimWorld::state_hash() const {
    if (!world_) {
        return "";
    }
    return godot::String(std::format("0x{:016x}", world_->state_hash()).c_str());
}

int64_t SimWorld::get_tick() const {
    return static_cast<int64_t>(ticks_done_);
}

godot::Dictionary SimWorld::enqueue(const godot::Dictionary& command) {
    if (!world_) {
        return not_loaded();
    }
    const sim::Scenario& sc = world_->scenario();
    auto parsed = command_from_dict(command, sc.bed_rows, sc.bed_cols);
    if (!parsed) {
        // 로더는 InvalidData를 돌려주지만, 여기서는 호출자가 넘긴 사전이 틀린 것이라 InvalidArgument예요 (4.3).
        return error_result("InvalidArgument", parsed.error());
    }
    sim::Command cmd = std::move(*parsed);
    const auto seq = world_->enqueue(cmd);
    if (!seq) {
        return error_result(seq.error());
    }
    log_.push_back(LogEntry{.tick = ticks_done_, .seq = *seq, .from_schedule = false, .command = std::move(cmd)});
    snapshot_.reset();
    godot::Dictionary d = ok_result();
    d["seq"] = static_cast<int64_t>(*seq);
    d["tick"] = static_cast<int64_t>(ticks_done_);
    return d;
}

godot::Dictionary SimWorld::can_enqueue(const godot::Dictionary& command) const {
    if (!world_) {
        return not_loaded();
    }
    const sim::Scenario& sc = world_->scenario();
    const auto parsed = command_from_dict(command, sc.bed_rows, sc.bed_cols);
    if (!parsed) {
        return error_result("InvalidArgument", parsed.error());
    }
    // 코어 변경 C2: enqueue와 같은 검증, 큐·상태는 그대로 (대기 중인 명령의 효과는 보지 않아요)
    const auto verdict = world_->can_enqueue(*parsed);
    return verdict ? ok_result() : error_result(verdict.error());
}

godot::Array SimWorld::take_events() {
    godot::Array out;
    for (const sim::Event& e : events_) {
        out.push_back(event_to_dict(e));
    }
    events_.clear();
    return out;
}

godot::Array SimWorld::take_command_log() {
    godot::Array out;
    for (std::size_t i = log_taken_; i < log_.size(); ++i) {
        out.push_back(log_entry_dict(log_[i].tick, log_[i].seq, log_[i].from_schedule, log_[i].command));
    }
    log_taken_ = log_.size();
    return out;
}

godot::Array SimWorld::get_command_log() const {
    godot::Array out;
    for (const LogEntry& e : log_) {
        out.push_back(log_entry_dict(e.tick, e.seq, e.from_schedule, e.command));
    }
    return out;
}

godot::String SimWorld::format_schedule_toml(const godot::Array& log) {
    std::string out;
    for (int64_t i = 0; i < log.size(); ++i) {
        const godot::Variant& v = log[i];
        if (v.get_type() != godot::Variant::DICTIONARY) {
            out += std::format("# entry {}: not a dictionary\n\n", i);
            continue;
        }
        const godot::Dictionary e = v;
        std::uint64_t tick = 0;
        if (!as_count(e.get("tick", godot::Variant()), tick) ||
            e.get("command", godot::Variant()).get_type() != godot::Variant::DICTIONARY) {
            out += std::format("# entry {}: needs tick and command\n\n", i);
            continue;
        }
        if (e.has("seq") || e.has("source")) {
            out += "# seq " + to_utf8(e.get("seq", "?").stringify()) + ", source " +
                   to_utf8(e.get("source", "?").stringify()) + "\n";
        }
        const godot::Dictionary command = e["command"];
        const auto text = command_dict_to_toml(command, tick);
        out += text ? *text : std::format("# entry {}: {}\n", i, text.error());
        out += "\n";
    }
    return gstr(out);
}

godot::Dictionary SimWorld::build_replay_scenario(const godot::String& scenario_text, const godot::Array& log,
                                                  const godot::String& weather_csv) {
    // 원래 시나리오에서 [[schedule]] 항목(머리글부터 다음 표 머리글 전까지)을 빼고, 기록을 일정표로 붙여요.
    const std::string src = to_utf8(scenario_text);
    const std::string weather = to_utf8(weather_csv);
    std::string out;
    bool in_schedule = false;
    std::size_t pos = 0;
    while (pos < src.size()) {
        std::size_t end = src.find('\n', pos);
        const std::size_t next = end == std::string::npos ? src.size() : end + 1;
        const std::string_view line(src.data() + pos, next - pos);
        const std::string_view t = trim(line.substr(0, line.size() - (end == std::string::npos ? 0 : 1)));
        pos = next;
        if (t.starts_with("[[schedule]]")) {
            in_schedule = true;
            continue;
        }
        if (in_schedule && t.starts_with("[")) {
            in_schedule = false;
        }
        if (in_schedule) {
            continue;
        }
        if (!weather.empty() && t.starts_with("weather_csv")) {
            const std::string_view rest = trim(t.substr(std::string_view("weather_csv").size()));
            if (rest.starts_with("=")) {
                out += "weather_csv = " + toml_quote(weather) + "\n";
                continue;
            }
        }
        out += line;
        if (end == std::string::npos) {
            out += "\n";
        }
    }
    out += "\n# ---- 명령 기록 (SimWorld.format_schedule_toml) ----\n\n";
    out += to_utf8(format_schedule_toml(log));
    // 검사: 그대로 sim-cli가 읽을 수 있어야 해요 (기상 경로는 확인하지 않아요)
    auto doc = simdata::parse_scenario_toml(out, "replay");
    if (!doc) {
        return error_result(doc.error());
    }
    if (doc->schedule.size() != static_cast<std::size_t>(log.size())) {
        return error_result("InvalidArgument",
                            std::format("replay schedule has {} entries, log has {}", doc->schedule.size(), log.size()));
    }
    godot::Dictionary d = ok_result();
    d["text"] = gstr(out);
    d["schedule_size"] = static_cast<int64_t>(doc->schedule.size());
    return d;
}

godot::Dictionary SimWorld::get_time() const {
    godot::Dictionary d;
    if (!world_) {
        return d;
    }
    // 다음에 처리할 틱(= 지금 화면의 시각)의 달력. Snapshot::time과 같은 코어 함수예요
    const sim::CalendarTime t = sim::calendar_at(world_->scenario().start_day_of_year, ticks_done_);
    int month_index = 0;
    for (std::size_t i = 0; i < kMonthStartDoy.size(); ++i) {
        if (t.day_of_year >= kMonthStartDoy[i]) {
            month_index = static_cast<int>(i);
        }
    }
    d["tick"] = static_cast<int64_t>(ticks_done_);
    d["elapsed_days"] = static_cast<int64_t>(t.elapsed_days);
    d["day_of_year"] = t.day_of_year;
    d["hour"] = t.hour;
    d["month"] = month_index + 1;                                                       // 1~12
    d["day"] = t.day_of_year - kMonthStartDoy[static_cast<std::size_t>(month_index)] + 1;  // 1~31
    return d;
}

godot::Vector2i SimWorld::get_grid_size() const {
    if (!world_) {
        return godot::Vector2i(0, 0);
    }
    return godot::Vector2i(world_->scenario().bed_cols, world_->scenario().bed_rows);
}

int64_t SimWorld::get_pending_command_count() const {
    const sim::Snapshot* s = snap();
    return s ? static_cast<int64_t>(s->pending_command_count) : 0;
}

godot::Dictionary SimWorld::get_catalog() const {
    const sim::Snapshot* s = snap();
    return s ? catalog(world_->params(), *s) : godot::Dictionary();
}

godot::Dictionary SimWorld::get_tile_visuals(double ripening_min_index) const {
    const sim::Snapshot* s = snap();
    return s ? tile_visuals(*s, world_->params(), ripening_min_index) : godot::Dictionary();
}

godot::Dictionary SimWorld::get_house_controls() const {
    const sim::Snapshot* s = snap();
    return s ? house_controls(*s) : godot::Dictionary();
}

godot::Dictionary SimWorld::get_weather() const {
    const sim::Snapshot* s = snap();
    return s ? weather(*s) : godot::Dictionary();
}

godot::Dictionary SimWorld::get_harvest_totals() const {
    const sim::Snapshot* s = snap();
    return s ? harvest_totals(*s) : godot::Dictionary();
}

godot::Dictionary SimWorld::get_true_house() const {
    const sim::Snapshot* s = snap();
    return s ? true_house(*s) : godot::Dictionary();
}

godot::Dictionary SimWorld::get_true_cell(int64_t index) const {
    const sim::Snapshot* s = snap();
    if (!s || index < 0 || static_cast<std::size_t>(index) >= s->soil.size()) {
        return godot::Dictionary();
    }
    return true_cell(*s, world_->params(), static_cast<std::size_t>(index));
}

godot::Dictionary SimWorld::get_true_crop(int64_t index) const {
    const sim::Snapshot* s = snap();
    if (!s || index < 0 || static_cast<std::size_t>(index) >= s->crops.size()) {
        return godot::Dictionary();
    }
    return true_crop(*s, world_->params(), static_cast<std::size_t>(index));
}

godot::Array SimWorld::get_true_fruits(int64_t index) const {
    const sim::Snapshot* s = snap();
    if (!s || index < 0 || static_cast<std::size_t>(index) >= s->crops.size()) {
        return godot::Array();
    }
    return true_fruits(*s, static_cast<std::size_t>(index));
}

godot::PackedFloat64Array SimWorld::get_true_column(const godot::String& kind, const godot::String& field) const {
    godot::PackedFloat64Array out;
    const sim::Snapshot* s = snap();
    if (s) {
        (void)true_column(*s, world_->params(), kind, field, out);
    }
    return out;
}

godot::Dictionary SimWorld::get_true_ledger() const {
    const sim::Snapshot* s = snap();
    return s ? true_ledger(*s, world_->params()) : godot::Dictionary();
}

godot::PackedByteArray SimWorld::save_bytes() const {
    godot::PackedByteArray out;
    if (!world_) {
        return out;
    }
    const std::vector<std::byte> bytes = world_->serialize();
    out.resize(static_cast<int64_t>(bytes.size()));
    if (!bytes.empty()) {
        std::copy(bytes.begin(), bytes.end(), reinterpret_cast<std::byte*>(out.ptrw()));
    }
    return out;
}

godot::Dictionary SimWorld::get_engine_state() const {
    godot::Dictionary d;
    d["format"] = kEngineStateFormat;
    d["tick"] = static_cast<int64_t>(ticks_done_);
    d["schedule_enabled"] = schedule_enabled_;
    d["next_schedule_index"] = static_cast<int64_t>(next_command_);
    d["schedule_toml"] = gstr(format_schedule_entries(schedule_, {}));
    std::vector<simdata::ScheduledCommand> log_entries;
    std::vector<std::string> comments;
    godot::Array seqs;
    godot::Array sources;
    for (const LogEntry& e : log_) {
        log_entries.push_back(simdata::ScheduledCommand{.tick = e.tick, .command = e.command});
        comments.push_back(std::format("seq {}, source {}", e.seq, e.from_schedule ? "schedule" : "player"));
        seqs.push_back(static_cast<int64_t>(e.seq));
        sources.push_back(e.from_schedule ? "schedule" : "player");
    }
    d["log_toml"] = gstr(format_schedule_entries(log_entries, comments));
    d["log_seq"] = seqs;
    d["log_source"] = sources;
    d["log_taken"] = static_cast<int64_t>(log_taken_);
    return d;
}

godot::Dictionary SimWorld::load_bytes(const godot::PackedByteArray& bytes, const godot::Dictionary& engine_state) {
    auto world = sim::World::deserialize(byte_span(bytes));
    if (!world) {
        return error_result(world.error());
    }
    const sim::Snapshot loaded = world->snapshot();

    // 엔진 쪽 상태(일정표·기록)를 먼저 다 읽고, 성공했을 때만 바꿔요 (실패하면 이전 월드 그대로)
    std::vector<simdata::ScheduledCommand> schedule;
    std::size_t next_command = 0;
    bool schedule_enabled = true;
    std::vector<LogEntry> log;
    std::size_t log_taken = 0;
    if (!engine_state.is_empty()) {
        std::uint64_t format = 0;
        std::uint64_t tick = 0;
        std::uint64_t next = 0;
        std::uint64_t taken = 0;
        if (!as_count(engine_state.get("format", godot::Variant()), format) || format != kEngineStateFormat) {
            return error_result("UnsupportedVersion", std::format("engine state format must be {}", kEngineStateFormat));
        }
        if (!as_count(engine_state.get("tick", godot::Variant()), tick) || tick != loaded.tick) {
            return error_result("InvalidData", std::format("engine state tick does not match core tick {}", loaded.tick));
        }
        const godot::Variant enabled = engine_state.get("schedule_enabled", godot::Variant());
        if (enabled.get_type() != godot::Variant::BOOL ||
            !as_count(engine_state.get("next_schedule_index", godot::Variant()), next) ||
            !as_count(engine_state.get("log_taken", godot::Variant()), taken) ||
            engine_state.get("schedule_toml", godot::Variant()).get_type() != godot::Variant::STRING ||
            engine_state.get("log_toml", godot::Variant()).get_type() != godot::Variant::STRING ||
            engine_state.get("log_seq", godot::Variant()).get_type() != godot::Variant::ARRAY ||
            engine_state.get("log_source", godot::Variant()).get_type() != godot::Variant::ARRAY) {
            return error_result("InvalidData", "engine state is missing keys or has wrong types");
        }
        schedule_enabled = enabled;
        const int rows = loaded.bed_rows;
        const int cols = loaded.bed_cols;
        const godot::String schedule_toml = engine_state["schedule_toml"];
        const godot::String log_toml = engine_state["log_toml"];
        auto parsed_schedule = simdata::parse_schedule_toml(to_utf8(schedule_toml), rows, cols, "engine_state.schedule");
        if (!parsed_schedule) {
            return error_result(parsed_schedule.error());
        }
        auto parsed_log = simdata::parse_schedule_toml(to_utf8(log_toml), rows, cols, "engine_state.log");
        if (!parsed_log) {
            return error_result(parsed_log.error());
        }
        const godot::Array seqs = engine_state["log_seq"];
        const godot::Array sources = engine_state["log_source"];
        if (static_cast<std::size_t>(seqs.size()) != parsed_log->size() ||
            static_cast<std::size_t>(sources.size()) != parsed_log->size() || next > parsed_schedule->size() ||
            taken > parsed_log->size()) {
            return error_result("InvalidData", "engine state sizes do not match");
        }
        for (std::size_t i = 0; i < parsed_log->size(); ++i) {
            std::uint64_t seq = 0;
            if (!as_count(seqs[static_cast<int64_t>(i)], seq)) {
                return error_result("InvalidData", "engine state log_seq must hold integers");
            }
            const godot::Variant source_v = sources[static_cast<int64_t>(i)];
            const godot::String source = source_v.get_type() == godot::Variant::STRING ? godot::String(source_v) : "";
            if (source != "schedule" && source != "player") {
                return error_result("InvalidData", "engine state log_source must hold \"player\" or \"schedule\"");
            }
            log.push_back(LogEntry{.tick = (*parsed_log)[i].tick,
                                   .seq = seq,
                                   .from_schedule = source == "schedule",
                                   .command = std::move((*parsed_log)[i].command)});
        }
        schedule = std::move(*parsed_schedule);
        next_command = static_cast<std::size_t>(next);
        log_taken = static_cast<std::size_t>(taken);
    }

    world_.emplace(std::move(*world));
    reset_wrapper_state();
    ticks_done_ = loaded.tick;
    schedule_ = std::move(schedule);
    next_command_ = next_command;
    schedule_enabled_ = schedule_enabled;
    log_ = std::move(log);
    log_taken_ = log_taken;
    godot::Dictionary d = ok_result();
    d["tick"] = static_cast<int64_t>(ticks_done_);
    return d;
}

godot::Dictionary SimWorld::peek_save_version(const godot::PackedByteArray& bytes) {
    // 코어 변경 C1: 머리글만 읽어요. version은 읽혔을 때만, supported = 지금 코어가 불러올 수 있는 버전인지
    const auto version = sim::peek_save_format_version(byte_span(bytes));
    if (!version) {
        godot::Dictionary d = error_result(version.error());
        d["version"] = static_cast<int64_t>(-1);
        d["supported"] = false;
        return d;
    }
    godot::Dictionary d = ok_result();
    d["version"] = static_cast<int64_t>(*version);
    d["supported"] = *version == sim::kSaveFormatVersion;
    return d;
}

int64_t SimWorld::core_save_version() {
    return static_cast<int64_t>(sim::kSaveFormatVersion);
}

void SimWorld::_bind_methods() {
    using godot::ClassDB;
    using godot::D_METHOD;
    ClassDB::bind_method(D_METHOD("load_files", "params_path", "scenario_path"), &SimWorld::load_files);
    ClassDB::bind_method(D_METHOD("is_loaded"), &SimWorld::is_loaded);
    ClassDB::bind_method(D_METHOD("set_schedule_enabled", "enabled"), &SimWorld::set_schedule_enabled);
    ClassDB::bind_method(D_METHOD("is_schedule_enabled"), &SimWorld::is_schedule_enabled);
    ClassDB::bind_method(D_METHOD("advance", "ticks"), &SimWorld::advance);
    ClassDB::bind_method(D_METHOD("state_hash"), &SimWorld::state_hash);
    ClassDB::bind_method(D_METHOD("get_tick"), &SimWorld::get_tick);

    ClassDB::bind_method(D_METHOD("enqueue", "command"), &SimWorld::enqueue);
    ClassDB::bind_method(D_METHOD("can_enqueue", "command"), &SimWorld::can_enqueue);
    ClassDB::bind_method(D_METHOD("take_events"), &SimWorld::take_events);
    ClassDB::bind_method(D_METHOD("take_command_log"), &SimWorld::take_command_log);
    ClassDB::bind_method(D_METHOD("get_command_log"), &SimWorld::get_command_log);
    ClassDB::bind_static_method("SimWorld", D_METHOD("format_schedule_toml", "log"), &SimWorld::format_schedule_toml);
    ClassDB::bind_static_method("SimWorld", D_METHOD("build_replay_scenario", "scenario_text", "log", "weather_csv"),
                                &SimWorld::build_replay_scenario);

    ClassDB::bind_method(D_METHOD("get_time"), &SimWorld::get_time);
    ClassDB::bind_method(D_METHOD("get_grid_size"), &SimWorld::get_grid_size);
    ClassDB::bind_method(D_METHOD("get_pending_command_count"), &SimWorld::get_pending_command_count);
    ClassDB::bind_method(D_METHOD("get_catalog"), &SimWorld::get_catalog);
    ClassDB::bind_method(D_METHOD("get_tile_visuals", "ripening_min_index"), &SimWorld::get_tile_visuals);
    ClassDB::bind_method(D_METHOD("get_house_controls"), &SimWorld::get_house_controls);
    ClassDB::bind_method(D_METHOD("get_weather"), &SimWorld::get_weather);
    ClassDB::bind_method(D_METHOD("get_harvest_totals"), &SimWorld::get_harvest_totals);

    ClassDB::bind_method(D_METHOD("get_true_house"), &SimWorld::get_true_house);
    ClassDB::bind_method(D_METHOD("get_true_cell", "index"), &SimWorld::get_true_cell);
    ClassDB::bind_method(D_METHOD("get_true_crop", "index"), &SimWorld::get_true_crop);
    ClassDB::bind_method(D_METHOD("get_true_fruits", "index"), &SimWorld::get_true_fruits);
    ClassDB::bind_method(D_METHOD("get_true_column", "kind", "field"), &SimWorld::get_true_column);
    ClassDB::bind_method(D_METHOD("get_true_ledger"), &SimWorld::get_true_ledger);

    ClassDB::bind_method(D_METHOD("save_bytes"), &SimWorld::save_bytes);
    ClassDB::bind_method(D_METHOD("get_engine_state"), &SimWorld::get_engine_state);
    ClassDB::bind_method(D_METHOD("load_bytes", "bytes", "engine_state"), &SimWorld::load_bytes,
                         DEFVAL(godot::Dictionary()));
    ClassDB::bind_static_method("SimWorld", D_METHOD("peek_save_version", "bytes"), &SimWorld::peek_save_version);
    ClassDB::bind_static_method("SimWorld", D_METHOD("core_save_version"), &SimWorld::core_save_version);
}

}  // namespace simgodot
