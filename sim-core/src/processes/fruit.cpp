// docs/spec/strawberry-fruit.md — 꽃 만들기, 개화 판정(수정률·착과·기형·크기 변이), 과실 진행, 크기 구간
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

#include "fruit_list.hpp"
#include "sim/calendar.hpp"
#include "sim/processes.hpp"

namespace sim::process {

void create_flowers(Crop& crop, std::uint32_t truss_index, double flowers_per_m2, const FruitParams& params,
                    std::uint64_t tick, Pcg32& biology_rng) {
    const double flowers = std::max(0.0, flowers_per_m2);
    const double whole = std::floor(flowers);
    const double fraction = flowers - whole;
    // 소수부 난수는 화방마다 항상 1회 뽑아요 (수열이 꽃 수에 따라 달라지지 않게)
    const double u = biology_rng.next_unit_double();
    const auto n = static_cast<std::uint64_t>(whole) + (u < fraction ? 1u : 0u);
    const std::size_t ranks = params.rank_fraction.size();
    if (n == 0 || ranks == 0) {
        return;
    }

    // 과위별 개수: rank_fraction × n 을 최대 나머지 방식으로 정수 배분 (동률이면 낮은 과위 먼저)
    std::vector<std::uint64_t> counts(ranks, 0);
    std::vector<double> remainders(ranks, 0.0);
    std::uint64_t assigned = 0;
    for (std::size_t r = 0; r < ranks; ++r) {
        const double exact = params.rank_fraction[r] * static_cast<double>(n);
        counts[r] = static_cast<std::uint64_t>(std::floor(exact));
        remainders[r] = exact - std::floor(exact);
        assigned += counts[r];
    }
    std::vector<std::size_t> order(ranks);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return remainders[a] > remainders[b]; });
    for (std::size_t i = 0; assigned < n; i = (i + 1) % ranks) {
        ++counts[order[i]];
        ++assigned;
    }
    while (assigned > n) {  // rank_fraction 합이 1을 살짝 넘는 반올림 경우의 안전장치
        for (std::size_t r = ranks; r-- > 0 && assigned > n;) {
            if (counts[r] > 0) {
                --counts[r];
                --assigned;
            }
        }
    }

    for (std::size_t r = 0; r < ranks; ++r) {
        for (std::uint64_t k = 0; k < counts[r]; ++k) {
            // 과실마다 개화 시점 편차 (biology 스트림, 과실 생성 순). 최소 지금
            const double u_jitter = biology_rng.next_unit_double();
            const double delay_days =
                params.rank_anthesis_delay_days[r] + params.anthesis_jitter_days * (2.0 * u_jitter - 1.0);
            const auto delay_ticks = static_cast<std::uint64_t>(std::max<long long>(
                0, std::llround(delay_days * kHoursPerDay)));
            Fruit fruit;
            fruit.truss_index = truss_index;
            fruit.rank = static_cast<std::uint32_t>(r);
            fruit.anthesis_tick = tick + delay_ticks;
            crop.fruits.push_back(fruit);
        }
    }
}

void judge_anthesis(Crop& crop, const FruitParams& params, double house_air_temp_mean_degC, std::uint64_t tick,
                    Pcg32& biology_rng) {
    if (params.rank_potential_fresh_g.empty()) {
        return;  // 검증된 파라미터에서는 일어나지 않아요 (과위 배열 길이 ≥ 1)
    }
    const double mean_pollination = params.pollination_mean_curve(house_air_temp_mean_degC);
    // 판정은 목록 순서대로 (난수 수열 고정). 수정률이 착과 문턱보다 낮으면 꽃이 떨어져 목록에서 빠져요(물질 이동 없음).
    detail::remove_fruits_if(crop.fruits, [&](Fruit& fruit) {
        if (fruit.set || fruit.anthesis_tick > tick) {
            return false;
        }
        const double u = biology_rng.next_unit_double();
        const double p = std::clamp(mean_pollination + params.pollination_spread * (2.0 * u - 1.0), 0.0, 1.0);
        fruit.pollination_index = p;
        if (p < params.set_threshold) {
            return true;
        }
        const double u_size = biology_rng.next_unit_double();
        fruit.set = true;
        fruit.deformed = p < params.deformed_threshold;
        const std::size_t rank = std::min<std::size_t>(fruit.rank, params.rank_potential_fresh_g.size() - 1);
        fruit.potential_fresh_g = std::max(0.0, params.rank_potential_fresh_g[rank] *
                                                    params.pollination_size_curve(p) *
                                                    (1.0 + params.size_variation_fraction * (2.0 * u_size - 1.0)));
        return false;
    });
}

double fruit_development_increment(const FruitParams& params, double house_air_temp_degC) {
    if (!(params.degree_days_to_harvest > 0.0)) {
        return 0.0;
    }
    return std::max(0.0, house_air_temp_degC - params.base_temp_degC) / kHoursPerDay / params.degree_days_to_harvest;
}

std::size_t size_class_index(const FruitParams& params, double fresh_g) {
    std::size_t index = 0;
    for (const double bound : params.size_class_bounds_g) {
        if (fresh_g >= bound) {
            ++index;
        }
    }
    return index;
}

}  // namespace sim::process
