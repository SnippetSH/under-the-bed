"""바깥 CO2 시각별 표 생성기 (docs/spec/house-climate.md "바깥 CO2 식", 디렉터 지정).

C(t) = 420 + 15 sin(pi t / 12) + 65.13 exp(-d1^2 / 3.125) - 21.25 exp(-d2^2 / 1.28)
d1 = ((t + 6.5) mod 24) - 12,  d2 = ((t - 3) mod 24) - 12

t = 0..23 정시 값 24개를 0.1 ppm으로 반올림해 출력해요. 코어는 sin·exp를 계산하지 않고
이 표를 데이터(house.outside_co2_ppm_by_hour)에서 읽어요 (ADR-0004).

사용법:
    python tools/gen_co2.py            # 표준 출력에 TOML 한 줄
    python tools/gen_co2.py out.txt    # 파일에 저장
"""

from __future__ import annotations

import math
import sys


def outside_co2_ppm(t: float) -> float:
    d1 = ((t + 6.5) % 24.0) - 12.0
    d2 = ((t - 3.0) % 24.0) - 12.0
    return (
        420.0
        + 15.0 * math.sin(math.pi * t / 12.0)
        + 65.13 * math.exp(-(d1 * d1) / 3.125)
        - 21.25 * math.exp(-(d2 * d2) / 1.28)
    )


def table() -> list[float]:
    return [round(outside_co2_ppm(float(t)), 1) for t in range(24)]


def main() -> None:
    values = table()
    line = "outside_co2_ppm_by_hour = [" + ", ".join(f"{v:.1f}" for v in values) + "]"
    if len(sys.argv) > 1:
        with open(sys.argv[1], "w", encoding="utf-8") as f:
            f.write(line + "\n")
    else:
        print(line)


if __name__ == "__main__":
    main()
