"""프로토타입 점검 3차: data/ 수정 (2차 위에, 멱등). 인자 key=value로 반복 보정값을 바꿔요.

    python tools/proto_data/pass3_data.py [TOP_CN=8] [TOP_K=10] [NO3=4] [LABILE_N=5] [STABLE_N=360]
        [HEATER=8.0] [TRUSS_DD=350.0] [FRUIT_DD=350.0] [FLOWERS=...] [NOV_OPEN=10] [NOV_CLOSE=15]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pass2_data as p2  # noqa: E402

REPO = p2.REPO
TAG = p2.TAG
CN_N_g_per_g = 0.155   # 질산칼슘 N (11 + 144 g/kg, data/params.toml)
UREA_N_g_per_g = 0.46  # 요소 N

cfg = {
    "TOP_CN": "10.0", "TOP_K": "8.0", "NO3": "4.0", "LABILE_N": None, "STABLE_N": None,
    "HEATER": "8.0", "TRUSS_DD": "350.0", "FRUIT_DD": "350.0",
    "FLOWERS": "[[1, 4], [3, 7], [6, 10], [10, 12]]", "NOV_OPEN": "10", "NOV_CLOSE": "15",
    "IRR1": "4.0", "IRR2": "2.5", "IRR3": "4.0", "BASE_UREA_EXCESS": "30.0",
}
for a in sys.argv[1:]:
    k, v = a.split("=", 1)
    cfg[k] = v


def params() -> None:
    t = p2.read(p2.P)
    sets = {
        ("phenology", "flowers_per_truss_curve"): cfg["FLOWERS"],
        ("fruit", "brix_n_excess_curve"): "[[0, 1], [0.3, 1], [0.5, 0.85]]",
        ("phenology", "truss_degree_days_to_anthesis"): cfg["TRUSS_DD"],
        ("fruit", "degree_days_to_harvest"): cfg["FRUIT_DD"],
    }
    for (sec, key), val in sets.items():
        t = p2.set_param(t, sec, key, val)
    # 모종 등급 초기 화아분화 진행도 (P6)
    for gid, val in (("A", "1.0"), ("B", "0.8"), ("C", "0.3")):
        t = re.sub(rf'(id = "{gid}"\n(?:[^\n]*\n)*?)initial_induction_index = [0-9.]+[^\n]*',
                   rf"\1initial_induction_index = {val}  # 촉성 모종은 꽃눈이 이미 분화. {TAG}", t, count=1)
    p2.write(p2.P, t)


def fert(tick: int, fid: str, g: float, every: int | None = None, count: int | None = None) -> str:
    s = f"[[schedule]]\ntick = {tick}\naction = \"add_fertilizer\"\nfertilizer = \"{fid}\"\ng_per_m2 = {g:.2f}\n"
    if every:
        s += f"repeat_every_ticks = {every}\nrepeat_count = {count}\n"
    return s + "\n"


def fert_block(kind: str) -> str:
    cn, k = float(cfg["TOP_CN"]), float(cfg["TOP_K"])
    head = (f"# 기비·웃거름 (2026-11-18 프로토타입 점검 3차: 기비 질산칼슘 없음, 웃거름 질산칼슘 {cn:g} g + 황산칼륨 {k:g} g)\n"
            "[[schedule]]\ntick = 0\naction = \"add_compost\"\ncompost = \"mature_manure\"\nkg_per_m2 = 2.0\n\n")
    td = dict(every=336, count=17)
    if kind == "no_topdress":
        return head
    if kind == "n_excess":
        urea = 2 * cn * CN_N_g_per_g / UREA_N_g_per_g
        return (head + fert(0, "urea", float(cfg["BASE_UREA_EXCESS"])) + fert(343, "calcium_nitrate", cn, **td) +
                fert(343, "potassium_sulfate", k, **td) + fert(343, "urea", urea, **td))
    if kind == "k_low":
        return head + fert(343, "calcium_nitrate", cn, **td)
    if kind == "k_excess":
        return head + fert(343, "calcium_nitrate", cn, **td) + fert(343, "potassium_sulfate", 3 * k, **td)
    return head + fert(343, "calcium_nitrate", cn, **td) + fert(343, "potassium_sulfate", k, **td)


def irrigation_block() -> str:
    return ("# 관수: 매일 08시 (2026-11-18 프로토타입 점검 3차)\n"
            f"[[schedule]]\ntick = 8\naction = \"irrigate\"\namount_mm = {cfg['IRR1']}\nrepeat_every_ticks = 24\nrepeat_count = 52\n\n"
            f"[[schedule]]\ntick = 1256\naction = \"irrigate\"\namount_mm = {cfg['IRR2']}\nrepeat_every_ticks = 24\nrepeat_count = 120\n\n"
            f"[[schedule]]\ntick = 4136\naction = \"irrigate\"\namount_mm = {cfg['IRR3']}\nrepeat_every_ticks = 24\nrepeat_count = 91\n\n")


def scenarios() -> None:
    p2.HEATER_SETPOINT = cfg["HEATER"]
    nov = p2.SEASON_BLOCKS[1]
    p2.SEASON_BLOCKS[1] = (nov[0], nov[1], nov[2], int(cfg["NOV_OPEN"]), int(cfg["NOV_CLOSE"]),
                           f"11/1~11/30 매일 {cfg['NOV_OPEN']}~{cfg['NOV_CLOSE']}시 바깥 겹 양쪽 열기")
    sd = REPO / "data/scenarios/m4"
    for p in sorted(sd.glob("*.toml")):
        name = p.stem
        t = p2.read(p)
        if name.startswith("season_") or name.startswith("s15_"):
            kind = name.replace("season_", "") if name.startswith("season_") else "standard"
            i0 = t.index("# 기비")
            i1 = t.index("# 기후 관리")
            t = t[:i0] + fert_block(kind) + irrigation_block() + p2.season_climate()
            t = re.sub(r"^no3_N_g_per_m2 = [0-9.]+[^\n]*", f"no3_N_g_per_m2 = {cfg['NO3']}  # 기준 토양보다 적게. {TAG}",
                       t, flags=re.M)
            if cfg["LABILE_N"]:
                t = re.sub(r"^labile_N_g_per_m2 = [0-9.]+[^\n]*", f"labile_N_g_per_m2 = {cfg['LABILE_N']}  # {TAG}",
                           t, flags=re.M)
            if cfg["STABLE_N"]:
                t = re.sub(r"^stable_N_g_per_m2 = [0-9.]+[^\n]*", f"stable_N_g_per_m2 = {cfg['STABLE_N']}  # {TAG}",
                           t, flags=re.M)
            if name == "season_k_low":
                t = re.sub(r"^k_g_per_m2 = [0-9.]+[^\n]*", f"k_g_per_m2 = 10.0  # K 부족 토양. {TAG}", t, flags=re.M)
        elif name == "s18_co2_vented":
            # 모든 겹 한쪽(왼쪽)만 열기: 오른쪽 창 명령 제거
            t = re.sub(r"\[\[schedule\]\]\ntick = \d+\naction = \"set_window\"\nlayer = \d\nside = \"right\"\n"
                       r"open = (?:true|false)\nrepeat_every_ticks = 24\nrepeat_count = \d+\n\n?", "", t)
            t = t.replace("11~14시 창 전부 열기", f"11~14시 모든 겹 왼쪽 창만 열기 {TAG}")
            t = t.replace("창 6개 전부 열고", f"모든 겹 왼쪽 창만 열고 {TAG}")
        p2.write(p, t)


if __name__ == "__main__":
    params()
    scenarios()
    print("ok", cfg)
