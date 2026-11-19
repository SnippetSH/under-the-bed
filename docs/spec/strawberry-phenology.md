# 딸기 생물계절: 활착, 화아분화, 화방 발달, 개화·착과, 모종 등급

- 상태: 승인 (2026-11-18, 프로토타입 점검 결과 반영본 최종 승인)
- M5a 사전 작업 반영 (2026-11-19 승인, [m5-overview](m5-overview.md) 13장): 화아분화 질소 곡선 A, 포기 간 화아분화 차이
- 마일스톤: M4
- 관련: [m4-overview](m4-overview.md), [strawberry-fruit](strawberry-fruit.md), [strawberry-minimal](strawberry-minimal.md), [ADR-0006](../decisions/0006-off-season-and-disinfection.md), 계획서 5장

## 목적

딸기가 "언제 꽃을 만들고, 언제 따는가"를 만들어요. 계획서 5장의 생육 단계(정식·활착 → 영양생장 → 화아분화 → 개화 → 착과·비대 → 수확, 화방 반복)예요.

- 질소를 많이 주면 화아분화가 늦어져 첫 수확이 밀려요. "비료를 많이 주면 좋다"는 직관을 깨는 장치예요(계획서 5장).
- 고온(9월 하우스)도 화아분화를 막아요. 환기와 정식 시기가 판단이 돼요.
- 좋은 모종(등급)은 꽃눈이 이미 생기기 시작한 상태라 첫 수확이 빨라요. 모종 값과 수확 시기의 트레이드오프예요(ADR-0006).
- 개화기에 너무 춥거나 더우면 착과가 줄고 기형과가 늘어요.

## 상태 변수

### 작물 (타일당 0~1개, M2 작물에 추가)

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `plants_per_m2` (정식 시 고정) | 재식 밀도 | 포기/m² |
| `establishment_index` | 활착 정도. 시작값은 시나리오 `[crop] initial_establishment_index`(필수, 0~1)예요. 작기 시나리오는 0(막 정식), 이미 자란 작물을 가정하는 M2·M3 시나리오·테스트는 1 (2026-11-18 프로토타입 점검으로 신설) | 0~1 |
| `induction_index` | 다음 화방 분화까지 누적된 진행도 | 0~1 |
| `trusses` | 화방 목록 (아래) | - |

### 화방 (작물마다 0개 이상, 분화 순서대로)

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `initiated_tick` | 분화된 틱 | 틱 |
| `development_index` | 분화 → 개화 진행도 | 0~1 |
| `flowers_per_m2` | 이 화방의 꽃 수 (분화 시 정해짐) | 개/m² |
| `opened` | 개화했는지. 개화 시 개별 꽃(과실)이 생김([strawberry-fruit](strawberry-fruit.md)) | bool |
| `opened_tick` (진단) | 개화한 틱 | 틱 |
| `harvested_fruits`, `harvested_fresh_g` (진단) | 이 화방에서 딴 정상과·기형과 개수, 생체중 합 | 개, g |
| `aborted_fruits` (진단) | 이 화방에서 낙과한 과실 수 | 개 |
| `first_harvest_tick`, `last_harvest_tick` (진단) | 이 화방의 첫·마지막 수확 틱 | 틱 |

(진단 항목은 시뮬레이션 결과를 바꾸지 않고 작기 리포트(M7)와 점검용으로만 기록해요. [m4-overview](m4-overview.md) "진단·리포트용 기록")

### 하우스 (월드)

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `house_air_temp_mean_degC` | 하우스 기온의 지수 이동 평균 (약 하루). 착과·기형 판정용 | °C |

## 파라미터 (`[phenology]`, `[calendar]`, `[[seedling_grade]]`, 모두 게임용 임의값이고 근거는 비고)

