# 딸기 생장 분배, 호흡·낙엽, 개별 과실, 수확, 품질

- 상태: 승인 (2026-11-18, 디렉터 결정 2번: 개별 과실)
- 마일스톤: M4
- 관련: [m4-overview](m4-overview.md), [strawberry-phenology](strawberry-phenology.md), [strawberry-minimal](strawberry-minimal.md), [cation-exchange](cation-exchange.md), [house-climate](house-climate.md), 계획서 5장

## 목적

토양·하우스 관리의 결과가 **수확량과 품질**로 나오게 해요. M4 완료 기준("처방별 수확량·당도 차이가 그래프로 확인됨")의 본체예요.

- 과실이 많이 달리면 잎·관부로 갈 몫이 줄고 과실도 작아져요(과실 부담).
- 칼륨 부족은 당도를, 칼슘 부족·질소 과다는 경도(저장성)를 떨어뜨려요. 질소 과다는 당도도 낮춰요.
- 시원한 기온은 과실을 천천히 키워 크고 달게 해요. 따뜻하면 빨리 익지만 무르고 덜 달아요.
- 겨울 밤에는 호흡으로 건물이 줄어요. 광이 부족한 겨울에 과실을 너무 많이 달면 포기가 지쳐요.
- 개화기 기온이 나쁘면 수정이 덜 돼 과실이 작고 모양이 나빠져요(기형과). **과실마다 크기와 모양이 달라서** 판매 등급(M7)이 크기 분포로 나뉘어요.

## 상태 변수

### 작물 (M2 작물에 추가·변경)

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `crown_g_per_m2` (신규) | 관부 건물. 화방 꽃 수를 정하는 세력 | g/m² |
| `leaf_g_per_m2`, `root_g_per_m2` (M2) | 그대로 | g/m² |
| `fruit_g_per_m2` (파생) | 달려 있는 과실 건물 합 | g/m² |
| `fruits` | 개별 과실 목록 (아래). 타일 1칸 = 1 m²라서 개수는 정수예요 | - |
| `harvest` | 누적 수확 기록 (아래) | - |

체내 양분(M2·M3)은 포기 전체의 풀이에요. 과실도 같은 풀에서 양분을 써요.

### 개별 과실

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `truss_index`, `rank` | 몇 번째 화방의 몇 번과인지 (과위 0~3: 1번과, 2번과, 3번과, 4번과 이후) | - |
| `anthesis_tick` | 이 꽃이 피는 틱 (화방 개화 + 과위별 지연) | 틱 |
| `set` | 개화 판정을 마쳤는지 (`false` = 아직 꽃) | bool |
| `pollination_index` | 수정률 (개화 시 정해짐) | 0~1 |
| `deformed` | 기형과 여부 | bool |
| `potential_fresh_g` | 이 과실이 클 수 있는 최대 생체중 | g |
| `dry_g` | 현재 건물 | g |
| `development_index` | 개화 → 수확 진행도 | 0~1 |
| `brix_factor_sum`, `firmness_factor_sum`, `weight_sum` | 품질 계수의 적산온도 가중 합 | - |

### 수확 기록 (작물마다, 누적)

| 이름 | 의미 |
| --- | --- |
| `harvested_fruits`, `harvested_fresh_g` | 정상과 개수, 생체중 |
| `harvested_deformed`, `harvested_deformed_fresh_g` | 기형과 개수, 생체중 |
| `harvested_brix_weighted`, `harvested_firmness_weighted` | 당도·경도 × 생체중 합 (정상과만) |
| `harvested_size_class_counts` | 크기 구간별 정상과 개수 (구간 경계는 데이터) |
| `first_harvest_tick` | 첫 수확 틱 (없으면 미정) |

평균 당도 = 가중 합 ÷ 정상과 생체중. 크기 구간은 판매 등급(M7)의 재료예요.

## 파라미터

### 생장·분배·호흡·낙엽 (`[crop]` 추가·변경, 게임용 임의값)

