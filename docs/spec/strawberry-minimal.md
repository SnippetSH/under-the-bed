# 딸기 작물 최소 버전: 흡수, 생장, 뿌리 건강도

- 상태: 승인 (2026-11-18)
- M3 반영: 2026-11-18 ([m3-overview](m3-overview.md))
- M4 반영: 2026-11-18 ([m4-overview](m4-overview.md)). `(M4 점검)` 표시는 프로토타입 점검으로 바뀐 값, `(M4, 2026-11-18)` 표시는 "M1~M3 명세 변경" 표의 구조 변경이에요. 분배·호흡·과실·낙엽의 상세는 [strawberry-fruit](strawberry-fruit.md), 생물계절·활착은 [strawberry-phenology](strawberry-phenology.md)가 정본이에요
- 마일스톤: M2
- 관련: [m2-overview](m2-overview.md), [soil-chemistry](soil-chemistry.md), [aeration](aeration.md), 계획서 5장

## 목적

토양 상태가 작물 결과로 이어지는 최소 경로를 만들어요. M2의 작물은 잎과 뿌리만 있는 영양생장 상태로 계속 자라요.

(M4, 2026-11-18) 생육 단계·화방·관부·개별 과실·품질·호흡·낙엽은 M4에서 추가됐어요([strawberry-phenology](strawberry-phenology.md), [strawberry-fruit](strawberry-fruit.md)). 이 문서의 스트레스·흡수·뿌리 건강도는 그대로 쓰되, 아래 "(M4, 2026-11-18)" 표시 항목(생장 식, 요구량, 뿌리 고사 몫 분모, 시나리오 `[crop]`)이 바뀌었어요.

- 유효량이 모자라면 충족도가 떨어지고 생장이 줄어요 (S4).
- 과습·고온·아질산·산성은 뿌리 건강도를 깎고, 뿌리가 상하면 양분이 충분해도 못 먹어요 (S8, S4b).
- 염류(EC)와 건조는 흡수와 생장을 함께 줄여요 (S10).

## 상태 변수 (타일당 작물 0~1개)

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `alive` | 생존 여부. 한 번 죽으면 되살아나지 않음 | bool |
| `leaf_g_per_m2`, `root_g_per_m2` | 잎·뿌리 건물량. (M4, 2026-11-18) 관부 `crown_g_per_m2`, 개별 과실, 화방, 활착·화아분화 지수가 더해져요([strawberry-fruit](strawberry-fruit.md), [strawberry-phenology](strawberry-phenology.md)) | g/m² |
| `root_health` | 뿌리 건강도 | 0~1 |
| `plant_N_g_per_m2`, `plant_P_…`, `plant_K_…`, `plant_Ca_…`, `plant_Mg_…` | 체내 양분 | g/m² |
| `plant_Mn_g_per_m2` (M3, 2026-11-18) | 체내 망간. 정의·파생값(`plant_Mn_ratio`)은 [manganese](manganese.md) | g/m² |
| `stress_hours[요인]` | 요인별로 생장을 가장 크게 제한한 누적 시간 (작기 리포트용) | h |

정식 시(시나리오 시작) 체내 양분은 `적정 농도 × (잎 + 뿌리)`로 시작해요(충족도 1).

(M4, 2026-11-18) 시나리오 `[crop]`의 시작값은 `leaf_g_per_m2`, `root_g_per_m2` 대신 `seedling_grade`(모종 등급), `plants_per_m2`(재식 밀도), `initial_establishment_index`(0~1, 필수)예요. 기관별 건물 = 포기당 값 × 재식 밀도(잎·관부·뿌리)이고, 체내 양분 = 적정 농도 × 요구 건물(`적정 농도 × (잎 + 관부 + 뿌리)`, 충족도 1)이에요. 스키마는 [m4-overview](m4-overview.md) "시나리오 TOML", 등급 표는 [strawberry-phenology](strawberry-phenology.md).

파생값(스냅샷 노출): `leaf_area_index`, 원소별 `sufficiency`(0~1)와 `excess_index`(≥0), 이번 틱 스트레스 계수들, 이번 틱 생장량. (M3, 2026-11-18) 원소에 Mn이 추가되고, 이번 틱 길항 배율(Ca, Mg)도 노출해요.

## 파라미터 (`[crop]`, `[root_damage]`, 모두 게임용 임의값이고 근거는 비고)