| 키 | 의미 | 초기값 | 비고 |
| --- | --- | --- | --- |
| `calendar.day_length_curve` | x = day_of_year, y = 낮 길이 h | 위도 36.5° 천문 계산값을 약 15일 간격 점으로 (`tools/gen_weather.py`가 함께 생성) | 합성 기상과 같은 위도 |
| `phenology.establishment_rate_per_hour` | 최적 조건에서 활착 진행 속도 | 0.0042 | 약 10일에 활착 완료 |
| `phenology.establishment_temp_curve` | x = 하우스 기온 °C | `[[5,0],[10,0.5],[18,1],[28,1],[35,0.3]]` | |
| `phenology.establishment_growth_curve` | x = 활착 정도, y = 생장·흡수 배율 | `[[0,0.3],[1,1]]` | 정식 몸살 |
| `phenology.induction_rate_per_hour` | 최적 조건에서 화아분화 진행 속도 | 0.002 | 약 21일에 화방 1개 |
| `phenology.induction_day_length_curve` | x = 낮 길이 h | `[[8,1],[11.5,1],[12.5,0.6],[13.5,0.1],[14,0]]` | 단일 식물. 봄 장일에서 분화가 멈춰 작기가 자연히 끝나요 |
| `phenology.induction_temp_curve` | x = 하우스 기온 °C | `[[3,0],[8,0.5],[12,1],[22,1],[26,0.3],[30,0]]` | 저온(10~20 °C)에서 촉진, 고온에서 억제 |
| `phenology.induction_nitrogen_curve` | x = 체내 N 비율(체내 N / 요구량, 요구량에는 관부·과실 포함) | `[[0,1],[1.05,1],[1.15,0.7],[1.5,0.5]]` | 질소 과다로 분화 지연 (계획서 5장). (M5a 사전 작업, 디렉터 선택 A: 부드러운 N 상한으로 표준이 겨울 약 1.05, 과다가 1.3~1.4가 되어 다시 맞춤. S14 첫 수확 +4~5일, 과다 누적 약 −20%) [이전 `[[0,1],[1.1,1],[1.3,0.6],[1.5,0.25]]` (2026-11-18 점검), 그전 `[[0,1],[1.0,1],[1.15,0.6],[1.3,0.25]]`] |
| `phenology.truss_base_temp_degC` | 화방 발달 적산온도 기준 | 5 | |
| `phenology.truss_degree_days_to_anthesis` | 분화 → 개화 적산온도 | 350 | °C·일. 가을 약 35~40일 (2026-11-18 프로토타입 점검으로 조정: 이전 600. 화방이 너무 느려 수확이 늦었어요) |
| `phenology.induction_spread_index` (M5a 사전 작업, 신설) | 정식 때 셀마다 화아분화 진행도를 0~이 값만큼 늦춰요 (포기 간 생육 차이). 0이면 꺼짐, 범위 [0, 1] | 0.4 | 게임용 임의값. 아래 2번 "포기 간 차이" |
| `phenology.flowers_per_truss_curve` | x = 포기당 관부 건물 g, y = 포기당 꽃 수 | `[[1,6],[3,12],[6,18],[10,22]]` | 관부가 굵을수록 꽃이 많아요 |
| `house.temp_mean_relax_rate_per_hour` | 이동 평균의 시간당 완화 비율 | 0.042 | 약 1/24 |

### 모종 등급 (`[[seedling_grade]]`, ADR-0006)

| id | 포기당 잎 g | 포기당 관부 g | 포기당 뿌리 g | 초기 화아분화 진행도 `initial_induction_index` | 근거 |
| --- | --- | --- | --- | --- | --- |
| `A` | 4.0 | 3.0 | 2.0 | 1.0 | 화아분화가 끝난 큰 모종. 게임용 임의값 (2026-11-18 프로토타입 점검으로 조정: 이전 0.8) |
| `B` | 3.5 | 2.0 | 1.5 | 0.8 | 보통 모종 (2026-11-18 프로토타입 점검으로 조정: 이전 0.4) |
| `C` | 2.5 | 1.2 | 1.0 | 0.3 | 작고 늦은 모종 (2026-11-18 프로토타입 점검으로 조정: 이전 0.0) |

- 모종 가격은 경제(M7)에서 붙여요.
- `initial_induction_index`의 검증 범위는 [0, 1]이에요. 1은 "분화 완료 모종"이라 활착이 끝나면 곧바로 첫 화방이 생겨요. 촉성 재배 모종은 정식 때 이미 꽃눈이 분화돼 있어서 점검에서 값을 올렸어요(A·B·C가 1.0·0.8·0.3).
- 시나리오 `[crop]`은 M2의 `leaf_g_per_m2`, `root_g_per_m2` 대신 `seedling_grade`, `plants_per_m2`를 받아요. 정식 시 기관별 건물 = 포기당 값 × 재식 밀도, 체내 양분 = 적정 농도 × 건물(M2 규칙 그대로).

## 프로세스 (셀 단계 h1, 작물이 살아 있을 때, 생장 계산 앞)

### 1. 활착

```
establishment += establishment_rate × establishment_temp_curve(house_air)     (최대 1)
growth_factor_establish = establishment_growth_curve(establishment)
```

- `growth_factor_establish`는 생장과 흡수 능력에 곱해요([strawberry-fruit](strawberry-fruit.md) 생장, [strawberry-minimal](strawberry-minimal.md) 흡수). **N·P·K·Ca·Mg·Mn 모든 원소의 흡수 능력**에 곱해져요(N 전용 능력 포함). 활착 전에는 화아분화가 진행되지 않아요.
- 활착 시작값이 시나리오마다 달라요: `initial_establishment_index`는 시나리오 `[crop]`의 필수 키예요([m4-overview](m4-overview.md) "시나리오 TOML"). 작기 시나리오는 0에서 시작하고, M2·M3 시나리오와 이미 자란 작물 테스트는 1이에요.

### 2. 화아분화

```
day_length = day_length_curve(day_of_year)
induction += induction_rate × induction_day_length_curve(day_length)
                            × induction_temp_curve(house_air)
                            × induction_nitrogen_curve(plant_N / N 요구량)
if induction ≥ 1: 새 화방 추가, induction −= 1
```

