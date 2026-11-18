"""M1 비교 시나리오(S1~S3)를 sim-cli로 돌리고 겹친 그래프를 runs/m1/에 저장해요.

시나리오 파일은 data/scenarios/m1/<name>.toml 이에요. 없는 파일이 있는 그룹은 건너뛰어요.

사용법:
    python tools/run_scenarios.py
    python tools/run_scenarios.py --cli build/vs2022/sim-cli/Debug/sim-cli.exe --groups S1 S3
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from plot_csv import plot_files  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
HOURS_PER_DAY = 24


@dataclass(frozen=True)
class Group:
    name: str
    scenarios: tuple[str, ...]
    days: int
    columns: tuple[str, ...]


GROUPS = [
    Group("S1", ("s0_control", "s1_immature", "s1_mature"), 7, ("soil_temp_degC", "nh4_N_g_per_m2")),
    Group("S2", ("s2_alkaline", "s2_neutral"), 21, ("no2_N_g_per_m2", "nh4_N_g_per_m2", "no3_N_g_per_m2")),
    Group("S3", ("s3_over", "s3_proper"), 14, ("no3_N_g_per_m2", "water_mm", "ledger_N_leached_g")),
]


def run_cli(cli: Path, params: Path, scenario: Path, ticks: int, out: Path) -> bool:
    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(cli), "--params", str(params), "--scenario", str(scenario), "--ticks", str(ticks), "--out", str(out)]
    result = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8")
    if result.returncode != 0:
        print(f"  FAILED ({scenario.name}): {result.stderr.strip()}")
        return False
    for line in result.stdout.splitlines():
        if line.startswith("events"):
            print(f"  {scenario.stem}: {line}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cli", type=Path, default=REPO / "build/vs2022/sim-cli/Debug/sim-cli.exe")
    parser.add_argument("--params", type=Path, default=REPO / "data/params.toml")
    parser.add_argument("--scenario-dir", type=Path, default=REPO / "data/scenarios/m1")
    parser.add_argument("--out-dir", type=Path, default=REPO / "runs/m1")
    parser.add_argument("--groups", nargs="+", default=[g.name for g in GROUPS], choices=[g.name for g in GROUPS])
    args = parser.parse_args()

    if not args.cli.exists():
        print(f"sim-cli not found: {args.cli} (use --cli)")
        return 1

    failures = 0
    for group in GROUPS:
        if group.name not in args.groups:
            continue
        paths = {name: args.scenario_dir / f"{name}.toml" for name in group.scenarios}
        missing = [str(p) for p in paths.values() if not p.exists()]
        if missing:
            print(f"[{group.name}] skipped, missing scenario file(s): {', '.join(missing)}")
            continue

        print(f"[{group.name}] {group.days} days: {', '.join(group.scenarios)}")
        csvs: list[Path] = []
        ok = True
        for name, path in paths.items():
            csv_path = args.out_dir / f"{name}.csv"
            if not run_cli(args.cli, args.params, path, group.days * HOURS_PER_DAY, csv_path):
                ok = False
                break
            csvs.append(csv_path)
        if not ok:
            failures += 1
            continue
        plot_files(csvs, list(group.scenarios), list(group.columns), args.out_dir / f"{group.name.lower()}.png",
                   f"{group.name}: {' vs '.join(group.scenarios)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
