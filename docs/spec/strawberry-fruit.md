# 딸기 생장 분배, 호흡·낙엽, 개별 과실, 수확, 품질

- 상태: 승인 (2026-11-18, 프로토타입 점검 결과 반영본 최종 승인)
- M5a 사전 작업 반영 (2026-11-19 승인, [m5-overview](m5-overview.md) 13장): 유지 호흡 활성 보정(3번), 저장 탄수화물(4b), 잎·뿌리 호흡 계수. `(M5a 사전 작업)` 표시
- 마일스톤: M4
- 관련: [m4-overview](m4-overview.md), [strawberry-phenology](strawberry-phenology.md), [strawberry-minimal](strawberry-minimal.md), [cation-exchange](cation-exchange.md), [house-climate](house-climate.md), 계획서 5장

## 목적

토양·하우스 관리의 결과가 **수확량과 품질**로 나오게 해요. M4 완료 기준("처방별 수확량·당도 차이가 그래프로 확인됨")의 본체예요.

- 과실이 많이 달리면 잎·관부로 갈 몫이 줄고 과실도 작아져요(과실 부담).
- 칼륨 부족은 당도를, 칼슘 부족·질소 과다는 경도(저장성)를 떨어뜨려요. 질소 과다는 당도도 낮춰요.
- 시원한 기온은 과실을 천천히 키워 크고 달게 해요. 따뜻하면 빨리 익지만 무르고 덜 달아요.
- 겨울 밤에는 호흡으로 건물이 줄어요. 광이 부족한 겨울에 과실을 너무 많이 달면 포기가 지쳐요.
- 개화기 기온이 나쁘면 수정이 덜 돼 과실이 작고 모양이 나빠져요(기형과). **과실마다 크기와 모양이 달라서** 판매 등급(M7)이 크기 분포로 나뉘어요.
- 꽃은 실제처럼 화방당 15~20송이 피지만, 생장량이 모자라면 늦게 핀 어린 과실부터 떨어져요(낙과). 플레이어가 상태를 보고 **꽃·어린 과실을 미리 따 주면(적화·적과)** 남은 과실이 커져요(2026-11-18 디렉터 결정).
- 당도는 낮에 광합성 적온(약 23 °C)에 가까울수록, 밤에 약 8.5 °C로 서늘할수록 높아요(주·야간 이중 가중, 2026-11-18 디렉터 결정).

## 상태 변수

### 작물 (M2 작물에 추가·변경)

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `crown_g_per_m2` (신규) | 관부 건물. 화방 꽃 수를 정하는 세력 | g/m² |
| `leaf_g_per_m2`, `root_g_per_m2` (M2) | 그대로 | g/m² |
| `fruit_g_per_m2` (파생) | 달려 있는 과실 건물 합 | g/m² |
| `fruits` | 개별 과실 목록 (아래). 타일 1칸 = 1 m²라서 개수는 정수예요 | - |
| `harvest` | 누적 수확 기록 (아래) | - |
| `size_satisfaction_mean` (저장 상태, 2026-11-18 프로토타입 점검으로 신설) | 크기 충족도의 이동 평균 (0~1). 당도의 공급 입력과 낙과 확률이 읽어요. 시작값 1 | 0~1 |
| `reserve_g_per_m2` (저장 상태, M5a 사전 작업) | 저장 탄수화물(건물 환산). 양분 없음, 관부·뿌리에 있다고 봐요. 유지 호흡을 받지 않아요(4b번). 시작값 0 | g/m² |
| `rgr_mean_per_day` (저장 상태, M5a 사전 작업) | 식물 상대 생장률(순생장 ÷ 전체 건물)의 이동 평균. 유지 호흡 활성 보정이 읽어요(3번). 시작값 0 | /일 |
| `last_gross_g_per_m2`, `last_respiration_g_per_m2`, `last_supply_ratio` (진단) | 이번 틱 총생산, 유지 호흡, 과실 공급 비율. 결과를 바꾸지 않는 기록 | g/m², g/m², - |

체내 양분(M2·M3)은 포기 전체의 풀이에요. 과실도 같은 풀에서 양분을 써요.

### 개별 과실

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `truss_index`, `rank` | 몇 번째 화방의 몇 번과인지 (과위 0~4: 1번과 ~ 5번과 이후, 2026-11-18 4 → 5단계) | - |
| `anthesis_tick` | 이 꽃이 피는 틱 (화방 개화 + 과위별 지연 + 개체 편차) | 틱 |
| `set` | 개화 판정을 마쳤는지 (`false` = 아직 꽃) | bool |
| `pollination_index` | 수정률 (개화 시 정해짐) | 0~1 |
| `deformed` | 기형과 여부 | bool |
| `potential_fresh_g` | 이 과실이 클 수 있는 최대 생체중 | g |
| `dry_g` | 현재 건물 | g |
| `development_index` | 개화 → 수확 진행도 | 0~1 |
| `brix_factor_sum`, `firmness_factor_sum`, `weight_sum` | 품질 계수(온도 제외)의 적산온도 가중 합 | - |
| `day_score_sum`, `day_weight_sum`, `night_score_sum`, `night_weight_sum` | 당도 온도 점수의 주·야간 가중 합 | - |

### 수확 기록 (작물마다, 누적)

