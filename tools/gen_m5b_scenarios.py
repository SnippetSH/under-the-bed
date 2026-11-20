"""M5b 시나리오 생성기 (S26 병해 변형, S27~S32, S34, docs/spec/foliar-disease.md 9장).

data/scenarios/m5b/ 에 s26_closed/s26_vented, s27~s32, s34 시나리오를 써요. s26 습도 변형(s26_heated 등)은 이미 있는 파일(s26_heated)에서 토양·작물을 읽고,
s29와 가을 작기 시나리오(s26_closed/vented, s27, s30~s32)의 원본은 m4 season_standard예요.
고치려면 이 스크립트를 고치고 다시 돌려요 (UTF-8, BOM 없음, LF). 값은 전부 게임용 임의값이에요.

    python tools/gen_m5b_scenarios.py

S31 천적 방사량(기본 15 마리/m2, 디렉터 결정 46: 구현 뒤 15/75/135 비교 전까지 보류)을 바꾼 변형은
커밋된 파일을 건드리지 않고 다른 폴더에 만들어요 (--only s31 은 이름이 s31로 시작하는 시나리오만):

    python tools/gen_m5b_scenarios.py --s31-release-per-m2 75 --out-dir runs/s31_release75 --only s31
"""
from __future__ import annotations

import argparse
import os
import pathlib
import math
import re
import tomllib

REPO = pathlib.Path(__file__).resolve().parent.parent
OUT = REPO / "data/scenarios/m5b"
SEED = 20261119
HOURS = 24
WEATHER_DIR = "../../weather"  # 시나리오 파일 기준 기상 폴더 (main이 --out-dir에 맞춰 바꿔요)


def fmt(v) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, str):
        return f'"{v}"'
    if isinstance(v, float):
        return repr(v)
    return str(v)


class Sched:
    def __init__(self):
        self.events: list[tuple[str | None, dict]] = []

    def add(self, tick: int, action: str, comment: str | None = None, repeat: tuple[int, int] | None = None, **kw):
        e = {"tick": tick, "action": action, **kw}
        if repeat:
            e["repeat_every_ticks"], e["repeat_count"] = repeat
        self.events.append((comment, e))

    def windows(self, layers: int, tick: int, frac: float, days: int, comment: str | None = None, n: int | None = None):
        """창 (layers x 좌우) 중 앞에서 n개(기본 전부)를 매일 tick시에 frac로 맞춰요 (days일 반복).
        순서: 0겹 왼쪽, 0겹 오른쪽, 1겹 왼쪽, ..."""
        first = True
        count = 0
        for layer in range(layers):
            for side in ("left", "right"):
                if n is not None and count >= n:
                    return
                count += 1
                self.add(tick, "set_window", comment if first else None, (HOURS, days),
                         layer=layer, side=side, open_fraction=frac)
                first = False

    def day_vent(self, layers: int, open_h: int, close_h: int, frac: float, days: int, start_day: int = 0,
                 comment: str | None = None, n: int | None = None):
        self.windows(layers, start_day * HOURS + open_h, frac, days, comment, n)
        self.windows(layers, start_day * HOURS + close_h, 0.0, days, None, n)

    def render(self) -> str:
        out = []
        for comment, e in self.events:
            if comment:
                out.append(f"# {comment}\n")
            lines = ["[[schedule]]"] + [f"{k} = {fmt(v)}" for k, v in e.items()]
            out.append("\n".join(lines) + "\n")
        return "\n".join(out)


def section(text: str, header: str) -> str:
    """[header] 줄부터 다음 [ 헤더 앞까지 (끝 공백 줄 제외)."""
    m = re.search(r"^%s[^\n]*\n(.*?)(?=^\[|\Z)" % re.escape(header), text, re.S | re.M)
    assert m, header
    return (header + m.group(0)[len(header):]).rstrip("\n") + "\n"


def read(rel: str) -> str:
    return (REPO / rel).read_text(encoding="utf-8")


