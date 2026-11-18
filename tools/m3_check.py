"""M3 프로토타입 점검: 모든 시나리오(M1·M2·M3)를 sim-cli로 돌리고, S5·S6·S11·S12 수용 조건과
기준 토양 점검값(용액 비율, φ, 흡수 능력 여유)을 PASS/FAIL로 출력해요. 그래프는 runs/proto/*.png.

사용법:
    python tools/m3_check.py --cli build/proto/sim-cli/Release/sim-cli.exe
    python tools/m3_check.py --no-run      # 이미 있는 CSV로 판정만
"""

from __future__ import annotations

import argparse
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path

import csv as csvmod
from types import SimpleNamespace

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from plot_csv import plot_files  # noqa: E402
from run_scenarios import GROUPS as M12_GROUPS  # noqa: E402
from run_scenarios import run_cli  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
HOURS_PER_DAY = 24


@dataclass(frozen=True)
class Group:
    name: str
    milestone: str
    scenarios: tuple[str, ...]
    days: int
    columns: tuple[str, ...]


M3_GROUPS = [
    Group("S5", "m3", ("s5_standard", "s5_k_excess"), 21,
          ("ledger_Ca_uptake_g", "sufficiency_Ca", "sufficiency_K", "ledger_Ca_leached_g", "antagonism_Ca_factor",
           "ca_solution_g_per_m2", "k_solution_g_per_m2", "ec_dS_per_m")),
    Group("S6", "m3", ("s6_loam", "s6_sand"), 21,
          ("ph", "k_solution_g_per_m2", "ec_dS_per_m", "ledger_K_leached_g", "exchange_free_site_index")),
    Group("S11", "m3", ("s11_neutral", "s11_acid", "s11_wet", "s11_alkaline"), 21,
          ("mn_solution_g_per_m2", "mn_g_per_m2", "mn_oxide_g_per_m2", "plant_Mn_ratio", "sufficiency_Mn", "aeration_index", "ph", "leaf_g_per_m2")),
    Group("S12", "m3", ("s12_neutral", "s12_acid", "s12_alkaline"), 28,
          ("p_g_per_m2", "p_fixed_g_per_m2", "p_total_g_per_m2", "ph")),
]

ALL_GROUPS = [Group(g.name, g.milestone, g.scenarios, g.days, g.columns) for g in M12_GROUPS] + M3_GROUPS

class Frame:
    """pandas 없이 CSV 열을 numpy 배열로 (tools/requirements.txt에는 numpy·matplotlib만 보장)."""

    def __init__(self, path: Path) -> None:
        with path.open(encoding="utf-8", newline="") as f:
            rows = list(csvmod.reader(f))
        header, body = rows[0], rows[1:]
        self.cols: dict[str, np.ndarray] = {}
        for i, name in enumerate(header):
            if name == "state_hash":
                continue
            self.cols[name] = np.array([float(r[i]) if r[i] != "" else np.nan for r in body])

    def __getattr__(self, name: str) -> np.ndarray:
        try:
            return self.__dict__["cols"][name]
        except KeyError as e:
            raise AttributeError(name) from e

    def __getitem__(self, name: str) -> np.ndarray:
        return self.cols[name]

    def row(self, i: int) -> SimpleNamespace:
        return SimpleNamespace(**{k: v[i] for k, v in self.cols.items()})


results: list[tuple[str, bool, str]] = []


def check(name: str, ok: bool, detail: str) -> None:
    results.append((name, bool(ok), detail))


def info(name: str, ok: bool, detail: str) -> None:
    results.append((name, None, detail))


