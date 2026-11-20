"""건조한 합성 기상: synthetic_year.csv의 바깥 상대습도만 비율로 줄여요 (M5b 4차, S31 건조 변형).

기온·일사는 그대로고 outside_rh_percent에 factor를 곱해요 (0~100%로 자름). 게임용 임의 자료예요.
사용법:
    python tools/gen_weather_dry.py data/weather/synthetic_year.csv data/weather/synthetic_year_dry.csv --factor 0.4
"""
from __future__ import annotations

import argparse
from pathlib import Path

HEADER = "day_of_year,hour,outside_air_temp_degC,solar_radiation_W_per_m2,outside_rh_percent"


def in_range(d: int, r) -> bool:
    """FROM <= TO면 FROM~TO, FROM > TO면 연말을 넘어가는 범위(FROM~365와 1~TO)."""
    return r[0] <= d <= r[1] if r[0] <= r[1] else (d >= r[0] or d <= r[1])


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("src", type=Path)
    ap.add_argument("dst", type=Path)
    ap.add_argument("--factor", type=float, default=0.4, help="바깥 상대습도에 곱하는 값 (게임용 임의값)")
    ap.add_argument("--doy-range", type=int, nargs=2, metavar=("FROM", "TO"), help="이 day_of_year 범위(양끝 포함, FROM > TO면 연말을 넘어가는 범위)만 건조하게 하고 나머지는 그대로 둬요")
    a = ap.parse_args()
    rng = f", day_of_year {a.doy_range[0]}~{a.doy_range[1]}만" if a.doy_range else ""
    out = [
        f"# 건조한 합성 기상 데이터 - 게임용 임의 자료 (tools/gen_weather_dry.py, synthetic_year.csv의 바깥 습도 x {a.factor}{rng})",
        "# 기온·일사는 synthetic_year.csv와 같고 (--doy-range가 있으면 그 밖의 날짜는 습도도 같아요), M5b S31 건조 변형(습도 되먹임 되돌림 시 하우스를 건조하게 만드는 용도)에만 써요",
        HEADER,
    ]
    for line in a.src.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or line.startswith("day_of_year") or not line.strip():
            continue
        d, h, t, sol, rh = line.split(",")
        if a.doy_range and not in_range(int(d), a.doy_range):
            out.append(line)
            continue
        rh_new = min(100.0, max(0.0, float(rh) * a.factor))
        out.append(f"{d},{h},{t},{sol},{rh_new:.1f}")
    a.dst.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    print(f"{a.dst}: {len(out) - 3}행, factor {a.factor}")


if __name__ == "__main__":
    main()