S26 = read("data/scenarios/m5b/s26_heated.toml")  # 겨울 습도 변형의 토양·작물·미생물 (s26_closed는 이제 가을 작기라 쓰지 않아요)
SOIL_S26 = section(S26, "[soil]")  # 12월 겨울 작기 토양 (s26)
CROP_S26 = section(S26, "[crop]")  # 등급 B, 8포기/m2, 활착 1.0
MICROBES_S26 = section(S26, "[microbes]")
SS = read("data/scenarios/m4/season_standard.toml")
SOIL_SS = section(SS, "[soil]")
CROP_SS = section(SS, "[crop]")  # 등급 B, 활착 0.0 (가을 정식)
MICROBES_SS = section(SS, "[microbes]")
SS_SCHEDULE = tomllib.loads(SS)["schedule"]


def with_soil_temp(soil: str, t: float) -> str:
    return re.sub(r"soil_temp_degC = [\d.]+", f"soil_temp_degC = {t}", soil)


def microbes_block(anthracnose: float = 0.0) -> str:
    b = MICROBES_S26
    b = re.sub(r"anthracnose = [\d.]+", f"anthracnose = {anthracnose}", b)
    return b


def render(*, name, comment, start_doy, layers, drip, soil, crop, sched: Sched, bg=(0.05, 0.02), weather="synthetic_year",
           microbes=None) -> str:
    head = "\n".join(f"# {c}" for c in comment)
    micro = microbes or microbes_block()
    parts = [
        f"{head}\n# 값은 전부 게임용 임의값이에요. 이 파일은 tools/gen_m5b_scenarios.py가 만들어요(고치려면 스크립트를 고쳐요).\n",
        "schema_version = 7\n",
        f"""[scenario]
name = "m5b/{name}"
seed = {SEED}
start_day_of_year = {start_doy}
weather_csv = "{WEATHER_DIR}/{weather}.csv" # 이 파일 기준 상대 경로
""",
        "[house]\nbed_rows = 4\nbed_cols = 20\n" + f"cover_layers = {layers}\n"
        + f"drip_installed = {fmt(drip)}\n",
        soil,
        crop,
        micro,
    ]
    parts.append(f"[foliar]  # 바깥에서 늘 들어오는 포자 수준 (병해 시나리오 기본값: 잿빛곰팡이 0.05, 흰가루병 0.02)\n"
                 f"botrytis_background_index = {bg[0]}\npowdery_mildew_background_index = {bg[1]}\n")
    parts += ['[difficulty]\nstress_combination = "minimum"\n', sched.render()]
    return "\n".join(parts)


# ----------------------------------------------------------------------------- S28
def s28(name, comment, *, drip, film):
    days = 90
    s = Sched()
    s.add(0, "add_compost", "기비 숙성 퇴비 2 kg/m2", compost="mature_manure", kg_per_m2=2.0)
    if film:
        s.add(0, "set_mulch", "흑색 필름 멀칭 (정식 직후)", mulch="black_film")
    s.add(343, "add_fertilizer", "웃거름: season_standard와 같은 처방 (질산칼슘 5 g, 11/1부터 2.5 g, 황산칼륨 6 g)", (336, 3),
          fertilizer="calcium_nitrate", g_per_m2=5.0)
    s.add(1351, "add_fertilizer", None, (336, 3), fertilizer="calcium_nitrate", g_per_m2=2.5)
    s.add(343, "add_fertilizer", None, (336, 6), fertilizer="potassium_sulfate", g_per_m2=6.0)
    s.add(24, "release_predators", "표준 관리: 정식 다음 날 칠레이리응애 15마리/m2 (season_standard와 같음)", predator="persimilis", per_m2=15.0)
    s.add(8, "irrigate", "관수: 매일 08시 4.0 mm (9/10~10/31), 이후 2.5 mm. 두상 관수 변형은 같은 양을 위에서 줘요", (HOURS, 52), amount_mm=4.0)
    s.add(1256, "irrigate", None, (HOURS, 38), amount_mm=2.5)
    s.day_vent(3, 8, 18, 1.0, 52, 0, "9/10~10/31 매일 8~18시 창 전부 100% (season_standard와 같음)")
    s.day_vent(3, 12, 14, 1.0, 30, 52, "11/1~11/30 매일 12~14시 창 전부 열기. (season_standard는 바깥 겹만 열었지만 단순화)")
    s.day_vent(3, 12, 13, 0.3, 8, 82, "12/1~12/8 매일 12~13시 30%")
    s.add(1248, "set_heater", "난방: 11/1부터 10 도", on=True, setpoint_degC=10.0)
    crop = '[crop]\nseedling_grade = "C"\nplants_per_m2 = 8.0\ninitial_establishment_index = 0.0\n'
    return name, days, render(name=name, comment=comment, start_doy=253, layers=3, drip=drip, soil=SOIL_SS, crop=crop, sched=s)


