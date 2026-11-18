"""sim-cli CSV를 그래프로 그려요.

열마다 서브플롯 하나씩, x축은 경과 일수(tick / 24)예요.
CSV를 여러 개 주면 같은 축에 겹쳐 그리고 범례를 달아요 (--labels로 이름 지정, 기본은 파일 이름).

사용법:
    python tools/plot_csv.py runs/empty.csv --out runs/empty.png
    python tools/plot_csv.py runs/empty.csv --columns outside_air_temp_degC --out runs/temp.png
    python tools/plot_csv.py runs/a.csv runs/b.csv --labels immature mature --columns soil_temp_degC --out runs/cmp.png
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


def plot_files(
    paths: list[Path],
    labels: list[str],
    columns: list[str],
    out: Path,
    title: str | None = None,
) -> None:
    """paths의 CSV들을 columns 열마다 하나의 서브플롯에 겹쳐 그려 out에 저장해요."""
    datasets = [read_csv(p) for p in paths]
    for path, data in zip(paths, datasets):
        missing = [c for c in columns if c not in data]
        if missing:
            raise SystemExit(f"{path}: columns not in CSV: {', '.join(missing)} (available: {', '.join(data)})")

    overlay = len(paths) > 1
    fig, axes = plt.subplots(len(columns), 1, figsize=(11, 2.4 * len(columns)), sharex=True, squeeze=False)
    for ax, column in zip(axes[:, 0], columns):
        for data, label in zip(datasets, labels):
            days = [int(t) / HOURS_PER_DAY for t in data["tick"]]
            ax.plot(days, [float(v) for v in data[column]], linewidth=0.8 if overlay else 0.6, label=label)
        ax.set_ylabel(column, fontsize=8)
        ax.grid(True, alpha=0.3)
        if overlay:
            ax.legend(fontsize=8, loc="best")
    axes[-1, 0].set_xlabel("elapsed days")
    fig.suptitle(title or (paths[0].name if not overlay else " vs ".join(labels)))
    fig.tight_layout()

    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=120)
    plt.close(fig)
    print(f"wrote {out}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, nargs="+", help="one or more sim-cli CSV files")
    parser.add_argument("--labels", nargs="+", default=None, help="legend labels, one per CSV")
    parser.add_argument("--columns", nargs="+", default=DEFAULT_COLUMNS)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--title", default=None)
    args = parser.parse_args()

    if args.labels is not None and len(args.labels) != len(args.csv):
        parser.error(f"--labels needs {len(args.csv)} values, got {len(args.labels)}")
    labels = args.labels if args.labels is not None else [p.stem for p in args.csv]
    plot_files(args.csv, labels, args.columns, args.out, args.title)


if __name__ == "__main__":
    main()
