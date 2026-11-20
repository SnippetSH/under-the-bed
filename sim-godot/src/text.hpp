#pragma once
// std::string(UTF-8) <-> godot::String 변환. 래퍼의 모든 글자는 UTF-8로 주고받아요.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/string.hpp>

namespace simgodot {

[[nodiscard]] inline godot::String gstr(std::string_view s) {
    return godot::String::utf8(s.data(), static_cast<int64_t>(s.size()));
}

[[nodiscard]] inline std::string to_utf8(const godot::String& s) {
    const godot::CharString cs = s.utf8();
    return std::string(cs.get_data(), static_cast<std::size_t>(cs.length()));
}

// 코어 id 표에서 i번째 이름. 범위 밖이면 "unknown" (코어 열거형과 표가 어긋났을 때만 생겨요)
template <std::size_t N>
[[nodiscard]] godot::String id_at(const std::array<std::string_view, N>& ids, std::size_t i) {
    return i < N ? gstr(ids[i]) : godot::String("unknown");
}

}  // namespace simgodot
