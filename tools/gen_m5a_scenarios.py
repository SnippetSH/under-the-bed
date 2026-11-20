"""M5a 시나리오(S19~S25, S33)를 data/scenarios/m5a/ 에 만들어요.

값은 전부 게임용 임의값이에요 (docs/spec/m5-overview.md 9장, disinfection.md "시나리오 테스트").
손으로 고치지 말고 이 스크립트를 고친 뒤 다시 돌려요: python tools/gen_m5a_scenarios.py
README(data/scenarios/m5a/README.md)도 같이 만들어요. 시나리오마다 실행 일수는 README 표와 tools/run_scenarios.py(M5a 그룹)가 같아야 해요.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "data" / "scenarios" / "m5a"
SEED = 20261119
HOURS = 24
JULY_10 = 191  # 7월 10일 (윤년 없는 달력, 1월 1일 = 1)
SEP_1 = 244
SEP_10 = 253
OCT_21 = 294
NOV_15 = 319
JAN_10 = 10


# --------------------------------------------------------------------------- 보조 도구
def fmt(v) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, str):
        return f'"{v}"'
    if isinstance(v, (list, tuple)):
        return "[" + ", ".join(fmt(x) for x in v) + "]"
    return repr(float(v)) if isinstance(v, float) else str(v)


@dataclass
class Sched:
    items: list[str] = field(default_factory=list)

    def add(self, tick: int, action: str, comment: str | None = None, repeat: tuple[int, int] | None = None, **kw):
        lines = []
        if comment:
            lines.append(f"# {comment}")
        lines += ["[[schedule]]", f"tick = {tick}", f'action = "{action}"']
        lines += [f"{k} = {fmt(v)}" for k, v in kw.items()]
        if repeat:
            lines += [f"repeat_every_ticks = {repeat[0]}", f"repeat_count = {repeat[1]}"]
        self.items.append("\n".join(lines))

    def windows(self, tick: int, open_fraction: float, comment: str | None = None, repeat: tuple[int, int] | None = None):
        """창 6개(3겹 x 좌우) 전부 같은 개방률로"""
        first = True
        for layer in range(3):
            for side in ("left", "right"):
                self.add(tick, "set_window", comment if first else None, repeat,
                         layer=layer, side=side, open_fraction=open_fraction)
                first = False

    def day_ventilation(self, start_tick: int, days: int, open_hour: int, close_hour: int, comment: str):
        self.windows(start_tick + open_hour, 1.0, comment, (HOURS, days))
        self.windows(start_tick + close_hour, 0.0, None, (HOURS, days))

    def render(self) -> str:
        return "\n\n".join(self.items) + "\n" if self.items else ""


def soil_block(*, water_mm=50.0, soil_temp=20.0, ph=6.2, nh4=2.0, no3=10.0, labile_C=50.0, labile_N=5.0,
               stable_N=360.0, compaction=0.3, p=25.0, note="기준 토양 = M3 s5_standard") -> str:
    return f"""[soil]
field_capacity_mm = 60.0
water_mm = {fmt(float(water_mm))}
soil_temp_degC = {fmt(float(soil_temp))}
ph = {fmt(float(ph))}
nh4_N_g_per_m2 = {fmt(float(nh4))}
no2_N_g_per_m2 = 0.0
no3_N_g_per_m2 = {fmt(float(no3))}
labile_C_g_per_m2 = {fmt(float(labile_C))}
labile_N_g_per_m2 = {fmt(float(labile_N))}
stable_C_g_per_m2 = 3600.0
stable_N_g_per_m2 = {fmt(float(stable_N))}
# M2 항목: 전부 게임용 임의값 (docs/spec/m2-overview.md 예시)
saturation_mm = 110.0
compaction_index = {fmt(float(compaction))}
base_buffer_eq_per_m2_per_pH = 1.25
# M3 항목: 전부 게임용 임의값 ({note}, docs/spec/m3-overview.md)
cec_mineral_eq_per_m2 = 25.0
mn_g_per_m2 = 5.0
mn_oxide_g_per_m2 = 100.0            # 산화물 Mn, 게임용 임의값 (ADR-0010, 2026-11-18 개정)
p_fixed_g_per_m2 = 150.0
k_g_per_m2 = 60.0
ca_g_per_m2 = 400.0
mg_g_per_m2 = 60.0
p_g_per_m2 = {fmt(float(p))}
s_g_per_m2 = 3.0
"""


def microbes_block(*, antagonist=0.5, mycorrhiza=0.1, fusarium=0.0, phytophthora=0.0, note="") -> str:
    tail = f". {note}" if note else ""
    return f"""[microbes]  # M5a 필수: 모든 셀의 무리별 시작 지수 (기본값은 m5-overview 11장){tail}
decomposer = 1.0
nitrifier_aob = 1.0
nitrifier_nob = 1.0
antagonist = {fmt(float(antagonist))}
mycorrhiza = {fmt(float(mycorrhiza))}
fusarium = {fmt(float(fusarium))}
phytophthora = {fmt(float(phytophthora))}
anthracnose = 0.0  # M5b: 탄저병균 시작 지수 (foliar-disease 8장)