| 키 | 의미 | 초기값 | 비고 |
| --- | --- | --- | --- |
| `crop.specific_leaf_area_m2_per_g` | 잎 1 g당 잎 면적 | 0.02 | 200 cm²/g |
| `crop.light_interception_curve` | x = LAI, y = 받는 빛 비율 | `[[0,0],[0.5,0.3],[1,0.5],[2,0.75],[3,0.88],[4,0.94]]` | 지수 감쇠(k≈0.7) 모양 |
| `crop.par_fraction` | 일사 중 광합성 유효광 비율 | 0.48 | 일반값 |
| `crop.light_use_efficiency_g_per_MJ` | 받은 PAR 1 MJ당 건물 생산 | 2.25 | 작물 일반 범위 (M4 점검, 2026-11-18 프로토타입 점검으로 조정: 이전 2.5. 5차 디렉터 승인) |
| `crop.temp_curve` | x = 하우스 기온 °C | `[[3,0],[8,0.3],[15,0.8],[20,1.0],[25,1.0],[30,0.6],[35,0.2],[40,0]]` | 딸기 생장 적온 대략 18~25 °C |
| ~~`crop.leaf_allocation_fraction`~~ | **(M4, 2026-11-18 삭제)** 현재 값이 아니에요. 생장 분배는 과실 따라잡기 수요 우선, 나머지를 `vegetative_*_fraction`으로 잎·관부·뿌리에 나눠요([strawberry-fruit](strawberry-fruit.md) 4번) | - | - |
| `crop.water_stress_curve` | x = relative_water | `[[0.2,0],[0.4,0.3],[0.6,0.9],[0.75,1.0],[2.0,1.0]]` | |
| `crop.salt_stress_curve` | x = EC dS/m (토양 용액) | `[[0,1],[2.0,1.0],[3.5,0.6],[5.5,0.25],[8,0.05]]` | 딸기는 염류에 약한 작물. 토양 용액 EC는 포화 추출액 EC의 약 2배라 문턱을 2.0으로 (2026-11-18 프로토타입 점검으로 조정) |
| `crop.nutrient_stress_curve` | x = 충족도, y = 생장 계수 (원소마다 적용, 최솟값 사용) | `[[0,0],[0.5,0.3],[0.8,0.8],[1.0,1.0]]` | |
| `crop.target_N_g_per_g` | 적정 체내 농도 (건물 1 g당) | 0.028 | 딸기 잎 분석 적정 범위 대략 2.0~2.8% |
| `crop.target_P_g_per_g` | | 0.0035 | 대략 0.2~0.4% |
| `crop.target_K_g_per_g` | | 0.020 | 대략 1.1~2.5% |
| `crop.target_Ca_g_per_g` | | 0.012 | 대략 0.5~1.5% |
| `crop.target_Mg_g_per_g` | | 0.004 | 대략 0.25~0.45% |
| `crop.target_Mn_g_per_g` (M3, 2026-11-18) | | 0.0001 | 대략 50~200 mg/kg ([manganese](manganese.md)) |
| `crop.luxury_uptake_ratio` | 적정 농도의 몇 배까지 더 먹는지 (과잉 지수의 원천) | 1.3 | |
| `crop.uptake_rate_per_g_root_per_hour` | 뿌리 1 g당 유효량에서 시간당 가져갈 수 있는 비율. (M3) N·P·K에 써요. Ca·Mg·Mn은 아래 전용 속도. (M4 점검) **N도 전용 속도로 옮겨서 P·K에만 써요** | 0.002 | |
| `crop.ca_uptake_rate_per_g_root_per_hour`, `crop.mg_uptake_rate_per_g_root_per_hour` (M3, 2026-11-18) | Ca·Mg 전용 흡수 속도. 일반 속도(0.002)는 Ca·Mg 흡수 능력이 필요량의 30~100배가 돼 길항이 무효였어요 | 0.00019, 0.00021 | 게임용 임의값. [cation-exchange](cation-exchange.md) |
| `crop.mn_uptake_rate_per_g_root_per_hour`, `crop.mn_max_uptake_ratio`, `crop.mn_toxicity_curve` (M3, 2026-11-18) | Mn 전용 흡수 속도, 수요 상한 배수, 독성 곡선 | 0.00001, 30, 곡선은 [manganese](manganese.md) | 게임용 임의값 (M4 점검, 2026-11-18 프로토타입 점검으로 조정: 속도 이전 0.00004) |
| `crop.n_luxury_uptake_ratio` (M4 점검, 신설) | N 전용 사치 흡수 상한 (적정 대비 배수). N 체내 비율이 공급을 따라가게 (Ca·Mg 전용 속도와 같은 패턴) | 1.2 | 게임용 임의값 (2026-11-18 프로토타입 점검으로 신설, 점검 중 1.5 → 1.2로 조정) |
| `crop.n_uptake_rate_per_g_root_per_hour` (M4 점검, 신설) | N 전용 흡수 속도 (N은 일반 속도 0.002를 더 이상 쓰지 않아요) | 0.00006 | 게임용 임의값 (2026-11-18 프로토타입 점검으로 신설, 점검 중 0.0005 → 0.00006으로 조정) |
| `antagonism.ca_curve`, `antagonism.mg_curve`, `antagonism.min_demand_factor` (M3, 2026-11-18) | Ca·Mg 길항 배율 곡선, 수요 상한 배율의 하한 | 곡선과 하한 0.6은 [cation-exchange](cation-exchange.md) | 게임용 임의값 |
| `crop.transpiration_mm_per_hour_per_W_per_m2` | 하우스 안 일사 × 받는 빛 비율 1 W/m²당 증산 | 0.0015 | 맑은 날 하루 약 1.5~2.5 mm |
| `crop.root_recovery_rate_per_hour` | 손상이 없을 때 뿌리 건강도 회복 (1까지 남은 양의 비율) | 0.002 | |
| `crop.death_root_health` | 이 아래로 내려가면 고사 | 0.05 | |
| `crop.root_dieback_per_damage` | 뿌리 건강도 손상 비율 1당 죽는 뿌리 건물 비율 (0~1) | 1.0 | 게임용 임의값 (2026-11-18 추가: 과습 손상이 생장에 남도록) |
| `crop.dry_matter_C_fraction` | 건물 중 탄소 비율 (죽은 뿌리가 토양 유기물로 갈 때) | 0.45 | 식물 건물 일반값 |
| `root_damage.waterlog_curve` | x = aeration_index, y = 시간당 손상 비율 | `[[0,0.015],[0.2,0.006],[0.4,0.001],[0.5,0]]` | (M4 점검, 2026-11-18 프로토타입 점검으로 조정: 이전 `[[0,0.01],[0.2,0.004],[0.4,0.0005],[0.5,0]]`) |
| `root_damage.heat_curve` | x = 토양 온도 °C | `[[32,0],[35,0.002],[40,0.01],[45,0.03]]` | 환기가 없는 M2 하우스 기준. M7에서 다시 봐요 (balancing.md 1번) |
| `root_damage.nitrite_curve` | x = 유효 아질산(g N/m², pH 배율 적용 후) | `[[0,0],[2,0],[4,0.004],[8,0.015]]` | |
| `root_damage.acid_curve` | x = pH | soil-chemistry 참고 | ADR-0007 |
| `nitrification.nitrite_toxicity_ph_curve` | x = pH, y = 아질산 독성 배율 | `[[4.5,8.0],[5.5,3.0],[6.5,1.3],[7.0,1.0],[9.0,1.0]]` | 산성일수록 HNO2 비율 증가(pKa 약 3.3). 중성·알칼리 1 |

