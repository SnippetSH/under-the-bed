#pragma once
// 부동소수점 제어 상태 읽기 (위험 R2 / U5, 프로토타입 전용). 읽기만 하고 바꾸지 않아요.
// x64 MSVC는 double 연산에 SSE2만 써요. x87 제어 워드는 x64에서 읽는 내장 함수가 없어서 빼요.

#include <cstdint>
#include <float.h>
#include <math.h>
#include <xmmintrin.h>

namespace simgodot {

struct FpState {
    std::uint32_t mxcsr = 0;       // SSE 제어·상태 레지스터 원본 (반올림 13~14비트, FTZ 15, DAZ 6)
    std::uint32_t controlfp = 0;   // CRT _controlfp(0, 0) 추상 값
    int fma3_enabled = 0;          // CRT의 FMA3 경로 사용 여부 (_get_FMA3_enable)
};

inline FpState read_fp_state() {
    FpState s;
    s.mxcsr = _mm_getcsr();
    unsigned int cw = 0;
    _controlfp_s(&cw, 0, 0);
    s.controlfp = cw;
    s.fma3_enabled = _get_FMA3_enable();
    return s;
}

}  // namespace simgodot
