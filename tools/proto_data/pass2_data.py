"""프로토타입 점검 2차: data/ 일괄 수정 (다시 돌려도 같은 결과가 나오도록 멱등).

- params: 값 바꾸기(set_param), 새 키 넣기
- 시나리오: [crop] initial_establishment_index, M1~M3 낮 환기 일정, S18 수정, 작기 기후 일정 생성

사용법: python tools/proto_data/pass2_data.py [section.key=value ...]   (인자는 params 덮어쓰기, 반복 보정용)
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TAG = "(2026-11-18 프로토타입 점검으로 조정)"
P = REPO / "data/params.toml"


def read(p: Path) -> str:
    return p.read_text(encoding="utf-8")


def write(p: Path, s: str) -> None:
    p.write_text(s, encoding="utf-8", newline="\n")


def set_param(text: str, section: str, key: str, value: str, old_note: str | None = None) -> str:
    """[section] 안의 key 줄 값을 바꾸고 주석 끝에 TAG(이전 값)를 붙여요."""
    lines = text.split("\n")
    cur = None
    for i, ln in enumerate(lines):
        m = re.match(r"^\[([a-z_]+)\]", ln)
        if m:
            cur = m.group(1)
            continue
        if cur == section and re.match(rf"^{re.escape(key)}\s*=", ln):
            m2 = re.match(rf"^({re.escape(key)}\s*=\s*)(.*?)(\s*#.*)?$", ln)
            old_value = m2.group(2).strip()
            comment = (m2.group(3) or "").strip()
            comment = re.sub(r"\s*\(2026-11-18 프로토타입 점검으로 조정[^)]*\)", "", comment)
            prev = re.search(r"\[이전 (.*)\]\s*$", comment)
            first_old = prev.group(1) if prev else old_value
            comment = re.sub(r"\s*\[이전 .*\]\s*$", "", comment)
            if first_old == value:
                note = comment
            else:
                note = f"{comment} {TAG} [이전 {first_old}]".strip()
            if not note.startswith("#") and note:
                note = "# " + note
            lines[i] = f"{key} = {value}  {note}".rstrip()
            return "\n".join(lines)
    raise KeyError(f"{section}.{key}")


def insert_after(text: str, section: str, after_key: str, line: str) -> str:
    key = line.split("=")[0].strip()
    if re.search(rf"^{re.escape(key)}\s*=", text, re.M):
        return text
    lines = text.split("\n")
    cur = None
    for i, ln in enumerate(lines):
        m = re.match(r"^\[([a-z_]+)\]", ln)
        if m:
            cur = m.group(1)
            continue
        if cur == section and re.match(rf"^{re.escape(after_key)}\s*=", ln):
            lines.insert(i + 1, line)
            return "\n".join(lines)
    raise KeyError(after_key)


# ---- params 기본 패키지 (A, B, D, E, G) ----
BASE = {
    ("crop", "mn_uptake_rate_per_g_root_per_hour"): "0.00001",
    ("house", "solar_heat_gain_fraction"): "0.18",
    ("phenology", "induction_nitrogen_curve"): "[[0, 1], [1.1, 1], [1.3, 0.6], [1.5, 0.25]]",
    ("fruit", "pollination_mean_curve"): "[[3, 0.2], [5, 0.3], [8, 0.6], [12, 0.92], [25, 0.92], [30, 0.65], [35, 0.35]]",
    # 반복 보정 결과 (2차)
    ("phenology", "truss_degree_days_to_anthesis"): "350.0",
    ("fruit", "degree_days_to_harvest"): "350.0",
    ("crop", "n_luxury_uptake_ratio"): "1.3",
    ("crop", "n_uptake_rate_per_g_root_per_hour"): "0.000015",
    # H (필요해서 씀)
    ("crop", "leaf_senescence_rate_per_hour"): "0.0003",
    ("crop", "maintenance_rate_leaf_per_hour"): "0.0008",
    # 패키지 밖 (승인 필요): 매일 관수로 작기 중 다짐이 1까지 올라 통기 0.5 → 망간 환원 → 독성
    ("aeration", "compaction_per_mm_irrigation"): "0.0004",
}


def update_params(overrides: dict[tuple[str, str], str]) -> None:
    t = read(P)
    t = insert_after(t, "crop", "mg_uptake_rate_per_g_root_per_hour",
                     "n_luxury_uptake_ratio = 1.5  # N 전용 사치 흡수 상한 (적정 대비). 게임용 임의값, "
                     "2026-11-18 프로토타입 점검으로 신설 (N 비율이 공급을 따라가게)")
    t = insert_after(t, "crop", "n_luxury_uptake_ratio",
                     "n_uptake_rate_per_g_root_per_hour = 0.0005  # N 전용 흡수 속도. 게임용 임의값, "
                     "2026-11-18 프로토타입 점검으로 신설")
    for (sec, key), val in {**BASE, **overrides}.items():
        t = set_param(t, sec, key, val)
    write(P, t)


# ---- 시나리오 ----

def set_crop_establishment(text: str, value: float) -> str:
    if "[crop]" not in text:
        return text
    if "initial_establishment_index" in text:
        return re.sub(r"initial_establishment_index = [0-9.]+[^\n]*",
                      f"initial_establishment_index = {value:.1f}  # {TAG}", text)
    return re.sub(r"(plants_per_m2 = [0-9.]+[^\n]*\n)",
                  rf"\1initial_establishment_index = {value:.1f}  # 0 = 새로 정식, 1 = 활착된 작물. {TAG}\n",
                  text, count=1)


def window_block(tick: int, layer: int, side: str, open_: float, every: int, count: int) -> str:
    return (f"[[schedule]]\ntick = {tick}\naction = \"set_window\"\nlayer = {layer}\nside = \"{side}\"\n"
            f"open_fraction = {float(open_):.1f}\nrepeat_every_ticks = {every}\nrepeat_count = {count}\n")


ALL6 = [(l, s) for l in range(3) for s in ("left", "right")]
OUTER2 = [(0, "left"), (0, "right")]
LEFT3 = [(l, "left") for l in range(3)]


def vent_schedule(windows, start_tick: int, days: int, open_h: int, close_h: int, comment: str,
                  fraction: float = 1.0) -> str:
    out = f"\n# {comment} {TAG}\n"
    for l, s in windows:
        out += "\n" + window_block(start_tick + open_h, l, s, fraction, 24, days)
        out += "\n" + window_block(start_tick + close_h, l, s, 0.0, 24, days)
    return out


M123_MARK = "# 낮 환기 (M4 이행): 매일 10시에 창 6개 전부 열고 16시에 닫기"


def add_m123_vent(text: str, days: int) -> str:
    if M123_MARK in text:
        return text
    return text.rstrip("\n") + "\n" + vent_schedule(ALL6, 0, days, 10, 16, M123_MARK[2:])


# 작기 기후 일정 (틱 0 = 9월 10일 0시). (시작 일, 일수, 창, 여는 시각, 닫는 시각, 설명)
SEASON_BLOCKS = [
    (0, 52, ALL6, 8, 18, "9/10~10/31 매일 8~18시 창 6개 전부 100%", 1.0),
    (52, 30, LEFT3, 12, 13, "11/1~11/30 매일 12~13시 모든 겹 왼쪽 30% (7차)", 0.3),
    (82, 62, LEFT3, 12, 13, "12/1~1/31 매일 12~13시 모든 겹 왼쪽 30% (6차)", 0.3),
    (144, 28, LEFT3, 12, 14, "2/1~2/28 매일 12~14시 모든 겹 왼쪽 30% (6차)", 0.3),
    (172, 31, LEFT3, 11, 14, "3/1~3/31 매일 11~14시 모든 겹 왼쪽 30% (6차)", 0.3),
    (203, 30, LEFT3, 9, 17, "4/1~4/30 매일 9~17시 모든 겹 왼쪽 50% (6차)", 0.5),
    (233, 30, ALL6, 8, 18, "5/1~5/30 매일 8~18시 창 6개 전부 100%", 1.0),
]
HEATER_SETPOINT = "8.0"


def season_climate() -> str:
    out = "# 기후 관리 (2026-11-18 프로토타입 점검 2차로 다시 짬: 월별 낮 환기, 12~1월은 닫아 둠)\n"
    for block in SEASON_BLOCKS:
        d0, n, wins, oh, ch, cm = block[:6]
        frac = block[6] if len(block) > 6 else 1.0
        out += vent_schedule(wins, d0 * 24, n, oh, ch, cm, frac)
    out += (f"\n# 난방: 11/1부터 {HEATER_SETPOINT} 도 설정\n[[schedule]]\ntick = 1248\naction = \"set_heater\"\non = true\n"
            f"setpoint_degC = {HEATER_SETPOINT}\n\n# 난방: 4/1 끔\n[[schedule]]\ntick = 4872\naction = \"set_heater\"\n"
            f"on = false\nsetpoint_degC = {HEATER_SETPOINT}\n")
    return out


def replace_season_climate(text: str) -> str:
    i = text.index("# 기후 관리")
    return text[:i] + season_climate()


OLD_DAYS = {
    "m1/s0_control": 7, "m1/s1_immature": 7, "m1/s1_mature": 7, "m1/s2_alkaline": 21, "m1/s2_neutral": 21,
    "m1/s3_over": 14, "m1/s3_proper": 14, "m2/s4_optimal": 21, "m2/s4_alkaline": 21, "m2/s4_acid": 21,
    "m2/s8_proper": 14, "m2/s8_waterlogged": 14, "m2/s9_lime": 42, "m2/s9_none": 42, "m2/s10_standard": 21,
    "m2/s10_excess": 21, "m2/s10_dry": 21, "m3/s5_standard": 21, "m3/s5_k_excess": 21, "m3/s6_loam": 28,
    "m3/s6_sand": 28, "m3/s11_neutral": 28, "m3/s11_acid": 28, "m3/s11_wet": 28, "m3/s11_alkaline": 28,
    "m3/s12_neutral": 60, "m3/s12_acid": 60, "m3/s12_alkaline": 60,
}


def update_scenarios() -> None:
    sd = REPO / "data/scenarios"
    for rel, days in OLD_DAYS.items():
        p = sd / f"{rel}.toml"
        t = read(p)
        t = set_crop_establishment(t, 1.0)
        t = add_m123_vent(t, max(days, 60))
        write(p, t)
    for p in sorted((sd / "m4").glob("*.toml")):
        t = read(p)
        name = p.stem
        if name.startswith("season_") or name.startswith("s15_"):
            t = set_crop_establishment(t, 0.0)
            t = replace_season_climate(t)
        elif name.startswith("s18_autumn"):
            t = set_crop_establishment(t, 0.0)
            t = t.replace("9월 10일 시작, 14일", "9월 10일 시작, 28일")
            t = re.sub(r"repeat_count = 14\b", "repeat_count = 28", t)
        elif name.startswith("s18_co2"):
            t = set_crop_establishment(t, 1.0)
            t = t.replace('seedling_grade = "B"', 'seedling_grade = "A"')
            t = re.sub(r"plants_per_m2 = [0-9.]+[^\n]*", f"plants_per_m2 = 16.0  # 큰 활착 작물. {TAG}", t)
            t = re.sub(r"labile_C_g_per_m2 = [0-9.]+[^\n]*", f"labile_C_g_per_m2 = 10.0  # 생퇴비 없음. {TAG}", t)
            t = re.sub(r"labile_N_g_per_m2 = [0-9.]+[^\n]*", f"labile_N_g_per_m2 = 1.0  # {TAG}", t)
        else:  # s13_*, s18_winter_*
            t = set_crop_establishment(t, 1.0)
        write(p, t)


if __name__ == "__main__":
    overrides = {}
    for arg in sys.argv[1:]:
        k, v = arg.split("=", 1)
        sec, key = k.split(".")
        overrides[(sec, key)] = v
    update_params(overrides)
    update_scenarios()
    print("ok")