| 이름 | 의미 |
| --- | --- |
| `harvested_fruits`, `harvested_fresh_g` | 정상과 개수, 생체중 |
| `harvested_deformed`, `harvested_deformed_fresh_g` | 기형과 개수, 생체중 |
| `harvested_brix_weighted`, `harvested_firmness_weighted` | 당도·경도 × 생체중 합 (정상과만) |
| `harvested_size_class_counts` | 크기 구간별 정상과 개수 (구간 경계는 데이터) |
| `first_harvest_tick` | 첫 수확 틱 (없으면 미정) |
| `aborted_fruits`, `thinned_fruits` (진단) | 낙과한 과실 수, 적화·적과로 딴 개수 |
| `harvested_day_score_weighted`, `harvested_night_score_weighted`, `harvested_temp_score_weighted` (진단) | 수확 과실의 낮 점수 S̄_day·밤 점수 S̄_night·온도 점수 K_temp × 생체중 합 (정상과만). 작기 평균 온도 점수를 리포트에서 볼 수 있게 |

평균 당도 = 가중 합 ÷ 정상과 생체중. 크기 구간은 판매 등급(M7)의 재료예요. 진단 항목은 결과를 바꾸지 않고 작기 리포트(M7)와 점검용으로만 기록해요([m4-overview](m4-overview.md) "진단·리포트용 기록").

## 파라미터

### 생장·분배·호흡·낙엽 (`[crop]` 추가·변경, 게임용 임의값)

| 키 | 의미 | 초기값 | 비고 |
| --- | --- | --- | --- |
| `vegetative_leaf_fraction`, `vegetative_crown_fraction`, `vegetative_root_fraction` | 과실 몫을 뺀 나머지의 기관 비율 (합 1) | 0.6, 0.15, 0.25 | M2 `leaf_allocation_fraction`(0.7)을 대체 |
| `max_fruit_assimilate_fraction` | 이번 틱 생장량 중 과실이 가져갈 수 있는 최대 비율 | 0.65 | 과실이 가장 강한 수요처 |
| `maintenance_rate_leaf_per_hour`, `…_crown_…`, `…_root_…`, `…_fruit_…` | 25 °C에서 건물 1 g당 유지 호흡 | 0.00125, 0.0004, 0.0004, 0.0004 | 하루 3%, 1%, 1%, 1%. (M5a 사전 작업) 잎은 SUCROS 0.03/일로 복귀(활성 보정과 함께), 뿌리는 TOMSIM 0.010/일·딸기 뿌리 실측 0.3~0.8%/일([references/strawberry-respiration-reserves.md](../../references/strawberry-respiration-reserves.md)) [이전 잎 0.0008 (2026-11-18 점검), 그전 0.00125 / 뿌리 0.0006] |
| `maintenance_temp_curve` | x = 하우스 기온 °C, y = 배율 (Q10 = 2, 25 °C에서 1) | `[[0,0.18],[10,0.35],[20,0.71],[25,1.0],[30,1.41],[40,2.83]]` | |
| `maintenance_activity_curve` (M5a 사전 작업, 신설) | x = `rgr_mean_per_day`, y = 유지 호흡 배율 | `[[-1,0.4],[0.0043,0.4],[0.01,0.7],[0.02,0.91],[0.03,0.97],[0.05,1]]` | 모양은 1 − e^(−c × RGR) (Vanthoor 2011, TOMSIM 토마토 c = 33일). 디렉터 결정으로 근거보다 덜 줄여요(c 약 120일, 하한 0.4, 게임용 임의값) |
| `rgr_mean_relax_rate_per_hour` (M5a 사전 작업, 신설) | RGR 이동 평균의 시간당 완화 비율 | 0.014 | 약 3일. 게임용 임의값 |
| `reserve_capacity_fraction` (M5a 사전 작업, 신설) | 저장 상한 = 이 값 × (관부 + 뿌리 건물). 0이면 기능 꺼짐 | 0.2 | 설향 관부+뿌리 NSC 11~19%(이상우 등 2019, references/strawberry-respiration-reserves.md) |
| `reserve_fill_fraction` (M5a 사전 작업, 신설) | 광합성만으로 과실이 다 채워진 틱에 영양 생장 몫 중 저장으로 가는 비율 | 0.5 | 게임용 임의값 |
| `reserve_release_rate_per_hour` (M5a 사전 작업, 신설) | 과실 공급 부족 시 틱당 저장에서 꺼낼 수 있는 비율 | 0.0005 | 게임용 임의값 |
| `leaf_senescence_rate_per_hour` | 잎이 늙어 떨어지는 시간당 비율 | 0.0003 | 잎 수명 약 80일 (2026-11-18 프로토타입 점검으로 조정: 이전 0.0005) |
| `fruit_target_N_g_per_g`, `…P`, `…K`, `…Ca`, `…Mg`, `…Mn` | 과실 적정 농도 | 0.015, 0.0035, 0.025, 0.0015, 0.0012, 0.00002 | 딸기 과실은 K가 많고 Ca가 적음 (일반 경향) |

관부의 적정 농도는 잎·뿌리와 같은 `target_*`를 써요.

### 과실 (`[fruit]`, 게임용 임의값)

