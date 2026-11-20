"""엔진(헤드리스 Godot) 대 sim-cli 틱별 상태 해시 비교 (m6-overview 10장 E2, 프로토타입 P1 축소판).

    python tools/compare_engine_cli.py --cli-csv runs/m6/cli.csv --engine-csv runs/m6/engine.csv

cli-csv: sim-cli --out 결과 (state_hash 열). engine-csv: game/tests/p1_hashes.gd 결과 (tick,state_hash).
모든 틱이 같으면 종료 코드 0, 처음 달라진 틱을 찍고 1.
"""

import argparse
import csv
import sys
from pathlib import Path


def read_hashes(path: Path) -> list[str]:
    with path.open(newline="", encoding="utf-8") as f:
        return [row["state_hash"] for row in csv.DictReader(f)]


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--cli-csv", type=Path, required=True)
    p.add_argument("--engine-csv", type=Path, required=True)
    args = p.parse_args()
    cli = read_hashes(args.cli_csv)
    eng = read_hashes(args.engine_csv)
    n = min(len(cli), len(eng))
    for t in range(n):
        if cli[t] != eng[t]:
            print(f"MISMATCH first_tick={t} cli={cli[t]} engine={eng[t]} compared={n}")
            return 1
    if len(cli) != len(eng):
        print(f"LENGTH cli_rows={len(cli)} engine_rows={len(eng)} (first {n} rows match)")
        return 1
    print(f"MATCH rows={n} (ticks 0..{n - 1}) final={cli[-1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