시나리오 `[difficulty] stress_combination = "minimum" | "product"`: 계획서 5장의 기본·하드 모드.

## 프로세스 (셀 단계 h, 작물이 살아 있을 때)

### 1. 스트레스 계수

```
water    = water_stress_curve(relative_water)
salt     = salt_stress_curve(ec_dS_per_m)
nutrient_X = nutrient_stress_curve(sufficiency_X)      X ∈ {N, P, K, Ca, Mg, Mn}     ← Mn은 M3, 2026-11-18
root       = root_health
mn_toxicity = mn_toxicity_curve(plant_Mn_ratio)                                           ← M3, 2026-11-18
combined   = minimum 모드: min(water, salt, nutrient_N … nutrient_Mn, root, mn_toxicity)
             product 모드: water × salt × Π_X nutrient_X × root × mn_toxicity
```

- (M3, 2026-11-18) `StressFactor`에 `NutrientMn`, `ToxicityMn`을 맨 뒤(Root 다음)에 추가해서 기존 인덱스와 동률 규칙은 그대로예요. 정의는 [manganese](manganese.md) "스트레스".

- (2026-11-18 디렉터 결정) product 모드는 원소별 영양 계수를 **각각** 곱해요. 계획서 5장 하드 모드("스트레스를 전부 곱해요")와 M2 코드에 명세를 맞춘 거예요. 처음 승인본은 영양 계수 중 최솟값 하나만 곱했어요.