| 키 | 의미 | 초기값 | 비고 |
| --- | --- | --- | --- |
| `rank_fraction` | 꽃 수 중 과위별 비율 (합 1, 5개) | `[0.06, 0.11, 0.22, 0.39, 0.22]` | 화방당 약 1·2·4·7·4개 (18송이 기준). 게임용 임의값 |
| `rank_anthesis_delay_days` | 화방 개화 후 과위별 개화 지연 | `[0, 4, 7, 10, 12]` | 디렉터 지정 (2026-11-18). 한 화방의 꽃이 약 2주에 걸쳐 피어요 |
| `anthesis_jitter_days` | 과실마다 개화 시점 편차 (±, 일) | 4 | 게임용 임의값. 칸 안 포기 사이 차이의 근사. 시드 기반 생물 변이 (2026-11-18 프로토타입 점검으로 조정: 이전 3) |
| `rank_potential_fresh_g` | 과위별 최대 생체중 | `[35, 25, 17, 12, 8]` | 1번과가 가장 큼 |
| `size_variation_fraction` | 개체별 최대 생체중 변이 폭 (±) | 0.1 | 시드 기반 생물 변이 |
| `pollination_mean_curve` | x = 하우스 기온 이동 평균 °C, y = 평균 수정률 | `[[3,0.2],[5,0.3],[8,0.6],[12,0.92],[25,0.92],[30,0.65],[35,0.35]]` | 저온·고온 수정 불량 (2026-11-18 프로토타입 점검으로 조정: 이전 `[[3,0.35],[8,0.65],[12,0.92],[25,0.92],[30,0.65],[35,0.35]]`. 저온 쪽을 더 낮추고 5 °C 점을 추가) |
| `pollination_spread` | 개체별 수정률 변이 폭 (±) | 0.15 | |
| `set_threshold` | 이보다 낮으면 꽃이 떨어짐(착과 실패) | 0.3 | |
| `deformed_threshold` | 이보다 낮으면 기형과 | 0.7 | 씨가 일부만 수정됨 |
| `pollination_size_curve` | x = 수정률, y = 최대 생체중 배율 | `[[0.3,0.4],[0.7,0.8],[0.9,1.0],[1,1.0]]` | 덜 수정되면 작아요 |
| `dry_matter_fraction` | 과실 생체중 중 건물 비율 | 0.09 | |
| `base_temp_degC` | 비대 적산온도 기준 | 5 | |
| `degree_days_to_harvest` | 개화 → 수확 적산온도 | 350 | 가을 약 30일, 겨울 약 60일 (2026-11-18 프로토타입 점검으로 조정: 이전 450) |
| `growth_shape_curve` | x = 진행도, y = 최대 크기 중 도달 비율 (누적, S자) | `[[0,0],[0.2,0.05],[0.5,0.35],[0.8,0.85],[1,1]]` | |
| `size_class_bounds_g` | 크기 구간 경계 (오름차순) | `[10, 15, 20, 25]` | 5개 구간. 판매 등급은 M7 |
| `abortion_rate_per_development_index` | 진행도 1당 낙과 확률 (최대, 곡선·과위 배율 1일 때) | 8.0 | 게임용 임의값. 시간당 `abortion_rate_per_hour`(0.01)를 대체 (2026-11-18 프로토타입 점검 5차로 신설: 겨울에 천천히 크는 과실이 더 많이 떨어지던 문제를 고침, 디렉터 결정 12번) |
| `abortion_window_index` | 이 진행도 미만의 어린 과실만 떨어질 수 있음 | 0.35 | 게임용 임의값 (2026-11-18 프로토타입 점검으로 조정: 이전 0.25) |
| `abortion_satisfaction_curve` | x = 크기 충족도(이동 평균), y = 낙과 확률 배율 | `[[0,1],[0.9,0.6],[1,0.1]]` | 게임용 임의값. 충분히 크고 있어도 조금은 떨어져요 (2026-11-18 프로토타입 점검으로 조정: 이전 `[[0,1],[0.6,0.5],[0.85,0],[1,0]]`) |
| `rank_abortion_weight` | 과위별 낙과 배율 | `[0.1, 0.4, 1.0, 1.4, 1.8]` | 늦게 핀 작은 과위가 잘 떨어짐 |
| `thinnable_max_index` | 적화·적과로 딸 수 있는 최대 진행도 | 0.3 | 꽃과 어린 과실까지 (프로토타입 값과 같음, 확인함) |
| `base_brix` | 모든 계수가 1일 때 당도 | 10.0 | °Brix |
| `brix_supply_curve` | x = **크기 충족도 이동 평균** `size_satisfaction_mean` (과실 건물 ÷ 목표 건물) | `[[0,0.7],[0.5,0.85],[1,1]]` | 곡선은 그대로. 입력을 "틱 공급 비율(공급 ÷ 수요)"에서 크기 충족도로 바꿨어요 (2026-11-18 프로토타입 점검으로 변경: 밤에는 공급이 0이라 비율이 0으로 떨어져 당도를 깎던 문제, 1차 점검) |
| `brix_day_temp_score_curve` | x = 낮 하우스 기온 °C, y = 낮 점수 S_day | 23 °C 중심 가우시안(σ = 5 °C)을 3~43 °C 1 °C 간격 점 41개로 (`tools/gen_brix_curves.py`, 소수 넷째 자리 반올림) | 디렉터 지정 식. 처음 점 `[3, 0.0003]`, 23 °C에서 1 |
| `brix_night_temp_score_curve` | x = 밤 하우스 기온 °C, y = 밤 점수 S_night | 아래 식을 −10~20 °C 1 °C 간격에 꼭짓점 8.5 °C를 더한 점 32개로 (같은 도구, 소수 넷째 자리 반올림) | 디렉터 지정 식. 8.5 °C에서 1, −5 °C 이하 0, 17 °C 이상 0 |
| `brix_day_weight`, `brix_night_weight` | 주·야간 비율 (합 1) | 0.6, 0.4 | 디렉터 지정 (0.55 : 0.45도 가능) |
| `brix_temp_score_curve` | x = 온도 점수 K_temp (0~1), y = 당도 배율 | `[[0,0.75],[1,1.15]]` | 게임용 임의값. 초안의 단일 `brix_temp_curve`(기온 → 배율)는 삭제하고 이 점수 곡선과 위 낮·밤 두 곡선으로 대체 (2026-11-18 프로토타입 점검에서 키 구성 확정) |
| `day_solar_threshold_W_per_m2` | 하우스 안 일사가 이보다 크면 낮 | 0 (0보다 크면 낮) | |
| `brix_k_curve` | x = K 충족도 | `[[0.5,0.75],[0.8,0.9],[1,1]]` | |
| `brix_n_excess_curve` | x = N 과잉 지수 | `[[0,1],[0.3,1],[0.5,0.85]]` | 게임용 임의값 (2026-11-18 프로토타입 점검으로 조정: 이전 `[[0,1],[0.15,0.95],[0.3,0.85]]`.) |
| `brix_water_curve` | x = relative_water | `[[0.4,1.08],[0.7,1.03],[0.9,1.0],[1.2,0.95]]` | 약간 마르면 달고 과습하면 싱거움 |
| `firmness_ca_curve` | x = Ca 충족도, y = 경도 계수 0~1 | `[[0.5,0.6],[0.8,0.85],[1,1]]` | |
| `firmness_temp_curve` | x = 하우스 기온 °C | `[[10,1],[20,0.95],[28,0.75]]` | |
| `firmness_n_excess_curve` | x = N 과잉 지수 | `[[0,1],[0.3,0.85]]` | |

