"""M4 프로토타입 점검: 모든 시나리오(m1~m4)를 sim-cli로 돌려 m4-overview "시나리오 테스트 (M4)" 표와
house-climate S18 조건을 PASS/FAIL로 찍고, 작기 타임라인·성능·그래프를 만들어요.

pandas 없이 csv 모듈만 써요. 그래프는 runs/proto/m4/에 저장해요.

사용법:
    python tools/m4_check.py [--cli build/proto/sim-cli/Release/sim-cli.exe]
                             [--tests build/proto/tests/Release/sim-tests.exe]
                             [--params data/params.toml] [--out runs/proto/m4] [--skip-tests]
"""

from __future__ import annotations

import argparse
import csv
import math
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HOURS_PER_DAY = 24
SEASON_TICKS = 6312   # 263일 (m4-overview)
S15_TICKS = 2712      # 113일
S13_TICKS = 14 * HOURS_PER_DAY
MONTH_START_DOY = [1, 32, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335]
MONTH_NAMES = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]

# M1~M3 시나리오 기간 (각 개요 명세, run_scenarios.py와 같음)
OLD_PERIOD_DAYS = {
    "m1/s0_control": 7, "m1/s1_immature": 7, "m1/s1_mature": 7, "m1/s2_alkaline": 21, "m1/s2_neutral": 21,
    "m1/s3_over": 14, "m1/s3_proper": 14,
    "m2/s4_optimal": 21, "m2/s4_alkaline": 21, "m2/s4_acid": 21, "m2/s8_proper": 14, "m2/s8_waterlogged": 14,
    "m2/s9_lime": 42, "m2/s9_none": 42, "m2/s10_standard": 21, "m2/s10_excess": 21, "m2/s10_dry": 21,
    "m3/s5_standard": 21, "m3/s5_k_excess": 21, "m3/s6_loam": 28, "m3/s6_sand": 28,
    "m3/s11_neutral": 28, "m3/s11_acid": 28, "m3/s11_wet": 28, "m3/s11_alkaline": 28,
    "m3/s12_neutral": 60, "m3/s12_acid": 60, "m3/s12_alkaline": 60,
}

SEASONS = ["season_standard", "season_no_topdress", "season_n_excess", "season_k_low", "season_k_excess",
           "season_acid"]


def month_of(doy: int) -> int:
    m = 0
    for i, start in enumerate(MONTH_START_DOY):
        if doy >= start:
            m = i
    return m


@dataclass
class Run:
    name: str
    rows: list[dict[str, float]]
    cells: int
    elapsed_ms: int
    final_hash: str
    events: str
    wall_s: float
    extra: dict = field(default_factory=dict)

    def col(self, name: str) -> list[float]:
        return [r[name] for r in self.rows]

    def last(self, name: str) -> float:
        return self.rows[-1][name]


def to_float(v: str) -> float:
    if v == "" or v is None:
        return math.nan
    if v.startswith("0x"):
        return float(int(v, 16) % (1 << 52))
    return float(v)