| 키 | 의미 | 초기값 | 비고 |
| --- | --- | --- | --- |
| `vegetative_leaf_fraction`, `vegetative_crown_fraction`, `vegetative_root_fraction` | 과실 몫을 뺀 나머지의 기관 비율 (합 1) | 0.6, 0.15, 0.25 | M2 `leaf_allocation_fraction`(0.7)을 대체 |
| `max_fruit_assimilate_fraction` | 이번 틱 생장량 중 과실이 가져갈 수 있는 최대 비율 | 0.65 | 과실이 가장 강한 수요처 |
| `maintenance_rate_leaf_per_hour`, `…_crown_…`, `…_root_…`, `…_fruit_…` | 25 °C에서 건물 1 g당 유지 호흡 | 0.00125, 0.0004, 0.0006, 0.0004 | 대략 하루 3%, 1%, 1.5%, 1% |
| `maintenance_temp_curve` | x = 하우스 기온 °C, y = 배율 (Q10 = 2, 25 °C에서 1) | `[[0,0.18],[10,0.35],[20,0.71],[25,1.0],[30,1.41],[40,2.83]]` | |
| `leaf_senescence_rate_per_hour` | 잎이 늙어 떨어지는 시간당 비율 | 0.0005 | 잎 수명 약 80일 |
| `fruit_target_N_g_per_g`, `…P`, `…K`, `…Ca`, `…Mg`, `…Mn` | 과실 적정 농도 | 0.015, 0.0035, 0.025, 0.0015, 0.0012, 0.00002 | 딸기 과실은 K가 많고 Ca가 적음 (일반 경향) |

관부의 적정 농도는 잎·뿌리와 같은 `target_*`를 써요.

### 과실 (`[fruit]`, 게임용 임의값)

| 키 | 의미 | 초기값 | 비고 |
| --- | --- | --- | --- |
| `rank_fraction` | 꽃 수 중 과위별 비율 (합 1) | `[0.07, 0.13, 0.27, 0.53]` | 화방당 약 1·2·4·8개 |
| `rank_anthesis_delay_days` | 화방 개화 후 과위별 개화 지연 | `[0, 2, 4, 6]` | 1번과가 먼저 피어요 |
| `rank_potential_fresh_g` | 과위별 최대 생체중 | `[35, 25, 16, 10]` | 1번과가 가장 큼 |
| `size_variation_fraction` | 개체별 최대 생체중 변이 폭 (±) | 0.1 | 시드 기반 생물 변이 |
| `pollination_mean_curve` | x = 하우스 기온 이동 평균 °C, y = 평균 수정률 | `[[3,0.35],[8,0.65],[12,0.92],[25,0.92],[30,0.65],[35,0.35]]` | 저온·고온 수정 불량 |
| `pollination_spread` | 개체별 수정률 변이 폭 (±) | 0.15 | |
| `set_threshold` | 이보다 낮으면 꽃이 떨어짐(착과 실패) | 0.3 | |
| `deformed_threshold` | 이보다 낮으면 기형과 | 0.7 | 씨가 일부만 수정됨 |
| `pollination_size_curve` | x = 수정률, y = 최대 생체중 배율 | `[[0.3,0.4],[0.7,0.8],[0.9,1.0],[1,1.0]]` | 덜 수정되면 작아요 |
| `dry_matter_fraction` | 과실 생체중 중 건물 비율 | 0.09 | |
| `base_temp_degC` | 비대 적산온도 기준 | 5 | |
| `degree_days_to_harvest` | 개화 → 수확 적산온도 | 450 | 가을 약 30일, 겨울 약 60일 |
| `growth_shape_curve` | x = 진행도, y = 최대 크기 중 도달 비율 (누적, S자) | `[[0,0],[0.2,0.05],[0.5,0.35],[0.8,0.85],[1,1]]` | |
| `size_class_bounds_g` | 크기 구간 경계 (오름차순) | `[10, 15, 20, 25]` | 5개 구간. 판매 등급은 M7 |
| `base_brix` | 모든 계수가 1일 때 당도 | 10.0 | °Brix |
| `brix_supply_curve` | x = 과실 공급 비율(공급 ÷ 수요) | `[[0,0.7],[0.5,0.85],[1,1]]` | 잎에 비해 과실이 많으면 덜 닮 |
| `brix_temp_curve` | x = 하우스 기온 °C | `[[8,1.15],[15,1.05],[20,1.0],[25,0.9],[30,0.8]]` | 시원하면 달아요 |
| `brix_k_curve` | x = K 충족도 | `[[0.5,0.75],[0.8,0.9],[1,1]]` | |
| `brix_n_excess_curve` | x = N 과잉 지수 | `[[0,1],[0.15,0.95],[0.3,0.85]]` | |
| `brix_water_curve` | x = relative_water | `[[0.4,1.08],[0.7,1.03],[0.9,1.0],[1.2,0.95]]` | 약간 마르면 달고 과습하면 싱거움 |
| `firmness_ca_curve` | x = Ca 충족도, y = 경도 계수 0~1 | `[[0.5,0.6],[0.8,0.85],[1,1]]` | |
| `firmness_temp_curve` | x = 하우스 기온 °C | `[[10,1],[20,0.95],[28,0.75]]` | |
| `firmness_n_excess_curve` | x = N 과잉 지수 | `[[0,1],[0.3,0.85]]` | |

