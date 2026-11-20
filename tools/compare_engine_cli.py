"""엔진(헤드리스 Godot) 대 sim-cli 틱별 상태 해시 비교 (m6-overview 10.1 E2, 완료 기준 "CLI 결과와 동일").

두 가지로 써요.

1) 이미 있는 CSV 두 개 비교
    python tools/compare_engine_cli.py --cli-csv runs/m6/cli.csv --engine-csv runs/m6/engine.csv

2) E2 목록 전체(또는 일부)를 직접 돌려 비교하고 결과표를 찍어요 (ctest `godot` 프리셋이 이걸 불러요)
    python tools/compare_engine_cli.py --suite --cli build/godot/sim-cli/Release/sim-cli.exe --out runs/m6/e2
        [--godot <실행 파일>] [--only empty season_standard]

   sim-cli는 저장소 data/의 원본을, 엔진은 빌드가 복사한 game/data/(res://data/)를 읽어요(실제 게임과 같은 경로).
   Godot 실행 파일은 --godot, 없으면 환경 변수 GODOT_BIN, 둘 다 없으면 건너뜀(종료 코드 77).

모든 틱이 같으면 종료 코드 0, 하나라도 다르면 처음 달라진 틱을 찍고 1.
"""

import argparse
import csv
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_engine_check import SKIP, find_godot, run_godot_script  # noqa: E402

REPO = Path(__file__).resolve().parent.parent


@dataclass(frozen=True)
class Case:
    name: str
    scenario: str  # data/scenarios/ 기준 상대 경로
    ticks: int
    why: str


# E2 대상 (m6-overview 10.1). --remove-on-onset을 쓰는 m5b/s28_overhead_remove는 CLI 전용 동작이라 빼요.
SEASON_TICKS = 6312  # 1작기 263일 (tools/season_regression.py와 같아요)
E2_CASES = [
    Case("empty", "empty.toml", 8760, "M0 1년"),
    *[Case(s, f"m4/{s}.toml", SEASON_TICKS, "M4 1작기")
      for s in ("season_standard", "season_n_excess", "season_k_low", "season_k_excess", "season_no_topdress",
                "season_acid", "season_thinned")],
    Case("s21_dazomet_day3", "m5a/s21_dazomet_day3.toml", 77 * 24, "M5a 소독 + 정식"),
    Case("s27_closed_fan", "m5b/s27_closed_fan.toml", 3456, "M5b 팬 + 천적"),
    Case("all_commands", "m6/all_commands.toml", 720, "명령 17종 (피복·점적·포기 제거 포함)"),
]


def read_hashes(path: Path) -> list[str]:
    with path.open(newline="", encoding="utf-8") as f:
        return [row["state_hash"] for row in csv.DictReader(f)]


def compare(cli: list[str], eng: list[str]) -> tuple[bool, str]:
    n = min(len(cli), len(eng))
    for t in range(n):
        if cli[t] != eng[t]:
            return False, f"MISMATCH first_tick={t} cli={cli[t]} engine={eng[t]} compared={n}"
    if len(cli) != len(eng):
        return False, f"LENGTH cli_rows={len(cli)} engine_rows={len(eng)} (first {n} rows match)"
    if n == 0:
        return False, "no rows"
    return True, f"MATCH rows={n} (ticks 0..{n - 1}) final={cli[-1]}"


def run_case(case: Case, godot: str, cli: Path, params: Path, out: Path) -> tuple[bool, str]:
    out.mkdir(parents=True, exist_ok=True)
    cli_csv = out / f"{case.name}_cli.csv"
    eng_csv = out / f"{case.name}_engine.csv"
    proc = subprocess.run([str(cli), "--params", str(params), "--scenario", str(REPO / "data/scenarios" / case.scenario),
                           "--ticks", str(case.ticks), "--out", str(cli_csv)],
                          capture_output=True, text=True, encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        return False, f"sim-cli exit {proc.returncode}: {proc.stderr.strip()}"
    ok, output = run_godot_script(godot, "res://tests/hash_trace.gd",
                                  [f"scenario=res://data/scenarios/{case.scenario}", f"ticks={case.ticks}",
                                   f"csv={eng_csv}"], timeout_s=3600.0)
    if not ok:
        return False, "engine run failed:\n" + output
    return compare(read_hashes(cli_csv), read_hashes(eng_csv))


def run_suite(args: argparse.Namespace) -> int:
    godot = find_godot(args.godot)
    if godot is None:
        print("SKIPPED: set GODOT_EXECUTABLE (CMake cache) or GODOT_BIN (environment) to run E2")
        return SKIP
    cases = [c for c in E2_CASES if not args.only or c.name in args.only]
    unknown = set(args.only or []) - {c.name for c in E2_CASES}
    if unknown or not cases:
        print(f"unknown case(s): {sorted(unknown)}; known: {[c.name for c in E2_CASES]}")
        return 2
    rows = []
    all_ok = True
    for case in cases:
        started = time.monotonic()
        ok, detail = run_case(case, godot, args.cli, args.params, args.out)
        all_ok = all_ok and ok
        rows.append((case, ok, detail, time.monotonic() - started))
        print(f"{case.name}: {detail.splitlines()[0] if detail else ''}", flush=True)
        if not ok:
            print(detail)
    print()
    print("| scenario | ticks | match | note |")
    print("| --- | --- | --- | --- |")
    for case, ok, detail, _secs in rows:
        verdict = "all ticks match" if ok else detail.splitlines()[0]
        print(f"| {case.scenario} | {case.ticks} | {verdict} | {case.why} |")
    print()
    print("E2 PASS" if all_ok else "E2 FAIL")
    return 0 if all_ok else 1


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--cli-csv", type=Path)
    p.add_argument("--engine-csv", type=Path)
    p.add_argument("--suite", action="store_true", help="E2 목록을 직접 돌려요")
    p.add_argument("--cli", type=Path, help="sim-cli 실행 파일 (--suite). 엔진과 같은 트리·같은 구성의 것")
    p.add_argument("--params", type=Path, default=REPO / "data/params.toml")
    p.add_argument("--out", type=Path, default=REPO / "runs/m6/e2")
    p.add_argument("--godot", default=None)
    p.add_argument("--only", nargs="*", help="E2 목록 중 이 이름들만")
    args = p.parse_args()
    if args.suite:
        if args.cli is None or not args.cli.exists():
            print(f"sim-cli not found: {args.cli} (use --cli)")
            return 2
        return run_suite(args)
    if args.cli_csv is None or args.engine_csv is None:
        p.error("--cli-csv and --engine-csv are required without --suite")
    ok, detail = compare(read_hashes(args.cli_csv), read_hashes(args.engine_csv))
    print(detail)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
