"""M5b 4차: data/params.toml에 지상부 병해충 키(foliar-disease 3장)를 한 번에 추가해요.

멱등: 이미 [foliar_disease]가 있으면 아무것도 하지 않아요. UTF-8 (BOM 없음), LF.
사용: python tools/add_params_m5b_pass4.py [data/params.toml]
"""
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "data/params.toml"
s = open(path, encoding="utf-8").read()
if "[foliar_disease]" in s:
    print("이미 적용됨:", path)
    sys.exit(0)


def rep(old, new):
    global s
    assert old in s, old
    s = s.replace(old, new, 1)


# [root_damage]
rep("fumigant_curve = [[0, 0], [0.03, 0]",
    "anthracnose_curve = [[0, 0], [0.15, 0.0003], [0.4, 0.003], [0.8, 0.012]]  # M5b: x = 탄저병 감염 지수, y = 시간당 손상 (시들음병·역병 곡선 사이). 게임용 임의값 (foliar-disease 3.2)\n"
    "fumigant_curve = [[0, 0], [0.03, 0]")

# microbes 주석
rep("fusarium, phytophthora\n# competition", "fusarium, phytophthora, anthracnose\n# competition")
rep("병원균(fusarium, phytophthora)만", "병원균(fusarium, phytophthora, anthracnose)만")

# 길항균 competition
rep("competition = { fusarium = 0.3, phytophthora = 0.3 }",
    "competition = { fusarium = 0.3, phytophthora = 0.3, anthracnose = 0.3 }")

# [microbes.anthracnose]
anth = """
[microbes.anthracnose]  # 탄저병균 (흙·병든 잔재의 접종원. foliar-disease 3.2, 무리 순서 7)
growth_rate_per_hour = 0.003  # 게임용 임의값
temp_curve = [[9, 0], [20, 0.6], [27, 1], [32, 1], [38, 0]]    # 감염 온도 곡선과 같은 범위 [R308 중간]
moisture_curve = [[0, 1], [14, 1]]                             # 쓰지 않는 축 (항등)
ph_curve = [[0, 1], [14, 1]]                                   # 쓰지 않는 축 (항등)
o2_curve = [[0, 1], [14, 1]]                                   # 쓰지 않는 축 (항등)
base_death_rate_per_hour = 0.002  # 95% 감소에 약 60일. 젖은 흙 4~10일·마른 흙 73~125일 [R311], 묻은 관부 56~98일 [R312]의 사이 (수분 의존은 넣지 않음)
heat_kill_curve = [[37, 0], [40, 0.017], [43, 0.07], [45, 0.09], [50, 0.25], [55, 0.6]]  # 시들음병균과 같은 곡선. 태양열 4주면 묻은 감염 과실에서 박멸 [R311]. 온도·시간 곡선은 출처 없음 (게임용)
immigration_rate_per_hour = 0.0                                # 병원균 공통 규칙: 유입 0
saprophytic_capacity_index = 0.05  # 부생 수용력. 숙주 없이는 거의 못 버팀 [R311][R312]
host_capacity_index = 0.8  # 숙주(감염 지수 1)가 더하는 수용력. 게임용 임의값
competition = { antagonist = 0.4 }  # 길항균이 병원균 자리를 크게 차지 (M5a 병원균과 같은 값)
"""
i = s.index("# 작물 감염 [soil_disease.<id>]")
s = s[:i].rstrip("\n") + "\n" + anth + "\n" + s[i:]

# 모종 등급
g = {"A": (0, 0.05, 0, 0.01, 0), "B": (0.05, 0.05, 0.05, 0.01, 0.005), "C": (0.25, 0.05, 0.15, 0.01, 0.02)}
for k, v in g.items():
    m = re.search(r'(id = "%s"\nleaf_g_per_plant.*?initial_induction_index = [^\n]*\n)' % k, s, re.S)
    assert m, k
    add = (
        f"latent_anthracnose_fraction = {v[0]}  # M5b: 잠복 감염 포기 비율. 무병묘 0%, 농가 관행묘 5~60% [A4], 대만 모주 평균 20% [R316]\n"
        f"latent_anthracnose_index = {v[1]}  # 잠복 감염 포기의 시작 감염 지수 (발병 기준 0.15 아래). 게임용 임의값\n"
        f"spider_mite_fraction = {v[2]}  # 응애를 달고 온 포기 비율. 게임용 임의값 (모종 유입 비율 실측 없음)\n"
        f"spider_mite_start_index = {v[3]}  # 그 포기의 시작 응애 지수 (소엽당 1마리). 게임용 임의값\n"
        f"powdery_mildew_start_index = {v[4]}  # 모든 포기의 시작 병든 잎 면적. 게임용 임의값 (감염 모종이 주 접종원 [R424])\n"
    )
    s = s[: m.end()] + add + s[m.end():]

