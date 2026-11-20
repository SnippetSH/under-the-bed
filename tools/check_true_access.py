"""E8 (m6-overview 10.1, 위험 R4): 게임 화면 코드가 진짜 값 접근자(get_true_*)를 부르지 않는지 글자로 검사해요.

    python tools/check_true_access.py

game/scripts/·game/scenes/ 아래 *.gd·*.tscn에서 `get_true_<이름>(` 호출을 찾아요(# 뒤 주석은 빼요).
개발자 패널 파일·폴더(ALLOWED_PATHS)는 예외예요. 검사 스크립트(game/tests/)는 대상이 아니에요.
GDScript 파일 배치가 바뀌면 ALLOWED_PATHS만 고치면 돼요.
0건이면 종료 코드 0, 있으면 위치를 찍고 1.
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# 진짜 값을 불러도 되는 곳 (저장소 기준 경로, "/"로 끝나면 폴더 전체)
ALLOWED_PATHS = (
    "game/scripts/dev_panel.gd",
    "game/scripts/dev/",
)
SCAN_DIRS = ("game/scripts", "game/scenes")
SCAN_SUFFIXES = (".gd", ".tscn")
CALL = re.compile(r"\bget_true_\w+\s*\(")


def allowed(rel: str) -> bool:
    return any(rel == a or (a.endswith("/") and rel.startswith(a)) for a in ALLOWED_PATHS)


def strip_comment(line: str) -> str:
    # GDScript 주석(#)을 빼요. 글자 안의 #은 드물어서 따옴표 안이면 남겨요(간단한 판정).
    out = []
    quote = None
    for ch in line:
        if quote:
            out.append(ch)
            if ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
            out.append(ch)
        elif ch == "#":
            break
        else:
            out.append(ch)
    return "".join(out)


def main() -> int:
    hits = []
    scanned = 0
    for d in SCAN_DIRS:
        root = REPO / d
        if not root.exists():
            continue
        for path in sorted(root.rglob("*")):
            if path.suffix not in SCAN_SUFFIXES or not path.is_file():
                continue
            rel = path.relative_to(REPO).as_posix()
            scanned += 1
            if allowed(rel):
                continue
            for no, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                if CALL.search(strip_comment(line)):
                    hits.append(f"{rel}:{no}: {line.strip()}")
    for h in hits:
        print(h)
    print(f"E8 scanned={scanned} files, get_true_ calls outside {list(ALLOWED_PATHS)}: {len(hits)}")
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main())