# ----------------------------------------------------------------------------- 가을 작기 (season_standard 기반)
def autumn_scenario(name, comment, *, window_tick, run_days, layers=3, drip=True, window_events, override_irrigation=False,
                    weather="synthetic_year", bg=(0.05, 0.02), standard_release=True):
    """season_standard(9/10 정식, day 253)와 같은 시작·토양·작물·처방으로 측정 창(window_tick) 직전까지 가고,
    창부터는 변형 관리만 해요. 창·난방 일정은 창 시작에서 잘라 내고(창 안의 season_standard 일정은 지움),
    비료·관수(override_irrigation이 아니면)는 그대로 이어져요. 겹 수가 4이면 2겹 창 일정을 3겹에도 복사하고, 2이면 2겹(index 2) 일정은 빼요.
    창 일정은 window_events(s)가 Sched에 더해요."""
    run_ticks = run_days * HOURS
    s = Sched()
    first = True
    for ev in SS_SCHEDULE:
        e = dict(ev)
        act = e["action"]
        if e["tick"] >= run_ticks:
            continue
        cutting = act in ("set_window", "set_heater") or (act == "irrigate" and override_irrigation)
        if cutting:
            if e["tick"] >= window_tick:
                continue
            if "repeat_every_ticks" in e:
                every = e["repeat_every_ticks"]
                e["repeat_count"] = min(e["repeat_count"], math.ceil((window_tick - e["tick"]) / every))
        if act == "set_window" and e["layer"] >= layers:
            continue
        if act == "release_predators" and not standard_release:
            continue  # S30·S31은 응애·천적을 직접 다뤄서 표준 방사(틱 24)를 뺌
        s.events.append(("m4 season_standard 일정 (창 시작 전까지)" if first else None, e))
        first = False
        if act == "set_window" and layers == 4 and e["layer"] == 2:
            e3 = dict(e)
            e3["layer"] = 3
            s.events.append((None, e3))
    window_events(s)
    soil, crop = SOIL_SS, CROP_SS
    return name, run_days, render(name=name, comment=comment, start_doy=253, layers=layers, drip=drip, soil=soil, crop=crop, sched=s,
                                  weather=weather, bg=bg, microbes=MICROBES_SS)


S26_W = (335 - 253) * HOURS  # 12/1 = 틱 1968, 1/31 끝 = 144일 = 3456틱
SPR_W, SPR_END = (365 - 253 + 91) * HOURS, (365 - 253 + 135 + 1)  # 4/1 = 틱 4872, SPR_END = 5/15 끝까지 248일(5952틱)  # 4/1 = 틱 4872, 5/15 끝 = 248일 = 5952틱
MAR_W = (365 - 253 + 60) * HOURS  # 3/1 = 틱 4128


