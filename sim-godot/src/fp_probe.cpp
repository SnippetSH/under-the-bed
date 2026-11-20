// fp-probe: sim-cli와 같은 컴파일 옵션·같은 CRT(/MD)로 만든 프로세스의 부동소수점 제어 상태를 찍어요 (R2/U5).
#include <cstdio>

#include "fp_state.hpp"

int main() {
    const simgodot::FpState s = simgodot::read_fp_state();
    std::printf("fp_state process=cli mxcsr=0x%08x controlfp=0x%08x fma3=%d\n", s.mxcsr, s.controlfp, s.fma3_enabled);
    return 0;
}
