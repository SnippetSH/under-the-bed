#pragma once

#include <cstdint>

namespace sim {

// 코어가 이해하는 파라미터 스키마 버전. data/params.toml의 schema_version과 같아야 해요.
inline constexpr std::int64_t kParamsSchemaVersion = 1;

// 시뮬레이션 계수 묶음. 값은 전부 data/params.toml에서 와요 (코드에 기본값을 두지 않아요).
// M0에는 계수가 없고, M1부터 프로세스별 항목이 추가돼요.
struct Params {
    std::int64_t schema_version = 0;

    bool operator==(const Params&) const = default;
};

}  // namespace sim
