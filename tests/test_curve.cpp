#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "sim/curve.hpp"

namespace {
const sim::PiecewiseLinear kCurve{{{{0.0, 0.0}}, {{10.0, 1.0}}, {{20.0, 0.5}}}};
}

TEST_CASE("Piecewise linear interpolates between points", "[unit][curve]") {
    CHECK(kCurve(0.0) == 0.0);
    CHECK(kCurve(5.0) == 0.5);
    CHECK(kCurve(10.0) == 1.0);
    CHECK(kCurve(15.0) == 0.75);
    CHECK(kCurve(20.0) == 0.5);
}

TEST_CASE("Piecewise linear clamps outside the range", "[unit][curve]") {
    CHECK(kCurve(-100.0) == 0.0);
    CHECK(kCurve(100.0) == 0.5);
    CHECK(kCurve(std::numeric_limits<double>::quiet_NaN()) == 0.0);
}

TEST_CASE("Curve validation", "[unit][curve]") {
    CHECK(sim::validate_curve(kCurve, "ok").has_value());
    CHECK_FALSE(sim::validate_curve(sim::PiecewiseLinear{{{{0.0, 1.0}}}}, "one point").has_value());
    CHECK_FALSE(sim::validate_curve(sim::PiecewiseLinear{{{{1.0, 1.0}}, {{1.0, 2.0}}}}, "dup x").has_value());
    CHECK_FALSE(sim::validate_curve(sim::PiecewiseLinear{{{{2.0, 1.0}}, {{1.0, 2.0}}}}, "desc").has_value());
    CHECK_FALSE(sim::validate_curve(sim::PiecewiseLinear{{{{0.0, -1.0}}, {{1.0, 2.0}}}}, "neg y").has_value());
}
