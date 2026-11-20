#pragma once
// SimWorld: sim::World를 Godot에서 쓰는 얇은 래퍼 (m6-overview 3·4장, 17.3).
// 메서드 목록과 사전 모양은 game/README_binding.md에 있어요.
//
// 원칙
// - 변환만 해요. 시뮬레이션 계산·반올림·보정은 하지 않아요(유일한 파생값은 CLI도 쓰는 nutrient_status).
// - 예외는 경계를 넘지 않아요: 실패할 수 있는 호출은 {ok, code, message} 사전을 돌려줘요(ADR-0005).
// - 틱 진행 순서는 sim-cli와 같아요: 틱 t 직전에 일정표의 tick == t 항목을 enqueue → step(1) → 이벤트 모으기.
// - 호출은 전부 Godot 메인 스레드에서 해요(m6-overview 3.3). 내부 잠금은 없어요.

#include <cstdint>
#include <optional>
#include <vector>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include "sim/world.hpp"
#include "simdata/loader.hpp"

namespace simgodot {

class SimWorld : public godot::RefCounted {
    GDCLASS(SimWorld, godot::RefCounted)

public:
    // ---- 만들기·진행 ----
    godot::Dictionary load_files(const godot::String& params_path, const godot::String& scenario_path);
    void set_schedule_enabled(bool enabled);
    bool is_schedule_enabled() const;
    godot::Dictionary advance(int64_t ticks);
    godot::String state_hash() const;
    int64_t get_tick() const;
    bool is_loaded() const;

    // ---- 명령 ----
    godot::Dictionary enqueue(const godot::Dictionary& command);
    godot::Dictionary can_enqueue(const godot::Dictionary& command) const;
    godot::Array take_events();
    godot::Array take_command_log();
    godot::Array get_command_log() const;
    static godot::String format_schedule_toml(const godot::Array& log);
    static godot::Dictionary build_replay_scenario(const godot::String& scenario_text, const godot::Array& log,
                                                   const godot::String& weather_csv);

    // ---- 보이는 것 ----
    godot::Dictionary get_time() const;
    godot::Vector2i get_grid_size() const;
    int64_t get_pending_command_count() const;
    godot::Dictionary get_catalog() const;
    godot::Dictionary get_tile_visuals(double ripening_min_index) const;
    godot::Dictionary get_house_controls() const;
    godot::Dictionary get_weather() const;
    godot::Dictionary get_harvest_totals() const;

    // ---- 진짜 값 (개발자 패널 전용, m6-overview 4.1 "get_true_*") ----
    godot::Dictionary get_true_house() const;
    godot::Dictionary get_true_cell(int64_t index) const;
    godot::Dictionary get_true_crop(int64_t index) const;
    godot::Array get_true_fruits(int64_t index) const;
    godot::PackedFloat64Array get_true_column(const godot::String& kind, const godot::String& field) const;
    godot::Dictionary get_true_ledger() const;

    // ---- 세이브 ----
    godot::PackedByteArray save_bytes() const;
    godot::Dictionary get_engine_state() const;
    godot::Dictionary load_bytes(const godot::PackedByteArray& bytes, const godot::Dictionary& engine_state);
    static godot::Dictionary peek_save_version(const godot::PackedByteArray& bytes);
    static int64_t core_save_version();

protected:
    static void _bind_methods();

private:
    struct LogEntry {
        std::uint64_t tick;
        std::uint64_t seq;
        bool from_schedule;
        sim::Command command;
    };

    const sim::Snapshot* snap() const;  // advance·enqueue·load 뒤 첫 조회 때 한 번 복사해 둬요
    void reset_wrapper_state();

    std::optional<sim::World> world_;
    std::vector<simdata::ScheduledCommand> schedule_;
    std::size_t next_command_ = 0;  // 다음에 볼 일정표 항목
    std::uint64_t ticks_done_ = 0;  // 처리한 틱 수 = sim-cli 루프 변수 t = 코어 tick
    bool schedule_enabled_ = true;
    std::vector<sim::Event> events_;  // take_events 전까지 쌓여요
    std::vector<LogEntry> log_;       // 받아들여진 명령 전부 (seq 순서)
    std::size_t log_taken_ = 0;       // take_command_log가 여기까지 가져갔어요
    mutable std::optional<sim::Snapshot> snapshot_;
};

}  // namespace simgodot