**당도 온도 점수 식** (디렉터 지정, 2026-11-18):

```
K_temp = brix_day_weight · S̄_day + brix_night_weight · S̄_night
S_day(T)   = exp(−(T − 23)² / (2 · 5²))
S_night(T) = exp(−(T − 8.5)² / (2 · 3²))       (T ≤ 8.5 °C, 전류 저하)
           = max(0, 1 − 0.12 · (T − 8.5))       (T > 8.5 °C, 호흡 소모 증가, 약 17 °C 이상 0)
```

- 낮 평균 S̄_day: 낮 시간마다 가중치 = Δt × 하우스 안 일사(W/m²). 해가 강한 시간일수록 많이 반영돼요.
- 밤 평균 S̄_night: 밤 시간마다 가중치 = Δt (순수 시간). 밤 점수 곡선 자체가 "야간 적정 저온" 반영이에요.
- 코어는 exp를 계산하지 않고, 도구 `tools/gen_brix_curves.py`가 위 식으로 만든 구간 선형 곡선을 `data/params.toml`에 써 둬요(결정론, ADR-0004). 낮은 3~43 °C를 1 °C 간격으로, 밤은 −10~20 °C를 1 °C 간격으로 하고 밤에는 꼭짓점 8.5 °C를 더해요. 값은 소수 넷째 자리로 반올림해요. 점 예: 낮 18 °C 0.61, 20 °C 0.84, 23 °C 1.0, 28 °C 0.61. 밤 3 °C 0.19, 5 °C 0.51, 8.5 °C 1.0, 12 °C 0.58, 17 °C 0.

### 생물 변이 난수

수정률과 최대 생체중의 개체 변이, 꽃 수의 소수점 처리, 저온 손실은 **코어 PRNG**(시나리오 시드, ADR-0004)를 써요. 용도별로 스트림을 나눠요(`biology`). 뽑는 순서는 타일 행 우선 → 화방 순 → 과실 생성 순으로 고정이에요. 다른 프로세스가 난수를 더 써도 이 수열은 밀리지 않아요(ADR-0009 공통 규칙 1번과 같은 방식).

## 프로세스 (셀 단계 h, 작물이 살아 있을 때)

### 1. 꽃 만들기 (화방 개화 시, [strawberry-phenology](strawberry-phenology.md) 3번에서 호출)

```
꽃 수 n = floor(flowers_per_m2) + (난수 < 소수부 ? 1 : 0)
과위별 개수 = rank_fraction × n 을 최대 나머지 방식으로 정수 배분
과실마다: anthesis_tick = 지금 + round((rank_anthesis_delay_days[rank] + anthesis_jitter_days × (2u − 1)) × 24), 최소 지금, set = false
         (u: biology 스트림 난수. 2026-11-18 디렉터 결정: 과실별 개화 시점 생물 변이)
```

### 2. 개화 판정 (각 과실의 `anthesis_tick`에)

```
μ = pollination_mean_curve(house_air_temp_mean)
p = clamp(μ + pollination_spread × (2u − 1), 0, 1)        (u: 0~1 난수)
p < set_threshold      → 꽃이 떨어짐 (목록에서 제거, 물질 이동 없음)
p < deformed_threshold → deformed = true
potential_fresh_g = rank_potential_fresh_g[rank] × pollination_size_curve(p) × (1 + size_variation × (2u′ − 1))
```

- 방향: 개화기가 춥거나 더우면 평균 수정률이 떨어져 낙화·기형과가 늘고 과실이 작아져요. 같은 조건에서도 과실마다 크기가 조금씩 달라요.

### 3. 총생산과 유지 호흡 (M2 생장 식 변경)

```
gross = (M2 잠재 생장) × co2_growth_curve(house_co2_ppm) × combined_stress × growth_factor_establish
resp  = Σ 기관 건물 × maintenance_rate_기관 × maintenance_temp_curve(house_air) × maintenance_activity_curve(rgr_mean_per_day)
net   = gross − resp
rgr_tick         = net / (잎 + 관부 + 뿌리 + 과실 + reserve) × 24                       (전체 건물 > 0일 때만)
rgr_mean_per_day += (rgr_tick − rgr_mean_per_day) × min(1, rgr_mean_relax_rate)
```

