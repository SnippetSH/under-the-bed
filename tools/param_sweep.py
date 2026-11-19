"""params.toml의 키 값을 바꿔 가며 시나리오를 병렬로 돌리고 요약 표를 만들어요 (보정 왕복 줄이기).

값 지정: --set 섹션.키=값1;값2;...   (반복 가능, 여러 개면 곱집합)
  - 값 구분자는 세미콜론(;)이에요. 배열·곡선 안의 쉼표와 안 부딪혀요. 값은 TOML 리터럴 그대로예요.
  - 예: --set crop.light_use_efficiency_g_per_MJ=2.0;2.25
        --set "phenology.induction_nitrogen_curve=[[0,1],[1.1,1],[1.3,0.6]];[[0,1],[1.0,1],[1.3,0.25]]"
  - 문자열 값은 따옴표까지 값에 포함해요: --set 'section.key="abc";"def"'
  - 배열 테이블은 `이름[필드=값].키`: --set "fertilizer[id=urea].no3_N_g_per_kg=0;10"
    (`[[이름]]` 블록 중 `필드 = "값"`이 일치하는 것. 한 줄 값과 여러 줄 배열 값은 지원, 중첩 테이블은 미지원)
  - 같은 키를 두 번 --set 하면 오류예요. 줄 끝 주석은 유지돼요.
조합 수 상한 32 (--max-combos로 올려요). 값은 loader가 엄격히 검증하니 TOML이 유효해야 해요.

출력: runs/sweep/<이름>/<c01_...>/params.toml, <시나리오>.csv, 그리고 runs/sweep/<이름>/summary.csv + 마크다운 표.
기본은 M4 작기 요약 지표(총 수확, 당도, 경도, 첫 수확, 월별 Dec..May, N 용탈·기체).
작기가 아닌 시나리오는 --metric 열:집계(집계 = max/min/mean/final)를 쓰고 --ticks 또는 --days로
길이를 정해요. --metric을 주면 작기 지표는 --season-metrics를 같이 줘야 나와요.

사용법:
    python tools/param_sweep.py --name lue --set crop.light_use_efficiency_g_per_MJ=2.0;2.25
    python tools/param_sweep.py --name n --scenarios m4/season_standard m4/season_n_excess --set crop.x=1;2 --jobs 4
    python tools/param_sweep.py --name s9 --scenarios m2/s9_lime --days 14 --set soil_chemistry.foo=1;2 --metric ph:final --metric ec_dS_per_m:max
"""

from __future__ import annotations

import argparse
import csv
import itertools
import re
import subprocess
import sys
import time
import tomllib
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from plot_csv import read_csv  # noqa: E402
from season_summary import summarize  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
HEADER_RE = re.compile(r"^\s*(\[\[?)\s*([A-Za-z0-9_.\-]+)\s*(\]\]?)\s*(#.*)?$")
KEY_RE = re.compile(r"^(\s*)([A-Za-z0-9_\-]+)(\s*=\s*)")
ARRAY_SEL_RE = re.compile(r"^([A-Za-z0-9_\-]+)\[([A-Za-z0-9_\-]+)=([^\]]+)\]\.([A-Za-z0-9_\-]+)$")


def value_end(text: str, start: int) -> int:
    """text[start:]에서 값(문자열·배열 괄호 균형 고려)이 끝나는 위치. 그 뒤는 주석/공백."""
    depth = 0
    i = start
    in_str: str | None = None
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\" and in_str == '"':
                i += 2
                continue
            if c == in_str:
                in_str = None
        elif c in "\"'":
            in_str = c
        elif c in "[{":
            depth += 1
        elif c in "]}":
            depth -= 1
        elif depth == 0 and (c == "#" or c == "\n"):
            break
        i += 1
    return i


