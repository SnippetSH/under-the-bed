#pragma once
// SimWorld: sim::World를 Godot에서 쓰는 얇은 래퍼 (m6-overview 4장). 프로토타입(proto/m6-check) P0~P3 범위.
// 메서드 목록과 사전 모양은 game/README_binding.md에 있어요.

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
    // 확장이 불렸음을 보여주는 확인용 글자 (P0)
    static godot::String hello();
    // 지금 스레드의 부동소수점 제어 상태 (R2/U5)
    static godot::Dictionary fp_state();

    // ---- 만들기·진행 ----
    godot::Dictionary load_files(const godot::String& params_path, const godot::String& scenario_path);
    void set_schedule_enabled(bool enabled);
    bool is_schedule_enabled() const;
    godot::Dictionary advance(int64_t ticks);
    godot::String state_hash() const;
    int64_t get_tick() const;

    // ---- 명령 ----
    godot::Dictionary enqueue(const godot::Dictionary& command);
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

    // ---- 진짜 값 (개발자 패널 전용) ----
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
    std::size_t next_command_ = 0;
    std::uint64_t ticks_done_ = 0;  // sim-cli 루프 변수 t와 같아요
    bool schedule_enabled_ = true;
    std::vector<sim::Event> events_;  // take_events 전까지 쌓여요
    std::vector<LogEntry> log_;       // 받아들여진 명령 전부 (seq 순서)
    std::size_t log_taken_ = 0;
    mutable std::optional<sim::Snapshot> snapshot_;
};

}  // namespace simgodot