- (M5a 사전 작업, 2026-11-19 승인) **유지 호흡 활성 보정**: 생장이 느린(겨울) 식물은 유지 호흡이 줄어요(TOMSIM의 RGR 보정). 저온 순화는 근거상 겨울 호흡을 오히려 늘리는 쪽이라 쓰지 않았어요([references/strawberry-respiration-reserves.md](../../references/strawberry-respiration-reserves.md)). 배율은 **이전 틱까지의** `rgr_mean_per_day`로 정하고, 이번 틱 `net`을 구한 직후 이동 평균을 갱신해요(다음 틱이 읽음). 저장 탄수화물(`reserve`)은 대사적으로 비활성이라 호흡 건물에 넣지 않지만 RGR 분모에는 넣어요. 결과: 표준 작기 겨울 유지 호흡이 총생산의 약 0.33~0.36(이전 0.34~0.41), 끄면(배율 1) 누적 수확 6,058 → 5,397 g/m².
- `net < 0`(밤, 겨울): (M5a 사전 작업) 부족분을 먼저 저장 탄수화물에서 꺼내고(4b번), 모자란 몫을 잎·관부·뿌리에서 건물 비율대로 줄여요(과실은 줄이지 않아요). 줄어든 건물의 탄소는 공기로 나가 장부 밖이에요(광합성 탄소처럼). 체내 양분은 그대로 남아요.
- 그래서 M2의 "생장은 0 이상"이 아니라 **순생장이 음수일 수 있어요**(호흡, CO2, 활착이 겹치는 밤·겨울·정식 직후). 이 틱의 `gross`, `resp`, 과실 공급 비율은 진단 기록(`last_gross_g_per_m2`, `last_respiration_g_per_m2`, `last_supply_ratio`)으로 남겨요.
- `gross`에 들어가는 `combined_stress`와 `growth_factor_establish`는 [strawberry-phenology](strawberry-phenology.md) 1번과 [strawberry-minimal](strawberry-minimal.md) 1번이에요. 활착이 덜 된 정식 직후에는 `growth_factor_establish`가 0.3까지 내려가요.

### 4. 분배 (net > 0일 때)

```
새 진행도 D'  = min(1, development + dD)                           (dD는 5번)
목표 건물 i    = potential_fresh_g × dry_matter_fraction × shape(D')
과실 i의 수요  = max(0, 목표 건물 i − dry_g_i)                     ← 따라잡기 수요 (catch-up)
fruit_demand  = Σ (착과된 과실) 수요
fruit_supply  = min(fruit_demand, net × max_fruit_assimilate_fraction)    (net ≤ 0이면 0)
supply_ratio  = fruit_supply / fruit_demand          (수요 0이면 1)
과실 i의 실제 증가 = 수요_i × supply_ratio
나머지(net − fruit_supply)를 잎·관부·뿌리에 vegetative_*_fraction대로
```

- (2026-11-18 프로토타입 점검으로 변경) **수요는 "이번 틱 진행도 증가분 몫"이 아니라 "새 진행도에서의 목표 건물 − 지금 건물"(따라잡기)이에요.** 초안(진행도 증가분 × 최대 크기)은 겨울·밤에 못 채운 몫이 영영 사라져 과실이 작게 끝났어요. 따라잡기식은 모자란 몫을 다음 틱 이후에 다시 요구해서, 광합성이 좋은 낮에 만회해요. 크게 못 채우면 진행도가 1이 돼도 목표보다 작은 채 수확돼요.
- 방향: 과실이 많이 달려 수요가 크면 과실 하나하나가 작아지고, 잎·관부 생장도 줄어 다음 화방 꽃 수가 줄어요.
- (M5a 사전 작업) 위 `fruit_supply`는 광합성 몫(`assimilate_supply`)이고, 모자라면 저장에서 더해요(4b번). 영양 기관 나머지는 `net − assimilate_supply`(저장 꺼낸 몫은 빼지 않음)이고 그중 일부가 저장으로 가요.

### 4b. 저장 탄수화물 (M5a 사전 작업, 2026-11-19 승인)

12~2월은 광합성 공급이 과실 수요에 크게 못 미쳐 어린 과실이 많이 떨어지고, 밤 호흡이 잎·관부·뿌리 구조를 바로 깎았어요. 실제 딸기는 관부·뿌리에 저장한 탄수화물(NSC)로 겨울을 버텨요(설향 관부+뿌리 NSC는 건물의 11~19%, 1~2월에 절반쯤 줄고 3~4월에 회복, [references/strawberry-respiration-reserves.md](../../references/strawberry-respiration-reserves.md)).

```
채우기 (net > 0, 광합성만으로 과실 수요를 다 채운 틱 또는 과실 수요 0):
    capacity = reserve_capacity_fraction × (crown + root)
    fill     = min(rest × reserve_fill_fraction, max(0, capacity − reserve))      (rest = net − assimilate_supply)
    reserve += fill,  rest −= fill  → 나머지를 vegetative_*_fraction대로
쓰기 1 (net < 0):  from_reserve = min(reserve, −net), 남은 부족분만 잎·관부·뿌리에서 건물 비율대로
쓰기 2 (net > 0, assimilate_supply < fruit_demand):
    release = min(fruit_demand − assimilate_supply, reserve × min(1, reserve_release_rate), reserve)
    fruit_supply = assimilate_supply + release     (저장 → 과실 내부 이동, CO2 수지는 그대로)
잃기:  뿌리 고사 때 reserve × min(1, 죽은 뿌리 / (관부 + 뿌리))를, 포기 사망 때 전부를
       토양 이분해성 C로 (C = 건물 × dry_matter_C_fraction, 장부 C_root_litter)
```

