#pragma once
// 세이브 형식 버전 (m6-overview 2.3 C1). 형식 자체는 sim-core/src/serialization.cpp 머리 주석에 있어요.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include "sim/error.hpp"

namespace sim {

// World::serialize()가 쓰는 형식 버전. 필드를 더하면 올려요
// (M0 = 1, M1 = 2, M2 = 3, 뿌리 고사 = 4, M3 = 5, M4 = 6, M5a = 7, M5b = 8).
// World::deserialize()는 이 값과 다른 버전을 UnsupportedVersion으로 거부해요.
inline constexpr std::uint32_t kSaveFormatVersion = 8;

// 세이브 바이트의 머리글(매직 + 버전)만 읽어 형식 버전을 돌려줘요. 전체를 역직렬화하지 않아요.
// 버전이 kSaveFormatVersion과 달라도 그 값을 돌려줘요(비교는 호출자 몫).
// 머리글 오류는 World::deserialize와 같아요: 매직이 다르거나 매직 전에 끝나면 InvalidData, 버전 중간에 끝나면 Truncated.
[[nodiscard]] std::expected<std::uint32_t, Error> peek_save_format_version(std::span<const std::byte> bytes);

}  // namespace sim