# 멀칭
for mid in ("clear_film", "black_film", "white_film"):
    m = re.search(r'id = "%s"\n.*?canopy_reflect_factor = [^\n]*\n' % mid, s, re.S)
    assert m, mid
    add = (
        "soil_splash_factor = 0.1  # M5b: 흙·잔재 접종원이 포기에 닿는 비율 (맨흙 = 1). 게임용 임의값, 필름이 흙을 덮음\n"
        "plant_splash_factor = 2.5  # M5b: 병든 포기 포자가 물방울로 다른 포기에 튀는 비율 (맨흙 = 1). 플라스틱 위 발병이 맨흙의 2.3~2.7배 [S301]~[S303][R302][R303] 중간 (구멍 뚫린 이랑 필름 직접 측정 없음)\n"
    )
    s = s[: m.end()] + add + s[m.end():]

# 소독 약제
rep("fusarium = 0.02, phytophthora = 0.02 }", "fusarium = 0.02, phytophthora = 0.02, anthracnose = 0.02 }")
rep("fusarium = 0.035, phytophthora = 0.03 }", "fusarium = 0.035, phytophthora = 0.03, anthracnose = 0.035 }")

fol = """[foliar_disease]  # docs/spec/foliar-disease.md 3장 (M5b). 출처 없는 값은 게임용 임의값, 속도 값은 프로토타입에서 7장 목표값에 맞춰 조정
n_susceptibility_curve = [[0, 1], [6, 1], [12, 1.4], [20, 1.8], [30, 2]]  # x = 토양 무기 N 이동 평균 g/m2, y = 감염 배율. 방향만 근거 [A9][G3], 크기는 게임용 임의값
n_mean_relax_rate_per_hour = 0.003  # 토양 무기 N 이동 평균 완화 비율 (약 2주, 2026-11-19 디렉터 결정)
mite_mean_relax_rate_per_hour = 0.003  # 응애 밀도 이동 평균 완화 비율 (약 2주). 게임용 임의값

[foliar_disease.anthracnose]  # 탄저병 (foliar-disease 3.2)
temp_curve = [[9, 0], [15, 0.16], [20, 0.58], [25, 0.95], [27, 1], [30, 0.91], [35, 0.34], [38, 0]]  # 최저 9, 최적 26.7, 최고 38 [R308] 중간 (C. gloeosporioides 종합, 딸기 관부 전용 곡선 없음)
wetness_curve = [[0, 0], [6, 0.05], [13, 0.8], [36, 1]]  # x = 연속 젖음 h. 최적 온도에서 젖음 13 h에 과실 감염 80% 초과 [R301] 중간
contact_rate_per_hour = 0.0005  # 흙 접종원 1·온도 1·젖음 1일 때 흙에서 올라오는 감염. 게임용 임의값
progress_rate_per_hour = 0.004  # 온도 1일 때 감염된 관부 안의 진행. 게임용 임의값 (잠복 묘의 수확 끝 고사 20~32% [R309]에 맞춤)
splash_transfer_index = 0.01  # 두상 관수 1회가 압력 1에서 옮기는 감염. 게임용 임의값
sporulation_curve = [[0, 0], [0.15, 0], [0.3, 0.5], [0.6, 1]]  # x = 감염 지수, y = 그 포기가 내는 포자. 게임용 임의값 (증상이 보인 뒤부터)
onset_infection_index = 0.15  # DiseaseOnset 기준 (M5a 시들음병과 같은 값)

[foliar_disease.botrytis]  # 잿빛곰팡이병 (foliar-disease 3.3)
infection_temp_curve = [[5, 0.02], [10, 0.08], [15, 0.55], [20, 1], [25, 0.79], [30, 0.11], [33, 0]]  # Bulger 1987 과실 감염 식 (젖음 32 h 값을 20 도로 나눔) 높음. 10 도는 식의 외삽
wetness_curve = [[0, 0], [6, 0.05], [12, 0.15], [16, 0.25], [24, 0.58], [32, 1]]  # x = 연속 젖음 h. 같은 식의 20 도 과실 감염에서 기준선을 뺀 값 높음. 경보 시스템 [R402][R415], 일본 오이타현 기준 [S110]과 같은 방향
infection_rate_per_hour = 0.03  # 포자 1·온도 1·젖음 1일 때 감염 압력 증가. 게임용 임의값
infection_decay_rate_per_hour = 0.002  # 감염 압력 감쇠. 게임용 임의값 (꽃 감염에서 과실 증상까지 2~4주 [R414][R404])
sporulation_temp_curve = [[5, 0.1], [10, 0.4], [15, 0.8], [17.5, 1], [22, 0.8], [25, 0.1], [30, 0]]  # 17~18 도 최대, 25 도 거의 없음, 30 도 없음 [R401] 중간. 사이 점은 임의값
spore_source_per_infection_per_hour = 0.02  # 감염 압력 1인 포기가 온도 1·젖은 상태에서 내는 포자. 게임용 임의값
spore_source_per_rotten_fruit_per_hour = 0.01  # 이번 틱 썩은 과실 1개/m2가 더하는 포자 (2차 전염원). 게임용 임의값
spore_decay_rate_per_hour = 0.02  # 포자 압력 자연 감쇠. 게임용 임의값, 실측 없음
spore_vent_fraction = 0.05  # 환기 횟수 1당 빠지는 비율 (min(1, 값 x ach)). 게임용 임의값, 실측 없음
fruit_rot_prob_per_hour = 0.002  # 감염 압력 1·단계 배율 1일 때 과실 1개가 그 틱에 썩을 확률. 게임용 임의값 (B4에 맞춤)
fruit_stage_curve = [[0, 1], [0.15, 0.6], [0.4, 0.2], [0.8, 0.6], [1, 1]]  # x = 과실 발달 0~1 (0 = 개화), y = 썩음 배율. 꽃과 익은 과실이 풋과실보다 잘 걸림 [G1][R406]
onset_infection_index = 0.2  # DiseaseOnset 기준. 게임용 임의값

[foliar_disease.powdery_mildew]  # 흰가루병 (foliar-disease 3.4)
temp_curve = [[5, 0.05], [10, 0.3], [15, 0.7], [20, 1], [25, 1], [30, 0.4], [32, 0]]  # 발아 최적 약 20 도, 병반 확대 최대 25 도 [R418], 15~25 도가 좋음 [R419] 중간
rh_curve = [[0.6, 0], [0.75, 1], [0.95, 1], [1, 0.7]]  # x = canopy_rh_index. 60% 초과에서 발아 [P1], 75~98% 최적, 95% 이상은 포자 생산 감소 [R419]
free_water_inhibition_factor = 0.7  # 잎 젖음 1일 때 감염 감소 비율. 방향은 근거 있음 [P3], 크기는 출처 없음 (게임용)
light_curve = [[0, 1.3], [60, 1.15], [120, 1]]  # x = 하우스 안 일사 하루 이동 평균 W/m2. 게임용 가정 (약광의 정량 근거 없음, 최대 1.3배, 디렉터 결정 5)
density_curve = [[0, 0.8], [2, 1], [4, 1.2]]  # x = LAI. 게임용 가정
young_leaf_curve = [[0, 0.3], [0.01, 0.6], [0.03, 1]]  # x = rgr_mean_per_day. 전개 후 10~15일 잎이 가장 잘 걸림 [R424][R420] 중간 (RGR 대용은 게임용 근사)
infection_rate_per_hour = 0.004  # 모든 배율 1일 때 병든 잎 면적 증가. 게임용 임의값 (잠복 4~7일 [R419][R424])
spore_source_per_infection_per_hour = 0.02  # 병든 잎 면적 1인 포기가 내는 포자. 게임용 임의값
spore_decay_rate_per_hour = 0.005  # 포자 압력 감쇠. 게임용 임의값 (병든 잎의 포자는 오래 감 [R419])
spore_vent_fraction = 0.05  # 환기 횟수 1당 빠지는 비율. 게임용 임의값
photosynthesis_damage_curve = [[0, 0], [0.17, 0.05], [0.5, 0.2], [1, 0.5]]  # x = 병든 잎 면적, y = 총생산 감소 비율. 잎 병반 17%에서 수량 손실 5% [P2][R422] 중간, 그 위는 임의값
fruit_prob_curve = [[0, 0], [0.1, 0.02], [0.3, 0.08], [0.6, 0.17]]  # x = 병든 잎 면적, y = 흰가루 과실 확률. 감수성 품종 터널 과실 6~17%, 저항성 0.4~2.3% [R207] 높음, 설향은 낮은 쪽
onset_infection_index = 0.05  # DiseaseOnset 기준. 경보 문턱 잎 병반 5% [R422]

[foliar_disease.mites]  # 점박이응애·칠레이리응애 (foliar-disease 3.5)
mite_growth_curve_per_hour = [[12, 0], [15, 0.0013], [20, 0.0044], [25, 0.0063], [30, 0.0079], [35, 0.005], [40, 0]]  # x = 하우스 기온, y = 시간당 내적 증가율. 발육 영점 12~14 도, 콩 20/25/30 도 [R503] 중간. 딸기 25 도 약 0.15 [M2]에 맞춰 줄임 (비율은 게임용)
mite_rh_curve = [[0.2, 0.8], [0.4, 1.15], [0.55, 1.15], [0.7, 1], [0.85, 0.9], [1, 0.8]]  # x = house_rh_mean_index, y = 증가율 배율. 근거 엇갈림 [S407][S408][S409] 대 [R502]. 디렉터 결정 6: 약한 곡선, 크기는 게임용 임의값
mite_spread_rate_per_hour = 0.0005  # 하우스 평균 쪽으로 옮겨 가는 비율. 게임용 임의값, 확산 속도 실측 없음
photosynthesis_damage_curve = [[0, 0], [0.05, 0.1], [0.3, 0.4], [1, 0.6]]  # x = spider_mite_mean_index (2주 이동 평균), y = 총생산 감소 비율. 누적 응애-일 15·50 /cm2에서 약 -40%·-60% [S410][R519] 낮음, 지수 환산은 게임용 (소엽 30 cm2 가정)
predator_temp_curve = [[10, 0], [17, 0.7], [20, 0.9], [25, 1], [28, 1], [30, 0.7], [35, 0]]  # x = 하우스 기온, y = 포식·번식 0~1. 적온 17~28 도 [R505], 35 도 이상 성충 못 됨 [M7] 중간
predator_hatch_curve = [[0.4, 0], [0.5, 0.15], [0.6, 0.45], [0.7, 0.75], [0.8, 0.95], [1, 1]]  # x = house_rh_mean_index (약 3일 평균), y = 알 부화율. 부화 50% 습도 약 70% [S415], 상시 60%면 3~6% [S417] (일정 습도 실험). 3일 평균 입력이라 실험값보다 너그럽게 (평균 0.62에서 50%, 게임용 근사)
attack_rate_per_hour = 5.0  # 포식 공격률 (Holling II). 게임용 임의값 (반포화 밀도 소엽당 약 15마리 = 지수 0.15)
handling_time_hours = 1.25  # 처리 시간. 성충 암컷 하루 12~25마리 [R503][R504], 알 0.49~0.96 h/개 [S420][S421] 평균 1~1.5 h
conversion_factor = 0.01  # 포식량이 천적 증가로 바뀌는 비율. 게임용 임의값 (먹이 충분 시 증가율 하루 약 0.2 [R503])
predator_death_rate_per_hour = 0.0022  # 천적 기본 사망. 먹이가 있을 때 평균 수명 19일 [S416][M6] 중간
predator_starvation_death_per_hour = 0.012  # 먹이가 전혀 없을 때 더해지는 사망. 물만 있으면 6일, 먹이·물 없으면 2~4일 [S416][R507] 중간
mite_onset_index = 0.05  # DiseaseOnset(응애) 기준. 가을 정식 경제적 방제 기준 소엽당 5마리 [M5]

"""
i = s.index("[calendar]")
s = s[:i] + fol + s[i:]

pred = """
# ---- M5b 천적 제품 [[predator]] (foliar-disease 3.5) ----
[[predator]]
id = "persimilis"
name = "칠레이리응애"
index_per_individual_per_m2 = 0.00003  # 게임용 임의값. 소엽 약 300장/m2, 지수 1 = 소엽당 100마리 가정. 국내 방사 기준 약 15마리/m2 [M8] = 지수 0.00045
"""
s = s.rstrip("\n") + "\n" + pred
with open(path, "w", encoding="utf-8", newline="\n") as f:
    f.write(s)
print("적용:", path)