[foliar]  # M5b 필수: 바깥에서 늘 들어오는 포자 수준 (foliar-disease 8장). 0 = 배경 포자 없음
botrytis_background_index = 0.0
powdery_mildew_background_index = 0.0
"""


@dataclass
class Scenario:
    name: str  # 파일 이름(확장자 제외). 시나리오 name은 "m5a/<name>"
    days: int  # sim-cli --ticks = days * 24
    comment: list[str]
    start_doy: int
    soil: str
    microbes: str
    sched: Sched
    crop: str | None = None  # [crop] 블록 (tick 0 정식)
    group: str = ""
    note: str = ""  # README 비고


def crop_block(grade="B", plants=8.0, establishment=0.0) -> str:
    return f"""[crop]
seedling_grade = "{grade}"
plants_per_m2 = {fmt(float(plants))}
initial_establishment_index = {fmt(float(establishment))}
"""


def render(sc: Scenario) -> str:
    head = "\n".join(f"# {line}" for line in sc.comment)
    parts = [
        f"{head}\n"
        f"# 길이는 sim-cli의 --ticks로 정해요: {sc.days}일 = {sc.days * HOURS}틱. 시작: day {sc.start_doy}, 틱 0 = 그날 0시.\n"
        "# 값은 전부 게임용 임의값이에요. 이 파일은 tools/gen_m5a_scenarios.py가 만들어요(고치려면 스크립트를 고쳐요).\n",
        "schema_version = 7\n",
        f"""[scenario]
name = "m5a/{sc.name}"
seed = {SEED}
start_day_of_year = {sc.start_doy}
weather_csv = "../../weather/synthetic_year.csv" # 이 파일 기준 상대 경로
""",
        """[house]