- 저장에는 양분이 없어요(양분 반환 share에 넣지 않아요). 유지 호흡도 받지 않아요(SUCROS: 저장 탄수화물은 비활성).
- 결과(표준 작기): 저장은 가을에 차고, 11월 밤 부족분을 덮고, 12~4월은 거의 0이에요(밤 부족분은 구조에서 — 실측 2월 뿌리 건물 감소와 같은 방향).

### 5. 과실 진행과 품질 적산 (착과된 과실마다)

```
dD = max(0, house_air − base_temp) / 24 / degree_days_to_harvest
development += dD
q_brix = brix_supply_curve(size_satisfaction_mean) × brix_k_curve(suff_K)
       × brix_n_excess_curve(excess_N) × brix_water_curve(relative_water)
낮(inside_solar > 0): day_sum += S_day(house_air) × inside_solar ; day_w += inside_solar
밤:                   night_sum += S_night(house_air)        ; night_w += 1
q_firm = firmness_ca_curve(suff_Ca) × firmness_temp_curve(house_air) × firmness_n_excess_curve(excess_N)
brix_factor_sum += q_brix × dD ;  firmness_factor_sum += q_firm × dD ;  weight_sum += dD
```

- 품질은 과실이 크는 동안의 조건을 **적산온도 가중 평균**해요. 수확 직전 며칠만 좋아져서는 회복되지 않아요(시간 지연).
- **당도의 공급 입력은 `size_satisfaction_mean`이에요**(2026-11-18 프로토타입 점검으로 변경. 초안은 틱 공급 비율 `supply_ratio`를 썼는데, 밤에는 공급이 0이라 당도가 매 밤마다 깎였어요).

```
크기 충족도(틱) = Σ 과실 현재 건물 ÷ Σ 과실 목표 건물        (착과된 과실, 목표 = 4번의 shape(D') 기준, 과실이 없으면 1, 0~1로 제한)
순생장 net > 0인 틱에만:
size_satisfaction_mean += (크기 충족도(틱) − size_satisfaction_mean) × min(1, house.temp_mean_relax_rate_per_hour)
```

  완화 비율은 하우스 기온 이동 평균과 같아요(약 하루). 순생장이 0 이하인 틱(밤)에는 값을 그대로 둬서, 낮에 과실이 얼마나 크고 있었는지만 당도에 반영돼요. 시작값은 1이고 저장 상태예요.

### 6. 수확 (자동, 디렉터 결정 3번)

- `development ≥ 1`인 과실을 그 틱에 따요. 생체중 = `dry_g ÷ dry_matter_fraction`.
- 당도 = `base_brix × (brix_factor_sum / weight_sum) × brix_temp_score_curve(K_temp)`. `K_temp = day_weight × day_sum/day_w + night_weight × night_sum/night_w`(한쪽 가중치 합이 0이면 다른 쪽만, 둘 다 0이면 1). 경도 = `firmness_factor_sum / weight_sum`(0~1).
- 정상과는 개수·생체중·당도·경도·크기 구간에, 기형과는 기형과 개수·생체중에 더해요. 처음이면 `first_harvest_tick`을 남겨요.
- 이벤트: 그 틱에 딴 것을 타일마다 하나로 묶어 `FruitHarvested{tile, fruits, deformed, fresh_g, mean_brix, mean_firmness}`.
- **양분 반출**: 원소 X마다 `min(plant_X, 수확 건물 × fruit_target_X × min(1, ratio_X))`를 체내 풀에서 빼고 장부 `X_harvested_g`에 더해요(N, P, K, Ca, Mg, Mn). `ratio_X`는 포기 전체의 체내량 ÷ 요구량이에요.

### 6b. 낙과 (생장량 부족 시, 착과된 어린 과실)

```
p_tick = abortion_rate_per_development_index × dD × abortion_satisfaction_curve(size_satisfaction_mean) × rank_abortion_weight[rank]
development < abortion_window_index 인 착과된 과실마다 난수 < p_tick 이면 떨어짐     (수확 판정 뒤, 같은 틱)
```

- (2026-11-18 프로토타입 점검 5차로 변경, 디렉터 결정 12번) 확률은 **시간이 아니라 진행도 증가분 `dD`에 비례**해요. 시간당 확률이면 겨울에 천천히 크는 과실이 더 오래 "위험 구간"에 있어서 같은 부족에도 더 많이 떨어졌어요. 진행도 기준이면 같은 부족은 계절과 무관하게 같은 비율로 떨어져요(진행도 0.35까지 지나는 동안의 위험은 계절과 무관하게 같아요).
- `size_satisfaction_mean`은 5번에서 정의한 포기 전체의 크기 충족도 이동 평균이에요.
- 떨어진 과실의 건물·양분은 낙엽 규칙으로 토양에 가요. 난수는 `biology` 스트림이에요.
- 방향: 과실이 너무 많아 잘 못 크면 늦게 핀 작은 과실부터 스스로 솎아져요. 남은 과실은 더 커요.

### 6c. 적화·적과 (명령 `ThinFlowers`, 2026-11-18 디렉터 결정)

| 필드 | 뜻 |
| --- | --- |
| `area` | 대상 칸 (다른 명령과 같은 영역 지정) |
| `per_plant` | 포기당 딸 개수 (실수 허용). 칸마다 `per_plant × plants_per_m2`를 정수로(난수 반올림) |