- `combined < 1`인 틱에만, 가장 작은 요인(동률이면 위 순서상 앞선 것, nutrient는 원소까지)을 `stress_hours`에 1시간 더해요. 모든 계수가 1이면 기록하지 않아요.
- `sufficiency_X = min(1, plant_X / (target_X × (leaf + root)))`, `excess_index_X = max(0, plant_X / (target_X × (leaf + root)) − 1)`.
- (M4, 2026-11-18) 위 식의 `target_X × (leaf + root)`는 **요구량 `requirement_X = target_X × (leaf + crown + root) + fruit_target_X × fruit`**로 바뀌어요(관부·과실 포함, [strawberry-fruit](strawberry-fruit.md) 8번). 충족도·과잉 지수·흡수 수요(아래 3번)가 모두 이 요구량을 써요.

### 2. 생장

(M4, 2026-11-18 변경) 생장은 **총생산 − 유지 호흡**이고 순생장이 **음수일 수 있어요**(밤·겨울·활착 전). 분배·호흡 식과 과실 우선 규칙은 [strawberry-fruit](strawberry-fruit.md) 3·4번이 정본이에요.

```
LAI       = leaf × specific_leaf_area
potential = light_use_efficiency × inside_solar × par_fraction × 0.0036 × interception(LAI) × temp_curve(house_air)
gross     = potential × co2_growth_curve(house_co2_ppm) × combined × growth_factor_establish     (g/m²/h, 0.0036 = W·h → MJ 환산)
resp      = Σ 기관 건물 × maintenance_rate_기관 × maintenance_temp_curve(house_air)
net       = gross − resp
net > 0: 과실 따라잡기 수요 → 나머지를 잎·관부·뿌리에 vegetative_*_fraction대로 (leaf_allocation_fraction은 삭제)
net < 0: 부족분을 잎·관부·뿌리에서 건물 비율대로 줄임 (체내 양분은 그대로, 탄소는 장부 밖)
```

- `inside_solar`는 `일사 × layer_transmittance_factor^겹수`예요([house-climate](house-climate.md)). `growth_factor_establish`는 활착 배율(정식 직후 0.3까지, [strawberry-phenology](strawberry-phenology.md) 1번), `co2_growth_curve`는 하우스 CO2 배율이에요.
- (M2 식) `growth = potential × combined`, `leaf += growth × leaf_allocation`, `root += growth × (1 − leaf_allocation)`은 현재 식이 아니에요.

### 3. 흡수 (원소 X마다)

```
demand_X   = max(0, target_X × luxury_uptake_ratio × (leaf + root) − plant_X)
capacity_X = available_X × min(1, uptake_rate_per_g_root × root × root_health) × water × salt
uptake_X   = min(demand_X, capacity_X)
```

- (M4, 2026-11-18) 위 식의 `target_X × luxury_uptake_ratio × (leaf + root)`는 요구량 기준(`requirement_X`, 관부·과실 포함)으로 바뀌고, 모든 원소(N·P·K·Ca·Mg·Mn)의 `capacity_X`에 활착 배율 `growth_factor_establish`가 곱해져요. 아래 (M3) 식들은 이 두 가지를 같이 적용해서 읽어요.
- `available_X`는 [soil-chemistry](soil-chemistry.md) "유효량". 흡수량은 해당 토양 풀에서 빼고 체내 풀과 `X_uptake_g` 장부에 더해요. (M3, 2026-11-18) 양이온(NH4, K, Ca, Mg, Mn)은 총량과 **용액 상태에서 같은 양을** 빼요. 흡수는 유효량(용액 × pH 유효도) 안에서만 일어나요([cation-exchange](cation-exchange.md)).
- (M3, 2026-11-18) Ca·Mg는 전용 속도 `crop.ca_uptake_rate…`, `crop.mg_uptake_rate…`를 `uptake_rate_per_g_root` 자리에 써요. 또 길항 배율 `a_X = antagonism.X_curve(antagonism_index)`를 흡수 능력과 수요 상한에 곱해요. 수요 상한 쪽 배율은 하한이 있어요:
  ```
  capacity_X = available_X × min(1, X_uptake_rate × root × root_health) × water × salt × a_X          (X = Ca, Mg)
  demand_X   = max(0, target_X × luxury_uptake_ratio × max(min_demand_factor, a_X) × (leaf + root) − plant_X)
  ```
  `min_demand_factor` = 0.6이고 흡수 능력에는 하한이 없어요. 길항 지수 `(용액 K + 용액 NH4) / (용액 Ca + 용액 Mg)`(당량)와 근거는 [cation-exchange](cation-exchange.md) "흡수 길항". N·P·K는 길항을 받지 않아요.