bed_rows = 4
bed_cols = 20
cover_layers = 3
drip_installed = true  # M5b: 점적 설치 (house-humidity)
""",
        sc.soil,
    ]
    if sc.crop:
        parts.append(sc.crop)
    parts += [sc.microbes, '[difficulty]\nstress_combination = "minimum"\n', sc.sched.render()]
    return "\n".join(parts)


# --------------------------------------------------------------------------- 시나리오 정의
SCENARIOS: list[Scenario] = []


def add(sc: Scenario) -> Scenario:
    SCENARIOS.append(sc)
    return sc


PATHOGEN_SOIL_NOTE = "시들음병균 0.5, 역병균 0.5 (오염 토양)"
DISINFECT_SOIL = soil_block(water_mm=30.0, soil_temp=25.0, ph=6.2)
DISINFECT_MICROBES = microbes_block(fusarium=0.5, phytophthora=0.5, note=PATHOGEN_SOIL_NOTE)
SOLAR_END = 4 * 7 * HOURS  # 4주 = 672틱
TWO_WEEKS = 2 * 7 * HOURS  # 336틱


def disinfection(name, group, comment, days, *, film=None, windows_open=False, irrigate=True, film_ticks=SOLAR_END,
                 chem=None, chem_g=0.0, post=True, tills=(), extra=None, start=JULY_10, soil=None, microbes=None):
    """소독 단계 시나리오 틀 (disinfection.md "시나리오 테스트" 표).

    film: 멀칭 id(없으면 필름 없음). windows_open: 소독 중 창을 열어 둠(기본은 닫음 = 시작 상태).
    film_ticks: 이 틱에 필름을 걷고 창을 열어요(post=True일 때). chem: 약제 id (tick 1에 g/m2).
    tills: 필름 걷은 뒤 경운할 틱들.
    """
    s = Sched()
    if windows_open:
        s.windows(0, 1.0, "소독 중에도 창을 열어 둠 (필름·약제 효과만 보려고 하우스 가열을 막음)")
    if irrigate:
        s.add(0, "irrigate", "관수 30 mm (포장용수량 근처까지)", amount_mm=30.0)
    if chem:
        s.add(1, "apply_disinfectant", f"약제 소독 {chem} {chem_g} g/m2 (표준량 = 잔류 지수 1)", disinfectant=chem, g_per_m2=chem_g)
    if film:
        s.add(1, "set_mulch", f"{film} 덮기", mulch=film)
    if post:
        if film:
            s.add(film_ticks, "set_mulch", "필름 걷기 (소독 끝)", mulch="none")
        s.windows(film_ticks, 1.0, "창 전부 열기 (소독 끝)")
        for t in tills:
            s.add(t, "till", "경운 (약제 잔류 절반, 가스 빼기)")
    if extra:
        extra(s)
    return add(Scenario(name, days, comment, start, soil or DISINFECT_SOIL, microbes or DISINFECT_MICROBES, s, group=group))


# ---- S19 태양열 소독: 필름·환기·기간·수분 -------------------------------------------------
disinfection("s19_solar_4w", "S19", [
    "S19 태양열 소독 4주: 7/10 시작, 관수 30 mm -> 투명 필름 -> 창 전부 닫음, 28일 (m5-overview 9장).",
    "비교: s19_solar_4w(필름) > s19_closed_nofilm(필름 없음) > s19_vented(창 열고 필름 없음) 순으로 지온이 높고 시들음병균·역병균 사멸이 커요.",
    "S19b: s19_solar_2w(2주 필름 후 걷음)보다 4주 쪽 사멸이 커요(지속 시간). S19c: s19_solar_dry(관수 없음)보다 이 시나리오 쪽 사멸이 커요(젖은 흙이 잘 죽음).",
], 28, film="clear_film")
disinfection("s19_closed_nofilm", "S19", [
    "S19 필름 없음·창 닫음: 관수 30 mm 후 필름 없이 하우스만 밀폐 (s19_solar_4w와 같은 조건에서 필름만 뺀 변형), 28일.",
    "기대: s19_solar_4w보다 지온이 낮고 사멸이 작아요 (증발 냉각이 남고 필름 일사 이득이 없음).",
], 28)
disinfection("s19_vented", "S19", [
    "S19 환기: 관수 30 mm 후 필름 없이 창을 전부 열어 둠 (소독 안 한 대조), 28일.",
    "기대: S19 세 시나리오 중 지온·사멸이 가장 작아요 (필름 > 필름 없음·밀폐 > 환기).",
], 28, windows_open=True)
disinfection("s19_solar_2w", "S19", [
    "S19b 태양열 소독 2주: s19_solar_4w와 같은데 14일째 필름을 걷고 창을 열어요, 28일 실행.",
    "기대: 소독 기간이 짧아서 s19_solar_4w보다 누적 사멸이 작아요 (완료 기준: 지속 시간).",
], 28, film="clear_film", film_ticks=TWO_WEEKS)
disinfection("s19_solar_dry", "S19", [
    "S19c 마른 흙 태양열: s19_solar_4w에서 관수만 뺀 것 (시작 수분 30 mm = 상대수분 0.5), 28일.",
    "기대: 마른 흙은 열 사멸 배율이 낮아서(moist_heat_curve) s19_solar_4w보다 사멸이 작아요.",
], 28, film="clear_film", irrigate=False)

# ---- S20 소독 방식 비교 (유익·유해 함께 감소) -----------------------------------------------
CHEM_FILM_DAYS = TWO_WEEKS
disinfection("s20_none", "S20", [
    "S20 대조(소독 안 함): 관수 30 mm 후 필름 없이 창을 열어 둠. 28일 실행, 회복 확인은 70일 실행.",
    "기대: 다른 세 시나리오보다 유익균(분해균·질산화균·길항균·균근균)도 병원균도 거의 안 줄어요.",
], 70, windows_open=True, post=False)
disinfection("s20_solar", "S20", [
    "S20 태양열 소독: 투명 필름 + 창 닫음 4주 후 필름 걷고 창 열기 (= s19_solar_4w). 28일 실행, 회복 확인은 70일 실행.",
    "기대: 유익·유해가 함께 감소. 약제보다 길항균이 덜 줄어요(포자 형성균은 열에 강함). 질산화균은 열에 잘 죽어요.",
], 70, film="clear_film")
disinfection("s20_dazomet", "S20", [
    "S20 다조멧: 관수 30 mm 후 다조멧 30 g/m2(표준량) + 흑색 필름 14일, 이후 필름 걷고 경운 2회(0·2일 뒤). 28일 실행, 회복 확인은 70일 실행.",
    "기대: 유익·유해가 함께 감소. 길항균도 크게 줄어요. 질산화균은 거의 살아요. 소독 방식 외 가열 영향을 줄이려고 흑색 필름 + 창 열기(명세 틀의 투명 필름·창 닫음에서 벗어난 점).",
], 70, film="black_film", windows_open=True, chem="dazomet", chem_g=30.0, film_ticks=CHEM_FILM_DAYS,
    tills=(CHEM_FILM_DAYS, CHEM_FILM_DAYS + 48))
disinfection("s20_chloropicrin", "S20", [
    "S20 클로로피크린: 관수 30 mm 후 클로로피크린 4 g/m2(표준량) + 흑색 필름 14일, 이후 필름 걷고 경운 2회. 28일 실행, 회복 확인은 70일 실행.",
    "기대: 유익·유해가 함께 감소. 질산화균을 거의 다 죽여서 회복이 다조멧보다 늦어요. 가스도 오래 남아요.",
], 70, film="black_film", windows_open=True, chem="chloropicrin", chem_g=4.0, film_ticks=CHEM_FILM_DAYS,
    tills=(CHEM_FILM_DAYS, CHEM_FILM_DAYS + 48))


# ---- S21 소독 후 정식 시기 -----------------------------------------------------------------
def plant_after(s: Sched, plant_tick: int, run_days: int, comment: str, *, irrigate_from: int, irrigate_days: int):
    s.add(plant_tick, "add_fertilizer", "기비 황산암모늄 20 g/m2 (정식 당일)", fertilizer="ammonium_sulfate", g_per_m2=20.0)
    s.add(plant_tick, "plant", comment, seedling_grade="B", plants_per_m2=8.0)
    s.add(irrigate_from + 8, "irrigate", "관수: 소독 끝 다음 날부터 매일 08시 4 mm (정식 여부와 무관하게 같음)",
          repeat=(HOURS, irrigate_days), amount_mm=4.0)


S21_SOLAR_DAYS = 28 + 70
S21_CHEM_FILM_TICKS = 7 * HOURS  # S21 약제 변형은 필름 7일(일본 지침 7~14일의 짧은 쪽): 14일이면 잔류가 다 빠져 정식 시기 차이가 안 보여요
S21_CHEM_DAYS = 7 + 70


def s21(name, comment, *, solar=True, plant_day, chem=None, chem_g=0.0, ph=6.2):
    end = SOLAR_END if solar else S21_CHEM_FILM_TICKS
    days = S21_SOLAR_DAYS if solar else S21_CHEM_DAYS

    def extra(s):
        plant_after(s, end + plant_day * HOURS, days, f"정식 B 8포기/m2 (소독 끝 {plant_day}일 뒤)",
                    irrigate_from=end, irrigate_days=70)

    soil = soil_block(water_mm=30.0, soil_temp=25.0, ph=ph, note="기준 토양 = M3 s5_standard" + (", pH 7.8 변형" if ph > 7 else ""))
    if solar:
        disinfection(name, "S21", comment, days, film="clear_film", extra=extra, soil=soil)
    else:
        disinfection(name, "S21", comment, days, film="black_film", windows_open=True, chem=chem, chem_g=chem_g,
                     film_ticks=end, tills=(end,), extra=extra, soil=soil)


s21("s21_plant_day0", [
    "S21 소독 끝 당일 정식: 태양열 4주 후(tick 672) 필름을 걷고 같은 틱에 Plant B 8 + 황산암모늄 기비 20 g/m2. 98일(28 소독 + 70) 실행.",
    "기대: s21_plant_day42보다 NH4+ 최고치와 지속이 크고, K+NH4 / Ca+Mg 길항이 커서 Ca 충족도가 낮아요.",
], plant_day=0)
s21("s21_plant_day42", [
    "S21 6주 뒤 정식: 태양열 4주 후 필름을 걷고 42일(tick 1680) 뒤 Plant B 8 + 황산암모늄 기비 20 g/m2. 98일(28 소독 + 70) 실행.",
    "기대: 그 사이 질산화균이 회복해서 NH4+가 거의 쌓이지 않고 Ca 충족도가 높아요.",
], plant_day=42)
s21("s21_plant_day0_alkaline", [
    "S21 알칼리 변형: s21_plant_day0과 같고 토양 pH만 7.8. 98일 실행.",
    "기대: 소독 직후 정식 + 알칼리에서만 NO2-가 임계치를 넘어요 (유리 암모니아가 2단계 질산화를 막음, nitrite_threshold 이벤트).",
], plant_day=0, ph=7.8)
s21("s21_dazomet_day0", [
    "S21 다조멧 당일 정식(추가 변형): 다조멧 30 g/m2 + 흑색 필름 7일, 필름 걷고 경운 1회, 같은 틱(tick 168)에 Plant. 77일(7 소독 + 70) 실행.",
    "기대: 잔류가 가장 많이 남은 채 심어서 s21_dazomet_day3·day21보다 뿌리 건강도 손상이 커요 (day3은 잔류가 거의 빠져 차이가 작음).",
], solar=False, plant_day=0, chem="dazomet", chem_g=30.0)
s21("s21_dazomet_day3", [
    "S21 다조멧 3일 뒤 정식: 다조멧 30 g/m2 + 흑색 필름 7일, 필름 걷고 경운 1회, 3일 뒤(tick 240) Plant. 77일(7 소독 + 70) 실행.",
    "기대: s21_dazomet_day21보다 약제 잔류(fumigant_residue_dazomet_index)가 남은 채 심어서 뿌리 건강도 손상(고사 원인 fumigant).",
], solar=False, plant_day=3, chem="dazomet", chem_g=30.0)
s21("s21_dazomet_day21", [
    "S21 다조멧 21일 뒤 정식: 다조멧 30 g/m2 + 흑색 필름 7일, 필름 걷고 경운 1회, 21일 뒤(tick 672) Plant. 77일(7 소독 + 70) 실행.",
    "기대: 잔류가 다 빠진 뒤라 s21_dazomet_day3보다 뿌리 손상이 작아요.",
], solar=False, plant_day=21, chem="dazomet", chem_g=30.0)
s21("s21_chloropicrin_day21", [
    "S21 클로로피크린 21일 뒤 정식: 클로로피크린 4 g/m2 + 흑색 필름 7일, 필름 걷고 경운 1회, 21일 뒤(tick 672) Plant. 77일(7 소독 + 70) 실행.",
    "기대: s21_dazomet_day21보다 질산화균이 거의 죽어 있어 NH4+가 더 오래 남아요(암모늄 함정).",
], solar=False, plant_day=21, chem="chloropicrin", chem_g=4.0)

# ---- S22 시들음병: 오염 토양 -----------------------------------------------------------
SEASON_SOIL = soil_block(water_mm=50.0, soil_temp=20.0, ph=6.2, no3=4.0, labile_N=1.0, stable_N=200.0,
                         note="기준 토양 = M4 season_standard")
S22_DAYS = 120
S22_SOLAR_DAYS = 62 + 120  # 7/10 시작, 9/10(tick 1488) 정식, 정식 뒤 120일


def autumn_management(s: Sched, plant_tick: int, days: int, topdress: bool = True):
    """정식 뒤 관리 (m4 season_standard를 줄인 것): 퇴비 기비, 매일 관수, 9/10~10/31 낮 환기, 2주마다 웃거름"""
    s.add(plant_tick, "add_compost", "기비 숙성 퇴비 2 kg/m2", compost="mature_manure", kg_per_m2=2.0)
    s.add(plant_tick + 8, "irrigate", "관수: 처음 52일은 매일 08시 4 mm", repeat=(HOURS, 52), amount_mm=4.0)
    s.add(plant_tick + 8 + 52 * HOURS, "irrigate", "이후 2.5 mm", repeat=(HOURS, days - 52), amount_mm=2.5)
    s.day_ventilation(plant_tick, 52, 8, 18, "9/10~10/31 매일 8~18시 창 6개 전부 열기, 이후 닫아 둠")
    if topdress:
        n = days // 14
        s.add(plant_tick + 14 * HOURS, "add_fertilizer", "웃거름 질산칼슘 5 g + 황산칼륨 6 g, 2주마다", repeat=(14 * HOURS, n),
              fertilizer="calcium_nitrate", g_per_m2=5.0)
        s.add(plant_tick + 14 * HOURS, "add_fertilizer", None, repeat=(14 * HOURS, n),
              fertilizer="potassium_sulfate", g_per_m2=6.0)


def s22(name, comment, *, ph=6.2, antagonist_product=False):
    s = Sched()
    if antagonist_product:
        s.add(0, "add_inoculant", "길항균 투입 (고초균제 30 g/m2 -> 길항균 지수 +0.6 포화식)", inoculant="bacillus_product", g_per_m2=30.0)
    autumn_management(s, 0, S22_DAYS)
    soil = soil_block(water_mm=50.0, soil_temp=20.0, ph=ph, no3=4.0, labile_N=1.0, stable_N=200.0,
                      note="기준 토양 = M4 season_standard" + (", pH 5.3 변형" if ph < 5.5 else ""))
    add(Scenario(name, S22_DAYS, comment, SEP_10, soil,
                 microbes_block(fusarium=0.5, note="시들음병균 0.5 (오염 토양), 역병균 0"), s,
                 crop=crop_block("B", 8.0, 0.0), group="S22"))


s22("s22_fusarium_none", [
    "S22 시들음병 오염 토양, 처리 없음: 9/10 정식(tick 0, [crop]), 120일 (12월 말까지).",
    "기대: 감염·고사 순서 산성 > 무처리 > 길항균 > 소독.",
])
s22("s22_fusarium_acid", [
    "S22 산성 토양: s22_fusarium_none과 같고 pH 5.3. 120일.",
    "기대: pH 5가 6.5보다 감염이 1.6~1.8배 빨라서(infection_env_curve_ph) 네 변형 중 감염·고사가 가장 커요.",
], ph=5.3)
s22("s22_fusarium_antagonist", [
    "S22 길항균 투입: s22_fusarium_none에 정식 때 고초균제 30 g/m2. 120일.",
    "기대: 길항균 지수가 올라 감염 압력(antagonist_protection_factor)과 병원균 수용력이 줄어서 무처리보다 감염이 느려요.",
], antagonist_product=True)


def s22_solar():
    s = Sched()
    s.add(0, "irrigate", "관수 30 mm", amount_mm=30.0)
    s.add(1, "set_mulch", "투명 필름 4주 (7/10~8/7)", mulch="clear_film")
    s.add(SOLAR_END, "set_mulch", "필름 걷기", mulch="none")
    s.windows(SOLAR_END, 1.0, "창 전부 열기")
    plant_tick = (SEP_10 - JULY_10) * HOURS  # 1488
    s.add(SOLAR_END + 8, "irrigate", "소독 후 정식 전까지 매일 08시 4 mm (흙이 마르지 않게)",
          repeat=(HOURS, (plant_tick - SOLAR_END) // HOURS - 1), amount_mm=4.0)
    s.add(plant_tick, "plant", "9/10 정식 B 8포기/m2 (다른 S22 변형과 같은 날짜)", seedling_grade="B", plants_per_m2=8.0)
    autumn_management(s, plant_tick, S22_DAYS)
    add(Scenario("s22_fusarium_solar", S22_SOLAR_DAYS, [
        "S22 7~8월 태양열 소독 후 정식: 7/10 시작, 투명 필름 4주 소독, 9/10(tick 1488) Plant. 182일(62 + 정식 뒤 120일) 실행.",
        "기대: 소독으로 시들음병균이 줄어서 네 변형 중 정식 뒤 감염·고사가 가장 작아요. 다른 변형과 x축을 맞추려면 앞 1488틱을 잘라요(tools/run_scenarios.py가 해요).",
    ], JULY_10, SEASON_SOIL.replace("water_mm = 50.0", "water_mm = 30.0"),
        microbes_block(fusarium=0.5, note="시들음병균 0.5 (오염 토양), 역병균 0"), s, group="S22"))


s22_solar()

# ---- S23 역병: 과습·지온 --------------------------------------------------------------------
S23_DAYS = 28


def s23(name, comment, *, start, soil_temp, waterlogged: bool, heater: float | None = None):
    s = Sched()
    if heater is not None:
        s.add(0, "set_heater", f"난방 설정온도 {heater} 도 (지온을 목표 범위에 맞추려고)", on=True, setpoint_degC=float(heater))
    s.add(0, "add_fertilizer", "질산칼슘 15 g N/m2 (96.8 g/m2, S13과 같음)", fertilizer="calcium_nitrate", g_per_m2=96.8)
    if waterlogged:
        s.add(8, "irrigate", "과관수: 매일 08시 25 mm (M2 S8 과습 설정)", repeat=(HOURS, S23_DAYS), amount_mm=25.0)
    else:
        s.add(8, "irrigate", "적정 관수: 매일 08시 6 mm", repeat=(HOURS, S23_DAYS), amount_mm=6.0)
    soil = soil_block(water_mm=50.0, soil_temp=soil_temp, ph=6.5, compaction=0.8 if waterlogged else 0.1,
                      note="기준 토양 = M3 s5_standard, " + ("다짐 0.8 (S8 과습)" if waterlogged else "다짐 0.1 (S8 적정)"))
    add(Scenario(name, S23_DAYS, comment, start, soil,
                 microbes_block(phytophthora=0.5, note="역병균 0.5 (오염 토양), 시들음병균 0"), s,
                 crop=crop_block("B", 8.0, 0.0), group="S23",
                 note="창 닫음(시작 상태)" + (f", 난방 {heater} 도" if heater is not None else "")))


s23("s23_proper", [
    "S23 적정 관수: 10/21 시작, 6 mm/일, 다짐 0.1, 난방 20 도(지온 약 20~23 도), 역병균 0.5. 28일. 창은 닫아 둠(시작 상태).",
    "기대: 통기가 좋아서 역병 감염이 가장 작아요 (과습 변형 세 개보다 작음).",
], start=OCT_21, soil_temp=20.0, waterlogged=False, heater=20.0)
s23("s23_waterlogged_mild", [
    "S23 과습·온화: 10/21 시작, 매일 25 mm 과관수, 다짐 0.8, 난방 20 도(지온 약 20~23 도). 28일. 창은 닫아 둠.",
    "기대: 과습 변형 중 역병 감염이 가장 커요 (역병 적온 20~25 도 + 통기 부족).",
], start=OCT_21, soil_temp=20.0, waterlogged=True, heater=20.0)
s23("s23_waterlogged_hot", [
    "S23 과습·고온: 9/1 시작, 매일 25 mm 과관수, 난방 없음, 지온 시작 28 도 (밀폐 하우스 일사로 첫 2주 약 30~33 도, 28일째 약 26 도). 28일. 창은 닫아 둠.",
    "기대: 역병 적온(20~25 도)보다 더워서 과습·온화보다 감염이 작아요 (그래도 적정 관수보다는 커요).",
], start=SEP_1, soil_temp=28.0, waterlogged=True)
s23("s23_waterlogged_cold", [
    "S23 과습·저온: 1/10 시작, 매일 25 mm 과관수, 난방 12 도(지온 약 12~13 도, 난방 없으면 4 도까지 내려가서 넣음). 28일. 창은 닫아 둠.",
    "기대: 역병 적온보다 서늘해서 과습·온화보다 감염이 작아요 (그래도 적정 관수보다는 커요).",
], start=JAN_10, soil_temp=12.0, waterlogged=True, heater=12.0)

# ---- S24 퇴비와 길항균 ------------------------------------------------------------------------
S24_DAYS = 60


def s24(name, comment, *, compost: bool):
    s = Sched()
    if compost:
        s.add(0, "add_compost", "숙성 퇴비 4 kg/m2 (이분해성 C 공급 -> 길항균 수용력 상승)", compost="mature_manure", kg_per_m2=4.0)
    s.add(8, "irrigate", "매일 08시 4 mm", repeat=(HOURS, S24_DAYS), amount_mm=4.0)
    s.day_ventilation(0, 52, 8, 18, "9/10~10/31 매일 8~18시 창 열기, 이후 닫음")
    soil = soil_block(water_mm=50.0, soil_temp=20.0, ph=6.2, no3=4.0, labile_C=5.0, labile_N=0.5, stable_N=200.0,
                      note="기준 토양 = M4 season_standard에서 이분해성 C·N만 줄임(퇴비 없이 오래 재배한 흙)")
    add(Scenario(name, S24_DAYS, comment, SEP_10, soil,
                 microbes_block(antagonist=0.3, fusarium=0.3,
                                note="길항균 0.3(먹이 부족한 흙), 시들음병균 0.3, 역병균 0"), s,
                 crop=crop_block("B", 8.0, 0.0), group="S24"))


s24("s24_no_compost", [
    "S24 퇴비 없음: 9/10 시작, 이분해성 C 5 g/m2의 지친 흙, 시들음병균 0.3, [crop] B 8 (tick 0 정식), 60일.",
    "기대: 길항균 먹이(이분해성 C)가 적어 길항균 수용력이 낮고 시들음병균이 더 늘어요 (s24_compost보다).",
], compost=False)
s24("s24_compost", [
    "S24 퇴비: s24_no_compost에 숙성 퇴비 4 kg/m2를 tick 0에 넣음, 60일.",
    "기대: 길항균 지수가 높고 시들음병균 증식이 느려요 (s24_no_compost보다).",
], compost=True)

# ---- S25 균근균 ------------------------------------------------------------------------------
S25_DAYS = 42


def s25(name, comment, *, variant: str, inoculate: bool):
    s = Sched()
    s.add(0, "add_fertilizer", "질산칼슘 30 g/m2 (S10 표준 시비)", fertilizer="calcium_nitrate", g_per_m2=30.0)
    s.add(0, "add_fertilizer", "황산칼륨 20 g/m2", fertilizer="potassium_sulfate", g_per_m2=20.0)
    if inoculate:
        s.add(0, "add_inoculant", "균근균제 30 g/m2 (균근균 지수 0.1 -> 약 0.64)", inoculant="mycorrhiza_product", g_per_m2=30.0)
    if variant == "dry":
        s.add(8, "irrigate", "S10 dry와 같게: 관수는 처음 3일만 6 mm", repeat=(HOURS, 3), amount_mm=6.0)
    else:
        s.add(8, "irrigate", "매일 08시 6 mm (S10 표준)", repeat=(HOURS, S25_DAYS), amount_mm=6.0)
    s.day_ventilation(0, S25_DAYS, 10, 16, "낮 환기: 매일 10~16시 창 6개 전부 열기 (M2 S10 이행 값)")
    p = 100.0 if variant == "highp" else 25.0
    soil = soil_block(water_mm=45.0, soil_temp=20.0, ph=6.2, compaction=0.3, p=p,
                      note="기준 토양 = M3 s5_standard" + (", 유효 인산 100 g/m2 (고인산 변형)" if variant == "highp" else ""))
    add(Scenario(name, S25_DAYS, comment, SEP_1, soil, microbes_block(), s,
                 crop=crop_block("B", 8.0, 1.0), group="S25"))


s25("s25_standard_none", ["S25 표준 수분·표준 인산, 균근균 무투입 (기준), 42일."], variant="std", inoculate=False)
s25("s25_standard_inoc", ["S25 표준 수분·표준 인산, 균근균제 30 g/m2 투입, 42일.", "기대: 균근균 지수가 s25_standard_none보다 높게 정착해요."],
    variant="std", inoculate=True)
s25("s25_dry_none", ["S25 건조(S10 dry 조건), 균근균 무투입, 42일."], variant="dry", inoculate=False)
s25("s25_dry_inoc", [
    "S25 건조(S10 dry 조건), 균근균제 30 g/m2 투입, 42일.",
    "기대: 균근균이 수분 스트레스 결손을 덜어(mycorrhiza_water_relief_factor) s25_dry_none보다 건조 손해가 작아요.",
], variant="dry", inoculate=True)
s25("s25_highp_none", ["S25 고인산(유효 P 100 g/m2), 균근균 무투입, 42일."], variant="highp", inoculate=False)
s25("s25_highp_inoc", [
    "S25 고인산(유효 P 100 g/m2), 균근균제 30 g/m2 투입, 42일.",
    "기대: 균근균 수용력이 0.1로 낮아서(mycorrhiza_p_curve) 투입해도 정착 지수가 s25_standard_inoc보다 낮고 점점 줄어요.",
], variant="highp", inoculate=True)

# ---- S33 필름 종류 비교 ------------------------------------------------------------------------
FILMS = [("clear", "clear_film", "투명"), ("black", "black_film", "흑색"), ("white", "white_film", "백색"), ("none", None, "필름 없음")]
for key, film, label in FILMS:
    disinfection(f"s33_disinfect_{key}", "S33", [
        f"S33 소독 단계 필름 비교 ({label}): s19_solar_4w와 같은 조건(관수 30 mm, 창 닫음, 28일)에서 필름 종류만 바꿈.",
        "기대: 지온·병원균 사멸 투명 > 흑색 > 없음 > 백색. 모든 필름에서 증발이 줄어요 (없음은 제외).",
    ], 28, film=film)

S33_DAYS = 60


def s33_planted(key, film, label):
    s = Sched()
    s.add(0, "add_compost", "기비 숙성 퇴비 2 kg/m2", compost="mature_manure", kg_per_m2=2.0)
    if film:
        s.add(0, "set_mulch", f"{label} 필름 멀칭 (정식 후)", mulch=film)
    s.add(8, "irrigate", "매일 08시 3 mm", repeat=(HOURS, S33_DAYS), amount_mm=3.0)
    add(Scenario(f"s33_planted_{key}", S33_DAYS, [
        f"S33 정식 후 필름 비교 ({label}): 11/15 시작, [crop] B 8 (활착 1.0), 창 닫음, 난방 없음, 60일.",
        "기대: 지온 투명 > 흑색 > 없음 > 백색, 증발 감소(없음 제외). 백색은 반사광으로 총생산(harvest_fresh_g_per_m2, last_gross_g_per_m2)이 조금 커요.",
    ], NOV_15, SEASON_SOIL, microbes_block(), s, crop=crop_block("B", 8.0, 1.0), group="S33"))


for key, film, label in FILMS:
    s33_planted(key, film, label)


# --------------------------------------------------------------------------- 출력
def readme() -> str:
    rows = "\n".join(f"| `{sc.name}` | {sc.group} | {sc.days} | {sc.days * HOURS} | {'tick 0 [crop]' if sc.crop else ('Plant 명령' if any('\"plant\"' in i for i in sc.sched.items) else '작물 없음')} |"
                     for sc in SCENARIOS)
    return f"""# M5a 시나리오 (S19~S25, S33)

