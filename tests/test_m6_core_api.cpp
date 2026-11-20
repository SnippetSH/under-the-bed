// M6a 코어 공개 API 추가 C1~C3 (docs/spec/m6-overview.md 2.3, core-api.md "M6a 공개 API 추가").
// 셋 다 선언만 여는 것이라 상태·해시·세이브 형식은 그대로여야 해요.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/save_format.hpp"
#include "sim/soil.hpp"
#include "sim/world.hpp"
#include "test_helpers.hpp"

using test::make_world;

// C1: 세이브 머리글의 버전은 공개 상수와 같고, peek은 전체 역직렬화 없이 그 값을 읽어요.
TEST_CASE("Peek save format version reads the header of a fresh save", "[unit][m6][serialization]") {
    sim::World world = make_world();
    test::must_enqueue(world, sim::NoopCommand{});
    world.step(5);
    const std::vector<std::byte> bytes = world.serialize();
    const auto version = sim::peek_save_format_version(bytes);
    REQUIRE(version.has_value());
    CHECK(*version == sim::kSaveFormatVersion);
    CHECK(sim::kSaveFormatVersion == 8);  // M5b 형식 v8 그대로 (m6-overview 9장)
    // 머리글만 있는 버퍼(본문이 잘린 세이브)도 버전은 읽혀요. 불러오기는 Truncated예요.
    const std::span<const std::byte> header_only(bytes.data(), 8);
    const auto peeked = sim::peek_save_format_version(header_only);
    REQUIRE(peeked.has_value());
    CHECK(*peeked == sim::kSaveFormatVersion);
    const auto loaded = sim::World::deserialize(header_only);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == sim::ErrorCode::Truncated);
}

// C1: 다른 버전도 거부하지 않고 그 값을 돌려줘요. 머리글 오류 코드는 deserialize와 같아요.
TEST_CASE("Peek save format version reports other versions and header errors like deserialize",
          "[unit][m6][serialization]") {
    sim::World world = make_world();
    const std::vector<std::byte> good = world.serialize();

    SECTION("other version") {
        std::vector<std::byte> other = good;
        other[4] = std::byte{7};
        const auto peeked = sim::peek_save_format_version(other);
        REQUIRE(peeked.has_value());
        CHECK(*peeked == 7);
        const auto loaded = sim::World::deserialize(other);
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error().code == sim::ErrorCode::UnsupportedVersion);
    }
    SECTION("empty") {
        const auto peeked = sim::peek_save_format_version({});
        REQUIRE_FALSE(peeked.has_value());
        CHECK(peeked.error().code == sim::ErrorCode::InvalidData);
        const auto loaded = sim::World::deserialize({});
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error().code == peeked.error().code);
    }
    SECTION("bad magic") {
        std::vector<std::byte> bad = good;
        bad[1] = std::byte{'X'};
        const auto peeked = sim::peek_save_format_version(bad);
        REQUIRE_FALSE(peeked.has_value());
        CHECK(peeked.error().code == sim::ErrorCode::InvalidData);
    }
    SECTION("cut inside the version") {
        const std::span<const std::byte> cut(good.data(), 6);
        const auto peeked = sim::peek_save_format_version(cut);
        REQUIRE_FALSE(peeked.has_value());
        CHECK(peeked.error().code == sim::ErrorCode::Truncated);
        const auto loaded = sim::World::deserialize(cut);
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error().code == sim::ErrorCode::Truncated);
    }
}

