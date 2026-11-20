#pragma once
// 명령 사전 <-> sim::Command 변환 (m6-overview 4.3). 프로토타입(proto/m6-check) 코드예요.
//
// 사전 -> 명령은 직접 해석하지 않아요: 사전을 시나리오 [[schedule]] 항목 글자(TOML)로 적은 뒤
// sim-data의 parse_schedule_toml(로더의 read_schedule_entry와 같은 코드)로 읽어요. 그래서 키 이름, 빠진 키,
// 모르는 키, 타입 검사가 시나리오 파일과 완전히 같아요. 명령 기록을 일정표로 내보낼 때도 같은 글자 형식을 써요.

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include <godot_cpp/variant/dictionary.hpp>

#include "sim/command.hpp"
#include "simdata/loader.hpp"

namespace simgodot {

// 사전 하나를 "[[schedule]]\ntick = <tick>\n..." 글자로. 실패하면 오류 문장 (영어).
// tick·repeat_* 키는 래퍼가 정하므로 사전에 있으면 거부해요.
[[nodiscard]] std::expected<std::string, std::string> command_dict_to_toml(const godot::Dictionary& command,
                                                                           std::uint64_t tick);

// sim::Command -> 사전 (키는 [[schedule]]과 같음, 영역 명령은 area를 항상 적어요). NoopCommand는 {action = "noop"}.
[[nodiscard]] godot::Dictionary command_to_dict(const sim::Command& command);

// ScheduledCommand 목록 -> [[schedule]] 글자. 앞 주석 줄(빈 문자열이면 없음)을 항목마다 붙일 수 있어요.
[[nodiscard]] std::string format_schedule_entries(const std::vector<simdata::ScheduledCommand>& entries,
                                                  const std::vector<std::string>& comments);

// TOML 기본 문자열 따옴표 (이스케이프 포함)
[[nodiscard]] std::string toml_quote(std::string_view s);

}  // namespace simgodot