- (M3, 2026-11-18) Mn: `demand_Mn = max(0, target_Mn × mn_max_uptake_ratio × (leaf + root) − plant_Mn)`, `capacity_Mn = 용액 Mn × min(1, mn_uptake_rate × root × root_health) × water × salt`. 길항은 적용하지 않아요. 식과 독성은 [manganese](manganese.md) "흡수"·"스트레스".
- (M4 점검, 2026-11-18 프로토타입 점검으로 신설) **N은 자기 사치 흡수 상한과 흡수 속도를 가져요**(Ca·Mg 패턴). 일반 `luxury_uptake_ratio`(1.3)·`uptake_rate_per_g_root`(0.002)로는 체내 N 비율이 쉽게 포화해 화아분화가 막혔어요(프로토타입 1차 점검). N 비율이 공급 수준을 따라가도록 전용 값으로 나눴어요. 흡수 능력에는 활착 배율(`growth_factor_establish`)이 들어가요([strawberry-phenology](strawberry-phenology.md)):
  ```
  demand_N   = max(0, target_N × n_luxury_uptake_ratio × (요구 건물) − plant_N)          (요구 건물은 strawberry-fruit 8번: 관부·과실 포함)
  capacity_N = (NH4 유효량 + NO3 유효량) × min(1, n_uptake_rate_per_g_root × root × root_health) × water × salt × growth_factor_establish
  ```
  P·K의 능력식에도 `growth_factor_establish`가 곱해져요. Ca·Mg·Mn도 같아요(길항 배율 등은 위 식 그대로).
- N은 NH4+와 NO3- 유효량 비율대로 나눠 가져가요. NH4+ 흡수는 H+, NO3- 흡수는 OH-를 `pending_H`에 기록해요(다음 틱 pH 갱신).
- 방향: 뿌리가 적거나 상하면, 마르거나 짜면 덜 먹어요. 유효량이 많으면 적정 농도의 1.3배까지 더 먹어요(질소 과잉의 원천, M4에서 화아분화 지연 등에 사용. (M4 점검) N은 전용 상한 1.2배).

### 4. 증산 (셀 단계 a에서 계산)

```
transpired = min(water_mm, transpiration_coeff × inside_solar × interception(LAI) × min(water, salt))
```

토양 증발은 `× (1 − interception(LAI))`로 줄어요. 장부 `water_transpired_mm`.

### 5. 뿌리 건강도

```
damage = waterlog_curve(aeration) + heat_curve(soil_temp)
       + nitrite_curve(no2 × nitrite_toxicity_ph_curve(pH)) + acid_curve(pH)
if damage > 0: root_health ×= (1 − min(1, damage))
else:          root_health += root_recovery_rate × (1 − root_health)
if root_health < death_root_health: alive = false → 이벤트 PlantDied{tile, cause}
```

- `cause`는 이번 틱 손상 항 중 가장 큰 것(waterlog | heat | nitrite | acid).
#### 뿌리 고사 (2026-11-18 추가)

손상이 있는 틱(`damage > 0`)에는 뿌리 건강도뿐 아니라 **뿌리 건물 자체도 죽어요**.

```
dead_root   = root × min(1, damage) × root_dieback_per_damage
share       = dead_root / (leaf + crown + root + 과실 건물 합)   (죽기 전 생체량 기준. M4, 2026-11-18: 이전 분모 `leaf + root`, 관부·과실 포함)
root       −= dead_root
체내 양분 X −= plant_X × share   (X = N, P, K, Ca, Mg, Mn; 양분은 생체량에 고르게 분포한다고 단순화. M4: 관부·과실에도 고르게)   ← Mn은 M3, 2026-11-18
토양으로:  N → labile_N,  P·K·Ca·Mg → 각 토양 풀,  Mn → 유효태 Mn,  C = dead_root × dry_matter_C_fraction → labile_C
          (M3, 2026-11-18: K·Ca·Mg·Mn은 총량과 용액 상태에 함께 돌아가요. N은 M2처럼 이분해성 유기 N으로)
장부:      C_root_litter_g += C
```

