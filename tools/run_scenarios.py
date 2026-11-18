"""M1~M3 비교 시나리오(S1~S3, S4~S10, S5·S6·S11·S12)를 sim-cli로 돌리고 겹친 그래프를 runs/<m1|m2|m3>/에 저장해요.

시나리오 파일은 data/scenarios/<m1|m2|m3>/<name>.toml 이에요(S11-lime은 m2의 s9_* 시나리오를 읽고 runs/m3/에 써요). 없는 파일이 있는 그룹은 건너뛰어요.

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
    milestone: str  # "m1" | "m2" | "m3": 출력 폴더 이름 (scenario_dir이 없으면 시나리오 폴더도)
    scenarios: tuple[str, ...]
    days: int
    columns: tuple[str, ...]
    scenario_dir: str | None = None  # 시나리오 폴더가 출력 폴더와 다를 때


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
    # M3 (2026-11-18). 열은 m3-overview 시나리오 표의 판정 지표를 중심으로 골랐어요.
    Group("S5", "m3", ("s5_standard", "s5_k_excess"), 21,
          ("ledger_Ca_uptake_g", "sufficiency_Ca", "sufficiency_K", "ledger_Ca_leached_g", "antagonism_Ca_factor",
           "ca_solution_g_per_m2", "k_solution_g_per_m2", "ec_dS_per_m")),
    Group("S6", "m3", ("s6_loam", "s6_sand"), 21,
          ("ph", "k_solution_g_per_m2", "ec_dS_per_m", "ledger_K_leached_g", "exchange_free_site_index")),
    Group("S11", "m3", ("s11_neutral", "s11_acid", "s11_wet", "s11_alkaline"), 21,
          ("mn_solution_g_per_m2", "mn_g_per_m2", "mn_oxide_g_per_m2", "plant_Mn_ratio", "sufficiency_Mn",
           "aeration_index", "ph", "leaf_g_per_m2")),
    Group("S11-lime", "m3", ("s9_lime", "s9_none"), 14,
          ("ph", "mn_solution_g_per_m2", "mn_g_per_m2", "mn_oxide_g_per_m2", "plant_Mn_ratio",
           "exchange_free_site_index"), scenario_dir="m2"),
    Group("S12", "m3", ("s12_neutral", "s12_acid", "s12_alkaline"), 28,
          ("p_g_per_m2", "p_fixed_g_per_m2", "p_total_g_per_m2", "ph")),
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
    parser.add_argument("--scenario-root", type=Path, default=REPO / "data/scenarios", help="contains m1/, m2/ and m3/")
    parser.add_argument("--out-root", type=Path, default=REPO / "runs", help="outputs go to <out-root>/m1, m2 or m3")
    parser.add_argument("--groups", nargs="+", default=[g.name for g in GROUPS], choices=[g.name for g in GROUPS])
    args = parser.parse_args()

    if not args.cli.exists():
        print(f"sim-cli not found: {args.cli} (use --cli)")
        return 1

    failures = 0
    for group in GROUPS:
        if group.name not in args.groups:
            continue
        scenario_dir = args.scenario_root / (group.scenario_dir or group.milestone)
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
