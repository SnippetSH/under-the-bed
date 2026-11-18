"""낮 길이 곡선 생성기 (calendar.day_length_curve, 위도 36.5도).

천문 공식 (gen_weather.py와 같은 적위 식):
    delta(d) = 23.44deg * sin(2 pi (284 + d) / 365)
    D(d)     = (24 / pi) * acos(-tan(lat) * tan(delta))      [h]
약 15일 간격(day 1, 16, ..., 361)과 마지막 day 365를 0.01 h로 반올림해요.

사용법:
    python tools/gen_day_length.py
"""

from __future__ import annotations

import math

LATITUDE_DEG = 36.5  # gen_weather.py와 같은 위도 (게임용 가정)


def day_length_h(day: int) -> float:
    delta = math.radians(23.44) * math.sin(2 * math.pi * (284 + day) / 365)
    lat = math.radians(LATITUDE_DEG)
    x = max(-1.0, min(1.0, -math.tan(lat) * math.tan(delta)))
    return 24.0 / math.pi * math.acos(x)


def curve() -> list[tuple[int, float]]:
    days = list(range(1, 365, 15)) + [365]
    return [(d, round(day_length_h(d), 2)) for d in days]


def main() -> None:
    pts = curve()
    print("day_length_curve = [" + ", ".join(f"[{d}, {v:.2f}]" for d, v in pts) + "]")


if __name__ == "__main__":
    main()
