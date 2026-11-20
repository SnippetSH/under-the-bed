#pragma once

#include <cassert>
#include <cstdint>

namespace sim {

// PCG32 (XSH RR 64/32), M. E. O'Neill, pcg-random.org.
// 출력은 기준 구현 pcg32_srandom_r / pcg32_random_r / pcg32_boundedrand_r 과 같아야 해요.
// std::*_distribution은 표준 라이브러리마다 결과가 달라서 쓰지 않아요 (ADR-0004).
class Pcg32 {
public:
    Pcg32() = default;
    Pcg32(std::uint64_t init_state, std::uint64_t stream_id) { seed(init_state, stream_id); }

    void seed(std::uint64_t init_state, std::uint64_t stream_id) {
        state_ = 0;
        inc_ = (stream_id << 1u) | 1u;
        next_u32();
        state_ += init_state;
        next_u32();
    }

    std::uint32_t next_u32() {
        const std::uint64_t old = state_;
        state_ = old * kMultiplier + inc_;
        const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // [0, 1) 균등 분포, 53비트 정밀도
    double next_unit_double() {
        const std::uint64_t hi = next_u32() >> 5u;  // 27비트
        const std::uint64_t lo = next_u32() >> 6u;  // 26비트
        return static_cast<double>((hi << 26u) | lo) * 0x1.0p-53;
    }

    // [0, bound) 균등 정수. 거부 샘플링으로 편향을 없애요.
    std::uint32_t next_below(std::uint32_t bound) {
        assert(bound > 0);
        const std::uint32_t threshold = (0u - bound) % bound;
        for (;;) {
            const std::uint32_t r = next_u32();
            if (r >= threshold) {
                return r % bound;
            }
        }
    }

    // 직렬화용 원시 상태
    [[nodiscard]] std::uint64_t raw_state() const { return state_; }
    [[nodiscard]] std::uint64_t raw_increment() const { return inc_; }
    [[nodiscard]] static Pcg32 from_raw(std::uint64_t state, std::uint64_t increment) {
        Pcg32 rng;
        rng.state_ = state;
        rng.inc_ = increment | 1u;
        return rng;
    }

    bool operator==(const Pcg32&) const = default;

private:
    static constexpr std::uint64_t kMultiplier = 6364136223846793005ULL;

    std::uint64_t state_ = 0x853c49e6748fea9bULL;
    std::uint64_t inc_ = 0xda3e39cb94b95bdbULL;
};

// 마스터 시드에서 서브시스템별 독립 스트림을 파생해요 (ADR-0004).
// 서브시스템 ID는 스트림이 필요한 프로세스가 생길 때 명세와 함께 정해요.
// 서브시스템 스트림 ID. 순서를 바꾸지 말고 뒤에만 추가해요.
enum class RngStream : std::uint64_t {
    Biology = 1,  // M4 strawberry-fruit "생물 변이 난수": 꽃 수 소수부, 수정률, 크기 변이, 저온 과실 손실
    Disease = 2,  // M5a 토양 병해 차이: 포기 감수성, 병원균 시작 편차 (생물 변이 스트림 순서를 건드리지 않게 분리)
    Foliar = 3,   // M5b 지상부 병해충: 모종 잠복 탄저·응애, 잿빛곰팡이 과실 썩음, 흰가루 과실 (foliar-disease 4.7)
};

[[nodiscard]] inline Pcg32 make_stream(std::uint64_t master_seed, std::uint64_t stream_id) {
    return Pcg32(master_seed, stream_id);
}

}  // namespace sim