- 새 화방의 꽃 수: `flowers_per_m2 = flowers_per_truss_curve(관부 / plants_per_m2) × plants_per_m2`. 분화할 때의 포기 세력으로 정해져요.
- 방향: 가을 단일·저온에서 화방이 이어서 생기고, 봄 장일이 되면 멈춰요. 질소 과다·고온이면 늦어져요.

**포기 간 차이 (정식 때, M5a 사전 작업, 2026-11-19 승인)**

```
induction_spread_index > 0이면 월드 생성 때 타일 순서(행 우선)대로 셀마다:
    induction_index = max(0, 모종 등급 initial_induction_index − induction_spread_index × U[0,1))     (biology 난수)
```

- 셀(1 m², 8포기)은 한 덩어리로 움직이고 하우스의 모든 셀이 같은 환경을 받아서, 차이가 없으면 80셀의 화방이 한꺼번에 분화·개화해요. 그러면 1월 초 탄소가 모자란 시기에 하우스 전체의 어린 과실이 한꺼번에 떨어져 겨울 월별 수확이 크게 출렁였어요(m5-overview 13.4b 5차). 셀마다 시작 진행도를 다르게 두면 화방 파동이 흩어져요.
- 값이 0이면 난수를 뽑지 않아요. 그래서 M1~M4 시나리오·테스트의 biology 난수 순서가 그대로예요.
- 이후 화아분화는 같은 식으로 진행하고, 셀 사이의 차이(위상)가 유지돼요.
- M5a `Plant` 명령([m5-overview](m5-overview.md) 8장)으로 다시 심을 때도 같은 규칙을 써야 해요.

### 3. 화방 발달과 개화

```
development += max(0, house_air − truss_base_temp) / 24 / truss_degree_days_to_anthesis
if development ≥ 1 and not opened: 개화 → 꽃 만들기 ([strawberry-fruit](strawberry-fruit.md) 1번)
```

- 과위별로 며칠씩 늦게 피고, 피는 순간 기온으로 수정률·착과·기형이 정해져요([strawberry-fruit](strawberry-fruit.md) 2번). 2026-11-18 디렉터 결정 2번(개별 과실)으로 착과·기형 곡선은 과실 명세의 수정률 곡선으로 옮겼어요.
- 개화한 화방은 목록에 남지만 더는 계산하지 않아요(작기 리포트용 기록).

### 하우스 기온 이동 평균 (하우스 단계 2)

`mean += (house_air − mean) × min(1, temp_mean_relax_rate)`. 월드 생성 시 첫 틱 하우스 기온으로 시작해요.

## 보존·불변 조건

- 생물계절 자체는 물질을 옮기지 않아요(꽃 수·진행도만).
- 진행도 0~1, 꽃·착과 수 ≥ 0, 기형 ≤ 착과.

## 테스트 (단위)

| 조건 | 기대 |
| --- | --- |
| 활착 전 | 화아분화 진행 0 |
| 낮 길이 11 h 대 14 h, 같은 기온 | 11 h에서 진행, 14 h에서 0 |
| 기온 18 °C 대 30 °C | 30 °C에서 진행 0 |
| 체내 N 비율 1.0 대 1.3 | 1.3에서 진행이 느림 |
| 진행도가 1을 넘음 | 화방 1개 추가, 남는 진행도 이월 |
| 관부가 큼 | 새 화방의 꽃 수가 많음 |
| 적산온도 350 °C·일 도달 | 개화, 꽃(과실) 생성 |
| 모종 A 대 C | 초기 화아분화 진행도·건물이 A가 큼 |
| `initial_induction_index` 1.2 또는 −0.1 | 검증에서 거부 (범위 [0, 1]) |
| (M5a 사전 작업) `induction_spread_index` 0 대 0.4, 같은 시드 | 0이면 모든 셀이 등급 시작값, 0.4면 셀 절반 넘게 서로 다르고 모두 [시작값 − 0.4, 시작값] 안. 같은 시드 두 번은 같은 배치 |
| 활착 시작값 1 대 0 | 1 쪽은 활착 배율 1로 바로 생장·흡수, 0 쪽은 약 10일에 걸쳐 올라감 |

시나리오(M5a 사전 작업, `tests/test_m4_scenarios.cpp`):

| 이름 | 기대 |
| --- | --- |
| Induction spread evens out the winter harvest | season_standard에서 포기 간 차이를 끄면(0) 12·1·2월 수확의 (최대 − 최소) / 평균이 커짐 (0.4: 0.185, 0: 0.210) |
| M5a prework standard season has an even winter and rising spring harvest | season_standard 12·1·2월이 평균 ±10% 안, 3 < 4 < 5월 (733 / 608 / 680 / 1,017 / 1,270 / 1,751 g/m²) |

## 단순화와 비목표

- 휴면(겨울 단일·저온에서 잎자루가 짧아지고 생장이 멈춤)은 없어요. 촉성 재배에서 난방으로 피한다고 봐요.
- 런너는 없어요(육묘 제외, ADR-0006).
- 액아 화방과 정화방을 구분하지 않아요. 분화 순서만 있어요.
- 꿀벌(수분 매개)은 항상 있다고 봐요.
