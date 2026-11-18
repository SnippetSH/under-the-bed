#include <cstddef>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/world.hpp"
#include "test_helpers.hpp"

using test::make_world;

TEST_CASE("Serialize -> deserialize -> serialize is byte-identical", "[unit][serialization]") {
    sim::World world = make_world();
    const sim::TileArea all = sim::whole_grid(2, 3);
    test::must_enqueue(world, sim::AddCompostCommand{all, "immature_manure", 3.0});
    world.step(100);
    test::must_enqueue(world, sim::NoopCommand{});
    test::must_enqueue(world, sim::IrrigateCommand{{0, 1, 2, 2}, 12.5});
    test::must_enqueue(world, sim::AddFertilizerCommand{all, "calcium_nitrate", 40.0});

    const std::vector<std::byte> bytes = world.serialize();
    auto loaded = sim::World::deserialize(bytes);
    REQUIRE(loaded.has_value());
    CHECK(loaded->serialize() == bytes);
    CHECK(loaded->state_hash() == world.state_hash());
    CHECK(loaded->params() == world.params());
    CHECK(loaded->scenario() == world.scenario());
    CHECK(loaded->snapshot().pending_command_count == 3);
    CHECK(loaded->snapshot().soil == world.snapshot().soil);
    CHECK(loaded->snapshot().ledger == world.snapshot().ledger);
}

// 작물과 M2 명령(석회, 경운)이 있어도 왕복 저장이 바이트 단위로 같아야 해요.
TEST_CASE("Round trip with crop and M2 commands is byte-identical", "[unit][serialization]") {
    sim::Scenario scenario = test::test_scenario();
    scenario.crop = test::test_crop_init();
    scenario.stress_combination = sim::StressCombination::Product;
    sim::World world = make_world(std::move(scenario));
    const sim::TileArea all = sim::whole_grid(2, 3);
    test::must_enqueue(world, sim::AddFertilizerCommand{all, "ammonium_sulfate", 30.0});
    world.step(200);
    test::must_enqueue(world, sim::AddLimeCommand{{0, 0, 1, 2}, "calcium_carbonate", 100.0});
    test::must_enqueue(world, sim::TillCommand{{1, 1, 1, 2}});

    const std::vector<std::byte> bytes = world.serialize();
    auto loaded = sim::World::deserialize(bytes);
    REQUIRE(loaded.has_value());
    CHECK(loaded->serialize() == bytes);
    CHECK(loaded->state_hash() == world.state_hash());
    CHECK(loaded->params() == world.params());
    CHECK(loaded->scenario() == world.scenario());
    CHECK(loaded->snapshot().pending_command_count == 2);
    CHECK(loaded->snapshot().soil == world.snapshot().soil);
    CHECK(loaded->snapshot().crops == world.snapshot().crops);
    CHECK(loaded->snapshot().ledger == world.snapshot().ledger);

    // 복원한 월드는 원본과 같은 미래를 가져야 해요.
    world.step(50);
    loaded->step(50);
    CHECK(loaded->state_hash() == world.state_hash());
}

TEST_CASE("Deserialize rejects damaged save data", "[unit][serialization]") {
    sim::World world = make_world();
    test::must_enqueue(world, sim::NoopCommand{});
    const std::vector<std::byte> good = world.serialize();

    SECTION("empty") {
        CHECK_FALSE(sim::World::deserialize({}).has_value());
    }
    SECTION("bad magic") {
        std::vector<std::byte> bad = good;
        bad[0] = std::byte{'X'};
        auto r = sim::World::deserialize(bad);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code == sim::ErrorCode::InvalidData);
    }
    SECTION("unsupported format version") {
        std::vector<std::byte> bad = good;
        bad[4] = std::byte{99};
        auto r = sim::World::deserialize(bad);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code == sim::ErrorCode::UnsupportedVersion);
    }
    SECTION("truncated at every length") {
        // 모든 길이로 잘라 봐도 크래시 없이 오류를 돌려줘야 해요. 기상 표 구간은 건너뛰어 시간을 줄여요.
        for (std::size_t len = 0; len < good.size(); len += (len < 64 || len > good.size() - 64) ? 1 : 997) {
            auto r = sim::World::deserialize(std::span(good.data(), len));
            REQUIRE_FALSE(r.has_value());
        }
    }
    SECTION("trailing garbage") {
        std::vector<std::byte> bad = good;
        bad.push_back(std::byte{0});
        CHECK_FALSE(sim::World::deserialize(bad).has_value());
    }
}
