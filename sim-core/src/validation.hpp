#pragma once

// 파라미터·시나리오 값 검증 (World::create, World::deserialize가 사용)
// M2 항목 검증은 패키지 0b에서 채워요 (docs/spec/m2-overview.md "구현 작업 분할").

#include <expected>
#include <string_view>

#include "sim/error.hpp"
#include "sim/params.hpp"
#include "sim/scenario.hpp"

namespace sim::validation {

[[nodiscard]] std::expected<void, Error> require(bool ok, std::string_view what);
[[nodiscard]] std::expected<void, Error> non_negative(double v, std::string_view name);
[[nodiscard]] std::expected<void, Error> fraction(double v, std::string_view name);
[[nodiscard]] std::expected<void, Error> positive(double v, std::string_view name);
[[nodiscard]] std::expected<void, Error> finite(double v, std::string_view name);

[[nodiscard]] std::expected<void, Error> validate_params(const Params& p);
[[nodiscard]] std::expected<void, Error> validate_scenario(const Scenario& s);

}  // namespace sim::validation

// 실패한 expected를 그대로 돌려주는 도우미
#define SIM_CHECK(expr)                                                  \
    do {                                                                 \
        if (auto sim_check_result = (expr); !sim_check_result) {         \
            return std::unexpected(std::move(sim_check_result.error())); \
        }                                                                \
    } while (false)