def set_key(text: str, target: str, value: str) -> str:
    """params 텍스트에서 target(`섹션.키` 또는 `이름[필드=값].키`)의 값을 value로 바꿔요."""
    lines = text.splitlines(keepends=True)
    m = ARRAY_SEL_RE.match(target)
    if m:
        table, field, want, key = m.groups()
        want = want.strip().strip("\"'")
        is_array = True
    else:
        if "." not in target:
            raise SystemExit(f"bad key '{target}': use section.key")
        table, key = target.rsplit(".", 1)
        is_array = False
    # 대상 블록들의 줄 범위
    blocks: list[tuple[int, int]] = []
    start = None
    for idx, line in enumerate(lines):
        h = HEADER_RE.match(line)
        if h:
            if start is not None:
                blocks.append((start, idx))
                start = None
            if h.group(2) == table and (h.group(1) == "[[") == is_array:
                start = idx + 1
    if start is not None:
        blocks.append((start, len(lines)))
    if not blocks:
        raise SystemExit(f"section {table} ({'[[array table]]' if is_array else '[table]'}) not found in params")
    if is_array:
        sel = re.compile(rf"""^\s*{re.escape(field)}\s*=\s*["']?{re.escape(want)}["']?\s*(#.*)?$""")
        blocks = [b for b in blocks if any(sel.match(ln) for ln in lines[b[0]:b[1]])]
        if not blocks:
            raise SystemExit(f"no [[{table}]] entry with {field} = {want}")
    for b0, b1 in blocks:
        for idx in range(b0, b1):
            k = KEY_RE.match(lines[idx])
            if k and k.group(2) == key:
                # 여러 줄 값을 위해 이 줄부터 블록 끝까지 이어 붙여 값 끝을 찾아요
                tail = "".join(lines[idx:b1])
                ve = value_end(tail, k.end())
                n_lines = tail[:ve].count("\n")
                rest_line = tail[ve:].split("\n", 1)[0]  # 값 뒤 같은 줄의 나머지(주석)
                eol = "\n" if lines[idx + n_lines].endswith("\n") else ""
                sep = "  " if rest_line.strip() else ""
                lines[idx + n_lines] = k.group(1) + key + k.group(3) + value + sep + rest_line.strip() + eol
                for j in range(idx, idx + n_lines):  # 여러 줄 값의 앞쪽 줄은 비워요
                    lines[j] = ""
                return "".join(lines)
    raise SystemExit(f"key '{key}' not found in '{table}'")


def parse_set(spec: str) -> tuple[str, list[str]]:
    # 배열 선택자 [id=urea]의 '='는 건너뛰고 첫 번째 바깥 '='에서 나눠요
    depth, cut = 0, -1
    for i, c in enumerate(spec):
        if c == "[":
            depth += 1
        elif c == "]":
            depth -= 1
        elif c == "=" and depth == 0:
            cut = i
            break
    if cut < 0:
        raise SystemExit(f"bad --set '{spec}': use key=v1;v2")
    key, raw = spec[:cut].strip(), spec[cut + 1:]
    values = [v.strip() for v in raw.split(";") if v.strip()]
    if not values:
        raise SystemExit(f"--set '{spec}' has no values")
    for v in values:
        try:
            tomllib.loads(f"x = {v}")
        except tomllib.TOMLDecodeError as e:
            raise SystemExit(f"invalid TOML value '{v}' for {key}: {e}")
    return key, values


def short(v: str) -> str:
    return re.sub(r"[^A-Za-z0-9.\-]+", "_", v).strip("_")[:24]


def resolve_scenario(name: str) -> Path:
    for cand in (Path(name), REPO / name, REPO / "data/scenarios" / f"{name}.toml", REPO / "data/scenarios" / name):
        if cand.is_file():
            return cand.resolve()
    raise SystemExit(f"scenario not found: {name}")


def aggregate(values: list[float], agg: str) -> float:
    return {"max": max, "min": min, "final": lambda v: v[-1], "mean": lambda v: sum(v) / len(v)}[agg](values)


def run_job(job: dict) -> dict:
    cmd = [str(job["cli"]), "--params", str(job["params"]), "--scenario", str(job["scenario"]),
           "--ticks", str(job["ticks"]), "--out", str(job["out"]), "--no-hash"]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    row = {"combo": job["combo"], "scenario": job["scenario_name"], **job["assign"]}
    if r.returncode != 0:
        row["error"] = r.stderr.strip().splitlines()[-1] if r.stderr.strip() else "failed"
        return row
    if job["season"]:
        s = summarize(job["out"])
        row.update({"total_g_per_m2": s["total_harvest_g_per_m2"], "brix": s["mean_brix"],
                    "firmness": s["mean_firmness"], "first_harvest": s["first_harvest_date"] or "-",
                    **{f"m_{k}": v for k, v in s["monthly_harvest_g_per_m2"].items() if k not in ("Sep", "Oct", "Nov")},
                    "N_leached_g_per_m2": s["N_leached_g_per_m2"], "N_gas_g_per_m2": s["N_gas_g_per_m2"]})
    if job["metrics"]:
        data = read_csv(job["out"])
        for col, agg in job["metrics"]:
            if col not in data:
                row[f"{col}:{agg}"] = "no-column"
                continue
            vals = [float(v) for v in data[col] if v != ""]
            row[f"{col}:{agg}"] = aggregate(vals, agg) if vals else "-"
    return row


