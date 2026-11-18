#include <array>
#include <cstddef>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sim/hash.hpp"
#include "sim/rng.hpp"

TEST_CASE("PCG32 matches the reference implementation output", "[unit][rng]") {
    // pcg-c-basic pcg32-demo: pcg32_srandom_r(&rng, 42u, 54u)
    sim::Pcg32 rng(42u, 54u);
    constexpr std::array<std::uint32_t, 6> expected = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u,
                                                       0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (const std::uint32_t value : expected) {
        CHECK(rng.next_u32() == value);
    }
}

TEST_CASE("PCG32 is reproducible from the same seed and stream", "[unit][rng][determinism]") {
    sim::Pcg32 a = sim::make_stream(777, 3);
    sim::Pcg32 b = sim::make_stream(777, 3);
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(a.next_u32() == b.next_u32());
    }
    CHECK(a == b);
}

TEST_CASE("Different streams from one master seed diverge", "[unit][rng]") {
    sim::Pcg32 a = sim::make_stream(777, 0);
    sim::Pcg32 b = sim::make_stream(777, 1);
    int equal = 0;
    for (int i = 0; i < 64; ++i) {
        equal += a.next_u32() == b.next_u32() ? 1 : 0;
    }
    CHECK(equal < 4);
}

// 테스트 이름에 '['를 쓰면 Catch2 태그로 해석돼서 CTest 등록이 깨져요.
TEST_CASE("Unit double lies in the half-open unit interval with a sane mean", "[unit][rng]") {
    sim::Pcg32 rng = sim::make_stream(1, 0);
    constexpr int kSamples = 100000;
    double sum = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        const double x = rng.next_unit_double();
        REQUIRE(x >= 0.0);
        REQUIRE(x < 1.0);
        sum += x;
    }
    CHECK_THAT(sum / kSamples, Catch::Matchers::WithinAbs(0.5, 0.01));
}

TEST_CASE("Bounded integers stay below the bound", "[unit][rng]") {
    sim::Pcg32 rng = sim::make_stream(9, 0);
    std::array<int, 6> counts{};
    for (int i = 0; i < 60000; ++i) {
        const std::uint32_t v = rng.next_below(6);
        REQUIRE(v < 6u);
        ++counts[v];
    }
    for (const int c : counts) {
        CHECK(c > 9000);
        CHECK(c < 11000);
    }
    CHECK(rng.next_below(1) == 0u);
}

TEST_CASE("Raw state round-trips", "[unit][rng]") {
    sim::Pcg32 a = sim::make_stream(5, 7);
    a.next_u32();
    sim::Pcg32 b = sim::Pcg32::from_raw(a.raw_state(), a.raw_increment());
    CHECK(a == b);
    CHECK(a.next_u32() == b.next_u32());
}

TEST_CASE("FNV-1a 64 matches published test vectors", "[unit][hash]") {
    const auto bytes = [](std::string_view s) { return std::as_bytes(std::span(s.data(), s.size())); };
    CHECK(sim::fnv1a64(bytes("")) == 0xcbf29ce484222325ULL);
    CHECK(sim::fnv1a64(bytes("a")) == 0xaf63dc4c8601ec8cULL);
}
