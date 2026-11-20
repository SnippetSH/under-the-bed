"""E8 (m6-overview 10.1, 위험 R4): 게임 화면 코드가 진짜 값 접근자(get_true_*)를 부르지 않는지 글자로 검사해요.

    python tools/check_true_access.py
    python tools/check_true_access.py --scene res://scenes/char_play.tscn   (C7, m6b-character-play 9.1)

game/scripts/·game/scenes/ 아래 *.gd·*.tscn에서 `get_true_<이름>(` 호출을 찾아요(# 뒤 주석은 빼요).
개발자 패널 파일·폴더(ALLOWED_PATHS)는 예외예요. 검사 스크립트(game/tests/)는 대상이 아니에요.
GDScript 파일 배치가 바뀌면 ALLOWED_PATHS만 고치면 돼요.
0건이면 종료 코드 0, 있으면 위치를 찍고 1.

--scene (C7: E8을 캐릭터 화면에도): 그 장면에서 시작해 장면의 스크립트와 preload/load("res://…")로 이어지는 파일을
전부 따라가(어느 폴더에 있든) 같은 검사를 하고, 그 파일들에서는 호출 꼴이 아니어도 `get_true_` 글자가 코드에 있으면
(예: call("get_true_cell"), Callable(w, "get_true_crop")) 실패로 봐요. 따라간 파일 목록을 찍고, 장면 스크립트를 못 찾거나
따라간 파일이 REQUIRED_SCENE_FILES보다 적으면 실패해요(검사 범위가 몰래 줄지 않게).
"""

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
GAME = REPO / "game"
# --scene 검사에서 꼭 따라가야 하는 캐릭터 화면 파일 (범위가 줄면 실패)
REQUIRED_SCENE_FILES = {
    "res://scenes/char_play.tscn": (
        "game/scripts/char_play.gd", "game/scripts/char_session.gd", "game/scripts/house_view_3d.gd",
        "game/scripts/house_cover.gd", "game/scripts/house_layout.gd", "game/scripts/cell_info.gd",
        "game/scripts/char_tools.gd", "game/scripts/reject_text.gd", "game/scripts/sim_runner.gd",
        "game/scripts/save_store.gd", "game/scripts/plant_art.gd", "game/scripts/art_library.gd",
    ),
}
RES_REF = re.compile(r"""(?:preload|load)\s*\(\s*["'](res://[^"']+)["']|path\s*=\s*"(res://[^"]+\.gd)\"""")
ANY_TRUE = re.compile(r"get_true_\w+")

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


def res_to_path(res: str) -> Path:
    return GAME / res[len("res://"):]


def scene_closure(scene_res: str) -> list[Path]:
    """장면에서 시작해 스크립트·preload·load로 이어지는 .gd·.tscn 파일 (주석 줄의 경로는 빼요)."""
    seen: dict[Path, None] = {}
    todo = [res_to_path(scene_res)]
    while todo:
        p = todo.pop()
        if p in seen or not p.is_file():
            continue
        seen[p] = None
        for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
            code = strip_comment(line) if p.suffix == ".gd" else line
            for m in RES_REF.finditer(code):
                ref = m.group(1) or m.group(2)
                if ref.endswith((".gd", ".tscn")):
                    todo.append(res_to_path(ref))
    return list(seen)


def check_scene(scene_res: str) -> int:
    files = scene_closure(scene_res)
    rels = sorted(p.relative_to(REPO).as_posix() for p in files)
    hits = []
    for p in files:
        rel = p.relative_to(REPO).as_posix()
        if allowed(rel):
            continue
        for no, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if ANY_TRUE.search(strip_comment(line) if p.suffix == ".gd" else line):
                hits.append(f"{rel}:{no}: {line.strip()}")
    missing = [f for f in REQUIRED_SCENE_FILES.get(scene_res, ()) if f not in rels]
    for r in rels:
        print(f"C7 followed {r}{'  (allowed: dev panel)' if allowed(r) else ''}")
    for h in hits:
        print(h)
    for m in missing:
        print(f"C7 scope shrank: {m} is no longer reached from {scene_res}")
    print(f"C7 scene={scene_res} files={len(rels)} get_true_ uses outside {list(ALLOWED_PATHS)}: {len(hits)}, missing required: {len(missing)}")
    return 1 if hits or missing or not rels else 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--scene", default=None, help="res://scenes/<장면>.tscn (C7: 그 장면에서 이어지는 파일 전부)")
    args = ap.parse_args()
    if args.scene:
        return check_scene(args.scene)
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
