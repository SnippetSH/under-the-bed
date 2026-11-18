"""프로토타입 점검 4차: data/ 수정 (3차 위에, 멱등). 인자 KEY=value로 반복 보정값을 바꿔요.

    python tools/proto_data/pass4_data.py [ABORT_RATE=0.01] [ABORT_WINDOW=0.25] [WATERLOG=...]
        [CO2_START=340] [CO2_OPEN=11] [CO2_CLOSE=14] [THIN_TICKS=t1,t2,...] [THIN_PER_PLANT=6]
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pass2_data as p2  # noqa: E402

REPO = p2.REPO
TAG = p2.TAG

cfg = {
    "ABORT_RATE": "0.01", "ABORT_WINDOW": "0.25", "ABORT_CURVE": "[[0, 1], [0.6, 0.5], [0.85, 0], [1, 0]]",
    "RANK_ABORT": "[0.1, 0.4, 1.0, 1.6]",
    "WATERLOG": None, "COMPACTION": None,
    "CO2_START": "340", "CO2_OPEN": "11", "CO2_CLOSE": "14",
    "THIN_TICKS": "", "THIN_PER_PLANT": "6.0", "LUE": None, "TEMP_CURVE": None,
}
for a in sys.argv[1:]:
    k, v = a.split("=", 1)
    cfg[k] = v


def gen_curves() -> dict[str, str]:
    out = subprocess.run([sys.executable, str(REPO / "tools/gen_brix_curves.py")], capture_output=True, text=True,
                         check=True).stdout
    res = {}
    for ln in out.splitlines():
        k, v = ln.split(" = ", 1)
        res[k] = v
    return res


def params() -> None:
    t = p2.read(p2.P)
    # 승인값으로 되돌림 (4차: 꽃은 실제처럼, 낙과로 솎음)
    t = p2.set_param(t, "phenology", "flowers_per_truss_curve", "[[1, 6], [3, 12], [6, 18], [10, 22]]")
    # 당도 기온 곡선 삭제 → 주·야간 점수
    t = re.sub(r"^brix_temp_curve = [^\n]*\n", "", t, flags=re.M)
    t = re.sub(r"^abortion_rate_per_hour = [^\n]*\n", "", t, flags=re.M)  # 5차: 진행도당으로 바뀜
    curves = gen_curves()
    new = [
        f"brix_day_temp_score_curve = {curves['brix_day_temp_score_curve']}  # x = 낮 하우스 기온 °C, y = S_day. "
        "디렉터 지정 식(23 °C 중심 가우시안, σ 5), tools/gen_brix_curves.py로 생성",
        f"brix_night_temp_score_curve = {curves['brix_night_temp_score_curve']}  # x = 밤 하우스 기온 °C, y = S_night. "
        "디렉터 지정 식(8.5 °C 비대칭), tools/gen_brix_curves.py로 생성",
        "brix_day_weight = 0.6                     # 주·야간 비율 (합 1). 디렉터 지정",
        "brix_night_weight = 0.4",
        "brix_temp_score_curve = [[0, 0.75], [1, 1.15]]  # x = K_temp, y = 당도 배율. 게임용 임의값",
        "day_solar_threshold_W_per_m2 = 0.0        # 하우스 안 일사가 이보다 크면 낮",
        f"abortion_rate_per_development_index = {cfg['ABORT_RATE']}  # 진행도 1당 낙과 확률 (5차: 시간당 → 진행도당). 게임용 임의값",
        f"abortion_window_index = {cfg['ABORT_WINDOW']}  # 이 진행도 미만의 어린 과실만 떨어짐. 게임용 임의값",
        f"abortion_satisfaction_curve = {cfg['ABORT_CURVE']}  # x = 크기 충족도 이동 평균, y = 낙과 확률 배율. 게임용 임의값",
        f"rank_abortion_weight = {cfg['RANK_ABORT']}  # 과위별 낙과 배율. 게임용 임의값",
        "thinnable_max_index = 0.3                 # 적화·적과로 딸 수 있는 최대 진행도. 게임용 임의값",
    ]
    for ln in new:
        key = ln.split("=")[0].strip()
        if re.search(rf"^{key}\s*=", t, re.M):
            t = re.sub(rf"^{key}\s*=[^\n]*", ln.replace("\\", "\\\\"), t, flags=re.M)
        else:
            t = p2.insert_after(t, "fruit", "brix_supply_curve" if key.startswith(("brix", "day_")) else
                                "size_class_bounds_g", ln)
    if cfg["WATERLOG"]:
        t = p2.set_param(t, "root_damage", "waterlog_curve", cfg["WATERLOG"])
    if cfg["COMPACTION"]:
        t = p2.set_param(t, "aeration", "compaction_per_mm_irrigation", cfg["COMPACTION"])
    if cfg["LUE"]:
        t = p2.set_param(t, "crop", "light_use_efficiency_g_per_MJ", cfg["LUE"])
    if cfg["TEMP_CURVE"]:
        t = p2.set_param(t, "crop", "temp_curve", cfg["TEMP_CURVE"])
    p2.write(p2.P, t)


def thin_block(ticks: list[int], per_plant: str) -> str:
    out = f"\n# 적화·적과 (4차): 화방 개화 약 5일 뒤 포기당 {per_plant}개 {TAG}\n"
    for tk in ticks:
        out += (f"\n[[schedule]]\ntick = {tk}\naction = \"thin_flowers\"\nper_plant = {per_plant}\n")
    return out


def cn_entry(tick: int, g: float, count: int) -> str:
    return (f"[[schedule]]\ntick = {tick}\naction = \"add_fertilizer\"\nfertilizer = \"calcium_nitrate\"\n"
            f"g_per_m2 = {g:.2f}\nrepeat_every_ticks = 336\nrepeat_count = {count}\n")


WINTER_CN_BLOCK = cn_entry(343, 5.0, 17)


def winter_cn(text: str) -> str:
    """11/1~2/28 웃거름 질산칼슘 절반 (재배자 관행, 4차): 틱 343 + 336k, k = 3..11 은 2.5 g"""
    if WINTER_CN_BLOCK not in text:
        return text
    rep = (f"# 웃거름 질산칼슘: 11/1~2/28은 절반 2.5 g (겨울 N 줄이기) {TAG}\n" + cn_entry(343, 5.0, 3) + "\n" +
           cn_entry(1351, 2.5, 9) + "\n" + cn_entry(4375, 5.0, 5))
    return text.replace(WINTER_CN_BLOCK, rep)


def scenarios() -> None:
    sd = REPO / "data/scenarios/m4"
    for name in ("season_standard", "season_acid", "season_k_excess", "season_k_low", "s15_grade_a", "s15_grade_c"):
        p = sd / f"{name}.toml"
        p2.write(p, winter_cn(p2.read(p)))
    # S16: K 부족 = 퇴비 없음 (퇴비 K 약 28 g/m²가 가렸음). 퇴비 N·P 몫은 비료로
    p = sd / "season_k_low.toml"
    t = p2.read(p)
    t = t.replace('[[schedule]]\ntick = 0\naction = "add_compost"\ncompost = "mature_manure"\nkg_per_m2 = 2.0\n\n',
                  f'# 퇴비 대신 (4차, K 없는 기비) {TAG}\n'
                  '[[schedule]]\ntick = 0\naction = "add_fertilizer"\nfertilizer = "calcium_nitrate"\ng_per_m2 = 15.0\n\n'
                  '[[schedule]]\ntick = 0\naction = "add_fertilizer"\nfertilizer = "superphosphate"\ng_per_m2 = 30.0\n\n')
    p2.write(p, t)
    # S18 CO2: 시작일·환기 시각
    for name in ("s18_co2_closed", "s18_co2_vented"):
        p = sd / f"{name}.toml"
        t = p2.read(p)
        t = re.sub(r"^start_day_of_year = \d+[^\n]*", f"start_day_of_year = {cfg['CO2_START']}  # {TAG}", t, flags=re.M)
        t = re.sub(r"12월 6일 시작|\d+일째 시작", f"day {cfg['CO2_START']} 시작".replace("day", "d"), t)
        if name.endswith("vented"):
            lines = t.split("\n")
            for i, ln in enumerate(lines):
                if ln.startswith("tick = ") and i + 1 < len(lines) and lines[i + 1] == 'action = "set_window"':
                    opening = lines[i + 4] not in ("open = false", "open_fraction = 0.0")
                    lines[i] = f"tick = {cfg['CO2_OPEN'] if opening else cfg['CO2_CLOSE']}"
            t = "\n".join(lines)
        p2.write(p, t)
    # 적과 시나리오
    if cfg["THIN_TICKS"]:
        base = p2.read(sd / "season_standard.toml")
        base = base.replace('name = "m4/season_standard"', 'name = "m4/season_thinned"')
        base = re.sub(r"^# [^\n]*\n", "", base, count=1)
        base = ("# 적과 비교: season_standard + 화방 개화 약 5일 뒤 적화·적과 포기당 "
                f"{cfg['THIN_PER_PLANT']}개 — docs/spec/strawberry-fruit.md 6c. 값은 전부 게임용 임의값이에요.\n" + base)
        ticks = [int(x) for x in cfg["THIN_TICKS"].split(",") if x]
        p2.write(sd / "season_thinned.toml", base.rstrip("\n") + "\n" + thin_block(ticks, cfg["THIN_PER_PLANT"]))


if __name__ == "__main__":
    params()
    scenarios()
    print("ok", cfg)
