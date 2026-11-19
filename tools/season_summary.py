"""M4 작기(263일 = 6,312틱) sim-cli CSV를 요약 지표 dict로 만들어요 (모듈 + CLI).

지표: 월별 수확(Sep..May, 신선 g/m2), 총 수확, 수확 가중 평균 당도·경도(CSV의 누적 평균 열 마지막 값),
첫 수확 날짜(day_of_year, 달-일), 포기당 수확 과실 수, root_health 평균·최솟값, 원장 N_gas·N_leached
(`ledger_` 열은 하우스 전체 합이라 셀 수(기본 80 = bed_rows*bed_cols, 셀당 1 m2)로 나눠 m2당 값도 같이 줘요),
누적 난방 에너지(MJ/m2), no2_N 최댓값.
유도하지 못한 지표: 없음 (모두 CSV 열에서 계산).

사용법:
    python tools/season_summary.py runs/regression/season_standard.csv
    python tools/season_summary.py runs/a.csv --plants-per-m2 8 --cells 80 --json
모듈로: from season_summary import summarize; summarize(Path("x.csv"))
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from plot_csv import read_csv  # noqa: E402
from run_scenarios import MONTH_NAMES, MONTH_START_DOY, SEASON_MONTH_ORDER, month_of  # noqa: E402

HOURS_PER_DAY = 24
DAYS_IN_MONTH = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]


def _floats(data: dict[str, list[str]], name: str) -> list[float]:
    return [float(v) if v != "" else 0.0 for v in data[name]]


def doy_to_date(doy: int) -> str:
    m = month_of(doy)
    return f"{MONTH_NAMES[m]} {doy - MONTH_START_DOY[m] + 1}"


def summarize(csv_path: Path, plants_per_m2: float = 8.0, cells: int = 80) -> dict:
    """CSV 하나를 요약해요. 값은 JSON으로 저장할 수 있는 float/int/dict만."""
    data = read_csv(csv_path)
    rows = len(data["tick"])
    harvest = _floats(data, "harvest_fresh_g_per_m2")
    doy = [int(v) for v in data["day_of_year"]]

    per_month = [0.0] * 12
    for i in range(1, rows):
        # 행 i는 (i-1)번째 틱 처리 후 상태: 처리한 틱의 날짜는 행 i-1의 day_of_year (run_scenarios와 같은 규칙)
        per_month[month_of(doy[i - 1])] += harvest[i] - harvest[i - 1]
    monthly = {MONTH_NAMES[m]: per_month[m] for m in SEASON_MONTH_ORDER}

    first_raw = data["first_harvest_tick"][-1]
    first_doy = None
    first_date = None
    if first_raw != "":
        tick = int(float(first_raw))
        # tick 번째 틱의 날짜 = 행 tick의 day_of_year (행 tick+1이 처리 후 상태)
        first_doy = doy[min(tick, rows - 1)]
        first_date = doy_to_date(first_doy)

    root = _floats(data, "root_health")
    last = {k: float(data[k][-1]) if data[k][-1] != "" else 0.0 for k in
            ("harvest_mean_brix", "harvest_mean_firmness", "harvest_count", "ledger_N_gas_g",
             "ledger_N_leached_g", "heating_energy_MJ_per_m2")}
    return {
        "monthly_harvest_g_per_m2": monthly,
        "total_harvest_g_per_m2": harvest[-1],
        "mean_brix": last["harvest_mean_brix"],
        "mean_firmness": last["harvest_mean_firmness"],
        "first_harvest_doy": first_doy,
        "first_harvest_date": first_date,
        "harvest_count_per_plant": last["harvest_count"] / plants_per_m2,
        "root_health_mean": sum(root) / len(root),
        "root_health_min": min(root),
        "N_gas_total_g": last["ledger_N_gas_g"],
        "N_leached_total_g": last["ledger_N_leached_g"],
        "N_gas_g_per_m2": last["ledger_N_gas_g"] / cells,
        "N_leached_g_per_m2": last["ledger_N_leached_g"] / cells,
        "heating_energy_MJ_per_m2": last["heating_energy_MJ_per_m2"],
        "no2_N_max_g_per_m2": max(_floats(data, "no2_N_g_per_m2")),
    }


def format_summary(s: dict) -> str:
    lines = [f"total harvest  {s['total_harvest_g_per_m2']:.0f} g/m2   per plant {s['harvest_count_per_plant']:.1f} fruits",
             "monthly        " + ", ".join(f"{k} {v:.0f}" for k, v in s["monthly_harvest_g_per_m2"].items()),
             f"Brix {s['mean_brix']:.2f}  firmness {s['mean_firmness']:.3f}  first harvest {s['first_harvest_date']} (doy {s['first_harvest_doy']})",
             f"root_health mean {s['root_health_mean']:.3f} min {s['root_health_min']:.3f}",
             f"N_gas {s['N_gas_g_per_m2']:.2f} g/m2  N_leached {s['N_leached_g_per_m2']:.2f} g/m2 (house totals {s['N_gas_total_g']:.1f} / {s['N_leached_total_g']:.1f} g)",
             f"heating {s['heating_energy_MJ_per_m2']:.1f} MJ/m2  max no2_N {s['no2_N_max_g_per_m2']:.3f} g/m2"]
    return "\n".join(lines)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("csv", type=Path)
    p.add_argument("--plants-per-m2", type=float, default=8.0)
    p.add_argument("--cells", type=int, default=80, help="ledger_ 열이 합한 셀 수 (m2당 환산용)")
    p.add_argument("--json", action="store_true")
    a = p.parse_args()
    s = summarize(a.csv, a.plants_per_m2, a.cells)
    print(json.dumps(s, indent=2) if a.json else format_summary(s))
    return 0


if __name__ == "__main__":
    sys.exit(main())
