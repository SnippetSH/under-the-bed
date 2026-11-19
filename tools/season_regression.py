"""M4 작기 회귀: data/scenarios/m4/season_* 7개를 병렬로 돌려 요약하고 기준선(baseline)과 비교해요.

기준선: tests/baselines/m4_season_summary.json (--write-baseline으로 다시 만들어요).
허용 오차 기본값: 총 수확·평균 당도·평균 경도 3%(상대), 월별 수확 5%(기준선 50 g/m2 미만인 달은 건너뜀),
첫 수확 +-3일. 하나라도 FAIL이면 종료 코드 1.
CSV는 runs/regression/<label>/에 써요 (runs/는 커밋하지 않아요).

사용법:
    python tools/season_regression.py
    python tools/season_regression.py --params runs/proto/params.toml --label proto1
    python tools/season_regression.py --scenarios season_standard season_thinned --tol-total 0.05
    python tools/season_regression.py --write-baseline
    python tools/season_regression.py --cli build/vs2022/sim-cli/Debug/sim-cli.exe
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from season_summary import summarize  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
SEASON_TICKS = 263 * 24
ALL_SCENARIOS = ["season_standard", "season_no_topdress", "season_n_excess", "season_k_low",
                 "season_k_excess", "season_acid", "season_thinned"]
BASELINE = REPO / "tests/baselines/m4_season_summary.json"


def run_scenario(cli: Path, params: Path, scenario_root: Path, name: str, out_dir: Path) -> tuple[str, Path | None, str]:
    out = out_dir / f"{name}.csv"
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [str(cli), "--params", str(params), "--scenario", str(scenario_root / f"{name}.toml"),
           "--ticks", str(SEASON_TICKS), "--out", str(out), "--no-hash"]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        return name, None, r.stderr.strip()
    return name, out, ""


def flat_metrics(s: dict) -> dict[str, float]:
    """비교 대상 지표를 평평한 dict로 (키 순서 = 표 순서)."""
    m = {"total_harvest": s["total_harvest_g_per_m2"], "mean_brix": s["mean_brix"],
         "mean_firmness": s["mean_firmness"]}
    for month, v in s["monthly_harvest_g_per_m2"].items():
        m[f"month_{month}"] = v
    if s["first_harvest_doy"] is not None:
        m["first_harvest_doy"] = float(s["first_harvest_doy"])
    return m


def compare(base: dict, now: dict, tol: argparse.Namespace) -> list[tuple[str, float, float, float | None, str]]:
    """(지표, 기준선, 현재, Δ% 또는 None, PASS/FAIL/SKIP) 목록."""
    b, n = flat_metrics(base), flat_metrics(now)
    rows = []
    for key, bv in b.items():
        nv = n.get(key)
        if nv is None:
            rows.append((key, bv, float("nan"), None, "FAIL"))
            continue
        if key == "first_harvest_doy":
            rows.append((key, bv, nv, None, "PASS" if abs(nv - bv) <= tol.tol_first_days else "FAIL"))
            continue
        if key.startswith("month_"):
            if bv < tol.month_min_g:
                continue  # 기준선이 너무 작은 달은 건너뜀
            limit = tol.tol_month
        elif key == "total_harvest":
            limit = tol.tol_total
        else:
            limit = tol.tol_quality
        delta = (nv - bv) / bv if bv != 0 else (0.0 if nv == 0 else float("inf"))
        rows.append((key, bv, nv, delta * 100, "PASS" if abs(delta) <= limit else "FAIL"))
    return rows


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--cli", type=Path, default=REPO / "build/vs2022/sim-cli/Release/sim-cli.exe")
    p.add_argument("--params", type=Path, default=REPO / "data/params.toml")
    p.add_argument("--scenario-root", type=Path, default=REPO / "data/scenarios/m4")
    p.add_argument("--scenarios", nargs="+", default=ALL_SCENARIOS, help="이름만 (예: season_standard)")
    p.add_argument("--label", default="latest", help="CSV 폴더 이름: runs/regression/<label>/")
    p.add_argument("--baseline", type=Path, default=BASELINE)
    p.add_argument("--write-baseline", action="store_true", help="기준선을 현재 결과로 다시 써요 (비교 안 함)")
    p.add_argument("--jobs", type=int, default=7)
    p.add_argument("--tol-total", type=float, default=0.03, help="총 수확 상대 허용 오차")
    p.add_argument("--tol-quality", type=float, default=0.03, help="평균 당도·경도 상대 허용 오차")
    p.add_argument("--tol-month", type=float, default=0.05, help="월별 수확 상대 허용 오차")
    p.add_argument("--month-min-g", type=float, default=50.0, help="기준선이 이보다 작은 달은 건너뜀")
    p.add_argument("--tol-first-days", type=float, default=3.0, help="첫 수확 허용 일수")
    p.add_argument("--only-failures", action="store_true", help="FAIL 행만 출력")
    args = p.parse_args()

    if not args.cli.exists():
        print(f"sim-cli not found: {args.cli} (use --cli)")
        return 2
    t0 = time.time()
    out_dir = REPO / "runs/regression" / args.label
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as ex:
        results = list(ex.map(lambda n: run_scenario(args.cli, args.params, args.scenario_root, n, out_dir),
                              args.scenarios))
    failed_runs = [(n, err) for n, path, err in results if path is None]
    for n, err in failed_runs:
        print(f"RUN FAILED {n}: {err}")
    summaries = {n: summarize(path) for n, path, _ in results if path is not None}
    print(f"ran {len(summaries)}/{len(args.scenarios)} scenarios in {time.time() - t0:.1f} s -> {out_dir}")

    if args.write_baseline:
        if failed_runs:
            return 1
        data = {}
        if args.baseline.exists() and set(args.scenarios) != set(ALL_SCENARIOS):
            data = json.loads(args.baseline.read_text(encoding="utf-8"))  # 일부만 다시 쓸 때 나머지 유지
        data.update(summaries)
        args.baseline.parent.mkdir(parents=True, exist_ok=True)
        args.baseline.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"wrote baseline {args.baseline}")
        return 0

    if not args.baseline.exists():
        print(f"baseline not found: {args.baseline} (use --write-baseline)")
        return 2
    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    n_fail = len(failed_runs)
    print("\n| scenario | metric | baseline | now | delta % | result |\n| --- | --- | ---: | ---: | ---: | --- |")
    for name, now in summaries.items():
        if name not in baseline:
            print(f"| {name} | (no baseline) | | | | FAIL |")
            n_fail += 1
            continue
        for key, bv, nv, d, res in compare(baseline[name], now, args):
            if res == "FAIL":
                n_fail += 1
            if args.only_failures and res != "FAIL":
                continue
            ds = "-" if d is None else f"{d:+.2f}"
            print(f"| {name} | {key} | {bv:.4g} | {nv:.4g} | {ds} | {res} |")
    print(f"\n{'FAIL' if n_fail else 'PASS'}: {n_fail} failing check(s)")
    return 1 if n_fail else 0


if __name__ == "__main__":
    sys.exit(main())