def cells_of(scenario_path: Path) -> int:
    with scenario_path.open("rb") as f:
        doc = tomllib.load(f)
    return int(doc["house"]["bed_rows"]) * int(doc["house"]["bed_cols"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cli", type=Path, default=REPO / "build/proto/sim-cli/Release/sim-cli.exe")
    parser.add_argument("--params", type=Path, default=REPO / "data/params.toml")
    parser.add_argument("--scenario-root", type=Path, default=REPO / "data/scenarios")
    parser.add_argument("--out-root", type=Path, default=REPO / "runs/proto")
    parser.add_argument("--no-run", action="store_true")
    args = parser.parse_args()

    csv: dict[str, Path] = {}
    for g in ALL_GROUPS:
        paths = []
        for name in g.scenarios:
            out = args.out_root / g.milestone / f"{name}.csv"
            scen = args.scenario_root / g.milestone / f"{name}.toml"
            if not args.no_run and name not in csv:
                if not run_cli(args.cli, args.params, scen, g.days * HOURS_PER_DAY, out):
                    return 1
            csv[name] = out
            paths.append(out)
        plot_files(paths, list(g.scenarios), list(g.columns), args.out_root / f"{g.name.lower()}.png",
                   f"{g.name}: {' vs '.join(g.scenarios)} (M3 prototype pass 5)")

    proto_dir = REPO / "tools/proto_data"
    proto_dir.mkdir(exist_ok=True)
    base = (args.scenario_root / "m2" / "s9_none.toml").read_text(encoding="utf-8")
    base = base.replace('weather_csv = "../../weather/synthetic_year.csv"', 'weather_csv = "../../data/weather/synthetic_year.csv"')
    head, sched = base.split("[crop]", 1)
    sched = sched.split("[difficulty]", 1)[1]
    lime_g = 300.0  # s9_lime와 같은 양: 300 g x 20 eq/kg = 6 eq Ca
    cano3_g = lime_g / 1000 * 20.0 * 20.039 / 190.0 * 1000  # 같은 Ca 당량의 질산칼슘 (Ca 190 g/kg)
    for name, action in (("proto_acid_lime", f'action = "add_lime"\nlime = "calcium_carbonate"\ng_per_m2 = {lime_g}\n'),
                         ("proto_acid_cano3", f'action = "add_fertilizer"\nfertilizer = "calcium_nitrate"\ng_per_m2 = {cano3_g:.3f}\n')):
        text = head + "[difficulty]" + sched + f"\n[[schedule]]\ntick = 0\n{action}"
        text = text.replace('name = "m2/s9_none"', f'name = "proto/{name}"').replace('name = "s9_none"', f'name = "proto/{name}"')
        path = proto_dir / f"{name}.toml"
        path.write_text(text, encoding="utf-8")
        out = args.out_root / "proto" / f"{name}.csv"
        if not args.no_run and not run_cli(args.cli, args.params, path, 14 * HOURS_PER_DAY, out):
            return 1
        csv[name] = out
    plot_files([csv["s9_lime"], csv["s9_none"], csv["proto_acid_lime"], csv["proto_acid_cano3"]],
               ["s9_lime", "s9_none", "acid+lime (no crop)", "acid+Ca(NO3)2 (no crop)"],
               ["ph", "mn_solution_g_per_m2", "mn_g_per_m2", "mn_oxide_g_per_m2", "ca_solution_g_per_m2",
                "exchange_free_site_index"],
               args.out_root / "s11_lime.png", "S11 lime condition + lime vs Ca(NO3)2 (M3 prototype pass 5)")

    d = {name: Frame(path) for name, path in csv.items()}

    # ---- S5 ----
    std, exc = d["s5_standard"].row(-1), d["s5_k_excess"].row(-1)
    check("S5-1 Ca uptake: excess < standard", exc.ledger_Ca_uptake_g < std.ledger_Ca_uptake_g,
          f"std={std.ledger_Ca_uptake_g:.3f} exc={exc.ledger_Ca_uptake_g:.3f} g (sum of cells)")
    check("S5-2 final Ca sufficiency: excess < standard", exc.sufficiency_Ca < std.sufficiency_Ca,
          f"std={std.sufficiency_Ca:.4f} exc={exc.sufficiency_Ca:.4f}")
    check("S5-3 final K sufficiency: excess >= standard", exc.sufficiency_K >= std.sufficiency_K,
          f"std={std.sufficiency_K:.4f} exc={exc.sufficiency_K:.4f}")
    check("S5-4 Ca leached: excess > standard", exc.ledger_Ca_leached_g > std.ledger_Ca_leached_g,
          f"std={std.ledger_Ca_leached_g:.3f} exc={exc.ledger_Ca_leached_g:.3f} g")
    ec_max = d["s5_k_excess"].ec_dS_per_m.max()
    info("S5-aux excess max EC (diagnostic; was <= 2.5 target)", ec_max <= 2.5,
          f"max EC exc={ec_max:.3f} std={d['s5_standard'].ec_dS_per_m.max():.3f}; "
          f"min antagonism Ca factor std={d['s5_standard'].antagonism_Ca_factor.min():.3f} "
          f"exc={d['s5_k_excess'].antagonism_Ca_factor.min():.3f}; final biomass std="
          f"{std.leaf_g_per_m2 + std.root_g_per_m2:.2f} exc={exc.leaf_g_per_m2 + exc.root_g_per_m2:.2f}")

    s5s = d["s5_standard"]
    for el in ("Ca", "Mg"):
        col = s5s[f"sufficiency_{el}"]
        check(f"S5-guard standard {el} sufficiency final & min >= 0.95", col[-1] >= 0.95 and np.nanmin(col) >= 0.95,
              f"final={col[-1]:.4f} min={np.nanmin(col):.4f}")

    print_s5 = []
    for name in ("s5_standard", "s5_k_excess"):
        f = d[name]
        bio = f.leaf_g_per_m2 + f.root_g_per_m2
        ca_ratio = f.plant_Ca_g_per_m2 / (0.012 * bio)
        mg_ratio = f.plant_Mg_g_per_m2 / (0.004 * bio)
        print_s5.append(
            f"  {name:12s} Ca factor mean={np.nanmean(f.antagonism_Ca_factor):.3f} min={np.nanmin(f.antagonism_Ca_factor):.3f} "
            f"Mg factor mean={np.nanmean(f.antagonism_Mg_factor):.3f} min={np.nanmin(f.antagonism_Mg_factor):.3f} | "
            f"plant Ca ratio final={ca_ratio[-1]:.3f} min={np.nanmin(ca_ratio):.3f} Mg ratio final={mg_ratio[-1]:.3f} | "
            f"Ca suff final={f.sufficiency_Ca[-1]:.4f} min={np.nanmin(f.sufficiency_Ca):.4f} "
            f"Mg suff final={f.sufficiency_Mg[-1]:.4f} min={np.nanmin(f.sufficiency_Mg):.4f} | "
            f"Ca uptake={f.ledger_Ca_uptake_g[-1]:.2f} g | stress h Ca={f.stress_hours_Ca[-1]:.0f} Mg={f.stress_hours_Mg[-1]:.0f} "
            f"| biomass={bio[-1]:.2f}")

    # ---- S6 ----
    loam, sand = d["s6_loam"], d["s6_sand"]
    for col in ("ph", "k_solution_g_per_m2", "ec_dS_per_m"):
        rl = loam[col].max() - loam[col].min()
        rs = sand[col].max() - sand[col].min()
        check(f"S6 range {col}: sand > loam", rs > rl, f"loam={rl:.4f} sand={rs:.4f}")
    kl, ks = loam.ledger_K_leached_g[-1], sand.ledger_K_leached_g[-1]
    check("S6 K leached: sand > loam", ks > kl, f"loam={kl:.3f} sand={ks:.3f} g")

    # ---- S11 ----
    mean_mn = {k: d[f"s11_{k}"].mn_solution_g_per_m2.mean() for k in ("neutral", "acid", "wet", "alkaline")}
    ratio = {k: d[f"s11_{k}"].plant_Mn_ratio[-1] for k in mean_mn}
    suff = {k: d[f"s11_{k}"].sufficiency_Mn[-1] for k in mean_mn}
    for a, b in (("acid", "neutral"), ("wet", "neutral"), ("neutral", "alkaline")):
        check(f"S11 mean solution Mn: {a} > {b}", mean_mn[a] > mean_mn[b],
              f"{a}={mean_mn[a]:.5f} {b}={mean_mn[b]:.5f} g/m2")
    for a, b in (("acid", "neutral"), ("wet", "neutral"), ("neutral", "alkaline")):
        check(f"S11 final plant Mn ratio: {a} > {b}", ratio[a] > ratio[b], f"{a}={ratio[a]:.3f} {b}={ratio[b]:.3f}")
    check("S11 Mn sufficiency: alkaline < neutral", suff["alkaline"] < suff["neutral"],
          f"alkaline={suff['alkaline']:.4f} neutral={suff['neutral']:.4f}")
    tox = {k: d[f"s11_{k}"].stress_hours_Mn_toxicity[-1] for k in mean_mn}
    defi = {k: d[f"s11_{k}"].stress_hours_Mn_deficiency[-1] for k in mean_mn}
    bio = {k: d[f"s11_{k}"].leaf_g_per_m2[-1] + d[f"s11_{k}"].root_g_per_m2[-1] for k in mean_mn}
    info("S11-info (not a condition)", True,
          f"Mn tox hours={ {k: round(v, 1) for k, v in tox.items()} } def hours={ {k: round(v, 1) for k, v in defi.items()} } "
          f"biomass={ {k: round(v, 2) for k, v in bio.items()} }")

    # ---- S11 석회 조건 (M2 s9_lime 대 s9_none, 14일) ----
    lime, none = d["s9_lime"], d["s9_none"]
    check("S11-lime mean solution Mn: s9_lime < s9_none",
          np.mean(lime.mn_solution_g_per_m2) < np.mean(none.mn_solution_g_per_m2),
          f"lime={np.mean(lime.mn_solution_g_per_m2):.4f} none={np.mean(none.mn_solution_g_per_m2):.4f} g/m2")
    check("S11-lime final Mn2+ (exch+sol): s9_lime < s9_none", lime.mn_g_per_m2[-1] < none.mn_g_per_m2[-1],
          f"lime={lime.mn_g_per_m2[-1]:.4f} none={none.mn_g_per_m2[-1]:.4f} g/m2 (oxide lime={lime.mn_oxide_g_per_m2[-1]:.2f} "
          f"none={none.mn_oxide_g_per_m2[-1]:.2f}; pH lime={lime.ph[-1]:.2f} none={none.ph[-1]:.2f}; "
          f"Mn tox h lime={lime.stress_hours_Mn_toxicity[-1]:.0f} none={none.stress_hours_Mn_toxicity[-1]:.0f})")

    check("S11-lime final plant Mn ratio: s9_lime < s9_none", lime.plant_Mn_ratio[-1] < none.plant_Mn_ratio[-1],
          f"lime={lime.plant_Mn_ratio[-1]:.3f} none={none.plant_Mn_ratio[-1]:.3f}; phi mean lime={np.mean(lime.exchange_free_site_index):.4f} "
          f"none={np.mean(none.exchange_free_site_index):.4f}; final sol Mn lime={lime.mn_solution_g_per_m2[-1]:.4f} "
          f"none={none.mn_solution_g_per_m2[-1]:.4f}")

    # ---- 단위식 비교: 산성 토양, 석회 대 질산칼슘 (같은 Ca 당량), 작물 없음, 14일 ----
    pl, pn = d["proto_acid_lime"], d["proto_acid_cano3"]
    check("Unit lime vs Ca(NO3)2 same Ca eq: final solution Mn lime < CaNO3",
          pl.mn_solution_g_per_m2[-1] < pn.mn_solution_g_per_m2[-1],
          f"lime={pl.mn_solution_g_per_m2[-1]:.4f} cano3={pn.mn_solution_g_per_m2[-1]:.4f} "
          f"(mean lime={np.mean(pl.mn_solution_g_per_m2):.4f} cano3={np.mean(pn.mn_solution_g_per_m2):.4f})")
    check("Unit lime vs Ca(NO3)2 same Ca eq: final Mn2+ lime < CaNO3", pl.mn_g_per_m2[-1] < pn.mn_g_per_m2[-1],
          f"lime={pl.mn_g_per_m2[-1]:.4f} cano3={pn.mn_g_per_m2[-1]:.4f} (pH lime={pl.ph[-1]:.2f} cano3={pn.ph[-1]:.2f}; "
          f"Ca sol lime={pl.ca_solution_g_per_m2[-1]:.2f} cano3={pn.ca_solution_g_per_m2[-1]:.2f}; "
          f"lime undissolved eq={pl.lime_undissolved_eq_per_m2[-1]:.2f}; EC max cano3={np.max(pn.ec_dS_per_m):.2f})")

    # ---- S12 ----
    p1 = {k: d[f"s12_{k}"].p_g_per_m2[24] for k in ("neutral", "acid", "alkaline")}
    p28 = {k: d[f"s12_{k}"].p_g_per_m2[28 * 24] for k in p1}
    check("S12 day-28 labile P: neutral > acid", p28["neutral"] > p28["acid"],
          f"neutral={p28['neutral']:.3f} acid={p28['acid']:.3f}")
    check("S12 day-28 labile P: neutral > alkaline", p28["neutral"] > p28["alkaline"],
          f"neutral={p28['neutral']:.3f} alkaline={p28['alkaline']:.3f}")
    for k in p1:
        check(f"S12 {k}: day1 > day28", p1[k] > p28[k], f"day1={p1[k]:.3f} day28={p28[k]:.3f}")

    width = max(len(r[0]) for r in results)
    print("\n==== M3 acceptance (prototype pass 5) ====")
    for name, ok, detail in results:
        tag = "INFO" if ok is None else ("PASS" if ok else "FAIL")
        print(f"{tag}  {name:<{width}}  {detail}")

    # ---- 기준 토양 점검 ----
    print("\n==== S5 E1 detail ====")
    print("\n".join(print_s5))

    print("\n==== Baseline sanity (s5_standard, t=0) ====")
    r0 = d["s5_standard"].row(0)
    m2 = {"nh4": 0.1, "k": 0.03, "ca": 0.01, "mg": 0.02}
    frac = {
        "nh4": r0.nh4_solution_N_g_per_m2 / r0.nh4_N_g_per_m2,
        "k": r0.k_solution_g_per_m2 / r0.k_g_per_m2,
        "ca": r0.ca_solution_g_per_m2 / r0.ca_g_per_m2,
        "mg": r0.mg_solution_g_per_m2 / r0.mg_g_per_m2,
    }
    for k, v in frac.items():
        print(f"  solution fraction {k:>3}: {v:.5f}  (M2 {m2[k]}, rel {v / m2[k] - 1:+.1%})")
    print(f"  Mn solution fraction: {r0.mn_solution_g_per_m2 / r0.mn_g_per_m2:.5f}  "
          f"(w_Mn {r0.mn_retention_factor:.1f}, aeration {r0.aeration_index:.3f}, pH {r0.ph})")
    print(f"  phi = {r0.exchange_free_site_index:.5f}, CEC = {r0.cec_eq_per_m2:.3f} eq/m2, EC = {r0.ec_dS_per_m:.3f} dS/m")

    print("\n==== Uptake capacity surplus, daytime (solar > 0), per tile ====")
    for scen in ("s5_standard", "s5_k_excess", "s11_neutral"):
        df = d[scen]
        n_cells = cells_of(args.scenario_root / "m3" / f"{scen}.toml")
        for el in ("K", "Ca", "Mg", "Mn"):
            actual = np.concatenate([[np.nan], np.diff(df[f"ledger_{el}_uptake_g"])]) / n_cells  # 이번 틱 흡수 (행 t = 틱 t-1 처리 후)
            cap = df[f"diag_capacity_{el}_g_per_m2"]
            dem = df[f"diag_demand_{el}_g_per_m2"]
            solar_prev = np.concatenate([[np.nan], df.solar_radiation_W_per_m2[:-1]])
            day = (solar_prev > 0) & (actual > 1e-12)
            if day.sum() == 0:
                print(f"  {scen} {el}: no daytime uptake")
                continue
            surplus = cap[day] / actual[day]
            limited = float(np.mean(cap[day] < dem[day]))
            print(f"  {scen:12s} {el:>2}: capacity/actual median={np.median(surplus):8.2f} "
                  f"p10={np.quantile(surplus, 0.1):8.2f} p90={np.quantile(surplus, 0.9):8.2f}  "
                  f"capacity-limited hours={limited:.1%}  day hours={int(day.sum())}")
        print(f"  {scen:12s} antagonism index-> Ca factor mean={np.nanmean(df.antagonism_Ca_factor):.3f} "
              f"min={np.nanmin(df.antagonism_Ca_factor):.3f}; Mg factor mean={np.nanmean(df.antagonism_Mg_factor):.3f}")

    print("\n==== M1/M2 reference scenarios (final biomass, Mn) ====")
    for name in ("s4_optimal", "s4_alkaline", "s4_acid", "s8_proper", "s8_waterlogged", "s10_standard", "s10_excess",
                 "s10_dry", "s9_lime", "s9_none"):
        f = d[name]
        print(f"  {name:15s} biomass={f.leaf_g_per_m2[-1] + f.root_g_per_m2[-1]:7.2f} Mn ratio={f.plant_Mn_ratio[-1]:6.2f} "
              f"Mn tox h={f.stress_hours_Mn_toxicity[-1]:5.0f} Mn def h={f.stress_hours_Mn_deficiency[-1]:5.0f} "
              f"Ca suff={f.sufficiency_Ca[-1]:.3f} Mg suff={f.sufficiency_Mg[-1]:.3f} max EC={np.nanmax(f.ec_dS_per_m):.2f}")

    print("\n==== E2 side effects (Ca/Mg) ====")
    for name in ("s6_loam", "s6_sand", "s10_standard", "s10_excess", "s10_dry", "s4_optimal", "s8_proper",
                 "s8_waterlogged", "s9_lime", "s11_neutral"):
        f = d[name]
        bio = f.leaf_g_per_m2 + f.root_g_per_m2
        print(f"  {name:14s} Ca suff final={f.sufficiency_Ca[-1]:.3f} min={np.nanmin(f.sufficiency_Ca):.3f} "
              f"Mg suff final={f.sufficiency_Mg[-1]:.3f} min={np.nanmin(f.sufficiency_Mg):.3f} "
              f"stress h Ca={f.stress_hours_Ca[-1]:.0f} Mg={f.stress_hours_Mg[-1]:.0f} "
              f"a_Ca mean={np.nanmean(f.antagonism_Ca_factor):.3f} min={np.nanmin(f.antagonism_Ca_factor):.3f} "
              f"a_Mg mean={np.nanmean(f.antagonism_Mg_factor):.3f} min={np.nanmin(f.antagonism_Mg_factor):.3f} "
              f"biomass={bio[-1]:.2f} Mn2+ {f.mn_g_per_m2[0]:.2f}->{f.mn_g_per_m2[-1]:.2f} Mn ratio={f.plant_Mn_ratio[-1]:.2f}")

    print("\n==== S6 Ca/Mg uptake detail ====")
    for name in ("s6_sand", "s6_loam"):
        f = d[name]
        dca = np.diff(f.ledger_Ca_uptake_g)
        growing = (f.crop_alive_fraction[1:] > 0) & (f.last_growth_g_per_m2[1:] > 0)
        longest = cur = 0
        for zero, grow in zip(dca <= 0.0, growing):
            if grow:
                cur = cur + 1 if zero else 0
                longest = max(longest, cur)
        zero_growing = int(np.sum((dca <= 0.0) & growing))
        bio = f.leaf_g_per_m2 + f.root_g_per_m2
        ca_ratio = f.plant_Ca_g_per_m2 / (0.012 * bio)
        first_floor = int(np.argmax(ca_ratio <= 0.781)) if np.any(ca_ratio <= 0.781) else -1
        print(f"  {name}: Ca uptake total={f.ledger_Ca_uptake_g[-1]:.3f} g, Mg uptake total={f.ledger_Mg_uptake_g[-1]:.3f} g "
              f"(per tile Ca={f.ledger_Ca_uptake_g[-1] / 80:.4f}, Mg={f.ledger_Mg_uptake_g[-1] / 80:.4f}); "
              f"growing hours={int(np.sum(growing))}, zero-Ca-uptake among them={zero_growing}; "
              f"longest zero-Ca-uptake run while growing={longest} h; Ca ratio reaches floor cap 0.78 at tick {first_floor}")

    print("\n==== Mn redox drift (s11_neutral 21 d, s12_neutral 28 d no crop) ====")
    for name in ("s11_neutral", "s12_neutral", "s5_standard", "s11_acid", "s11_wet", "s11_alkaline"):
        f = d[name]
        print(f"  {name:13s} Mn2+ {f.mn_g_per_m2[0]:.3f} -> {f.mn_g_per_m2[-1]:.3f}  oxide {f.mn_oxide_g_per_m2[0]:.2f} -> "
              f"{f.mn_oxide_g_per_m2[-1]:.2f}  first-tick ox={f.mn_oxidized_this_tick_g_per_m2[1]:.5f} "
              f"red={f.mn_reduced_this_tick_g_per_m2[1]:.5f}  mean sol Mn={np.mean(f.mn_solution_g_per_m2):.4f} "
              f"pH {f.ph[0]:.2f}->{f.ph[-1]:.2f}")

    n_fail = sum(1 for _, ok, _ in results if ok is False)
    n_pass = sum(1 for _, ok, _ in results if ok is True)
    print(f"\n{n_pass} PASS, {n_fail} FAIL (INFO lines excluded)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
