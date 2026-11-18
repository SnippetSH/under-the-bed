"""당도 온도 점수 곡선 생성기 (docs/spec/strawberry-fruit.md "당도 온도 점수 식", 디렉터 지정 2026-11-18).

    S_day(T)   = exp(-(T - 23)^2 / (2 * 5^2))
    S_night(T) = exp(-(T - 8.5)^2 / (2 * 3^2))      (T <= 8.5)
               = max(0, 1 - 0.12 * (T - 8.5))        (T >  8.5)

코어는 exp를 계산하지 않고 이 도구가 만든 구간 선형 곡선을 읽어요 (ADR-0004).
낮은 3~43 °C 1 °C 간격, 밤은 -10~20 °C 1 °C 간격에 꼭짓점 8.5 °C를 더해요. 값은 소수 넷째 자리로 반올림해요.

사용법:
    python tools/gen_brix_curves.py        # 표준 출력에 TOML 두 줄
"""

from __future__ import annotations

import math


def day_score(t: float) -> float:
    return math.exp(-((t - 23.0) ** 2) / (2.0 * 5.0 ** 2))


def night_score(t: float) -> float:
    if t <= 8.5:
        return math.exp(-((t - 8.5) ** 2) / (2.0 * 3.0 ** 2))
    return max(0.0, 1.0 - 0.12 * (t - 8.5))


def fmt(x: float) -> str:
    s = f"{x:.4f}".rstrip("0").rstrip(".")
    return s if s else "0"


def curve(fn, xs) -> str:
    return "[" + ", ".join(f"[{fmt(x)}, {fmt(round(fn(x), 4))}]" for x in xs) + "]"


def main() -> None:
    day_x = [float(t) for t in range(3, 44)]
    night_x = sorted({float(t) for t in range(-10, 21)} | {8.5})
    print("brix_day_temp_score_curve = " + curve(day_score, day_x))
    print("brix_night_temp_score_curve = " + curve(night_score, night_x))


if __name__ == "__main__":
    main()
