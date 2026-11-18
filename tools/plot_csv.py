"""sim-cli CSV를 그래프로 그려요.

열마다 서브플롯 하나씩, x축은 경과 일수(tick / 24)예요.

사용법:
    python tools/plot_csv.py runs/empty.csv --out runs/empty.png
    python tools/plot_csv.py runs/empty.csv --columns outside_air_temp_degC --out runs/temp.png
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")  # 화면 없이 파일로만 저장
import matplotlib.pyplot as plt  # noqa: E402

HOURS_PER_DAY = 24
DEFAULT_COLUMNS = ["outside_air_temp_degC", "solar_radiation_W_per_m2", "outside_rh_percent"]


def read_csv(path: Path) -> dict[str, list[str]]:
    with path.open(encoding="utf-8", newline="") as f:
        reader = csv.DictReader(line for line in f if not line.startswith("#"))
        columns: dict[str, list[str]] = {name: [] for name in reader.fieldnames or []}
        for row in reader:
            for name in columns:
                columns[name].append(row[name])
    return columns


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--columns", nargs="+", default=DEFAULT_COLUMNS)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--title", default=None)
    args = parser.parse_args()

    data = read_csv(args.csv)
    missing = [c for c in args.columns if c not in data]
    if missing:
        parser.error(f"columns not in CSV: {', '.join(missing)} (available: {', '.join(data)})")

    days = [int(t) / HOURS_PER_DAY for t in data["tick"]]
    fig, axes = plt.subplots(len(args.columns), 1, figsize=(11, 2.4 * len(args.columns)), sharex=True, squeeze=False)
    for ax, column in zip(axes[:, 0], args.columns):
        ax.plot(days, [float(v) for v in data[column]], linewidth=0.6)
        ax.set_ylabel(column, fontsize=8)
        ax.grid(True, alpha=0.3)
    axes[-1, 0].set_xlabel("elapsed days")
    fig.suptitle(args.title or args.csv.name)
    fig.tight_layout()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, dpi=120)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
