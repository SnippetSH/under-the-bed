"""M1·M2 비교 시나리오(S1~S3, S4~S10)를 sim-cli로 돌리고 겹친 그래프를 runs/<m1|m2>/에 저장해요.

시나리오 파일은 data/scenarios/<m1|m2>/<name>.toml 이에요. 없는 파일이 있는 그룹은 건너뛰어요.

사용법:
    python tools/run_scenarios.py
    python tools/run_scenarios.py --cli build/vs2022/sim-cli/Debug/sim-cli.exe --groups S1 S3 S4
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
    milestone: str  # "m1" | "m2": 시나리오 폴더와 출력 폴더 이름
    scenarios: tuple[str, ...]
    days: int
    columns: tuple[str, ...]


GROUPS = [
    Group("S1", "m1", ("s0_control", "s1_immature", "s1_mature"), 7, ("soil_temp_degC", "nh4_N_g_per_m2")),
    Group("S2", "m1", ("s2_alkaline", "s2_neutral"), 21, ("no2_N_g_per_m2", "nh4_N_g_per_m2", "no3_N_g_per_m2")),
    Group("S3", "m1", ("s3_over", "s3_proper"), 14, ("no3_N_g_per_m2", "water_mm", "ledger_N_leached_g")),
    Group("S4", "m2", ("s4_optimal", "s4_alkaline"), 21,
          ("sufficiency_P", "p_g_per_m2", "last_growth_g_per_m2", "ph")),
    Group("S4b", "m2", ("s4_optimal", "s4_acid"), 21, ("root_health", "leaf_g_per_m2", "ph")),
    Group("S8", "m2", ("s8_proper", "s8_waterlogged"), 14,
          ("aeration_index", "root_health", "leaf_g_per_m2", "water_mm")),
    Group("S9", "m2", ("s9_lime", "s9_none"), 14, ("ph", "lime_undissolved_eq_per_m2", "ca_g_per_m2")),
    Group("S10", "m2", ("s10_standard", "s10_excess", "s10_dry"), 14,
          ("ec_dS_per_m", "leaf_g_per_m2", "water_mm")),
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
    parser.add_argument("--scenario-root", type=Path, default=REPO / "data/scenarios", help="contains m1/ and m2/")
    parser.add_argument("--out-root", type=Path, default=REPO / "runs", help="outputs go to <out-root>/m1 or m2")
    parser.add_argument("--groups", nargs="+", default=[g.name for g in GROUPS], choices=[g.name for g in GROUPS])
    args = parser.parse_args()

    if not args.cli.exists():
        print(f"sim-cli not found: {args.cli} (use --cli)")
        return 1

    failures = 0
    for group in GROUPS:
        if group.name not in args.groups:
            continue
        scenario_dir = args.scenario_root / group.milestone
        out_dir = args.out_root / group.milestone
        paths = {name: scenario_dir / f"{name}.toml" for name in group.scenarios}
        missing = [str(p) for p in paths.values() if not p.exists()]
        if missing:
            print(f"[{group.name}] skipped, missing scenario file(s): {', '.join(missing)}")
            continue

        print(f"[{group.name}] {group.days} days: {', '.join(group.scenarios)}")
        csvs: list[Path] = []
        ok = True
        for name, path in paths.items():
            csv_path = out_dir / f"{name}.csv"
            if not run_cli(args.cli, args.params, path, group.days * HOURS_PER_DAY, csv_path):
                ok = False
                break
            csvs.append(csv_path)
        if not ok:
            failures += 1
            continue
        plot_files(csvs, list(group.scenarios), list(group.columns), out_dir / f"{group.name.lower()}.png",
                   f"{group.name}: {' vs '.join(group.scenarios)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