def fmt(v) -> str:
    if isinstance(v, float):
        return f"{v:.4g}" if abs(v) < 1000 else f"{v:.0f}"
    return str(v)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--name", required=True, help="출력 폴더 이름: runs/sweep/<name>/")
    p.add_argument("--set", dest="sets", action="append", required=True, metavar="KEY=V1;V2")
    p.add_argument("--scenarios", nargs="+", default=["m4/season_standard"])
    p.add_argument("--params", type=Path, default=REPO / "data/params.toml", help="바탕 params 파일")
    p.add_argument("--cli", type=Path, default=REPO / "build/vs2022/sim-cli/Release/sim-cli.exe")
    p.add_argument("--ticks", type=int)
    p.add_argument("--days", type=int, help="--ticks 대신 일수 (기본: 작기 263일)")
    p.add_argument("--metric", action="append", default=[], metavar="COL:AGG", help="max/min/mean/final")
    p.add_argument("--season-metrics", action="store_true", help="--metric과 함께 작기 요약도 낼 때")
    p.add_argument("--jobs", type=int, default=8)
    p.add_argument("--max-combos", type=int, default=32)
    args = p.parse_args()

    if not args.cli.exists():
        print(f"sim-cli not found: {args.cli} (use --cli)")
        return 2
    specs = [parse_set(s) for s in args.sets]
    keys = [k for k, _ in specs]
    if len(set(keys)) != len(keys):
        raise SystemExit("duplicate --set key")
    combos = list(itertools.product(*[v for _, v in specs]))
    if len(combos) > args.max_combos:
        raise SystemExit(f"{len(combos)} combos > --max-combos {args.max_combos}")
    metrics = []
    for m in args.metric:
        col, _, agg = m.partition(":")
        if agg not in ("max", "min", "mean", "final"):
            raise SystemExit(f"bad --metric '{m}': COL:max|min|mean|final")
        metrics.append((col, agg))
    season = args.season_metrics or not metrics
    ticks = args.ticks or (args.days or 263) * 24
    scenarios = [(n, resolve_scenario(n)) for n in args.scenarios]

    base = args.params.read_text(encoding="utf-8")
    out_root = REPO / "runs/sweep" / args.name
    jobs = []
    for ci, combo in enumerate(combos, 1):
        text = base
        for key, val in zip(keys, combo):
            text = set_key(text, key, val)
        tomllib.loads(text)  # 편집 결과가 유효한 TOML인지 확인
        label = f"c{ci:02d}_" + "_".join(f"{k.split('.')[-1][:20]}={short(v)}" for k, v in zip(keys, combo))
        cdir = out_root / label
        cdir.mkdir(parents=True, exist_ok=True)
        (cdir / "params.toml").write_text(text, encoding="utf-8", newline="\n")
        for sname, spath in scenarios:
            jobs.append({"cli": args.cli, "params": cdir / "params.toml", "scenario": spath, "scenario_name": sname,
                         "ticks": ticks, "out": cdir / f"{spath.stem}.csv", "combo": label,
                         "assign": dict(zip(keys, combo)), "season": season, "metrics": metrics})
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as ex:
        rows = list(ex.map(run_job, jobs))
    print(f"{len(jobs)} runs in {time.time() - t0:.1f} s -> {out_root}\n")

    cols: list[str] = []
    for r in rows:
        for k in r:
            if k not in cols:
                cols.append(k)
    with (out_root / "summary.csv").open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        w.writerows(rows)
    print("| " + " | ".join(cols) + " |\n|" + " --- |" * len(cols))
    for r in rows:
        print("| " + " | ".join(fmt(r.get(c, "")) for c in cols) + " |")
    print(f"\nwrote {out_root / 'summary.csv'}")
    return 1 if any("error" in r for r in rows) else 0


if __name__ == "__main__":
    sys.exit(main())