- 방향: 손상이 반복되면 뿌리가 실제로 줄어서, 손상이 멈춘 뒤에도 흡수 능력이 한동안 낮게 남아요(다시 자라야 회복). 죽은 뿌리는 이분해성 유기물이 되어 분해되면서 다시 무기 질소로 돌아와요.
- 고사 순서: 뿌리 건강도 갱신 → 뿌리 고사 → 고사 판정.

- 죽은 작물은 생장·흡수·증산을 하지 않아요. 체내 양분은 그대로 남아요(잔재 처리는 M5 "병든 포기 제거"와 함께).

## 보존·불변 조건

- 원소별 `토양 + 체내 = 초기 + in − leached − gas − harvested`. 흡수와 뿌리 고사는 계 안의 이동이에요. (M4, 2026-11-18) 수확으로 반출되는 양이 `*_harvested_g`, 탈질 손실이 `N_gas_g`예요. (M3, 2026-11-18) 원소에 Mn이 추가돼요(토양 Mn = 유효태 + 산화물, [manganese](manganese.md)).
- 물: `토양 = 초기 + in − 증발 − 증산 − 배수`.
- 생체량, 체내 양분 ≥ 0. root_health 0~1. 광합성 탄소는 장부 밖이에요. 토양 C 보존식은 `유기 C = 초기 + C_in + C_root_litter − C_respired`. (M4, 2026-11-18) 낙엽·낙과·언 과실의 C가 장부 `C_leaf_litter_g`로 들어와 `유기 C = 초기 + C_in + C_root_litter + C_leaf_litter − C_respired`예요([strawberry-fruit](strawberry-fruit.md)).

## 테스트 (단위)

| 조건 | 기대 |
| --- | --- |
| 밤 (일사 0) | 생장 0, 증산 0 (M4, 2026-11-18: 총생산 0이고 유지 호흡만 있어 순생장이 0 이하. 기대값 변경은 [m4-overview](m4-overview.md) "기대값이 바뀌는 단위 테스트" 승인 목록) |
| 같은 조건, 잎 많음 | 생장량 증가 (받는 빛 증가) |
| minimum 대 product | product 생장량 ≤ minimum 생장량 |
| 유효 P만 부족 | P 충족도 하락, stress_hours의 P 증가 |
| 체내 양분이 목표의 1.3배 | 흡수 0 |
| 뿌리 건강도 0.5 | 흡수 능력 절반 |
| NH4 흡수 | pending_H 증가, NO3 흡수는 감소 |
| 손상 요인 4가지 각각 | 뿌리 건강도 감소, 해당 요인이 사망 원인으로 기록 |
| 손상 없음 | 뿌리 건강도가 1 쪽으로 회복, 뿌리 건물은 줄지 않음 |
| 손상 있음 | 뿌리 건물이 `root × min(1, damage) × dieback`만큼 줄고, 그 몫의 양분·C가 토양으로 (원소·C 보존) |
| dieback = 0 | 뿌리 건물 변화 없음 (M2 초안 동작) |
| 같은 아질산, pH 5.0 대 7.0 | 5.0에서 손상 큼 |
| 죽은 작물 | 생장·흡수·증산 0 |

## 단순화와 비목표

- 생육 단계, 화방, 관부, 개별 과실, 품질, 유지 호흡, 낙엽(노화)은 M4에서 들어왔어요(2026-11-18, [strawberry-phenology](strawberry-phenology.md), [strawberry-fruit](strawberry-fruit.md)). 적엽·수확 명령, 과숙 손실은 이후예요
- 증상 화면 표현 (M7). 충족도·스트레스·품질 지표까지만
- 하우스 CO2는 M4에서 들어왔어요([house-climate](house-climate.md), 2026-11-18). CO2 시용기는 M7, 병해는 M5. (길항은 M3에서 들어왔어요, 2026-11-18)
- 정식은 시나리오 시작 시점으로만. 정식 명령은 필요해지면 추가
