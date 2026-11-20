#pragma once
// sim::Snapshot -> Godot 값 (m6-overview 4.2). 변환만 하고 계산은 하지 않아요.
// 예외: nutrient_status(CLI와 같은 코어 함수), 과실 개수 세기, 수확 합계(칸별 HarvestRecord 합)는 개수·합만 해요.
// 프로토타입(proto/m6-check) 코드예요.

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "sim/params.hpp"
#include "sim/snapshot.hpp"

namespace simgodot {

// 보이는 것 (게임 화면이 쓰는 유일한 칸별 상태). ripening_min_index는 게임 쪽 표현 문턱이에요.
[[nodiscard]] godot::Dictionary tile_visuals(const sim::Snapshot& s, const sim::Params& p, double ripening_min_index);
[[nodiscard]] godot::Dictionary house_controls(const sim::Snapshot& s);
[[nodiscard]] godot::Dictionary weather(const sim::Snapshot& s);
[[nodiscard]] godot::Dictionary harvest_totals(const sim::Snapshot& s);
[[nodiscard]] godot::Dictionary catalog(const sim::Params& p, const sim::Snapshot& s);

// 진짜 값 (개발자 패널 전용)
[[nodiscard]] godot::Dictionary true_house(const sim::Snapshot& s);
[[nodiscard]] godot::Dictionary true_ledger(const sim::Snapshot& s, const sim::Params& p);
[[nodiscard]] godot::Dictionary true_cell(const sim::Snapshot& s, const sim::Params& p, std::size_t index);
[[nodiscard]] godot::Dictionary true_crop(const sim::Snapshot& s, const sim::Params& p, std::size_t index);
[[nodiscard]] godot::Array true_fruits(const sim::Snapshot& s, std::size_t index);
// kind = "soil" | "crop", field = 실수 필드 이름 또는 "microbe_index.<id>" 같은 배열 필드. 모르면 빈 배열 + ok=false
[[nodiscard]] bool true_column(const sim::Snapshot& s, const sim::Params& p, const godot::String& kind,
                               const godot::String& field, godot::PackedFloat64Array& out);
// PlantDied 이벤트의 원인 이름 (sim-cli kDeathCauseNames와 같은 표)
[[nodiscard]] godot::String root_damage_cause_id(sim::RootDamageCause cause);

}  // namespace simgodot