// C2: can_enqueue는 enqueue와 같은 판정을 하고, 큐·일련번호·해시를 바꾸지 않아요.
TEST_CASE("Can enqueue gives the same verdict as enqueue without changing state", "[unit][m6]") {
    sim::Scenario scenario = test::test_scenario(1, 2, 3);  // day 1: 피복 변경 기간(91~243) 밖
    scenario.crop = test::test_crop_init();
    sim::World world = make_world(std::move(scenario));
    world.step(2);

    const std::vector<sim::Command> commands = {
        sim::NoopCommand{},
        sim::IrrigateCommand{{0, 0, 2, 3}, 4.0},
        sim::IrrigateCommand{{0, 0, 2, 3}, -1.0},                             // 음수
        sim::IrrigateCommand{{1, 2, 2, 2}, 1.0},                              // 그리드 밖
        sim::AddFertilizerCommand{{0, 0, 1, 1}, "gold_dust", 1.0},            // 모르는 id
        sim::SetWindowCommand{.layer = 0, .side = sim::WindowSide::Left, .open_fraction = 0.4},  // 허용 단계 아님
        sim::SetCoverLayersCommand{.layers = 2},                              // 휴작기 밖
        sim::PlantCommand{.seedling_grade = "B", .plants_per_m2 = 8.0},       // 살아 있는 작물이 있음
        sim::RemovePlantCommand{{0, 0, 1, 1}},
        sim::InstallDripCommand{},
        sim::SetFanCommand{.on = true},
    };
    for (std::size_t i = 0; i < commands.size(); ++i) {
        INFO("command index " << i);
        const std::uint64_t hash_before = world.state_hash();
        const std::size_t pending_before = world.snapshot().pending_command_count;
        const auto verdict = world.can_enqueue(commands[i]);
        CHECK(world.state_hash() == hash_before);
        CHECK(world.snapshot().pending_command_count == pending_before);

        // 같은 상태의 복사본에 실제로 넣어 보고 판정을 비교해요 (원본 큐는 그대로 둬요)
        auto copy = sim::World::deserialize(world.serialize());
        REQUIRE(copy.has_value());
        const auto enqueued = copy->enqueue(commands[i]);
        CHECK(verdict.has_value() == enqueued.has_value());
        if (!verdict && !enqueued) {
            CHECK(verdict.error().code == enqueued.error().code);
            CHECK(verdict.error().message == enqueued.error().message);
        }
    }
    // 판정 방향 몇 개는 직접 확인해요 (나머지는 위에서 enqueue와 같다는 것만 봐요)
    CHECK(world.can_enqueue(sim::NoopCommand{}).has_value());
    CHECK_FALSE(world.can_enqueue(sim::IrrigateCommand{{0, 0, 2, 3}, -1.0}).has_value());
    CHECK_FALSE(world.can_enqueue(sim::PlantCommand{.seedling_grade = "B", .plants_per_m2 = 8.0}).has_value());
    CHECK_FALSE(world.can_enqueue(sim::SetCoverLayersCommand{.layers = 2}).has_value());

    // can_enqueue를 불러도 다음 enqueue의 일련번호는 그대로 이어져요
    const auto first = world.enqueue(sim::NoopCommand{});
    REQUIRE(first.has_value());
    (void)world.can_enqueue(sim::NoopCommand{});
    const auto second = world.enqueue(sim::NoopCommand{});
    REQUIRE(second.has_value());
    CHECK(*second == *first + 1);
}

// C3: 고사 원인 id 표는 RootDamageCause 순서이고 sim-cli가 찍던 이름과 같아요.
TEST_CASE("Root damage cause ids follow the enum order", "[unit][m6]") {
    static_assert(sim::kRootDamageCauseIds.size() == sim::kRootDamageCauseCount);
    using C = sim::RootDamageCause;
    const auto id = [](C c) { return sim::kRootDamageCauseIds[static_cast<std::size_t>(c)]; };
    CHECK(id(C::Waterlog) == std::string_view("waterlog"));
    CHECK(id(C::Heat) == std::string_view("heat"));
    CHECK(id(C::Nitrite) == std::string_view("nitrite"));
    CHECK(id(C::Acid) == std::string_view("acid"));
    CHECK(id(C::Fusarium) == std::string_view("fusarium"));
    CHECK(id(C::Phytophthora) == std::string_view("phytophthora"));
    CHECK(id(C::Fumigant) == std::string_view("fumigant"));
    CHECK(id(C::Anthracnose) == std::string_view("anthracnose"));
    // 병해 원인 id는 병 id 표와 같은 글자예요 (이벤트·CSV 이름이 한 벌)
    CHECK(id(C::Fusarium) == sim::kSoilDiseaseIds[static_cast<std::size_t>(sim::SoilDisease::Fusarium)]);
    CHECK(id(C::Phytophthora) == sim::kSoilDiseaseIds[static_cast<std::size_t>(sim::SoilDisease::Phytophthora)]);
    CHECK(id(C::Anthracnose) == sim::kFoliarDiseaseIds[static_cast<std::size_t>(sim::FoliarDisease::Anthracnose)]);
}
