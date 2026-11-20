#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "sim/command.hpp"
#include "sim/error.hpp"
#include "sim/event.hpp"
#include "sim/house.hpp"
#include "sim/params.hpp"
#include "sim/rng.hpp"
#include "sim/save_format.hpp"
#include "sim/scenario.hpp"
#include "sim/snapshot.hpp"
#include "sim/soil.hpp"

namespace sim {

namespace detail {
class ByteWriter;
class ByteReader;
}  // namespace detail

// 베드 타일 = 토양 셀 1개 + 작물 0~1개 (작물은 M2부터)
struct Tile {
    SoilCell soil;
    Crop crop;

    bool operator==(const Tile&) const = default;
};

// 시뮬레이션 월드. 공개 인터페이스는 docs/spec/core-api.md, 틱 순서는 docs/spec/m4-overview.md를 따라요.
// 공개 함수는 예외를 던지지 않고, 실패할 수 있는 호출은 std::expected를 돌려줘요.
class World {
public:
    // 1. 초기화: 파라미터와 시나리오를 검증하고 월드를 만들어요.
    [[nodiscard]] static std::expected<World, Error> create(Params params, Scenario scenario);

    // 2. 명령 투입: 검증 후 큐에 넣고 일련번호를 돌려줘요. 다음 틱 시작 시 투입 순서대로 적용돼요.
    //    모르는 퇴비·비료·석회 ID, 음수·비유한 양, 그리드 밖 영역은 거부하고 큐에 넣지 않아요.
    //    M4: 없는 겹의 창·허용 단계가 아닌 개방률, 범위 밖 난방 설정 온도, 범위 밖 겹수나
    //    휴작기(cover_change_window_doy) 밖에 적용될 피복 변경도 거부해요 (house-climate "명령").
    [[nodiscard]] std::expected<std::uint64_t, Error> enqueue(Command command);

    // 2a. 명령 미리 검사 (m6-overview 2.3 C2): enqueue와 같은 검증만 하고 큐·상태는 바꾸지 않아요.
    //     지금 상태 기준이라, 아직 적용되지 않은 대기 명령의 효과는 보지 않아요 (enqueue도 같아요).
    [[nodiscard]] std::expected<void, Error> can_enqueue(const Command& command) const {
        return validate_command(command);
    }

    // 3. 틱 진행: N틱 진행해요. 호출할 때마다 events()가 비워져요.
    void step(std::uint32_t ticks);

    // 4. 상태 조회
    [[nodiscard]] Snapshot snapshot() const;

    // 5. 이벤트 조회: 마지막 step() 호출 동안 발생한 이벤트
    [[nodiscard]] std::span<const Event> events() const { return events_; }

    // 6. 직렬화: 바이트 버퍼만 주고받아요. 파일 쓰기는 호출자 몫이에요.
    //    이벤트는 일시적인 값이라 저장하지 않아요.
    [[nodiscard]] std::vector<std::byte> serialize() const;
    [[nodiscard]] static std::expected<World, Error> deserialize(std::span<const std::byte> bytes);

    // 결정론 검증용: 동적 상태(정적 입력 제외)의 FNV-1a 64비트 해시
    [[nodiscard]] std::uint64_t state_hash() const;

    [[nodiscard]] const Params& params() const { return params_; }
    [[nodiscard]] const Scenario& scenario() const { return scenario_; }

private:
    World(Params params, Scenario scenario);

    void tick_once();
    void apply_command(const NoopCommand& command);
    void apply_command(const IrrigateCommand& command);
    void apply_command(const AddCompostCommand& command);
    void apply_command(const AddFertilizerCommand& command);
    void apply_command(const AddLimeCommand& command);
    void apply_command(const TillCommand& command);
    void apply_command(const SetWindowCommand& command);
    void apply_command(const SetHeaterCommand& command);
    void apply_command(const SetCoverLayersCommand& command);
    void apply_command(const ThinFlowersCommand& command);
    // M5a (disinfection, soil-microbes)
    void apply_command(const SetMulchCommand& command);
    void apply_command(const ApplyDisinfectantCommand& command);
    void apply_command(const AddInoculantCommand& command);
    void apply_command(const PlantCommand& command);
    // M5b (house-humidity 4.4, foliar-disease 4.5·4.6)
    void apply_command(const SetFanCommand& command);
    void apply_command(const RemovePlantCommand& command);
    void apply_command(const ReleasePredatorsCommand& command);
    void apply_command(const InstallDripCommand& command);

    [[nodiscard]] std::expected<void, Error> validate_command(const Command& command) const;
    [[nodiscard]] std::expected<void, Error> validate_area(const TileArea& area) const;
    [[nodiscard]] const CompostDef* find_compost(std::string_view id) const;
    [[nodiscard]] const FertilizerDef* find_fertilizer(std::string_view id) const;
    [[nodiscard]] const LimeDef* find_lime(std::string_view id) const;
    // M5a
    [[nodiscard]] std::optional<std::size_t> find_mulch_index(std::string_view id) const;
    [[nodiscard]] std::optional<std::size_t> find_disinfectant_index(std::string_view id) const;
    [[nodiscard]] const InoculantDef* find_inoculant(std::string_view id) const;
    [[nodiscard]] const PredatorDef* find_predator(std::string_view id) const;  // M5b
    // M5b: 정식 직후 포기마다 모종 병해(Foliar 난수, 타일 행 우선). 월드 생성과 Plant 명령이 같이 써요
    void plant_seedling_disease(const SeedlingGradeDef& grade);
    [[nodiscard]] bool any_live_crop() const;
    void refresh_derived_cell_values();  // 생성·역직렬화 직후 통기 지수·EC 계산

    template <typename Fn>
    void for_each_tile_in(const TileArea& area, Fn&& fn);

    void write_dynamic(detail::ByteWriter& writer) const;
    [[nodiscard]] std::expected<void, Error> read_dynamic(detail::ByteReader& reader);

    // 정적 입력
    Params params_;
    Scenario scenario_;

    // 동적 상태 (state_hash와 직렬화 대상)
    std::uint64_t tick_ = 0;
    std::uint64_t next_command_seq_ = 0;
    std::vector<QueuedCommand> pending_commands_;
    std::vector<Tile> tiles_;  // 행 우선 순서, bed_rows × bed_cols
    HouseState house_;         // M4 하우스 기후 (house-climate)
    Pcg32 biology_rng_;        // M4 생물 변이 스트림 (RngStream::Biology)
    Pcg32 disease_rng_;        // M5a 토양 병해 차이 스트림 (RngStream::Disease)
    Pcg32 foliar_rng_;         // M5b 지상부 병해충 스트림 (RngStream::Foliar)
    Ledger ledger_;

    // 일시적 출력
    std::vector<Event> events_;
};

}  // namespace sim