### 생물 변이 난수

수정률과 최대 생체중의 개체 변이, 꽃 수의 소수점 처리, 저온 손실은 **코어 PRNG**(시나리오 시드, ADR-0004)를 써요. 용도별로 스트림을 나눠요(`biology`). 뽑는 순서는 타일 행 우선 → 화방 순 → 과실 생성 순으로 고정이에요. 다른 프로세스가 난수를 더 써도 이 수열은 밀리지 않아요(ADR-0009 공통 규칙 1번과 같은 방식).

## 프로세스 (셀 단계 h, 작물이 살아 있을 때)

### 1. 꽃 만들기 (화방 개화 시, [strawberry-phenology](strawberry-phenology.md) 3번에서 호출)

```
꽃 수 n = floor(flowers_per_m2) + (난수 < 소수부 ? 1 : 0)
과위별 개수 = rank_fraction × n 을 최대 나머지 방식으로 정수 배분
과실마다: anthesis_tick = 지금 + rank_anthesis_delay_days[rank] × 24, set = false
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
resp  = Σ 기관 건물 × maintenance_rate_기관 × maintenance_temp_curve(house_air)
net   = gross − resp
```

- `net < 0`(밤, 겨울): 부족분을 잎·관부·뿌리에서 건물 비율대로 줄여요(과실은 줄이지 않아요). 줄어든 건물의 탄소는 공기로 나가 장부 밖이에요(광합성 탄소처럼). 체내 양분은 그대로 남아요.

### 4. 분배 (net > 0일 때)

```
과실 i의 잠재 증가 = potential_fresh_g × dry_matter_fraction × (shape(새 진행도) − shape(옛 진행도))
fruit_demand = Σ (착과된 과실) 잠재 증가
fruit_supply = min(fruit_demand, net × max_fruit_assimilate_fraction)
supply_ratio = fruit_supply / fruit_demand          (수요 0이면 1)
과실 i의 실제 증가 = 잠재 증가 × supply_ratio
나머지(net − fruit_supply)를 잎·관부·뿌리에 vegetative_*_fraction대로
```

- 방향: 과실이 많이 달려 수요가 크면 과실 하나하나가 작아지고, 잎·관부 생장도 줄어 다음 화방 꽃 수가 줄어요.

### 5. 과실 진행과 품질 적산 (착과된 과실마다)

```
dD = max(0, house_air − base_temp) / 24 / degree_days_to_harvest
development += dD
q_brix = brix_supply_curve(supply_ratio) × brix_temp_curve(house_air) × brix_k_curve(suff_K)
       × brix_n_excess_curve(excess_N) × brix_water_curve(relative_water)
q_firm = firmness_ca_curve(suff_Ca) × firmness_temp_curve(house_air) × firmness_n_excess_curve(excess_N)
brix_factor_sum += q_brix × dD ;  firmness_factor_sum += q_firm × dD ;  weight_sum += dD
```

- 품질은 과실이 크는 동안의 조건을 **적산온도 가중 평균**해요. 수확 직전 며칠만 좋아져서는 회복되지 않아요(시간 지연).

### 6. 수확 (자동, 디렉터 결정 3번)

- `development ≥ 1`인 과실을 그 틱에 따요. 생체중 = `dry_g ÷ dry_matter_fraction`.
- 당도 = `base_brix × brix_factor_sum / weight_sum`, 경도 = `firmness_factor_sum / weight_sum`(0~1).
- 정상과는 개수·생체중·당도·경도·크기 구간에, 기형과는 기형과 개수·생체중에 더해요. 처음이면 `first_harvest_tick`을 남겨요.
- 이벤트: 그 틱에 딴 것을 타일마다 하나로 묶어 `FruitHarvested{tile, fruits, deformed, fresh_g, mean_brix, mean_firmness}`.
- **양분 반출**: 원소 X마다 `min(plant_X, 수확 건물 × fruit_target_X × min(1, ratio_X))`를 체내 풀에서 빼고 장부 `X_harvested_g`에 더해요(N, P, K, Ca, Mg, Mn). `ratio_X`는 포기 전체의 체내량 ÷ 요구량이에요.

### 7. 낙엽 (뿌리 고사와 같은 규칙)

