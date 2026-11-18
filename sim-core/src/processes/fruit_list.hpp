#pragma once

// 과실 목록 도우미 (crop.cpp, fruit.cpp 내부용)

#include <cstddef>
#include <iterator>
#include <vector>

#include "sim/soil.hpp"

namespace sim::process::detail {

// 목록 순서를 지키면서 remove(fruit)가 true인 과실을 지워요 (안정, 새 할당 없음).
// remove는 목록 순서대로 과실마다 정확히 한 번 불려요. 난수를 뽑는 판정도 이 순서를 따르므로
// 생물 변이 스트림의 수열이 목록 순서로 고정돼요 (strawberry-fruit "생물 변이 난수").
template <typename Remove>
void remove_fruits_if(std::vector<Fruit>& fruits, Remove&& remove) {
    std::size_t kept = 0;
    for (std::size_t i = 0; i < fruits.size(); ++i) {
        if (remove(fruits[i])) {
            continue;
        }
        if (kept != i) {
            fruits[kept] = fruits[i];
        }
        ++kept;
    }
    fruits.erase(std::next(fruits.begin(), static_cast<std::ptrdiff_t>(kept)), fruits.end());
}

}  // namespace sim::process::detail