def run_cli(cli: Path, params: Path, scenario: Path, ticks: int, out_csv: Path) -> Run:
    cmd = [str(cli), "--params", str(params), "--scenario", str(scenario), "--ticks", str(ticks), "--out",
           str(out_csv), "--no-hash"]
    t0 = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True, text=True)
    wall = time.perf_counter() - t0
    if p.returncode != 0:
        raise RuntimeError(f"sim-cli failed for {scenario}: {p.stderr.strip()}")
    elapsed = int(re.search(r"elapsed_ms=(\d+)", p.stdout).group(1))
    final_hash = re.search(r"final_state_hash=(0x[0-9a-f]+)", p.stdout).group(1)
    events = re.search(r"events (.*)", p.stdout).group(1)
    with open(out_csv, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        rows = [{k: to_float(v) for k, v in r.items() if k != "state_hash"} for r in reader]
    text = scenario.read_text(encoding="utf-8")
    rows_n = int(re.search(r"bed_rows\s*=\s*(\d+)", text).group(1))
    cols_n = int(re.search(r"bed_cols\s*=\s*(\d+)", text).group(1))
    return Run(scenario.stem, rows, rows_n * cols_n, elapsed, final_hash, events, wall)


# ---- 판정 도우미 ----

RESULTS: list[tuple[str, str, bool, str]] = []


def check(group: str, name: str, ok: bool, detail: str) -> None:
    RESULTS.append((group, name, ok, detail))


def nan_to(x: float, v: float) -> float:
    return v if math.isnan(x) else x


def first_tick(run: Run, column: str, threshold: float = 0.0) -> float:
    for r in run.rows:
        if r[column] > threshold:
            return r["tick"]
    return math.inf


def window_mean(run: Run, column: str, months: set[int]) -> float:
    vals = [r[column] for r in run.rows if month_of(int(r["day_of_year"])) in months and not math.isnan(r[column])]
    return sum(vals) / len(vals) if vals else math.nan


def fmt_day(tick: float) -> str:
    return "never" if math.isinf(tick) else f"d{tick / HOURS_PER_DAY:.1f}"


# ---- 보존 (CSV 하우스 합계, 매 틱) ----

def conservation_errors(run: Run) -> dict[str, float]:
    cells = run.cells
    r0 = run.rows[0]

    def plant(r: dict, name: str) -> float:
        v = r.get(name, math.nan)
        return 0.0 if math.isnan(v) else v * cells

    def soil(r: dict, *names: str) -> float:
        return sum(r[n] for n in names) * cells

    def budget(r: dict) -> dict[str, float]:
        return {
            "water": soil(r, "water_mm"),
            "N": soil(r, "labile_N_g_per_m2", "stable_N_g_per_m2", "nh4_N_g_per_m2", "no2_N_g_per_m2",
                      "no3_N_g_per_m2") + plant(r, "plant_N_g_per_m2"),
            "P": soil(r, "p_g_per_m2", "p_fixed_g_per_m2") + plant(r, "plant_P_g_per_m2"),
            "K": soil(r, "k_g_per_m2") + plant(r, "plant_K_g_per_m2"),
            "Ca": soil(r, "ca_g_per_m2") + plant(r, "plant_Ca_g_per_m2"),
            "Mg": soil(r, "mg_g_per_m2") + plant(r, "plant_Mg_g_per_m2"),
            "Mn": soil(r, "mn_g_per_m2", "mn_oxide_g_per_m2") + plant(r, "plant_Mn_g_per_m2"),
            "S": soil(r, "s_g_per_m2"),
            "C": soil(r, "labile_C_g_per_m2", "stable_C_g_per_m2"),
        }

    init = budget(r0)
    worst = {k: 0.0 for k in init}
    for r in run.rows:
        now = budget(r)
        L = lambda n: r["ledger_" + n]  # noqa: E731
        expected = {
            "water": init["water"] + L("water_in_mm") - L("water_evaporated_mm") - L("water_transpired_mm") -
                     L("water_drained_mm"),
            "N": init["N"] + L("N_in_g") - L("N_leached_g") - L("N_gas_g") - L("N_harvested_g"),
            "P": init["P"] + L("P_in_g") - L("P_leached_g") - L("P_harvested_g"),
            "K": init["K"] + L("K_in_g") - L("K_leached_g") - L("K_harvested_g"),
            "Ca": init["Ca"] + L("Ca_in_g") - L("Ca_leached_g") - L("Ca_harvested_g"),
            "Mg": init["Mg"] + L("Mg_in_g") - L("Mg_leached_g") - L("Mg_harvested_g"),
            "Mn": init["Mn"] + L("Mn_in_g") - L("Mn_leached_g") - L("Mn_harvested_g"),
            "S": init["S"] + L("S_in_g") - L("S_leached_g"),
            "C": init["C"] + L("C_in_g") + L("C_root_litter_g") + L("C_leaf_litter_g") - L("C_respired_g"),
        }
        for k in now:
            err = abs(now[k] - expected[k]) / max(1.0, abs(expected[k]))
            worst[k] = max(worst[k], err)
    return worst


SIGNED_COLUMNS = {"outside_air_temp_degC", "house_air_temp_degC", "house_air_temp_mean_degC", "soil_temp_degC",
                  "last_growth_g_per_m2", "ledger_H_eq_net"}  # 부호가 있는 값 (기온, 순생장, 순 산)


def nonneg_finite(run: Run) -> list[str]:
    bad = set()
    for r in run.rows:
        for k, v in r.items():
            if math.isinf(v) or (k not in SIGNED_COLUMNS and not math.isnan(v) and v < -1e-9):
                bad.add(k)
    return sorted(bad)


# ---- 하우스 진단 ----

def daily(run: Run, column: str, fn) -> list[float]:
    days: dict[int, list[float]] = {}
    for r in run.rows[1:]:
        days.setdefault(int(r["elapsed_days"]), []).append(r[column])
    return [fn(v) for _, v in sorted(days.items())]


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", default=str(REPO / "build/proto/sim-cli/Release/sim-cli.exe"))
    ap.add_argument("--tests", default=str(REPO / "build/proto/tests/Release/sim-tests.exe"))
    ap.add_argument("--params", default=str(REPO / "data/params.toml"))
    ap.add_argument("--out", default=str(REPO / "runs/proto/m4"))
    ap.add_argument("--skip-tests", action="store_true")
    ap.add_argument("--skip-old", action="store_true")
    args = ap.parse_args()
    cli, params, out = Path(args.cli), Path(args.params), Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    sdir = REPO / "data/scenarios"

    runs: dict[str, Run] = {}

    def go(rel: str, ticks: int, scenario: Path | None = None) -> Run:
        path = scenario or (sdir / f"{rel}.toml")
        r = run_cli(cli, params, path, ticks, out / f"{rel.replace('/', '_')}.csv")
        runs[rel] = r
        return r

    # ---- 실행 ----
    for s in SEASONS:
        go(f"m4/{s}", SEASON_TICKS)
    go("m4/season_thinned", SEASON_TICKS)
    for s in ("s15_grade_a", "s15_grade_c"):
        go(f"m4/{s}", S15_TICKS)
    for s in ("s13_proper", "s13_waterlogged"):
        go(f"m4/{s}", S13_TICKS)
    s18_ticks = {}
    for s in ("s18_winter_none", "s18_winter_heated", "s18_autumn_closed", "s18_autumn_vented", "s18_co2_closed",
              "s18_co2_vented"):
        text = (sdir / f"m4/{s}.toml").read_text(encoding="utf-8")
        days = int(re.search(r"시작, (\d+)일", text.splitlines()[0]).group(1))  # "N월 M일 시작, D일"
        s18_ticks[s] = days * HOURS_PER_DAY
        go(f"m4/{s}", days * HOURS_PER_DAY)
    if not args.skip_old:
        for rel, d in OLD_PERIOD_DAYS.items():
            if (sdir / f"{rel}.toml").exists():
                go(rel, d * HOURS_PER_DAY)

    # 극한 (작기 시나리오 변형, runs/proto/m4/extreme_*.toml)
    base_text = (sdir / "m4/season_standard.toml").read_text(encoding="utf-8")
    base_text = base_text.replace('weather_csv = "../../weather/synthetic_year.csv"',
                                  f'weather_csv = "{(REPO / "data/weather/synthetic_year.csv").as_posix()}"')
    extremes = {
        "extreme_no_irrigation": re.sub(r'\[\[schedule\]\]\ntick = (\d+)\naction = "irrigate"\namount_mm = [0-9.]+',
                                        r'[[schedule]]\ntick = \1\naction = "irrigate"\namount_mm = 0.0', base_text),
        "extreme_n_100x": base_text.replace('fertilizer = "calcium_nitrate"\ng_per_m2 = 20.0',
                                            'fertilizer = "calcium_nitrate"\ng_per_m2 = 2000.0'),
        "extreme_no_heat": re.sub(r'action = "set_heater"\non = true', 'action = "set_heater"\non = false',
                                  base_text),
        "extreme_4layer_open_heat": None,
    }
    # 4중 + 창 전부 열림 + 난방 (설정 최대) 고정
    four = base_text.replace("cover_layers = 3", "cover_layers = 4")
    four = re.sub(r"\n\[\[schedule\]\]\ntick = \d+\naction = \"set_(window|heater)\"(\n[^\[\n][^\n]*)*", "", four)
    add = ""
    for layer in range(4):
        for side in ("left", "right"):
            add += f'\n[[schedule]]\ntick = 0\naction = "set_window"\nlayer = {layer}\nside = "{side}"\nopen_fraction = 1.0\n'
    add += '\n[[schedule]]\ntick = 0\naction = "set_heater"\non = true\nsetpoint_degC = 30.0\n'
    extremes["extreme_4layer_open_heat"] = four + add
    for name, text in extremes.items():
        p = out / f"{name}.toml"
        p.write_text(text, encoding="utf-8")
        go(f"extreme/{name}", SEASON_TICKS, p)

    # ---- 1작기 ----
    std = runs["m4/season_standard"]

    def monthly_harvest(run: Run) -> dict[int, tuple[float, float, float, float]]:
        """월별 (생체중 g/m², 개수/m², 평균 당도, 평균 크기 g)"""
        res: dict[int, list[float]] = {}
        prev = run.rows[0]
        for r in run.rows[1:]:
            m = month_of(int(r["day_of_year"]))
            d_fresh = r["harvest_fresh_g_per_m2"] - prev["harvest_fresh_g_per_m2"]
            d_count = r["harvest_count"] - prev["harvest_count"]
            w_now = nan_to(r["harvest_mean_brix"], 0.0) * r["harvest_fresh_g_per_m2"]
            w_prev = nan_to(prev["harvest_mean_brix"], 0.0) * prev["harvest_fresh_g_per_m2"]
            acc = res.setdefault(m, [0.0, 0.0, 0.0])
            acc[0] += d_fresh
            acc[1] += d_count
            acc[2] += w_now - w_prev
            prev = r
        return {m: (v[0], v[1], v[2] / v[0] if v[0] > 0 else math.nan, v[0] / v[1] if v[1] > 0 else math.nan)
                for m, v in res.items()}

    mh = monthly_harvest(std)
    season_months = [11, 0, 1, 2, 3, 4]  # Dec..May (5차 디렉터 승인: 첫 수확 12/15 이전 + 12~5월 매달)
    alive_end = std.last("crop_alive_fraction")
    every_month = all(mh.get(m, (0,))[0] > 0 for m in season_months)
    first_h = first_tick(std, "harvest_count", 0.0)
    dec15_tick = (349 - 253) * HOURS_PER_DAY  # day 349 = 12월 15일, 정식 day 253
    check("1작기", "first harvest by Dec 15, harvest every month Dec-May, crop alive",
          len(std.rows) == SEASON_TICKS + 1 and every_month and alive_end > 0 and first_h <= dec15_tick + 23,
          f"rows={len(std.rows)} alive={alive_end:.2f} first harvest {fmt_day(first_h)} (Dec 15 = d96); monthly fresh g/m2 " +
          " ".join(f"{MONTH_NAMES[m]}={mh.get(m, (0,))[0]:.0f}" for m in season_months))

    # ---- 처방 비교 ----
    yields = {s: runs[f"m4/{s}"].last("harvest_fresh_g_per_m2") for s in SEASONS}
    brix = {s: runs[f"m4/{s}"].last("harvest_mean_brix") for s in SEASONS}
    firm = {s: runs[f"m4/{s}"].last("harvest_mean_firmness") for s in SEASONS}
    distinct_y = len({round(v, 1) for v in yields.values()}) == len(yields)
    distinct_b = len({round(nan_to(v, -1), 3) for v in brix.values()}) == len(brix)
    check("처방 비교", "yield and mean brix differ between prescriptions", distinct_y and distinct_b and
          all(v > 0 for v in yields.values()),
          " ".join(f"{s.replace('season_', '')}={yields[s]:.0f}g/{nan_to(brix[s], 0):.2f}Bx" for s in SEASONS))

    # ---- 적과 (strawberry-fruit 6c) ----
    th = runs["m4/season_thinned"]
    size_std = std.last("harvest_fresh_g_per_m2") / max(std.last("harvest_count"), 1e-9)
    size_th = th.last("harvest_fresh_g_per_m2") / max(th.last("harvest_count"), 1e-9)
    check("적과", "thinned: larger mean fruit and higher brix than unthinned",
          size_th > size_std and nan_to(th.last("harvest_mean_brix"), 0) > nan_to(std.last("harvest_mean_brix"), 99),
          f"g/fruit {size_std:.1f} vs {size_th:.1f}; brix {std.last('harvest_mean_brix'):.2f} vs "
          f"{th.last('harvest_mean_brix'):.2f}; yield {std.last('harvest_fresh_g_per_m2'):.0f} vs "
          f"{th.last('harvest_fresh_g_per_m2'):.0f}; fruits/plant {std.last('harvest_count') / 8:.1f} vs "
          f"{th.last('harvest_count') / 8:.1f}; thinned {th.last('thinned_count'):.0f}/m2")

    # ---- S7 ----
    nt = runs["m4/season_no_topdress"]
    spring = {2, 3, 4}
    sN_std, sN_nt = window_mean(std, "sufficiency_N", spring), window_mean(nt, "sufficiency_N", spring)
    sK_std, sK_nt = window_mean(std, "sufficiency_K", spring), window_mean(nt, "sufficiency_K", spring)
    check("S7", "no topdress: lower Mar-May N sufficiency, lower yield (5차: K는 S16에서)",
          sN_nt < sN_std and yields["season_no_topdress"] < yields["season_standard"],
          f"suffN {sN_std:.3f} vs {sN_nt:.3f}, suffK {sK_std:.3f} vs {sK_nt:.3f}, yield "
          f"{yields['season_standard']:.0f} vs {yields['season_no_topdress']:.0f}")

    # ---- S13 ----
    p13, w13 = runs["m4/s13_proper"], runs["m4/s13_waterlogged"]
    gp, gw = p13.last("ledger_N_gas_g") / p13.cells, w13.last("ledger_N_gas_g") / w13.cells
    check("S13", "waterlogged N_gas >> proper, proper ~ 0", gw > gp and gp <= 0.05 * max(gw, 1e-12),
          f"N_gas g/m2 proper={gp:.4f} waterlogged={gw:.4f} (house totals {p13.last('ledger_N_gas_g'):.2f} vs "
          f"{w13.last('ledger_N_gas_g'):.2f} g)")

    # ---- S14 ----
    ne = runs["m4/season_n_excess"]
    t_truss_std, t_truss_ne = first_tick(std, "trusses_initiated", 0.5), first_tick(ne, "trusses_initiated", 0.5)
    t_h_std, t_h_ne = first_tick(std, "harvest_count", 0.0), first_tick(ne, "harvest_count", 0.0)
    check("S14", "N excess: later first truss & first harvest, lower brix",
          t_truss_ne > t_truss_std and t_h_ne > t_h_std and nan_to(brix["season_n_excess"], 99) <
          nan_to(brix["season_standard"], -1),
          f"first truss {fmt_day(t_truss_std)} vs {fmt_day(t_truss_ne)}, first harvest {fmt_day(t_h_std)} vs "
          f"{fmt_day(t_h_ne)}, brix {nan_to(brix['season_standard'], 0):.2f} vs {nan_to(brix['season_n_excess'], 0):.2f}"
          f", excess_N mean {window_mean(std, 'excess_index_N', set(range(12))):.2f} vs "
          f"{window_mean(ne, 'excess_index_N', set(range(12))):.2f}")

    # ---- S15 ----
    a15, c15 = runs["m4/s15_grade_a"], runs["m4/s15_grade_c"]
    ta, tc = first_tick(a15, "harvest_count", 0.0), first_tick(c15, "harvest_count", 0.0)
    ya, yc = a15.last("harvest_fresh_g_per_m2"), c15.last("harvest_fresh_g_per_m2")
    check("S15", "grade A: earlier first harvest, more yield by Dec 31", ta < tc and ya > yc,
          f"first harvest A {fmt_day(ta)} vs C {fmt_day(tc)}; yield by Dec 31 A={ya:.1f} C={yc:.1f} g/m2; "
          f"first truss A {fmt_day(first_tick(a15, 'trusses_initiated', 0.5))} C "
          f"{fmt_day(first_tick(c15, 'trusses_initiated', 0.5))}")

    # ---- S16 / S17 ----
    check("S16", "K low: lower mean brix", nan_to(brix["season_k_low"], 99) < nan_to(brix["season_standard"], -1),
          f"brix std={nan_to(brix['season_standard'], 0):.3f} k_low={nan_to(brix['season_k_low'], 0):.3f}; "
          f"mean suffK {window_mean(std, 'sufficiency_K', set(range(12))):.3f} vs "
          f"{window_mean(runs['m4/season_k_low'], 'sufficiency_K', set(range(12))):.3f}")
    check("S17", "K excess: lower mean firmness",
          nan_to(firm["season_k_excess"], 99) < nan_to(firm["season_standard"], -1),
          f"firmness std={nan_to(firm['season_standard'], 0):.4f} k_excess={nan_to(firm['season_k_excess'], 0):.4f};"
          f" mean suffCa {window_mean(std, 'sufficiency_Ca', set(range(12))):.3f} vs "
          f"{window_mean(runs['m4/season_k_excess'], 'sufficiency_Ca', set(range(12))):.3f}")

    # ---- S18 ----
    wn, wh = runs["m4/s18_winter_none"], runs["m4/s18_winter_heated"]
    min_n = min(wn.col("house_air_temp_degC")[1:])
    min_h = min(wh.col("house_air_temp_degC")[HOURS_PER_DAY:])  # 첫날은 열 관성 전이(난방 꺼진 채 생성)라 제외
    min_h_all = min(wh.col("house_air_temp_degC")[1:])
    leaf_n, leaf_h = wn.last("leaf_g_per_m2"), wh.last("leaf_g_per_m2")
    litter_n = wn.last("ledger_C_leaf_litter_g") - wh.last("ledger_C_leaf_litter_g")
    check("S18", "winter none: min < 0 C, leaf loss", min_n < 0 and leaf_n < leaf_h and litter_n > 0,
          f"min air {min_n:.1f} C; leaf none={leaf_n:.1f} heated={leaf_h:.1f} g/m2; extra leaf-litter C "
          f"{litter_n / wn.cells:.3f} g/m2")
    e_h = wh.last("heating_energy_MJ_per_m2")
    check("S18", "winter heated: min ~ setpoint 6 C, energy > 0", min_h >= 5.5 and e_h > 0,
          f"min air after day 1 {min_h:.2f} C (incl. day 1: {min_h_all:.2f}), energy {e_h:.1f} MJ/m2")
    ac, av = runs["m4/s18_autumn_closed"], runs["m4/s18_autumn_vented"]
    max_c, max_v = max(ac.col("house_air_temp_degC")), max(av.col("house_air_temp_degC"))
    ind_c = ac.last("induction_index") + ac.last("trusses_initiated")
    ind_v = av.last("induction_index") + av.last("trusses_initiated")
    check("S18", "autumn closed: max > 30 C, slower induction", max_c > 30 and ind_c < ind_v,
          f"max air closed={max_c:.1f} vented={max_v:.1f} C; induction progress closed={ind_c:.3f} "
          f"vented={ind_v:.3f}; establishment {ac.last('establishment_index'):.2f}/{av.last('establishment_index'):.2f}")
    cc, cv = runs["m4/s18_co2_closed"], runs["m4/s18_co2_vented"]
    day_min_c = min(r["house_co2_ppm"] for r in cc.rows if 8 <= r["hour"] <= 16)
    day_min_v = min(r["house_co2_ppm"] for r in cv.rows if 8 <= r["hour"] <= 16)
    gross_c, gross_v = sum(cc.col("last_gross_g_per_m2")), sum(cv.col("last_gross_g_per_m2"))
    tmean_c = sum(cc.col("house_air_temp_degC")) / len(cc.rows)
    tmean_v = sum(cv.col("house_air_temp_degC")) / len(cv.rows)
    e_c, e_v = cc.last("heating_energy_MJ_per_m2"), cv.last("heating_energy_MJ_per_m2")
    check("S18", "CO2 closed: lower daytime min CO2 and lower gross; vented colder (7차: 에너지 조건 제외)",
          day_min_c < day_min_v and gross_c < gross_v and tmean_v < tmean_c,
          f"day min CO2 closed={day_min_c:.0f} vented={day_min_v:.0f} ppm; gross sum {gross_c:.2f} vs "
          f"{gross_v:.2f} g/m2; mean air {tmean_c:.2f} vs {tmean_v:.2f} C; energy {e_c:.1f} vs {e_v:.1f} MJ/m2")

    # ---- 보존·불변 (CSV) ----
    worst_all = {}
    bad_nonneg = []
    for rel, r in runs.items():
        w = conservation_errors(r)
        for k, v in w.items():
            if v > worst_all.get(k, (0.0, ""))[0]:
                worst_all[k] = (v, rel)
        bad_cols = nonneg_finite(r)
        if bad_cols:
            bad_nonneg.append(f"{rel}:{','.join(bad_cols)}")
    tol = 1e-9
    check("보존", "water/N/P/K/Ca/Mg/Mn/S/C closed every tick (CSV house sums, rel err <= 1e-9)",
          all(v[0] <= tol for v in worst_all.values()),
          " ".join(f"{k}={v[0]:.1e}" for k, v in worst_all.items()) +
          f" (worst run {max(worst_all.values())[1]})")
    check("보존", "all pools >= 0 and finite (CSV means)", not bad_nonneg, f"bad: {bad_nonneg}" if bad_nonneg else
          f"{len(runs)} runs incl. 4 extremes")

    # ---- 결정론 ----
    again = run_cli(cli, params, sdir / "m4/season_standard.toml", SEASON_TICKS, out / "det_again.csv")
    check("결정론", "season_standard twice -> same final hash", again.final_hash == std.final_hash,
          f"{std.final_hash} / {again.final_hash}")

    # ---- C++ 테스트 ----
    if not args.skip_tests:
        for tag in ("[scenario]", "[conservation]", "[determinism]", "[m4]"):
            p = subprocess.run([args.tests, tag], capture_output=True, text=True)
            summary = [ln for ln in p.stdout.splitlines() if ln.startswith(("test cases", "All tests passed"))]
            failed_names = sorted(set(re.findall(r"^(\S.*)\n-+\n.*FAILED", p.stdout, re.M)))
            check("C++ tests", f"sim-tests {tag}", p.returncode == 0,
                  (summary[-1] if summary else p.stdout[-200:]).strip())

    # ---- 성능 ----
    t0 = time.perf_counter()
    p = subprocess.run([str(cli), "--params", str(params), "--scenario", str(sdir / "m4/season_standard.toml"),
                        "--ticks", str(SEASON_TICKS)], capture_output=True, text=True)
    wall = time.perf_counter() - t0
    core_ms = int(re.search(r"elapsed_ms=(\d+)", p.stdout).group(1))
    check("성능", "season_standard headless (Release) within a few seconds", wall < 5.0,
          f"wall {wall:.2f} s (process incl. load), step loop {core_ms} ms; with CSV+hash-free "
          f"{std.elapsed_ms} ms")

    # ---- 출력 ----
    print("\n=== PASS/FAIL ===")
    for g, n, ok, d in RESULTS:
        print(f"[{'PASS' if ok else 'FAIL'}] {g:9s} {n}\n         {d}")

    print("\n=== season_standard timeline (monthly) ===")
    hdr = ("month", "air_min", "air_mean", "air_max", "co2_mean", "co2_day_min", "trusses+", "fresh_g/m2",
           "count/m2", "brix", "g/fruit", "leaf", "crown", "root", "fruit", "suffN", "suffK", "suffCa", "exN")
    print(" ".join(f"{h:>9s}" for h in hdr))
    by_month: dict[int, list[dict]] = {}
    order: list[int] = []
    for r in std.rows[1:]:
        m = month_of(int(r["day_of_year"]))
        if m not in by_month:
            order.append(m)
        by_month.setdefault(m, []).append(r)
    prev_trusses = std.rows[0]["trusses_initiated"]
    timeline = []
    for m in order:
        rs = by_month[m]
        air = [r["house_air_temp_degC"] for r in rs]
        co2 = [r["house_co2_ppm"] for r in rs]
        co2_day = [r["house_co2_ppm"] for r in rs if 9 <= r["hour"] <= 15]
        last = rs[-1]
        h = mh.get(m, (0, 0, math.nan, math.nan))
        row = (MONTH_NAMES[m], min(air), sum(air) / len(air), max(air), sum(co2) / len(co2), min(co2_day),
               last["trusses_initiated"] - prev_trusses, h[0], h[1], h[2], h[3], last["leaf_g_per_m2"],
               last["crown_g_per_m2"], last["root_g_per_m2"], last["fruit_g_per_m2"],
               sum(r["sufficiency_N"] for r in rs) / len(rs), sum(r["sufficiency_K"] for r in rs) / len(rs),
               sum(r["sufficiency_Ca"] for r in rs) / len(rs), sum(r["excess_index_N"] for r in rs) / len(rs))
        prev_trusses = last["trusses_initiated"]
        timeline.append(row)
        print(f"{row[0]:>9s} " + " ".join(f"{v:9.2f}" for v in row[1:]))
    print(f"heating energy total {std.last('heating_energy_MJ_per_m2'):.1f} MJ/m2; first harvest "
          f"{fmt_day(first_tick(std, 'harvest_count', 0.0))}; total {std.last('harvest_fresh_g_per_m2'):.0f} g/m2, "
          f"{std.last('harvest_count'):.1f} fruits/m2, deformed {std.last('harvest_deformed_count'):.1f}/m2")

    # ---- 그래프 ----
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not available; skipping plots")
        return 0
    pngs = []

    days = [r["tick"] / HOURS_PER_DAY for r in std.rows]
    fig, ax = plt.subplots(4, 1, figsize=(11, 13), sharex=True)
    ax[0].plot(days, std.col("house_air_temp_degC"), lw=0.4, label="house air")
    ax[0].plot(days, std.col("outside_air_temp_degC"), lw=0.4, alpha=0.6, label="outside")
    ax[0].plot(days, std.col("house_air_temp_mean_degC"), lw=1.2, label="house mean")
    ax[0].set_ylabel("degC"); ax[0].legend(loc="upper right")
    ax[1].plot(days, std.col("house_co2_ppm"), lw=0.4, label="house CO2")
    ax[1].plot(days, std.col("outside_co2_ppm"), lw=0.4, alpha=0.6, label="outside CO2")
    ax[1].set_ylabel("ppm"); ax[1].legend(loc="upper right")
    for c in ("leaf_g_per_m2", "crown_g_per_m2", "root_g_per_m2", "fruit_g_per_m2"):
        ax[2].plot(days, std.col(c), label=c)
    ax[2].set_ylabel("g DM/m2"); ax[2].legend(loc="upper right")
    ax[3].plot(days, std.col("harvest_fresh_g_per_m2"), label="cum. harvest fresh g/m2")
    ax3b = ax[3].twinx()
    ax3b.plot(days, std.col("trusses_initiated"), color="tab:red", label="trusses")
    ax3b.plot(days, std.col("fruit_count"), color="tab:green", label="fruits set")
    ax[3].set_xlabel("days after planting (day 253)"); ax[3].legend(loc="upper left"); ax3b.legend(loc="center left")
    fig.suptitle("season_standard")
    fig.tight_layout()
    p = out / "season_standard.png"; fig.savefig(p, dpi=110); plt.close(fig); pngs.append(p)

    fig, ax = plt.subplots(1, 3, figsize=(15, 4.5))
    for s in SEASONS:
        r = runs[f"m4/{s}"]
        ax[0].plot([x["tick"] / HOURS_PER_DAY for x in r.rows], r.col("harvest_fresh_g_per_m2"),
                   label=s.replace("season_", ""))
    ax[0].set_title("cumulative harvest fresh g/m2"); ax[0].set_xlabel("days"); ax[0].legend()
    names = [s.replace("season_", "") for s in SEASONS]
    ax[1].bar(names, [nan_to(brix[s], 0) for s in SEASONS]); ax[1].set_title("mean brix")
    ax[1].tick_params(axis="x", rotation=45)
    ax[2].bar(names, [nan_to(firm[s], 0) for s in SEASONS]); ax[2].set_title("mean firmness")
    ax[2].tick_params(axis="x", rotation=45)
    fig.tight_layout()
    p = out / "prescriptions.png"; fig.savefig(p, dpi=110); plt.close(fig); pngs.append(p)

    fig, ax = plt.subplots(2, 3, figsize=(15, 7))
    pairs = [("s18_winter_none", "s18_winter_heated", "house_air_temp_degC"),
             ("s18_autumn_closed", "s18_autumn_vented", "house_air_temp_degC"),
             ("s18_co2_closed", "s18_co2_vented", "house_co2_ppm")]
    for i, (a, b, col) in enumerate(pairs):
        for s in (a, b):
            r = runs[f"m4/{s}"]
            ax[0][i].plot([x["tick"] / HOURS_PER_DAY for x in r.rows], r.col(col), lw=0.8, label=s)
        ax[0][i].set_title(col); ax[0][i].legend(fontsize=7)
    for i, (a, b, col) in enumerate([("s18_winter_none", "s18_winter_heated", "leaf_g_per_m2"),
                                     ("s18_autumn_closed", "s18_autumn_vented", "induction_index"),
                                     ("s18_co2_closed", "s18_co2_vented", "last_gross_g_per_m2")]):
        for s in (a, b):
            r = runs[f"m4/{s}"]
            ax[1][i].plot([x["tick"] / HOURS_PER_DAY for x in r.rows], r.col(col), lw=0.8, label=s)
        ax[1][i].set_title(col); ax[1][i].legend(fontsize=7)
    fig.tight_layout()
    p = out / "s18.png"; fig.savefig(p, dpi=110); plt.close(fig); pngs.append(p)

    fig, ax = plt.subplots(1, 3, figsize=(15, 4))
    for s in ("s15_grade_a", "s15_grade_c"):
        r = runs[f"m4/{s}"]
        d = [x["tick"] / HOURS_PER_DAY for x in r.rows]
        ax[0].plot(d, r.col("harvest_fresh_g_per_m2"), label=s); ax[1].plot(d, r.col("trusses_initiated"), label=s)
    ax[0].set_title("S15 cumulative harvest"); ax[0].legend(); ax[1].set_title("S15 trusses"); ax[1].legend()
    for s in ("s13_proper", "s13_waterlogged"):
        r = runs[f"m4/{s}"]
        ax[2].plot([x["tick"] / HOURS_PER_DAY for x in r.rows], [v / r.cells for v in r.col("ledger_N_gas_g")],
                   label=s)
    ax[2].set_title("S13 N_gas g/m2"); ax[2].legend()
    fig.tight_layout()
    p = out / "s13_s15.png"; fig.savefig(p, dpi=110); plt.close(fig); pngs.append(p)

    print("\nPNG:", *[str(x) for x in pngs], sep="\n  ")
    return 0 if all(ok for _, _, ok, _ in RESULTS) else 1


if __name__ == "__main__":
    sys.exit(main())