- 진행도 `thinnable_max_index` 미만인 꽃(아직 안 핀 것 포함)과 어린 과실 중에서 **과위가 높은 것부터, 같은 과위는 늦게 핀 것부터** 따요. 딴 것의 건물·양분은 낙엽 규칙으로 토양에 가요.
- 포기는 칸(8포기) 단위로 묶여 있어서, "포기마다" 지정은 칸의 포기당 평균 개수로 근사해요. 플레이어는 스냅샷의 칸별 과실 목록(과위·진행도·크기)을 보고 정해요.
- 방향: 일찍 솎으면 낙과로 잃을 몫을 남은 과실이 가져가 1번과·2번과가 커지고 당도가 올라요. 너무 많이 따면 수확 개수가 줄어요.

### 7. 낙엽 (뿌리 고사와 같은 규칙)

```
dead_leaf = leaf × min(1, leaf_senescence_rate)
share = dead_leaf / (잎 + 관부 + 뿌리 + 과실 건물 합)         (떨어지기 전 기준)
leaf −= dead_leaf ; 체내 X −= plant_X × share → 토양 (N → labile_N, 양이온 → 총량·용액, P → 유효 P)
C = dead_leaf × dry_matter_C_fraction → labile_C, 장부 C_leaf_litter_g
```

- 체내 양분이 **관부·과실에도 고르게 분포**한다고 보고(M2 뿌리 고사와 같은 단순화) 분모에 관부·과실을 넣어요. 뿌리 고사의 비율 분모도 같아요([strawberry-minimal](strawberry-minimal.md)).
- 낙엽, 뿌리 고사, 음의 순생장 수축(3번), 낙과·적과는 **한 틱의 같은 작물 갱신 호출 안에서 차례로** 일어나요(위 "틱 처리 순서 내 위치"). 이전 틱의 상태를 따로 거치지 않으므로, 단위 테스트가 이 중 하나만 따로 보려면 나머지를 0으로 두어야 해요.

- 실제 하우스에서는 늙은 잎을 따내요(적엽, 명령은 이후). M4에서는 베드에 떨어져 토양 유기물로 간다고 봐요.
- 저온 피해로 잃은 잎·과실([house-climate](house-climate.md))도 같은 규칙으로 토양에 가요. 과실은 개체마다 손실 확률 = `cold_fruit_loss_curve(house_air)`로 난수 판정해요.

### 8. 요구량 (M2 흡수 식의 `target × (leaf + root)` 대체)

```
requirement_X = target_X × (leaf + crown + root) + fruit_target_X × fruit
```

충족도·과잉 지수·흡수 수요(M2·M3 식)는 이 요구량을 써요. 과실이 커지면 K 요구가 늘어 K 공급이 판단이 돼요.

흡수 식도 M4에서 두 가지가 바뀌어요([strawberry-minimal](strawberry-minimal.md) 3번):

- **N은 전용 사치 흡수 상한과 흡수 속도를 가져요**(Ca·Mg 패턴): `demand_N = max(0, target_N-기반 요구량 × n_luxury_uptake_ratio − plant_N)`(요구량은 위 식), `capacity_N = (NH4 + NO3 유효량) × min(1, n_uptake_rate_per_g_root × root × root_health) × water × salt × growth_factor_establish`. `n_luxury_uptake_ratio` = 1.2, `n_uptake_rate_per_g_root_per_hour` = 0.00006 (2026-11-18 프로토타입 점검으로 신설, 점검 중 각각 1.5, 0.0005에서 조정). 체내 N 비율이 상한 1.2에 막히지 않고 공급을 따라가야 질소 과다(S14)가 화아분화와 당도에 보여요.
- 모든 원소(N·P·K·Ca·Mg·Mn)의 흡수 능력에 **활착 배율 `growth_factor_establish`** 가 곱해져요(정식 몸살, [strawberry-phenology](strawberry-phenology.md) 1번).

## 틱 처리 순서 내 위치

셀 단계 h 안에서: 저온 피해 → 생물계절([strawberry-phenology](strawberry-phenology.md), 꽃 만들기 포함) → 개화 판정 → 스트레스 → 총생산·호흡(활성 보정) → RGR 이동 평균 → 분배(따라잡기 수요, 저장 꺼내기·채우기, 순생장이 음수면 저장 먼저 → 수축) → 크기 충족도 이동 평균 → 과실 진행·품질 적산 → 수확 → 낙과 → 흡수 → 뿌리 건강도·고사 → 낙엽 → 사망 판정.

## 보존·불변 조건

- 원소별: `토양 + 체내 = 초기 + in − leached − gas − harvested`. 낙엽·언 과실은 계 안의 이동이에요.
- 토양 C: `유기 C = 초기 + C_in + C_root_litter + C_leaf_litter − C_respired` (언 과실의 C도 `C_leaf_litter`에 넣어요). (M5a 사전 작업) 뿌리 고사·포기 사망으로 잃은 저장 탄수화물의 C도 `C_root_litter`에 넣어요.
- (M5a 사전 작업) 저장 탄수화물은 작물 건물의 일부예요. 저장 ↔ 영양 기관·과실 사이의 이동은 내부 이동이라 CO2 수지(총생산 − 유지 호흡)는 그대로예요. `reserve ≥ 0`, `capacity` 이하로만 채워요.
- 모든 건물·개수·진행도 ≥ 0, 공급 비율 0~1, 수정률·경도 0~1, 수확 기록은 줄지 않음.
- 같은 시드·같은 명령이면 과실 목록과 수확 기록이 비트 단위로 같아요.

## 테스트 (단위)

