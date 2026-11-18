"""합성 기상 데이터 생성기.

365일 x 24시간 시간별 기상 CSV를 만들어요. 난수를 쓰지 않아서 항상 같은 파일이 나와요.
값은 모두 "게임용 임의값"이에요. 중부 지방 기후의 대략적인 모양(겨울 영하, 여름 25도 안팎,
오후 최고 기온, 낮 동안의 일사)만 흉내 내요.

사용법:
    python tools/gen_weather.py data/weather/synthetic_year.csv
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

# --- 게임용 임의값 ---------------------------------------------------------
LATITUDE_DEG = 36.5            # 가정한 위도
ANNUAL_MEAN_TEMP_DEGC = 12.5   # 연평균 기온
ANNUAL_TEMP_AMPLITUDE = 14.0   # 연교차의 절반
COLDEST_DAY_OF_YEAR = 20       # 가장 추운 날 (1월 20일)
DIURNAL_TEMP_AMPLITUDE = 4.5   # 일교차의 절반
WARMEST_HOUR = 15.0            # 하루 중 가장 더운 시각
CLEAR_SKY_PEAK_W_PER_M2 = 750  # 태양이 천정에 있을 때의 수평면 일사
RH_MEAN_PERCENT = 70.0
RH_DIURNAL_AMPLITUDE = 18.0    # 오후에 가장 건조
RH_SEASONAL_AMPLITUDE = 8.0    # 여름에 더 습함
MOST_HUMID_DAY_OF_YEAR = 200
# ---------------------------------------------------------------------------

DAYS_PER_YEAR = 365
HOURS_PER_DAY = 24
HEADER = "day_of_year,hour,outside_air_temp_degC,solar_radiation_W_per_m2,outside_rh_percent"


def air_temp_degC(day: int, hour_mid: float) -> float:
    seasonal = ANNUAL_MEAN_TEMP_DEGC - ANNUAL_TEMP_AMPLITUDE * math.cos(
        2 * math.pi * (day - COLDEST_DAY_OF_YEAR) / DAYS_PER_YEAR
    )
    diurnal = DIURNAL_TEMP_AMPLITUDE * math.cos(2 * math.pi * (hour_mid - WARMEST_HOUR) / HOURS_PER_DAY)
    return seasonal + diurnal


def solar_W_per_m2(day: int, hour_mid: float) -> float:
    declination = math.radians(23.44) * math.sin(2 * math.pi * (284 + day) / DAYS_PER_YEAR)
    hour_angle = math.radians(15.0 * (hour_mid - 12.0))
    lat = math.radians(LATITUDE_DEG)
    sin_elevation = math.sin(lat) * math.sin(declination) + math.cos(lat) * math.cos(declination) * math.cos(
        hour_angle
    )
    return max(0.0, CLEAR_SKY_PEAK_W_PER_M2 * sin_elevation)


def rh_percent(day: int, hour_mid: float) -> float:
    diurnal = -RH_DIURNAL_AMPLITUDE * math.cos(2 * math.pi * (hour_mid - WARMEST_HOUR) / HOURS_PER_DAY)
    seasonal = RH_SEASONAL_AMPLITUDE * math.cos(2 * math.pi * (day - MOST_HUMID_DAY_OF_YEAR) / DAYS_PER_YEAR)
    return min(100.0, max(0.0, RH_MEAN_PERCENT + diurnal + seasonal))


def generate() -> list[str]:
    lines = [
        "# 합성 기상 데이터 - 게임용 임의값 (tools/gen_weather.py로 생성, 난수 없음)",
        f"# 위도 {LATITUDE_DEG}도 가정, {DAYS_PER_YEAR}일 x {HOURS_PER_DAY}시간, hour는 해당 1시간 구간의 시작 시각",
        HEADER,
    ]
    for day in range(1, DAYS_PER_YEAR + 1):
        for hour in range(HOURS_PER_DAY):
            mid = hour + 0.5  # 1시간 구간의 대표값은 중간 시각으로 계산
            lines.append(
                f"{day},{hour},{air_temp_degC(day, mid):.1f},{solar_W_per_m2(day, mid):.1f},{rh_percent(day, mid):.1f}"
            )
    return lines


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(generate()) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
