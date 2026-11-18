"""M1~M4 비교 시나리오(S1~S3, S4~S10, S5·S6·S11·S12, M4 작기·처방·적과·S13·S15·S18)를 sim-cli로 돌리고
겹친 그래프를 runs/<m1|m2|m3|m4>/에 저장해요.

시나리오 파일은 data/scenarios/<m1|m2|m3|m4>/<name>.toml 이에요(S11-lime은 m2의 s9_* 시나리오를 읽고 runs/m3/에 써요).
없는 파일이 있는 그룹은 건너뛰어요. M4 그룹(M4-season, M4-prescriptions, M4-thinning, S13, S15, S18-winter,
S18-autumn, S18-co2)은 runs/m4/에 써요. 같은 시나리오·기간은 한 번만 돌려 CSV를 같이 써요.
작기(263일) 그룹은 Release 빌드 sim-cli가 빨라요(--cli로 지정).
M4-season, M4-prescriptions, M4-thinning은 월별 수확량 막대 그래프(<그룹>_monthly.png)도 만들어요.

사용법:
    python tools/run_scenarios.py
    python tools/run_scenarios.py --cli build/vs2022/sim-cli/Debug/sim-cli.exe --groups S1 S3 S4
    python tools/run_scenarios.py --cli build/vs2022/sim-cli/Release/sim-cli.exe --groups M4-season M4-prescriptions M4-thinning S13 S15 S18-winter S18-autumn S18-co2
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from plot_csv import plot_files, plt, read_csv  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
HOURS_PER_DAY = 24


@dataclass(frozen=True)
class Group:
    name: str
    milestone: str  # "m1" | "m2" | "m3" | "m4": 출력 폴더 이름 (scenario_dir이 없으면 시나리오 폴더도)
    scenarios: tuple[str, ...]
    days: int
    columns: tuple[str, ...]
    scenario_dir: str | None = None  # 시나리오 폴더가 출력 폴더와 다를 때
    monthly: bool = False  # True면 월별 수확량 막대 그래프(<그룹>_monthly.png)도 만들어요 (M4 작기)
    no_hash: bool = False  # True면 sim-cli에 --no-hash (긴 실행에서 해시 열을 빼서 빨라요)


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
    # M4 (2026-11-18). 작기 263일 = 6,312틱. 열은 m4-overview 시나리오 표의 판정 지표를 중심으로 골랐어요.
    Group("M4-season", "m4", ("season_standard",), 263,
          ("house_air_temp_degC", "house_co2_ppm", "leaf_g_per_m2", "fruit_g_per_m2", "trusses_initiated",
           "harvest_fresh_g_per_m2", "harvest_mean_brix"), monthly=True, no_hash=True),
    Group("M4-prescriptions", "m4",
          ("season_standard", "season_no_topdress", "season_n_excess", "season_k_low", "season_k_excess",
           "season_acid", "season_thinned"), 263,
          ("harvest_fresh_g_per_m2", "harvest_mean_brix", "harvest_mean_firmness", "sufficiency_N", "sufficiency_K",
           "sufficiency_Ca"), monthly=True, no_hash=True),
    Group("M4-thinning", "m4", ("season_standard", "season_thinned"), 263,
          ("harvest_fresh_g_per_m2", "harvest_count", "harvest_mean_brix", "fruit_count", "thinned_count",
           "size_satisfaction_mean"), monthly=True, no_hash=True),
    Group("S13", "m4", ("s13_proper", "s13_waterlogged"), 14,
          ("ledger_N_gas_g", "no3_N_g_per_m2", "aeration_index", "water_mm"), no_hash=True),
    Group("S15", "m4", ("s15_grade_a", "s15_grade_c"), 113,
          ("harvest_fresh_g_per_m2", "trusses_initiated", "induction_index", "leaf_g_per_m2"), no_hash=True),
    Group("S18-winter", "m4", ("s18_winter_none", "s18_winter_heated"), 7,
          ("house_air_temp_degC", "leaf_g_per_m2", "ledger_C_leaf_litter_g", "heating_energy_MJ_per_m2"),
          no_hash=True),
    Group("S18-autumn", "m4", ("s18_autumn_closed", "s18_autumn_vented"), 28,
          ("house_air_temp_degC", "induction_index", "trusses_initiated", "establishment_index"), no_hash=True),
    Group("S18-co2", "m4", ("s18_co2_closed", "s18_co2_vented"), 7,
          ("house_co2_ppm", "last_gross_g_per_m2", "house_air_temp_degC", "heating_energy_MJ_per_m2"),
          no_hash=True),
]

# 월별 수확량 막대 그래프의 달 이름·순서 (정식 9월 ~ 5월)
MONTH_NAMES = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
SEASON_MONTH_ORDER = [8, 9, 10, 11, 0, 1, 2, 3, 4]
MONTH_START_DOY = [1, 32, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335]  # 윤년 없는 365일 달력


def month_of(day_of_year: int) -> int:
    month = 0
    for i, start in enumerate(MONTH_START_DOY):
        if day_of_year >= start:
            month = i
    return month


def monthly_harvest(csv_path: Path) -> list[float]:
    """누적 수확 열(harvest_fresh_g_per_m2)의 월별 증가량 (g/m2), SEASON_MONTH_ORDER 순서."""
    data = read_csv(csv_path)
    cumulative = [float(v) if v != "" else 0.0 for v in data["harvest_fresh_g_per_m2"]]
    doy = [int(v) for v in data["day_of_year"]]
    per_month = [0.0] * 12
    for i in range(1, len(cumulative)):
        # 행 i는 (i-1)번째 틱을 처리한 뒤 상태예요: 처리한 틱의 날짜는 행 i-1의 day_of_year
        per_month[month_of(doy[i - 1])] += cumulative[i] - cumulative[i - 1]
    return [per_month[m] for m in SEASON_MONTH_ORDER]


def plot_monthly(csvs: list[Path], labels: list[str], out: Path, title: str) -> None:
    """시나리오마다 월별 수확량(g/m2) 막대를 나란히 그려요."""
    fig, ax = plt.subplots(figsize=(11, 4.2))
    width = 0.8 / len(csvs)
    for k, (csv_path, label) in enumerate(zip(csvs, labels)):
        values = monthly_harvest(csv_path)
        xs = [i + (k - (len(csvs) - 1) / 2) * width for i in range(len(values))]
        ax.bar(xs, values, width=width, label=label)
    ax.set_xticks(range(len(SEASON_MONTH_ORDER)))
    ax.set_xticklabels([MONTH_NAMES[m] for m in SEASON_MONTH_ORDER])
    ax.set_ylabel("monthly harvest, fresh g/m2")
    ax.grid(True, axis="y", alpha=0.3)
    ax.legend(fontsize=8)
    fig.suptitle(title)
    fig.tight_layout()
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=120)
    plt.close(fig)
    print(f"wrote {out}")


def run_cli(cli: Path, params: Path, scenario: Path, ticks: int, out: Path, no_hash: bool = False) -> bool:
    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(cli), "--params", str(params), "--scenario", str(scenario), "--ticks", str(ticks), "--out", str(out)]
    if no_hash:
        cmd.append("--no-hash")
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
    parser.add_argument("--scenario-root", type=Path, default=REPO / "data/scenarios", help="contains m1/, m2/, m3/ and m4/")
    parser.add_argument("--out-root", type=Path, default=REPO / "runs", help="outputs go to <out-root>/m1, m2, m3 or m4")
    parser.add_argument("--groups", nargs="+", default=[g.name for g in GROUPS], choices=[g.name for g in GROUPS])
    args = parser.parse_args()

    if not args.cli.exists():
        print(f"sim-cli not found: {args.cli} (use --cli)")
        return 1

    failures = 0
    done: dict[tuple[Path, int], Path] = {}  # (시나리오, 일수) -> 이미 만든 CSV (한 번만 돌려요)
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
            key = (path, group.days)
            if key not in done:
                if not run_cli(args.cli, args.params, path, group.days * HOURS_PER_DAY, csv_path, group.no_hash):
                    ok = False
                    break
                done[key] = csv_path
            csvs.append(done[key])
        if not ok:
            failures += 1
            continue
        plot_files(csvs, list(group.scenarios), list(group.columns), out_dir / f"{group.name.lower()}.png",
                   f"{group.name}: {' vs '.join(group.scenarios)}")
        if group.monthly:
            plot_monthly(csvs, list(group.scenarios), out_dir / f"{group.name.lower()}_monthly.png",
                         f"{group.name}: monthly harvest")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
