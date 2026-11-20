"""헤드리스 Godot 엔진 검사 하나를 돌려요 (m6-overview 10.1 E1·E3~E7·E9·E10, ctest `godot` 프리셋).

    python tools/run_engine_check.py --script res://tests/load_check.gd [--godot <실행 파일>] -- cli=<sim-cli> out=<폴더>

- Godot 실행 파일: --godot, 없으면 환경 변수 GODOT_BIN. 둘 다 없으면 건너뜀(종료 코드 77 = ctest SKIP_RETURN_CODE).
  저장소에는 경로를 적지 않아요(CLAUDE.md "현재 마일스톤").
- 통과 조건: 종료 코드 0, 출력에 "RESULT PASS", 그리고 엔진의 오류·누수 줄이 없음
  ("SCRIPT ERROR", "ERROR:", "leaked at exit", "resources still in use").
- `--` 뒤의 key=value 인자는 검사 스크립트에 그대로 넘겨요.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

SKIP = 77
REPO = Path(__file__).resolve().parent.parent
GAME_DIR = REPO / "game"
# 엔진이 찍으면 실패로 보는 줄 (검사 스크립트 쪽 실수·누수)
BAD_MARKERS = ("SCRIPT ERROR", "ERROR:", "leaked at exit", "resources still in use", "Parse Error")


def find_godot(explicit: str | None) -> str | None:
    for candidate in (explicit, os.environ.get("GODOT_BIN")):
        if candidate:
            return candidate
    return None


def run_godot_script(godot: str, script: str, script_args: list[str], timeout_s: float) -> tuple[bool, str]:
    """검사 스크립트를 헤드리스로 돌려 (통과 여부, 출력 전체)를 돌려줘요."""
    cmd = [godot, "--headless", "--path", str(GAME_DIR), "--script", script, "--", *script_args]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                              timeout=timeout_s)
    except subprocess.TimeoutExpired as e:
        return False, f"timeout after {timeout_s} s\n{e.stdout or ''}{e.stderr or ''}"
    output = proc.stdout + proc.stderr
    problems = []
    if proc.returncode != 0:
        problems.append(f"exit code {proc.returncode}")
    if "RESULT PASS" not in proc.stdout:
        problems.append("no 'RESULT PASS' line")
    for line in output.splitlines():
        if any(marker in line for marker in BAD_MARKERS):
            problems.append(f"engine reported: {line.strip()}")
    if problems:
        output += "\n" + "\n".join(f"run_engine_check: {p}" for p in problems) + "\n"
    return not problems, output


def main() -> int:
    argv = sys.argv[1:]
    script_args: list[str] = []
    if "--" in argv:
        i = argv.index("--")
        argv, script_args = argv[:i], argv[i + 1:]
    p = argparse.ArgumentParser()
    p.add_argument("--script", required=True, help="res://tests/<검사>.gd")
    p.add_argument("--godot", default=None, help="Godot 실행 파일 (없으면 GODOT_BIN)")
    p.add_argument("--timeout", type=float, default=1800.0)
    args = p.parse_args(argv)
    godot = find_godot(args.godot)
    if godot is None:
        print("SKIPPED: set GODOT_EXECUTABLE (CMake cache) or GODOT_BIN (environment) to run engine checks")
        return SKIP
    if not (GAME_DIR / "bin" / "sim-godot.dll").exists() and sys.platform == "win32":
        print("game/bin/sim-godot.dll is missing: build the sim-godot target first (godot preset)")
        return 1
    for a in script_args:
        if a.startswith("out="):
            Path(a[len("out="):]).mkdir(parents=True, exist_ok=True)
    ok, output = run_godot_script(godot, args.script, script_args, args.timeout)
    sys.stdout.write(output)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
