#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sim {

// FNV-1a 64비트. 결정론 검증용 상태 해시에 써요 (ADR-0004). 암호학적 용도가 아니에요.
[[nodiscard]] inline std::uint64_t fnv1a64(std::span<const std::byte> data) {
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffsetBasis;
    for (const std::byte b : data) {
        hash ^= std::to_integer<std::uint64_t>(b);
        hash *= kPrime;
    }
    return hash;
}

}  // namespace sim