def build(cfg: dict) -> dict[str, tuple[int, str]]:
    res: dict[str, tuple[int, str]] = {}

    def put(t):
        res[t[0]] = (t[1], t[2])

    win = 62
    w0 = S26_W // HOURS

    def s26_base(s):
        s.add(S26_W, "set_heater", "측정 창 12/1~1/31: 난방 6 도로 바꿔요 (창 일정은 season_standard 것을 지우고 아래 변형이 대신해요)", on=True, setpoint_degC=6.0)

    common = ["가을 작기(9/10 정식, day 253, 등급 B 모종)가 m4 season_standard와 같은 시작·토양·작물·비료·관수·난방·창·피복(3겹)으로 12/1(틱 1968)까지 가요.",
              "측정 창 12/1~1/31(day 335~31, 62일): season_standard의 창·난방 일정을 지우고 아래 변형으로 바꿔요. 길이: 시작부터 1/31 끝까지 3456틱.",
              f"시드 {SEED}, 배경 포자 잿빛곰팡이 0.05, 흰가루병 0.02"]

    def s26_closed(s):
        s26_base(s)

    def s26_vented(s):
        s26_base(s)
        s.day_vent(3, 9, 10, 0.3, win, w0, "창 6개 전부 30%: 아침 9~10시 (한낮 환기는 한겨울 하우스를 8~12 도로 식혀서 뺐어요)")
        s.day_vent(3, 16, 18, 0.3, win, w0, "창 6개 전부 30%: 저녁 16~18시")

    def s27(s):
        s26_base(s)
        s.add(S26_W, "set_fan", "유동팬: 창 시작(0~08시)부터 가동, 08시에 끄고 18시에 켜요. 62일 (게임용 임의값)", on=True)
        s.add(S26_W + 8, "set_fan", None, (HOURS, win), on=False)
        s.add(S26_W + 18, "set_fan", None, (HOURS, win), on=True)

    put(autumn_scenario("s26_closed", ["S26 닫힘(병해 측정용 가을 작기): 측정 창은 창 닫힘 + 난방 6 도"] + common, window_tick=S26_W, run_days=144, window_events=s26_closed))
    put(autumn_scenario("s26_vented", ["S26 환기(병해 측정용 가을 작기): 측정 창은 매일 9~10시, 16~18시 창 6개 전부 30% + 난방 6 도"] + common, window_tick=S26_W, run_days=144, window_events=s26_vented))
    put(autumn_scenario("s27_closed_fan", ["S27 닫힘 + 유동팬: s26_closed와 같고 측정 창 안에서 팬을 18시~다음 날 08시에 돌려요"] + common, window_tick=S26_W, run_days=144, window_events=s27))

    # ---- S28
    c28 = ["S28 점적 대 두상 관수: 9월 10일 정식(day 253), 90일(2160틱), 등급 C 모종(잠복 탄저병 25%), 흑색 필름",
           "처방·기후는 m4 season_standard를 90일에 맞춰 줄인 것. 등급 C의 잠복 감염이 접종원이에요"]
    put(s28("s28_drip", c28 + ["점적 설치(drip_installed = true) — 물 튐 없음"], drip=True, film=True))
    put(s28("s28_overhead", c28 + ["점적 없음(drip_installed = false) — 매일 관수가 두상 관수라 물 튐이 있음"], drip=False, film=True))
    put(s28("s28_overhead_bare", c28 + ["점적 없음 + 필름 없음(맨흙): 필름 + 두상보다 포기 -> 포기 물 튐이 작고 흙 -> 포기 물 튐이 큼"], drip=False, film=False))
    put(s28("s28_overhead_remove", c28 + ["s28_overhead와 같은 파일 — 발병 이벤트가 난 포기 제거는 sim-cli 옵션 --remove-on-onset anthracnose로 해요 (시나리오에는 remove_plant 일정이 없음)"],
            drip=False, film=True))

    # ---- S30~S32: 가을 작기 + 봄 측정 창
    def vent_events(vent, w_tick, days, heater, water_mm=None, release=None, layers=3):
        def f(s):
            s.add(w_tick, "set_heater", f"측정 창: 밤 난방 {heater} 도 (설정 온도 아래로 내려갈 때만 켜져요)", on=True, setpoint_degC=heater)
            if water_mm is not None:
                s.add(w_tick + 8, "irrigate", f"측정 창: 관수 매일 08시 {water_mm} mm (season_standard의 4 mm 대신)", (HOURS, days), amount_mm=water_mm)
            for v in vent:
                o, c, fr = v[:3]
                n = v[3] if len(v) > 3 else None
                which = "창 전부" if n is None else f"창 {n}개(0겹 왼쪽부터 순서대로)"
                s.day_vent(layers, o, c, fr, days, w_tick // HOURS, f"측정 창: 매일 {o}~{c}시 {which} {int(fr * 100)}%", n)
            if release is not None:
                s.add(w_tick + release * HOURS, "release_predators", f"천적 방사: 창 시작 {release}일째 칠레이리응애 15마리/m2", predator="persimilis", per_m2=15.0)
        return f

    spr_days = 45
    spr_run = SPR_END
    spr_common = ["가을 작기(9/10 정식, day 253, 등급 B 모종)가 m4 season_standard와 같은 시작·토양·작물·비료·관수·창·피복(3겹)으로 4/1(틱 4872)까지 가요.",
                  f"측정 창 4/1~5/15(day 91~135, 45일): season_standard의 창·난방 일정을 지우고 아래 변형으로 바꿔요. 길이: 시작부터 5/15 끝까지 {spr_run * HOURS}틱.",
                  f"시드 {SEED}, 배경 포자 잿빛곰팡이 0.05, 흰가루병 0.02"]
    hv, cv = cfg["hot_vent"], cfg["cool_vent"]
    put(autumn_scenario("s30_hot", ["S30 따뜻한 쪽: 측정 창은 낮 창을 적게 열어 한낮 25~30 도로 관리"] + spr_common, window_tick=SPR_W, run_days=spr_run,
                        window_events=vent_events(hv, SPR_W, spr_days, 8.0), standard_release=False))
    put(autumn_scenario("s30_cool", ["S30 서늘한 쪽: 측정 창은 환기를 일찍·많이 해서 낮 20 도 안팎으로 관리"] + spr_common, window_tick=SPR_W, run_days=spr_run,
                        window_events=vent_events(cv, SPR_W, spr_days, 8.0), standard_release=False))
    # ---- S31 천적 (6차 재설계): 응애 지수에 맞춘 방사 시점 + 방사 후 6주(1008틱) 측정 창
    # 모두 season_standard 기반 가을 작기에서 표준 방사(틱 24)를 뺀 것. 창 시작(early_tick)부터 같은 관리: 난방 + 창 닫힘 (낮 15~28 도)
    e_t, l_t, s31_end = cfg["s31_early_tick"], cfg["s31_late_tick"], cfg["s31_end_tick"]
    six = 6 * 7 * HOURS
    rel = cfg["s31_release_per_m2"]  # 천적 방사량 (마리/m2). 결정 48: 15/75/135 비교 뒤 75로 확정
    s31_common = ["가을 작기(9/10 정식, day 253, 등급 B 모종)가 m4 season_standard와 같은 시작·토양·작물·비료·관수·창·피복(3겹)으로 가요. 단 표준 방사(틱 24)는 뺐어요 (S31이 천적을 직접 다뤄요).",
                  f"창 시작(틱 {e_t}, 11/25)부터 모든 S31 시나리오가 같은 관리: 난방 {cfg['s31_heater']} 도, 창 닫힘 (season_standard의 창·난방 일정은 지움). 길이는 모두 {s31_end}틱.",
                  f"시드 {SEED}, 배경 포자 잿빛곰팡이 0.05, 흰가루병 0.02"]

    def s31_events(release=None, dry=False):
        def f(s):
            def close_all(tick, label):
                s.windows(3, tick, 0.0, 1, label)
            if not dry:
                s.add(e_t, "set_heater", f"난방 {cfg['s31_heater']} 도 (밤·낮 설정 온도 아래로 내려갈 때만 켜져요)", on=True, setpoint_degC=cfg["s31_heater"])
            close_all(e_t, "창 6개 닫기 (창 일정을 지운 뒤 상태 고정)")
            if dry:
                d_end = e_t + six
                s.add(e_t, "set_heater", f"건조 변형 창 안 난방 {cfg['dry_heat']} 도", on=True, setpoint_degC=cfg["dry_heat"])
                for v in cfg["dry31_vent"]:
                    s.day_vent(3, v[0], v[1], v[2], six // HOURS, e_t // HOURS, f"건조 변형: 창 안 매일 {v[0]}~{v[1]}시 창 전부 {int(v[2] * 100)}%")
                s.add(d_end, "set_heater", f"6주 뒤 난방 {cfg['s31_heater']} 도로 되돌림", on=True, setpoint_degC=cfg["s31_heater"])
            if release is not None:
                s.add(release, "release_predators", f"천적 방사: 틱 {release} 칠레이리응애 {rel:g}마리/m2", predator="persimilis", per_m2=rel)
        return f

    for nm, cm, kw in (
            ("s31_none", "S31 방사 없음: 이후 세 시나리오의 기준 (같은 관리, 천적 방사 없음)", dict()),
            ("s31_none_dry", "S31 건조 대조: s31_predator_dry와 같은 건조 기상·환기·난방(방사 후 6주), 천적 방사 없음 — s31_predator_dry의 비교 기준 (m5-overview 15.3 항목 28)",
             dict(dry=True, weather="synthetic_year_dry_winter")),
            ("s31_predator", f"S31 일찍 방사: 틱 {e_t}(11/25, 하우스 평균 응애 지수 약 0.045)에 칠레이리응애 {rel:g}마리/m2 (B8)", dict(release=e_t)),
            ("s31_predator_late", f"S31 늦은 방사: 틱 {l_t}(2/2, 하우스 평균 응애 지수 약 0.16)에 {rel:g}마리/m2 (B20)", dict(release=l_t)),
            ("s31_predator_dry", f"S31 건조: s31_predator와 같은 시점에 방사, 방사 후 6주 동안 건조 기상(synthetic_year_dry_winter.csv, 11/24~1/5만 바깥 습도 x {cfg['dry_factor']}) + 환기·난방 (B9)",
             dict(release=e_t, dry=True, weather="synthetic_year_dry_winter"))):
        put(autumn_scenario(nm, [cm] + s31_common, window_tick=e_t, run_days=s31_end // HOURS, standard_release=False,
                            weather=kw.get("weather", "synthetic_year"),
                            window_events=s31_events(kw.get("release"), kw.get("dry", False))))

    s32_days = 76
    s32_common = ["가을 작기(9/10 정식, day 253, 등급 B 모종)가 m4 season_standard와 같은 시작·토양·작물·비료·관수·창으로 3/1(틱 4128)까지 가요.",
                  "겹 수와 점적 설치는 피복 변경 기간(4/1~8/31)과 점적 철거 불가 때문에 시나리오 시작부터 작기 내내 적용돼요 (3/1 이전도 이 겹 수·관수 방식).",
                  f"측정 창 3/1~5/15(day 60~135, 76일): season_standard의 창·난방 일정을 지우고 아래 변형으로 바꿔요. 길이: 시작부터 5/15 끝까지 {spr_run * HOURS}틱.",
                  f"시드 {SEED}, 배경 포자 잿빛곰팡이 0.05, 흰가루병 0.02"]
    for nm, cm, kw in (
            ("s32_bright", "S32 밝음: 2겹, 점적, 창 안 환기 기본(낮 10~16시 30%)", dict(layers=2, drip=True, vent=cfg["s32_base_vent"])),
            ("s32_dark", "S32 약광: 4겹, 점적, 창 안 환기 기본(낮 10~16시 30%) — 4겹이라 일사가 약해요", dict(layers=4, drip=True, vent=cfg["s32_dark_vent"])),
            ("s32_overhead", "S32 두상 관수: 2겹, 점적 없음(작기 내내 두상 관수), 창 안 환기 기본", dict(layers=2, drip=False, vent=cfg["s32_base_vent"])),
            ("s32_dry", "S32 건조: 2겹, 점적, 3/1~5/15만 바깥 습도를 낮춘 기상(synthetic_year_dry_spring.csv) + 창 안 환기·난방으로 하우스 습도를 0.6~0.7로", dict(layers=2, drip=True, vent=cfg["s32_dry_vent"], weather="synthetic_year_dry_spring", heater=cfg["s32_dry_heat"]))):
        put(autumn_scenario(nm, [cm] + s32_common, window_tick=MAR_W, run_days=spr_run, layers=kw["layers"], drip=kw["drip"],
                            weather=kw.get("weather", "synthetic_year"), standard_release=True,
                            window_events=vent_events(kw["vent"], MAR_W, s32_days, kw.get("heater", 8.0), layers=kw["layers"])))

    # ---- S29: m4 season 복사 + 배경 포자
    for src, nm, label in (("season_standard", "s29_standard", "S29 표준 처방"),
                           ("season_n_excess", "s29_n_excess", "S29 질소 과다 처방")):
        t = read(f"data/scenarios/m4/{src}.toml")
        t = t.replace(f'name = "m4/{src}"', f'name = "m5b/{nm}"')
        t = t.replace("seed = 20261118", f"seed = {SEED}")
        t = re.sub(r"botrytis_background_index = [\d.]+", "botrytis_background_index = 0.05", t)
        t = re.sub(r"powdery_mildew_background_index = [\d.]+", "powdery_mildew_background_index = 0.02", t)
        t = f"# {label}: m4/{src}와 같은 처방·기후·모종(등급 B) + 병해 배경 포자(잿빛곰팡이 0.05, 흰가루병 0.02). 263일(6312틱), 시드 {SEED}\n# 원본 파일: data/scenarios/m4/{src}.toml (이 파일은 tools/gen_m5b_scenarios.py가 복사해서 만들어요)\n" + t
        res[nm] = (263, t)

    # ---- S34: m4 season 복사 + 배경 포자 (시드는 m4 원본 그대로 20261118)
    for src, nm, head in (
            ("season_standard", "s34_season_spores",
             "# S34 배경 포자 시즌: m4 season_standard와 같고 바깥 배경 포자(잿빛곰팡이 0.05, 흰가루병 0.02)만 넣음 (2026-11-20 디렉터 요청). 263일 = --ticks 6312. 게임용 임의값"),
            ("season_n_excess", "s34_season_spores_n_excess",
             "# S34 배경 포자 시즌, 질소 과다: m4 season_n_excess + 배경 포자(0.05, 0.02) (2026-11-20). 263일 = --ticks 6312. 게임용 임의값")):
        t = read(f"data/scenarios/m4/{src}.toml")
        t = t.replace(f'name = "m4/{src}"', f'name = "m5b/{nm}"')
        t = re.sub(r"botrytis_background_index = [\d.]+", "botrytis_background_index = 0.05", t)
        t = re.sub(r"powdery_mildew_background_index = [\d.]+", "powdery_mildew_background_index = 0.02", t)
        t = head + "\n" + t.split("\n", 1)[1]  # 첫 줄(m4 원본 설명)을 S34 설명으로 바꿔요
        res[nm] = (263, t)
    return res


# 초기 관리 값 (프로토타입에서 맞춰요). vent: (열기 시, 닫기 시, 열림)
CFG = dict(
    hot_vent=[(10, 16, 0.3, 5)],
    cool_vent=[(8, 18, 0.5)],
    s32_base_vent=[(10, 16, 0.3)],
    s32_dark_vent=[(8, 16, 0.3)],
    s32_dry_vent=[(8, 17, 0.5)], s32_dry_heat=24.0,
    s31_early_tick=1800, s31_late_tick=3600, s31_end_tick=4608, s31_heater=10.0,
    s31_release_per_m2=75.0,
    dry_heat=24.0, dry31_vent=[(8, 16, 0.3)], dry_factor=0.1,
)


def main():
    global WEATHER_DIR
    ap = argparse.ArgumentParser(description="M5b 시나리오 생성기")
    ap.add_argument("--s31-release-per-m2", type=float, default=CFG["s31_release_per_m2"],
                    help="S31 천적 방사량 (마리/m2). 기본값은 커밋된 파일의 값(15)이에요")
    ap.add_argument("--out-dir", type=pathlib.Path, default=OUT,
                    help="출력 폴더 (기본 data/scenarios/m5b). 방사량 변형은 runs/ 아래처럼 다른 곳에 써요")
    ap.add_argument("--only", help="이 문자열로 시작하는 이름의 시나리오만 써요 (예: s31)")
    a = ap.parse_args()
    cfg = dict(CFG)
    cfg["s31_release_per_m2"] = a.s31_release_per_m2
    out = a.out_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    WEATHER_DIR = pathlib.PurePath(os.path.relpath(REPO / "data/weather", out)).as_posix()
    res = build(cfg)
    for nm, (days, text) in sorted(res.items()):
        if a.only and not nm.startswith(a.only):
            continue
        (out / f"{nm}.toml").write_text(text, encoding="utf-8", newline="\n")
        print(f"{nm:28s} {days:4d} days {days * HOURS:5d} ticks")


if __name__ == "__main__":
    main()