| 조건 | 기대 |
| --- | --- |
| 밤, 과실 없음 | 생장 음수 → 잎·관부·뿌리가 비율대로 줄고 체내 양분은 그대로 |
| 과실 수요 > 생장 × 0.65 | 공급 비율 < 1, 과실 몫 = 생장 × 0.65 |
| 과실 없음 | 생장 전부 영양 기관으로, 비율 0.6/0.15/0.25 |
| 진행도 0 → 1, 공급 1 | 과실 건물 = 최대 생체중 × 건물 비율 |
| 한 틱 공급을 못 채움(공급 비율 0.5) 뒤 다음 틱 공급 충분 | 다음 틱 수요에 못 채운 몫이 더해져(따라잡기) 과실 건물이 목표 쪽으로 따라잡음 |
| 순생장 > 0인 틱 대 순생장 ≤ 0인 틱 | `size_satisfaction_mean`은 앞쪽에서만 갱신, 뒤쪽에서는 그대로 |
| 같은 충족도 부족, 진행 속도 2배 차이 | 어린 과실이 떨어지는 비율은 두 경우 거의 같음 (확률이 진행도 증가분 기준) |
| 체내 N 비율이 수요 상한에 도달 | N 흡수 0 (`n_luxury_max_ratio`, 활착 배율이 능력에 곱해짐. M5a 사전 작업: 하드 상한 1.2 → 부드러운 상한, [strawberry-minimal](strawberry-minimal.md) 3번) |
| 꽃 수 7.4, 같은 시드 두 번 | 같은 개수(7 또는 8), 과위 배분 합 = 꽃 수 |
| 이동 평균 5 °C 대 18 °C, 꽃 많음 | 5 °C 쪽 낙화·기형과 비율 큼, 평균 크기 작음 |
| 같은 조건의 과실들 | 최대 생체중이 ±변이 폭 안에서 서로 다름 |
| 같은 조건, 기온 15 °C 대 25 °C | 15 °C 쪽 수확이 늦음. (당도는 주·야간 이중 가중이라 일정 기온 비교로는 정하지 않고, 낮 23 대 30 °C, 밤 8.5 대 15 °C 행으로 검증해요. 2026-11-19 본 구현 때 정정) |
| K 충족도 0.6 대 1.0 | 0.6 쪽 당도 낮음 |
| Ca 충족도 0.6 대 1.0 | 0.6 쪽 경도 낮음 |
| 수확 | 수확 기록·크기 구간·장부 반출 증가, 체내 양분 감소, 과실 제거 |
| 낙엽 | 잎 감소, C·양분이 토양으로 (원소·C 보존) |
| 요구량 | 과실이 늘면 K 요구량이 증가 |
| 결정론 | 같은 시드 두 번 → 과실 목록 동일. 다른 시드 → 개수·크기가 다를 수 있음 |
| 크기 충족도 0.4 대 1.0 | 0.4 쪽이 훨씬 많이(5배 이상) 떨어지고, 높은 과위가 더 잘 떨어짐. 충족도 곡선의 하한(1.0에서 0.1) 때문에 1.0 쪽도 조금 떨어짐 (2026-11-19 본 구현 때 정정) |
| `ThinFlowers` 포기당 3개 | 칸마다 약 24개가 높은 과위·늦은 것부터 제거, 진행도 0.3 이상은 남음 |
| 적과한 칸 대 안 한 칸 | 적과 쪽 남은 과실 평균 크기가 큼 |
| 낮 23 °C 대 30 °C, 밤 같음 | 23 °C 쪽 당도 높음 |
| 밤 8.5 °C 대 15 °C, 낮 같음 | 8.5 °C 쪽 당도 높음 |
| 밤 2 °C | 밤 점수 낮음(전류 저하) |
| (M5a 사전 작업) 과실 수요 없음, 저장 켬 | 영양 생장 몫 × fill 비율이 저장으로(상한 capacity × (관부 + 뿌리)), 영양 기관 + 저장 증가 = 순생장. 상한이면 더 안 채움 |
| (M5a 사전 작업) 과실 공급 부족, 저장 10 g | 꺼낸 양 ≤ 저장 × 꺼내기 비율, 공급 비율 상승, 과실 증가 = 꺼낸 양, 영양 기관·순생장은 저장 없을 때와 같음 |
| (M5a 사전 작업) 밤, 저장 5 g > 부족분 | 저장이 부족분만큼 줄고 잎·관부·뿌리는 그대로 |
| (M5a 사전 작업) 활성 곡선 `[[-1,0.4],[0.05,1]]`, RGR 평균 −1 대 0.1 | 유지 호흡 0.4배 |
| (M5a 사전 작업) 완화 비율 0.5, 낮 한 틱 | `rgr_mean_per_day` = 0.5 × 순생장 ÷ (영양 기관 + 저장) × 24. 비율 0이면 그대로 |

시나리오(M5a 사전 작업, `tests/test_m4_scenarios.cpp`): "Respiration activity scaling raises the season harvest" — 활성 보정을 끄면 season_standard 누적 수확이 줄어요(6,058 대 5,397 g/m²).

## 단순화와 비목표

- 과실 착색, 산도(당산비), 붕소 결핍 기형은 없어요(붕소는 M8).
- 수확은 익는 즉시 자동이에요. 직접 수확·적엽, 과숙 손실은 이후 마일스톤이에요(적화·적과는 M4 명령).
- 판매 등급과 가격, 저장 중 품질 저하는 경제(M7)에서 이 지표로 계산해요.
- 포기 하나하나를 구분하지 않아요. 과실은 칸(1 m²) 단위 목록이에요.
- 잎 면적은 M2처럼 잎 건물 × 비엽면적이에요. 잎 나이 구조는 없어요.