```
dead_leaf = leaf × min(1, leaf_senescence_rate)
share = dead_leaf / (전 기관 건물)
leaf −= dead_leaf ; 체내 X −= plant_X × share → 토양 (N → labile_N, 양이온 → 총량·용액, P → 유효 P)
C = dead_leaf × dry_matter_C_fraction → labile_C, 장부 C_leaf_litter_g
```

- 실제 하우스에서는 늙은 잎을 따내요(적엽, 명령은 이후). M4에서는 베드에 떨어져 토양 유기물로 간다고 봐요.
- 저온 피해로 잃은 잎·과실([house-climate](house-climate.md))도 같은 규칙으로 토양에 가요. 과실은 개체마다 손실 확률 = `cold_fruit_loss_curve(house_air)`로 난수 판정해요.

### 8. 요구량 (M2 흡수 식의 `target × (leaf + root)` 대체)

```
requirement_X = target_X × (leaf + crown + root) + fruit_target_X × fruit
```

충족도·과잉 지수·흡수 수요(M2·M3 식)는 이 요구량을 써요. 과실이 커지면 K 요구가 늘어 K 공급이 판단이 돼요.

## 틱 처리 순서 내 위치

셀 단계 h 안에서: 저온 피해 → 생물계절([strawberry-phenology](strawberry-phenology.md), 꽃 만들기 포함) → 개화 판정 → 스트레스 → 총생산·호흡 → 분배 → 과실 진행·품질 적산 → 수확 → 흡수 → 뿌리 건강도·고사 → 낙엽.

## 보존·불변 조건

- 원소별: `토양 + 체내 = 초기 + in − leached − gas − harvested`. 낙엽·언 과실은 계 안의 이동이에요.
- 토양 C: `유기 C = 초기 + C_in + C_root_litter + C_leaf_litter − C_respired` (언 과실의 C도 `C_leaf_litter`에 넣어요).
- 모든 건물·개수·진행도 ≥ 0, 공급 비율 0~1, 수정률·경도 0~1, 수확 기록은 줄지 않음.
- 같은 시드·같은 명령이면 과실 목록과 수확 기록이 비트 단위로 같아요.

## 테스트 (단위)

| 조건 | 기대 |
| --- | --- |
| 밤, 과실 없음 | 생장 음수 → 잎·관부·뿌리가 비율대로 줄고 체내 양분은 그대로 |
| 과실 수요 > 생장 × 0.65 | 공급 비율 < 1, 과실 몫 = 생장 × 0.65 |
| 과실 없음 | 생장 전부 영양 기관으로, 비율 0.6/0.15/0.25 |
| 진행도 0 → 1, 공급 1 | 과실 건물 = 최대 생체중 × 건물 비율 |
| 꽃 수 7.4, 같은 시드 두 번 | 같은 개수(7 또는 8), 과위 배분 합 = 꽃 수 |
| 이동 평균 5 °C 대 18 °C, 꽃 많음 | 5 °C 쪽 낙화·기형과 비율 큼, 평균 크기 작음 |
| 같은 조건의 과실들 | 최대 생체중이 ±변이 폭 안에서 서로 다름 |
| 같은 조건, 기온 15 °C 대 25 °C | 15 °C 쪽 수확이 늦고 당도 높음 |
| K 충족도 0.6 대 1.0 | 0.6 쪽 당도 낮음 |
| Ca 충족도 0.6 대 1.0 | 0.6 쪽 경도 낮음 |
| 수확 | 수확 기록·크기 구간·장부 반출 증가, 체내 양분 감소, 과실 제거 |
| 낙엽 | 잎 감소, C·양분이 토양으로 (원소·C 보존) |
| 요구량 | 과실이 늘면 K 요구량이 증가 |
| 결정론 | 같은 시드 두 번 → 과실 목록 동일. 다른 시드 → 개수·크기가 다를 수 있음 |

## 단순화와 비목표

- 과실 착색, 산도(당산비), 붕소 결핍 기형은 없어요(붕소는 M8).
- 수확은 익는 즉시 자동이에요. 직접 수확·적엽·적과, 과숙 손실은 이후 마일스톤이에요.
- 판매 등급과 가격, 저장 중 품질 저하는 경제(M7)에서 이 지표로 계산해요.
- 포기 하나하나를 구분하지 않아요. 과실은 칸(1 m²) 단위 목록이에요.
- 잎 면적은 M2처럼 잎 건물 × 비엽면적이에요. 잎 나이 구조는 없어요.
