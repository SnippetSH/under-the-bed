#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "sim/error.hpp"

namespace sim {

// 구간 선형 곡선 (docs/spec/m1-overview.md "구간 선형 곡선")
// - x는 엄격하게 오름차순, 점 2개 이상, y ≥ 0
// - 범위 밖 x는 양 끝 y로 고정 (외삽하지 않음)
struct PiecewiseLinear {
    std::vector<std::array<double, 2>> points;  // {x, y}

    [[nodiscard]] double operator()(double x) const {
        if (points.empty()) {
            return 0.0;
        }
        if (std::isnan(x) || x <= points.front()[0]) {
            return points.front()[1];
        }
        if (x >= points.back()[0]) {
            return points.back()[1];
        }
        const auto upper = std::upper_bound(points.begin(), points.end(), x,
                                            [](double v, const std::array<double, 2>& p) { return v < p[0]; });
        const auto& p1 = *upper;
        const auto& p0 = *(upper - 1);
        return p0[1] + (p1[1] - p0[1]) * (x - p0[0]) / (p1[0] - p0[0]);
    }

    bool operator==(const PiecewiseLinear&) const = default;
};

[[nodiscard]] inline std::expected<void, Error> validate_curve(const PiecewiseLinear& curve,
                                                               std::string_view name) {
    const auto& pts = curve.points;
    if (pts.size() < 2) {
        return std::unexpected(
            Error{ErrorCode::InvalidArgument, std::format("curve '{}' needs at least 2 points", name)});
    }
    for (std::size_t i = 0; i < pts.size(); ++i) {
        if (!std::isfinite(pts[i][0]) || !std::isfinite(pts[i][1]) || pts[i][1] < 0.0) {
            return std::unexpected(Error{ErrorCode::InvalidArgument,
                                         std::format("curve '{}' point {} must be finite with y >= 0", name, i)});
        }
        if (i > 0 && !(pts[i][0] > pts[i - 1][0])) {
            return std::unexpected(Error{ErrorCode::InvalidArgument,
                                         std::format("curve '{}' x values must be strictly increasing", name)});
        }
    }
    return {};
}

}  // namespace sim