`tools/gen_m5a_scenarios.py`가 만들어요 (시드 {SEED}, 값은 전부 게임용 임의값). 시나리오마다 맨 위 주석에 비교와 기대 방향이 있어요.
기본 소독 틀은 [disinfection 명세](../../../docs/spec/disinfection.md) "시나리오 테스트", 비교는 [m5-overview 9장](../../../docs/spec/m5-overview.md)이에요.
실행: `python tools/run_scenarios.py --cli <sim-cli> --groups M5a-S19 ...` (아래 "실행 일수"가 그룹별 --ticks).

## 실행 일수

| 시나리오 | 그룹 | 일수 | 틱 | 작물 |
| --- | --- | --- | --- | --- |
{rows}

## 정한 점 (명세에 정확한 값이 없어서)

- 소독 틱 틀: tick 0 관수 30 mm, tick 1 필름·약제, 소독 끝(태양열 4주 = tick 672, 약제 2주 = tick 336)에 필름을 걷고 창을 전부 열어요. 28일만 돌리면 끝 명령은 실행되지 않아요(S19 기간 비교 때문에 파일에는 넣어 둠). 70일 변형(S20)은 끝 명령 뒤 회복을 봐요.
- 시작 수분은 소독 시나리오에서 30 mm(상대수분 0.5)로 잡아 `관수 없음`(s19_solar_dry)이 마른 흙이 되게 했어요. 관수하면 포장용수량(60 mm) 근처예요.
- 약제 소독(S20·S21)은 가열 영향을 줄이려고 흑색 필름 + 창 열기, 14일 후 필름 걷고 경운 2회(0일, 2일 뒤)예요. 태양열은 투명 필름 + 창 닫음 4주예요. 명세 틀은 "tick 1 clear_film, 창 닫음"이라 약제 쪽만 벗어났어요.
- S21 일수는 정식 후 70일을 기준으로 소독 일수를 더했어요(태양열 28 + 70 = 98일, 약제 7 + 70 = 77일). S21 약제 변형은 필름 7일·경운 1회예요(S20은 14일·경운 2회): 14일 필름 + 경운 2회면 잔류가 정식 전에 다 빠져서 3일 뒤 정식과 21일 뒤 정식의 약제 손상 차이가 안 보였어요. "정식 N일 뒤"는 소독 끝(필름 걷은 날)부터 N일이에요.
- S22 소독 변형은 7/10에 시작해 9/10(tick 1488)에 정식해서 182일이에요. x축을 맞추려면 앞 1488틱을 잘라 비교해요.
- S23은 모든 변형에서 창을 닫아 두고(시작 상태), 지온은 난방으로 맞춰요: 적정·온화 난방 20 도(지온 20~23 도), 저온 난방 12 도(지온 12~13 도), 고온은 난방 없이 9/1 밀폐(지온 첫 2주 30~33 도, 28일째 약 26 도). 난방 없으면 10/21 28일에 지온이 12 도까지, 1/10에는 4 도까지 내려가서 명세의 지온 범위(20~23 / 12 도)를 못 맞춰요. 지온 범위는 첫 실행으로 확인한 값이에요.
- S24 "퇴비 없음"은 이분해성 C·N을 5·0.5 g/m2로 낮춘 흙이에요(길항균 먹이 부족). 길항균·시들음병균 시작값 0.3.
- S33 정식 후는 11/15 시작 60일(11~1월), 활착 1.0, 난방 없음이에요.
"""


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    names = set()
    for sc in SCENARIOS:
        assert sc.name not in names, sc.name
        names.add(sc.name)
        (OUT / f"{sc.name}.toml").write_text(render(sc), encoding="utf-8", newline="\n")
    (OUT / "README.md").write_text(readme(), encoding="utf-8", newline="\n")
    print(f"wrote {len(SCENARIOS)} scenarios to {OUT}")
    for sc in SCENARIOS:
        print(f"  {sc.group:4s} {sc.name:28s} {sc.days:4d} days")


if __name__ == "__main__":
    main()
